# F-Droid recipe (draft)

`io.github.januszbit.brulion.yml` is the draft of `metadata/io.github.januszbit.brulion.yml` for
[fdroiddata](https://gitlab.com/fdroid/fdroiddata). It is kept here so that
`tests/scripts/tst_fdroid_recipe.sh` (ctest `tst_scripts_fdroid_recipe`) checks it against the build
scripts and the project version; once merged, fdroiddata's copy is the one F-Droid uses (its bot
adds the builds of new tags). The file is exactly what `fdroid rewritemeta` writes (no comments,
sorted categories), so it can be copied into fdroiddata as it is.

The APK is built entirely from source: Qt 6.11.2 (srclib `Qt5`, the qt5.git super-repository, only
the modules the app uses) and OpenSSL 3.5 (srclib `OpenSSL`) are compiled with the NDK by
`scripts/android-qt-from-source.sh` and `scripts/android-openssl-from-source.sh`, which
`scripts/android-build.sh` runs with `BRULION_ANDROID_QT=source` — no prebuilt Qt (aqt) or OpenSSL
(KDAB). Summary, description, icon and screenshots come from `fastlane/metadata/android/`.

Details, local verification in F-Droid's build server container, the signing decision and the
texts for the submission: [docs/ANDROID.md](../../docs/ANDROID.md), "Budowanie ze źródeł (F-Droid)".
