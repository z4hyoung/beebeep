//------------------------------------------------------------------
// ybcore_jni_entry.cpp - exported version probe for the Android shell.
//
// The Kotlin side (A1.3) will System.loadLibrary("ybcore") and can use
// this C symbol to sanity-check that the right core was loaded before
// any real JNI bridge exists.
//------------------------------------------------------------------
#include <QtCore>

extern "C" __attribute__((visibility("default"))) const char* ybcore_version()
{
  return "ybcore-android-" QT_VERSION_STR " proto=96";
}
