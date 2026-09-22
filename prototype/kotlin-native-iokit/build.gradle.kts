plugins {
    kotlin("multiplatform") version "2.4.20"
}

repositories {
    mavenCentral()
}

kotlin {
    macosArm64 {
        binaries {
            executable {
                baseName = "iokit-smoke"
            }
        }
    }
}
