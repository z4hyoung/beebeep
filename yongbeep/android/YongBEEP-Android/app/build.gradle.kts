plugins {
  id("com.android.application")
  id("org.jetbrains.kotlin.plugin.compose")
}

android {
  namespace = "com.yongbeep.app"
  compileSdk = 36

  defaultConfig {
    applicationId = "com.yongbeep.app"
    minSdk = 26
    targetSdk = 34
    versionCode = 1
    versionName = "0.1.0"
  }

  buildTypes {
    release {
      isMinifyEnabled = false
    }
  }

  compileOptions {
    sourceCompatibility = JavaVersion.VERSION_17
    targetCompatibility = JavaVersion.VERSION_17
  }

  buildFeatures {
    compose = true
  }

  // A1.3: native libs (libybcore.so + Qt6 runtime + libc++_shared) are
  // prebuilt outside Gradle and staged in app/libs/<abi>/.
  sourceSets.getByName("main") {
    jniLibs.srcDirs("libs")
  }
}

dependencies {
  // A1.3: Qt for Android Java bindings - libQt6Core/Network/Bluetooth's
  // JNI_OnLoad looks up org.qtproject.qt.android.QtNative & friends;
  // without these classes the .so loading fails with JNI_ERR.
  implementation(fileTree("libs") { include("*.jar") })

  val composeBom = platform("androidx.compose:compose-bom:2026.06.01")
  implementation(composeBom)
  implementation("androidx.activity:activity-compose:1.10.1")
  implementation("androidx.compose.material3:material3")
  implementation("androidx.compose.ui:ui")
  implementation("androidx.compose.ui:ui-tooling-preview")
  implementation("androidx.core:core-ktx:1.17.0")
  implementation("androidx.lifecycle:lifecycle-viewmodel-compose:2.9.4")
  debugImplementation("androidx.compose.ui:ui-tooling")
}
