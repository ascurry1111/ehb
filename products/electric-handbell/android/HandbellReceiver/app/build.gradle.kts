plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
}

android {
    namespace = "com.ehb.handbell"
    compileSdk = 34

    defaultConfig {
        applicationId = "com.ehb.handbell"
        // Built specifically for the v0.2 demo phone (OnePlus 15 Pro, Android 12+).
        // Targeting 31+ only lets us skip the legacy pre-Android-12 Bluetooth
        // permission model entirely.
        minSdk = 31
        targetSdk = 34
        versionCode = 1
        versionName = "0.2"
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

    kotlinOptions {
        jvmTarget = "17"
    }
}

dependencies {
    implementation("androidx.core:core-ktx:1.13.1")
    implementation("androidx.appcompat:appcompat:1.7.0")
    implementation("androidx.activity:activity-ktx:1.9.0")
}
