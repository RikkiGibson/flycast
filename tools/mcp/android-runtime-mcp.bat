@echo off
set ANDROID_STUDIO_MCP_BRIDGE_URL=http://127.0.0.1:8765
set ANDROID_STUDIO_EDITOR_DIAGNOSTICS_URL=http://127.0.0.1:8766
set ANDROID_SDK_ROOT=C:\Users\antho\AppData\Local\Android\Sdk
set ANDROID_HOME=C:\Users\antho\AppData\Local\Android\Sdk
set PATH=C:\Users\antho\AppData\Local\Android\Sdk\platform-tools;%PATH%
py -m android_runtime_companion
