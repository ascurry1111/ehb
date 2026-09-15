// Top-level build file. Per-module config lives in app/build.gradle.kts.
// Versions deliberately match android/HandbellReceiver so both apps build
// against the same toolchain.
plugins {
    id("com.android.application") version "8.13.2" apply false
    id("org.jetbrains.kotlin.android") version "1.9.24" apply false
}
