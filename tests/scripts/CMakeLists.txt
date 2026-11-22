# Windows: the scripts are bash for Linux and the release (GNU tools, ELF files, throwaway
# repositories), and the bash found there may be WSL's: not run on Windows.
if(WIN32)
    return()
endif()

# Tests of the shell scripts in scripts/: bash and git in throwaway repositories, no network.
find_program(BRULION_BASH bash)
find_package(Git QUIET)
if(BRULION_BASH AND GIT_FOUND)
    add_test(NAME tst_scripts_release
             COMMAND ${BRULION_BASH} ${CMAKE_CURRENT_SOURCE_DIR}/tst_release.sh
                     ${PROJECT_SOURCE_DIR}/scripts/release.sh)
endif()
# A binding follows what it reads for real (docs/UI.md): scripts/check-qml-reads.sh on small QML
# files, then on the app's QML — no bare `void x` read, which qmlcachegen compiles away together
# with the binding's dependency.
if(BRULION_BASH)
    add_test(NAME tst_scripts_check_qml_reads
             COMMAND ${BRULION_BASH} ${CMAKE_CURRENT_SOURCE_DIR}/tst_check_qml_reads.sh
                     ${PROJECT_SOURCE_DIR}/scripts/check-qml-reads.sh ${PROJECT_SOURCE_DIR}/src/app/qml)
endif()
# Every Text and Label says how its text is read (docs/UI.md "Text formats"):
# scripts/check-qml-text-format.sh on small QML files, then on the app's QML — none left at Qt's
# AutoText, which would take a tag in a page title or a task for markup.
if(BRULION_BASH)
    add_test(NAME tst_scripts_check_qml_text_format
             COMMAND ${BRULION_BASH} ${CMAKE_CURRENT_SOURCE_DIR}/tst_check_qml_text_format.sh
                     ${PROJECT_SOURCE_DIR}/scripts/check-qml-text-format.sh ${PROJECT_SOURCE_DIR}/src/app/qml)
endif()
# Android's own backup leaves out the passwords and tokens (docs/ANDROID.md "Backups"): the
# manifest, its two rule files and the folder the code writes its secrets to agree. (The Nix
# package's source has no android/: nothing to check there.)
if(BRULION_BASH AND EXISTS ${PROJECT_SOURCE_DIR}/android/AndroidManifest.xml)
    add_test(NAME tst_scripts_android_backup_rules
             COMMAND ${BRULION_BASH} ${CMAKE_CURRENT_SOURCE_DIR}/tst_android_backup_rules.sh ${PROJECT_SOURCE_DIR})
endif()
# Stand-in AppImages are ELF files made with objcopy; the script reads them with readelf.
find_program(BRULION_OBJCOPY objcopy)
find_program(BRULION_READELF readelf)
if(BRULION_BASH AND BRULION_OBJCOPY AND BRULION_READELF)
    add_test(NAME tst_scripts_appimage_update_check
             COMMAND ${BRULION_BASH} ${CMAKE_CURRENT_SOURCE_DIR}/tst_appimage_update_check.sh
                     ${PROJECT_SOURCE_DIR}/scripts/appimage-update-check.sh)
endif()
# scripts/bench.sh's time budget (-T, the CI performance smoke) with stand-in suites; its summary
# needs python3 (skipped without it).
if(BRULION_BASH)
    add_test(NAME tst_scripts_bench_budget
             COMMAND ${BRULION_BASH} ${CMAKE_CURRENT_SOURCE_DIR}/tst_bench_budget.sh
                     ${PROJECT_SOURCE_DIR}/scripts/bench.sh)
    set_tests_properties(tst_scripts_bench_budget PROPERTIES SKIP_RETURN_CODE 77)
endif()
# The store texts and pictures of F-Droid and IzzyOnDroid (fastlane/metadata/android, docs/ANDROID.md):
# scripts/check-fastlane-metadata.sh on small metadata folders, then on the repository's, which must
# have the changelog of the version in CMakeLists.txt.
if(BRULION_BASH)
    add_test(NAME tst_scripts_check_fastlane_metadata
             COMMAND ${BRULION_BASH} ${CMAKE_CURRENT_SOURCE_DIR}/tst_check_fastlane_metadata.sh
                     ${PROJECT_SOURCE_DIR}/scripts/check-fastlane-metadata.sh
                     ${PROJECT_SOURCE_DIR}/fastlane/metadata/android ${PROJECT_SOURCE_DIR}/CMakeLists.txt)
endif()

# The privacy policy's page (docs/PRIVACY.md -> privacy.html of the site, docs/GOOGLE_PLAY.md):
# scripts/markdown-page.py on Markdown it knows and must refuse, then on the real policy; needs
# python3 (skipped without it).
if(BRULION_BASH)
    add_test(NAME tst_scripts_markdown_page
             COMMAND ${BRULION_BASH} ${CMAKE_CURRENT_SOURCE_DIR}/tst_markdown_page.sh
                     ${PROJECT_SOURCE_DIR}/scripts/markdown-page.py ${PROJECT_SOURCE_DIR})
    set_tests_properties(tst_scripts_markdown_page PROPERTIES SKIP_RETURN_CODE 77)
endif()

# The draft F-Droid recipe (packaging/fdroid) against the Android build scripts it calls and the
# project version (docs/ANDROID.md, "The F-Droid recipe").
if(BRULION_BASH)
    add_test(NAME tst_scripts_fdroid_recipe
             COMMAND ${BRULION_BASH} ${CMAKE_CURRENT_SOURCE_DIR}/tst_fdroid_recipe.sh ${PROJECT_SOURCE_DIR})
endif()

# Brulion's own F-Droid repository (docs/ANDROID.md, "Brulion's own F-Droid repository"):
# scripts/fdroid-repo.sh with stand-ins for fdroid, keytool, gh, curl and unzip.
if(BRULION_BASH)
    add_test(NAME tst_scripts_fdroid_repo
             COMMAND ${BRULION_BASH} ${CMAKE_CURRENT_SOURCE_DIR}/tst_fdroid_repo.sh
                     ${PROJECT_SOURCE_DIR}/scripts/fdroid-repo.sh)
endif()

# The browser version in one HTML file (docs/WEB.md, "Single HTML file"): scripts/wasm-single-file.py
# on stand-in builds with the real wasm/index.html; needs python3 (skipped without it).
if(BRULION_BASH)
    add_test(NAME tst_scripts_wasm_single_file
             COMMAND ${BRULION_BASH} ${CMAKE_CURRENT_SOURCE_DIR}/tst_wasm_single_file.sh
                     ${PROJECT_SOURCE_DIR}/scripts/wasm-single-file.py ${PROJECT_SOURCE_DIR})
    set_tests_properties(tst_scripts_wasm_single_file PROPERTIES SKIP_RETURN_CODE 77)
endif()
