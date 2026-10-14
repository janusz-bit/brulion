# brulion_android_selftest — exercises AndroidBridge (JNI <-> Kotlin: permissions, native
# reminders, notification taps) on a device or emulator; not part of the app. Built with
# -DBRULION_ANDROID_SELFTEST=ON (scripts/android-build.sh: BRULION_ANDROID_SELFTEST=1) and run by
# scripts/android-emulator-test.sh. It uses the app's package (android/: same manifest, Kotlin
# code and package name — the Kotlin code imports io.github.januszbit.brulion.R), so it replaces
# an installed Brulion: test devices/emulators only.
qt_add_executable(brulion_android_selftest selftest.cpp)
target_link_libraries(brulion_android_selftest PRIVATE brulion_platform Qt6::Gui)
brulion_set_warnings(brulion_android_selftest)
set_target_properties(brulion_android_selftest PROPERTIES
    QT_ANDROID_PACKAGE_SOURCE_DIR ${PROJECT_SOURCE_DIR}/android
    QT_ANDROID_PACKAGE_NAME io.github.januszbit.brulion
    QT_ANDROID_APP_NAME "Brulion self-test"
    QT_ANDROID_VERSION_NAME ${PROJECT_VERSION}
    QT_ANDROID_MIN_SDK_VERSION 28
    QT_ANDROID_TARGET_SDK_VERSION 36
    QT_ANDROID_COMPILE_SDK_VERSION 36
)
