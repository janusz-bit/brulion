# OS integration (library brulion_platform, src/platform): "Start when you sign in" (autostart.h:
# the Run value's command, Task Manager's switch, Autostart on INI files) and the Windows
# AppUserModelID of the toasts (appidentity.h). Runs on every desktop platform.
brulion_add_test(tst_autostart SOURCES tst_autostart.cpp LIBS brulion_platform)
# The camera of capture.h: the photos the Android camera hands out (capturedPhotos: Polish names,
# entries without a file left out) and a camera of the app's own registered (setPhotoCamera) — pure
# or desktop-only, so tested on every platform.
brulion_add_test(tst_capture SOURCES tst_capture.cpp LIBS brulion_platform)
# The bookkeeping of the browser's file picker (pickedfiles.h, used by wasm/wasmfiles.h): files
# read or skipped above the limit, the reading stopped at the pick's total, an empty file, a
# cancel — pure, so tested on every platform.
brulion_add_test(tst_pickedfiles SOURCES tst_pickedfiles.cpp LIBS brulion_platform)
# The file calls of the vault's own writes (filesync.h): Windows' extended-length paths (pure, so
# tested on every platform), the installed sync, and on Windows the rename and its Busy.
brulion_add_test(tst_filesync SOURCES tst_filesync.cpp LIBS brulion_platform)
# PDF pages as pictures (pdfpages.h) with this build's backend (poppler in the Nix dev shell, Qt PDF
# on CI and Windows; none in the browser: skipped): a PDF written with QPdfWriter read and drawn,
# broken and missing files refused; PdfReader's parts of pages, text and search, the encrypted
# fixtures of tests/data/pdf (make-fixtures.sh writes them), a rotated page, two threads.
brulion_add_test(tst_pdfpages SOURCES tst_pdfpages.cpp LIBS brulion_platform)
target_compile_definitions(tst_pdfpages PRIVATE BRULION_TEST_PDF_DIR="${PROJECT_SOURCE_DIR}/tests/data/pdf")
# The PDF viewer's matcher (pdfsearch.h): Polish without diacritics, white space, soft hyphens,
# surrogates, the rectangles of a match, Android's spellings — pure, so tested on every platform.
brulion_add_test(tst_pdfsearch SOURCES tst_pdfsearch.cpp LIBS brulion_platform)
# Text picked by pointing, outline titles and link addresses (pdftext.h): the caret nearest to a
# point, a range between two, the clipboard's text, titles cleaned, only http(s) / mailto links —
# pure, so tested on every platform.
brulion_add_test(tst_pdftext SOURCES tst_pdftext.cpp LIBS brulion_platform)
# Which attachments may go to another app (externalfiles.h): the allow-list by name and first bytes
# — documents, sheets, presentations, e-books, audio, video, text; never a program, a script, a page
# or an archive, nor one renamed to a document — pure, so tested on every platform.
brulion_add_test(tst_externalfiles SOURCES tst_externalfiles.cpp LIBS brulion_platform)
# The file an Android document URI names (documentpaths.h: the phone's storage, a memory card,
# Downloads' raw paths; anything else none) — pure, so tested on every platform.
brulion_add_test(tst_documentpaths SOURCES tst_documentpaths.cpp LIBS brulion_platform)
# Where Hunspell's dictionaries are (spelldictionaries.h: DICPATH, next to the app, the build's
# folders, XDG data folders, the system's; the best of several per language, both files needed) —
# pure, so tested on every platform.
brulion_add_test(tst_spelldictionaries SOURCES tst_spelldictionaries.cpp LIBS brulion_platform)
# This build's spell checking backend (spellbackends.h) with dictionaries the test writes: Hunspell
# with an ISO8859-2 Polish and a UTF-8 English dictionary (diacritics, affixes, capitals,
# apostrophes, suggestions back in UTF-16, words the encoding cannot hold), through SpellChecker;
# "none" (a build without Hunspell): no languages, no backends.
brulion_add_test(tst_spellbackends SOURCES tst_spellbackends.cpp LIBS brulion_platform brulion_core_spell)
target_compile_definitions(tst_spellbackends PRIVATE BRULION_TEST_SPELL_BACKEND="${BRULION_SPELL_BACKEND_USED}")
# What a crash prints (crashreport.h): child processes crash on the main thread and on a named
# thread (a null write) and abort(); stderr names the signal, the address and the thread and holds
# a stack, and each still dies of its signal. Linux with glibc only (skipped elsewhere and in
# sanitizer builds).
brulion_add_test(tst_crashreport SOURCES tst_crashreport.cpp LIBS brulion_platform)
# Asking the allocator to give freed memory back (processmemory.h): with glibc what freed blocks
# leave behind in the heap — small ones, large ones once the mmap threshold moved over them —
# stays with the process until it is asked for, by the process's resident size; elsewhere (and
# under a sanitizer) the call says there is nothing to do.
brulion_add_test(tst_processmemory SOURCES tst_processmemory.cpp LIBS brulion_platform)
# Where OCR's engine and models are looked for and which languages it reads (ocrdata.h) — pure, so
# tested on every platform.
brulion_add_test(tst_ocrdata SOURCES tst_ocrdata.cpp LIBS brulion_platform)
# OCR with this build's backend (ocr.h: Tesseract loaded at run time — the Nix dev shell's and
# package's, with their Polish and English models; skipped where there is none): the pictures of
# tests/data/attachments (a whiteboard, a photo stored sideways with its EXIF orientation, a scanned
# PDF page) read with their words in place, a picture without text, a cancelled read, the switch
# that turns it off (a child process).
brulion_add_test(tst_ocr SOURCES tst_ocr.cpp LIBS brulion_platform brulion_core_text)
target_compile_definitions(tst_ocr PRIVATE BRULION_FONTS_DIR="${PROJECT_SOURCE_DIR}/assets/fonts")
# Tesseract reads a page in seconds; under the sanitizers much longer.
set_tests_properties(tst_ocr PROPERTIES TIMEOUT 600)
# A program on a pseudo-terminal (pty.h) with this build's backend (POSIX, ConPTY; "none" elsewhere:
# only available() is checked): /bin/sh's output read, input written to cat, `stty size` before and
# after a resize, the working directory and environment the child gets (TERM, no COLUMNS, no
# descriptors of the parent), exit codes and signals, a program that cannot be started, a
# terminated `sleep 100` and its jobs, the child reaped every time (no zombies), a paste larger
# than the pty's buffer. Windows: the same through pty_probe (a console program that prints what it
# sees) and cmd.exe, ConPTY's output read through the emulator (term) as the screen it paints,
# the command line's quoting, terminate() of a process tree, a flood.
brulion_add_test(tst_pty SOURCES tst_pty.cpp LIBS brulion_platform brulion_core_term)
target_compile_definitions(tst_pty PRIVATE BRULION_TEST_PTY_BACKEND="${BRULION_PTY_BACKEND_USED}")
if(WIN32)
    add_executable(pty_probe pty_probe.cpp)
    brulion_set_warnings(pty_probe)
    if(MINGW)
        target_link_options(pty_probe PRIVATE -municode)
    endif()
    add_dependencies(tst_pty pty_probe)
    target_compile_definitions(tst_pty PRIVATE BRULION_TEST_PTY_PROBE="$<TARGET_FILE:pty_probe>")
endif()
