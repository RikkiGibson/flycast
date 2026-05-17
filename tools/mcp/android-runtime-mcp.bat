REM Portions Copyright 2026 The Hollycast Authors
REM
REM This file is part of Hollycast.
REM
REM     Hollycast is free software: you can redistribute it and/or modify
REM     it under the terms of the GNU General Public License as published by
REM     the Free Software Foundation, either version 2 of the License, or
REM     (at your option) any later version.
REM
REM     Hollycast is distributed in the hope that it will be useful,
REM     but WITHOUT ANY WARRANTY; without even the implied warranty of
REM     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
REM     GNU General Public License for more details.
REM
REM     You should have received a copy of the GNU General Public License
REM     along with Hollycast.  If not, see <https://www.gnu.org/licenses/>.

@echo off
set ANDROID_STUDIO_MCP_BRIDGE_URL=http://127.0.0.1:8765
set ANDROID_STUDIO_EDITOR_DIAGNOSTICS_URL=http://127.0.0.1:8766
set ANDROID_SDK_ROOT=C:\Users\antho\AppData\Local\Android\Sdk
set ANDROID_HOME=C:\Users\antho\AppData\Local\Android\Sdk
set PATH=C:\Users\antho\AppData\Local\Android\Sdk\platform-tools;%PATH%
py -m android_runtime_companion
