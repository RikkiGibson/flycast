/*
	Copyright 2024 flyinghead
	Portions Copyright 2026 The Hollycast Authors

	This file is part of Flycast.

    Flycast is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.

    Flycast is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Flycast.  If not, see <https://www.gnu.org/licenses/>.
 */
#include "upstream/osd/osdcore.h"

#include <atomic>
#include <new>

struct osd_work_queue
{
	std::atomic<int> pending = 0;
};

struct osd_work_item
{
	osd_work_queue *queue = nullptr;
	osd_work_callback callback = nullptr;
	void *param = nullptr;
	void *result = nullptr;
	std::atomic<int> done = 0;
};

osd_work_queue *osd_work_queue_alloc(int flags)
{
	(void)flags;
	return new (std::nothrow) osd_work_queue();
}

int osd_work_queue_items(osd_work_queue *queue)
{
	return queue ? queue->pending.load() : 0;
}

bool osd_work_queue_wait(osd_work_queue *queue, osd_ticks_t timeout)
{
	(void)timeout;
	return !queue || queue->pending.load() == 0;
}

void osd_work_queue_free(osd_work_queue *queue)
{
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

		if (queue)
			queue->pending.fetch_add(1);
		item->result = callback ? callback(item->param, 0) : nullptr;
		item->done.store(1);
		if (queue)
			queue->pending.fetch_sub(1);

		if (flags & WORK_ITEM_FLAG_AUTO_RELEASE)
		{
			delete item;
			last = nullptr;
		}
		else
		{
			last = item;
		}
	}
	return last;
}

bool osd_work_item_wait(osd_work_item *item, osd_ticks_t timeout)
{
	(void)timeout;
	return !item || item->done.load() != 0;
}

void *osd_work_item_result(osd_work_item *item)
{
	return item ? item->result : nullptr;
}

void osd_work_item_release(osd_work_item *item)
{
	delete item;
}
