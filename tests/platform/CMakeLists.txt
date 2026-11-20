# OS integration (library brulion_platform, src/platform): "Start when you sign in" (autostart.h:
# the Run value's command, Task Manager's switch, Autostart on INI files) and the Windows
# AppUserModelID of the toasts (appidentity.h). Runs on every desktop platform.
brulion_add_test(tst_autostart SOURCES tst_autostart.cpp LIBS brulion_platform)
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
