# OS integration (library brulion_platform, src/platform): "Start when you sign in" (autostart.h:
# the Run value's command, Task Manager's switch, Autostart on INI files) and the Windows
# AppUserModelID of the toasts (appidentity.h). Runs on every desktop platform.
brulion_add_test(tst_autostart SOURCES tst_autostart.cpp LIBS brulion_platform)
# The bookkeeping of the browser's file picker (pickedfiles.h, used by wasm/wasmfiles.h): files
# read or skipped above the limit, an empty file, a cancel — pure, so tested on every platform.
brulion_add_test(tst_pickedfiles SOURCES tst_pickedfiles.cpp LIBS brulion_platform)
