/*
	Portions Copyright 2026 The Hollycast Authors

	This file is part of Hollycast.

    Hollycast is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.

    Hollycast is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Hollycast.  If not, see <https://www.gnu.org/licenses/>.
 */
#include "upstream/osd/osdcore.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <new>
#include <thread>
#include <vector>

int osd_num_processors = 0;

struct osd_work_queue
{
	std::atomic<int> pending = 0;
	std::mutex mutex;
	std::condition_variable cond;
	std::condition_variable idleCond;
	std::deque<osd_work_item *> items;
	std::vector<std::thread> workers;
	bool stopping = false;
};

struct osd_work_item
{
	osd_work_queue *queue = nullptr;
	osd_work_callback callback = nullptr;
	void *param = nullptr;
	void *result = nullptr;
	std::atomic<int> done = 0;
	bool autoRelease = false;
	std::mutex mutex;
	std::condition_variable cond;
};

static std::chrono::milliseconds timeoutToMilliseconds(osd_ticks_t timeout)
{
	if (timeout == 0)
		return std::chrono::milliseconds(0);
	const osd_ticks_t ticksPerSecond = osd_ticks_per_second();
	if (ticksPerSecond == 0)
		return std::chrono::milliseconds(0);
	return std::chrono::milliseconds((timeout * 1000 + ticksPerSecond - 1) / ticksPerSecond);
}

static unsigned workerCountForFlags(int flags)
{
	if (flags & WORK_QUEUE_FLAG_MULTI)
	{
		unsigned count = osd_num_processors > 0 ? (unsigned)osd_num_processors : std::thread::hardware_concurrency();
		if (count == 0)
			count = 1;
#ifdef __ANDROID__
		// CHD codecs allocate per worker. Mobile devices can report many cores,
		// but running a compressor on nearly all of them can exhaust native heap
		// during FLAC/LZMA-heavy batches before CPU scheduling becomes the limit.
		if (count > 1)
			count -= 1;
		count = std::min<unsigned>(count, 8);
#endif
		return std::min<unsigned>(count, WORK_MAX_THREADS);
	}
	if (flags & WORK_QUEUE_FLAG_IO)
	{
#ifdef __ANDROID__
		// One read worker restores read/compress overlap for SAF paths. More
		// than one can race CHD's shared read offset and stall early in a run.
		return 1;
#else
		return 1;
#endif
	}
	return 1;
}

static void runItem(osd_work_item *item, int threadid)
{
	item->result = item->callback ? item->callback(item->param, threadid) : nullptr;
	if (item->queue)
	{
		item->queue->pending.fetch_sub(1);
		item->queue->idleCond.notify_all();
	}
	// Publish completion last. Waiters may release non-auto items immediately
	// after this flag flips, so the worker must not touch the item afterward.
	item->done.store(1);
	item->cond.notify_all();
	if (item->autoRelease)
		delete item;
}

static void workerThread(osd_work_queue *queue, int threadid)
{
	for (;;)
	{
		osd_work_item *item = nullptr;
		{
			std::unique_lock<std::mutex> lock(queue->mutex);
			queue->cond.wait(lock, [&]() { return queue->stopping || !queue->items.empty(); });
			if (queue->stopping && queue->items.empty())
				return;
			item = queue->items.front();
			queue->items.pop_front();
		}
		runItem(item, threadid);
	}
}

osd_work_queue *osd_work_queue_alloc(int flags)
{
	auto *queue = new (std::nothrow) osd_work_queue();
	if (!queue)
		return nullptr;

	const unsigned workerCount = workerCountForFlags(flags);
	try
	{
		for (unsigned i = 0; i < workerCount; ++i)
			queue->workers.emplace_back(workerThread, queue, (int)i);
	}
	catch (...)
	{
		queue->stopping = true;
		queue->cond.notify_all();
		for (std::thread& worker : queue->workers)
		{
			if (worker.joinable())
				worker.join();
		}
		delete queue;
		return nullptr;
	}
	return queue;
}

int osd_work_queue_items(osd_work_queue *queue)
{
	return queue ? queue->pending.load() : 0;
}

bool osd_work_queue_wait(osd_work_queue *queue, osd_ticks_t timeout)
{
	if (!queue)
		return true;
	std::unique_lock<std::mutex> lock(queue->mutex);
	if (timeout == 0)
		return queue->pending.load() == 0;
	return queue->idleCond.wait_for(lock, timeoutToMilliseconds(timeout), [&]() { return queue->pending.load() == 0; });
}

void osd_work_queue_free(osd_work_queue *queue)
{
	if (!queue)
		return;
	osd_work_queue_wait(queue, 30 * osd_ticks_per_second());
	{
		std::lock_guard<std::mutex> lock(queue->mutex);
		queue->stopping = true;
	}
	queue->cond.notify_all();
	for (std::thread& worker : queue->workers)
	{
		if (worker.joinable())
			worker.join();
	}
	delete queue;
}

osd_work_item *osd_work_item_queue_multiple(osd_work_queue *queue, osd_work_callback callback, int32_t numitems, void *parambase, int32_t paramstep, uint32_t flags)
{
	osd_work_item *last = nullptr;
	for (int32_t i = 0; i < numitems; ++i)
	{
		auto *item = new (std::nothrow) osd_work_item();
		if (!item)
			return last;

		item->queue = queue;
		item->callback = callback;
		item->param = reinterpret_cast<std::uint8_t *>(parambase) + (paramstep * i);
		item->autoRelease = (flags & WORK_ITEM_FLAG_AUTO_RELEASE) != 0;

		if (queue)
			queue->pending.fetch_add(1);
		if (!queue || queue->workers.empty())
		{
			const bool autoRelease = item->autoRelease;
			runItem(item, 0);
			if (!autoRelease)
				last = item;
		}
		else
		{
			{
				std::lock_guard<std::mutex> lock(queue->mutex);
				queue->items.push_back(item);
			}
			queue->cond.notify_one();
			if (!item->autoRelease)
				last = item;
		}
	}
	return last;
}

bool osd_work_item_wait(osd_work_item *item, osd_ticks_t timeout)
{
	if (!item)
		return true;
	std::unique_lock<std::mutex> lock(item->mutex);
	if (timeout == 0)
		return item->done.load() != 0;
	return item->cond.wait_for(lock, timeoutToMilliseconds(timeout), [&]() { return item->done.load() != 0; });
}

void *osd_work_item_result(osd_work_item *item)
{
	return item ? item->result : nullptr;
}

void osd_work_item_release(osd_work_item *item)
{
	if (item)
		osd_work_item_wait(item, 30 * osd_ticks_per_second());
	delete item;
}
