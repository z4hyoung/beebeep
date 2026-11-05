package org.qtproject.qt.android;

import android.app.Activity;
import android.content.Context;

/**
 * A1.3: opens Qt for Android's package-private bootstrap to the
 * YongBEEP shell app.
 *
 * The core library runs Qt headless on its own thread (no QtActivity,
 * QtNative.startQtAndroidPlugin is never called), so Qt's cached
 * class loader and context stay empty. Native code that needs the
 * android context (QtBluetooth's adapter lookup,
 * QStandardPaths::writableLocation on Android, ...) resolves it via
 * QtNative.getContext() -> m_activity and aborts on the NULL path.
 *
 * Must be called once from the UI thread before the core is started
 * (MainActivity.onCreate, before any Qt usage).
 */
public final class YbQtGlue {

    private YbQtGlue() {}

    public static void setup(ClassLoader classLoader, Context context) {
        QtNative.setClassLoader(classLoader);
        if (context instanceof Activity) {
            QtNative.setActivity((Activity) context);
        } else {
            throw new IllegalArgumentException(
                    "YbQtGlue.setup expects an Activity context, got: " + context);
        }
    }
}
