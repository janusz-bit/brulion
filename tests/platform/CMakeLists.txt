# OS integration (library brulion_platform, src/platform): "Start when you sign in" (autostart.h:
# the Run value's command, Task Manager's switch, Autostart on INI files) and the Windows
# AppUserModelID of the toasts (appidentity.h). Runs on every desktop platform.
brulion_add_test(tst_autostart SOURCES tst_autostart.cpp LIBS brulion_platform)
# The bookkeeping of the browser's file picker (pickedfiles.h, used by wasm/wasmfiles.h): files
# read or skipped above the limit, the reading stopped at the pick's total, an empty file, a
# cancel — pure, so tested on every platform.
brulion_add_test(tst_pickedfiles SOURCES tst_pickedfiles.cpp LIBS brulion_platform)
# PDF pages as pictures (pdfpages.h) with this build's backend (poppler in the Nix dev shell, Qt PDF
# on CI and Windows; none in the browser: skipped): a PDF written with QPdfWriter read and drawn,
# broken and missing files refused.
brulion_add_test(tst_pdfpages SOURCES tst_pdfpages.cpp LIBS brulion_platform)
# The file an Android document URI names (documentpaths.h: the phone's storage, a memory card,
# Downloads' raw paths; anything else none) — pure, so tested on every platform.
brulion_add_test(tst_documentpaths SOURCES tst_documentpaths.cpp LIBS brulion_platform)
