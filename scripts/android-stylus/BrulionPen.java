// SPDX-License-Identifier: AGPL-3.0-or-later
// BrulionPen — a stylus with pressure for the Android emulator (scripts/android-emulator-test.sh
// --stylus; docs/ANDROID.md "Rysik"). `adb shell input stylus …` always presses with 1.0, so a
// line drawn with it has one pressure (Brulion then draws it as a mouse line); this injects
// MotionEvents with the pressures asked for, a stylus's tool type (or the eraser end's, or a
// finger's) and its buttons — through the hidden InputManager(Global).injectInputEvent, from the
// shell user (which holds INJECT_EVENTS: no root), as scrcpy does.
//
//   javac --release 17 -cp android.jar BrulionPen.java && d8 --min-api 30 --lib android.jar \
//       --output out BrulionPen.class && adb push out/classes.dex /data/local/tmp/brulion-pen.dex
//   adb shell CLASSPATH=/data/local/tmp/brulion-pen.dex app_process / BrulionPen \
//       stroke X0 Y0 X1 Y1 STEPS P0 P1 [stylus|eraser|finger] [BUTTONS]
//
// Coordinates in the display's pixels; the pressure goes from P0 to P1 over STEPS moves (8 ms
// apart), the pen lifts exactly where the last move was (pressure 0). BUTTONS: MotionEvent's button
// state (32 = BUTTON_STYLUS_PRIMARY, the barrel button). Prints "ok <events>" or fails.
import android.os.SystemClock;
import android.view.InputDevice;
import android.view.InputEvent;
import android.view.MotionEvent;

import java.lang.reflect.Method;

public final class BrulionPen {
    private static final int INJECT_WAIT_FOR_FINISH = 2;

    private BrulionPen() { }

    public static void main(String[] args) throws Exception {
        if (args.length < 8 || !"stroke".equals(args[0])) {
            System.err.println("usage: BrulionPen stroke X0 Y0 X1 Y1 STEPS P0 P1 [stylus|eraser|finger] [BUTTONS]");
            System.exit(2);
        }
        final float x0 = Float.parseFloat(args[1]);
        final float y0 = Float.parseFloat(args[2]);
        final float x1 = Float.parseFloat(args[3]);
        final float y1 = Float.parseFloat(args[4]);
        final int steps = Math.max(1, Integer.parseInt(args[5]));
        final float p0 = Float.parseFloat(args[6]);
        final float p1 = Float.parseFloat(args[7]);
        final String tool = args.length > 8 ? args[8] : "stylus";
        final int buttons = args.length > 9 ? Integer.parseInt(args[9]) : 0;

        final int toolType;
        final int source;
        switch (tool) {
        case "stylus":
            toolType = MotionEvent.TOOL_TYPE_STYLUS;
            source = InputDevice.SOURCE_STYLUS | InputDevice.SOURCE_TOUCHSCREEN;
            break;
        case "eraser":
            toolType = MotionEvent.TOOL_TYPE_ERASER;
            source = InputDevice.SOURCE_STYLUS | InputDevice.SOURCE_TOUCHSCREEN;
            break;
        case "finger":
            toolType = MotionEvent.TOOL_TYPE_FINGER;
            source = InputDevice.SOURCE_TOUCHSCREEN;
            break;
        default:
            System.err.println("unknown tool " + tool);
            System.exit(2);
            return;
        }

        final Object manager = inputManager();
        final Method inject = manager.getClass().getMethod("injectInputEvent", InputEvent.class, int.class);
        final long down = SystemClock.uptimeMillis();
        int sent = 0;
        sent += send(manager, inject, down, MotionEvent.ACTION_DOWN, x0, y0, p0, toolType, source, buttons);
        float x = x0;
        float y = y0;
        for (int i = 1; i <= steps; i++) {
            Thread.sleep(8);
            final float t = (float) i / steps;
            x = x0 + (x1 - x0) * t;
            y = y0 + (y1 - y0) * t;
            sent += send(manager, inject, down, MotionEvent.ACTION_MOVE, x, y, p0 + (p1 - p0) * t, toolType, source,
                         buttons);
        }
        Thread.sleep(8);
        sent += send(manager, inject, down, MotionEvent.ACTION_UP, x, y, 0f, toolType, source, 0);
        System.out.println("ok " + sent);
    }

    /** InputManagerGlobal (Android 14+) or InputManager (before): the hidden injectInputEvent's owner. */
    private static Object inputManager() throws Exception {
        try {
            final Class<?> global = Class.forName("android.hardware.input.InputManagerGlobal");
            return global.getMethod("getInstance").invoke(null);
        } catch (ClassNotFoundException e) {
            final Class<?> manager = Class.forName("android.hardware.input.InputManager");
            return manager.getMethod("getInstance").invoke(null);
        }
    }

    private static int send(Object manager, Method inject, long down, int action, float x, float y, float pressure,
                            int toolType, int source, int buttons) throws Exception {
        final MotionEvent.PointerProperties properties = new MotionEvent.PointerProperties();
        properties.id = 0;
        properties.toolType = toolType;
        final MotionEvent.PointerCoords coords = new MotionEvent.PointerCoords();
        coords.x = x;
        coords.y = y;
        coords.pressure = pressure;
        coords.size = 0.01f;
        final MotionEvent event = MotionEvent.obtain(down, SystemClock.uptimeMillis(), action, 1,
                new MotionEvent.PointerProperties[] {properties}, new MotionEvent.PointerCoords[] {coords}, 0, buttons,
                1f, 1f, 0, 0, source, 0);
        try {
            final Object ok = inject.invoke(manager, event, INJECT_WAIT_FOR_FINISH);
            if (!Boolean.TRUE.equals(ok))
                throw new IllegalStateException("injectInputEvent refused " + MotionEvent.actionToString(action));
        } finally {
            event.recycle();
        }
        return 1;
    }
}
