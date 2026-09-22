plugins {
    kotlin("jvm") version "2.4.20"
    application
}

kotlin {
    jvmToolchain(25)
}

dependencies {
    implementation("org.jetbrains.kotlinx:kotlinx-serialization-json:1.11.0")
}

application {
    mainClass = "ventilator.prototype.MainKt"
}
