# Applied to apps that use the plugin (Tauri minifies Android release builds with R8).
#
# Tauri's own rules (tauri-android) already keep @TauriPlugin classes with their constructor,
# @Command and @PermissionCallback methods, and every public @InvokeArg class. Receivers and
# AlarmActivity are kept through the manifest. Spelled out here so the plugin does not depend
# on those rules staying as they are:

# Instantiated from Rust through JNI by its fully qualified name; commands found by reflection.
-keep class io.github.januszbit.brulion.android.BrulionAndroidPlugin {
  public <init>(android.app.Activity);
  @app.tauri.annotation.Command public <methods>;
  @app.tauri.annotation.PermissionCallback <methods>;
}

# Command arguments are filled in by Jackson through their fields.
-keep class io.github.januszbit.brulion.android.ScheduleRemindersArgs { *; }
-keep class io.github.januszbit.brulion.android.ReminderArg { *; }
