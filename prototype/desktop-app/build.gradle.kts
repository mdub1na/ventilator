import org.jetbrains.compose.desktop.application.dsl.TargetFormat

plugins {
    kotlin("jvm") version "2.4.20"
    id("org.jetbrains.kotlin.plugin.compose") version "2.4.20"
    id("org.jetbrains.compose") version "1.12.0"
}

val nativeResources = layout.buildDirectory.dir("generated/native-resources")
val buildSmcReader = tasks.register<Exec>("buildSmcReader") {
    commandLine("make", "-C", "../smc-read", "build")
}
val bundleSmcReader = tasks.register<Copy>("bundleSmcReader") {
    dependsOn(buildSmcReader)
    from(file("../smc-read/smc-read"))
    into(nativeResources.map { it.dir("macos-arm64") })
    filePermissions { unix("755") }
}

kotlin {
    jvmToolchain(25)
}

dependencies {
    implementation(project(":reader"))
    implementation(compose.desktop.currentOs)
    implementation("org.jetbrains.compose.ui:ui-tooling-preview:1.12.0")
    implementation("org.jetbrains.compose.material3:material3:1.12.0-alpha03")
    implementation("org.jetbrains.kotlinx:kotlinx-coroutines-core:1.11.0")
    testImplementation(kotlin("test"))
}

compose.desktop {
    application {
        mainClass = "ventilator.desktop.DesktopMainKt"
        nativeDistributions {
            targetFormats(TargetFormat.Dmg)
            packageName = "Ventilator"
            // jpackage requires a positive major version even for an unreleased local prototype.
            packageVersion = "1.0.0"
            appResourcesRootDir.set(nativeResources)
        }
    }
}

tasks.test {
    useJUnitPlatform()
}

tasks.matching { it.name == "prepareAppResources" }.configureEach {
    dependsOn(bundleSmcReader)
}

tasks.matching { it.name == "createDistributable" }.configureEach {
    val packagedApp = layout.buildDirectory.dir("compose/binaries/main/app/Ventilator.app")
    val packagedReader = packagedApp.map { it.file("Contents/app/resources/smc-read") }
    outputs.upToDateWhen { packagedReader.get().asFile.canExecute() }
    // jpackage requires an empty destination when rebuilding a local app image.
    doFirst { delete(packagedApp) }
    doLast {
        val reader = packagedReader.get().asFile
        check(reader.setExecutable(true, false)) { "Bundled SMC reader is not executable" }
    }
}
