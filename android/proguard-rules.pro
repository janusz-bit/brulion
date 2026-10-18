# R8 rules of Brulion's release builds (android/settings.gradle turns R8 on; docs/ANDROID.md).

# Qt for Android: C++ calls its Java classes through JNI by class and member name, and Qt's loader
# finds some by reflection.
-keep class org.qtproject.qt.** { *; }

# Qt's C++ also calls androidx by name, and no Java code does, so R8 would drop these members:
# FileProvider.getUriForFile turns a local file into a content:// URI for QDesktopServices::openUrl
# and drag and drop (qandroidplatformservices.cpp, qandroidplatformdrag.cpp; the manifest keeps
# only the provider class); FontRequest and FontsContractCompat download icon fonts for
# QIcon::fromTheme (qandroidplatformiconengine.cpp). Brulion does not reach them today (only http,
# https and mailto links open, icons are its own SVGs); kept so that a later feature does not break
# in release builds only.
-keep class androidx.core.content.FileProvider { public static android.net.Uri getUriForFile(...); }
-keep class androidx.core.provider.FontRequest { public <init>(...); }
-keep class androidx.core.provider.FontsContractCompat { public static *** fetchFonts(...); }
-keep class androidx.core.provider.FontsContractCompat$FontFamilyResult { public *; }
-keep class androidx.core.provider.FontsContractCompat$FontInfo { public *; }

# Brulion's Kotlin (android/src): C++ calls BrulionAndroid by name and registers its native methods
# (src/platform/android/androidbridge.cpp); the manifest names the activities and receivers. Small,
# so it is kept whole.
-keep class io.github.januszbit.brulion.android.** { *; }

# Readable stack traces in logcat and bug reports: shrink, but keep the names of what is left.
-dontobfuscate
