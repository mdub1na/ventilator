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
val buildStatusItem = tasks.register<Exec>("buildStatusItem") {
    commandLine("make", "-C", "../menu-bar", "status-item-bridge")
}
val bundleStatusItem = tasks.register<Copy>("bundleStatusItem") {
    dependsOn(buildStatusItem)
    from(file("../menu-bar/status-item-bridge"))
    into(nativeResources.map { it.dir("macos-arm64") })
    filePermissions { unix("755") }
}
val buildLoginItem = tasks.register<Exec>("buildLoginItem") {
    commandLine("make", "-C", "../login-item", "build")
}
val bundleLoginItem = tasks.register<Copy>("bundleLoginItem") {
    dependsOn(buildLoginItem)
    from(file("../login-item/liblogin-item.dylib"))
    into(nativeResources.map { it.dir("macos-arm64") })
}
val buildHelperProbeBridge = tasks.register<Exec>("buildHelperProbeBridge") {
    commandLine("make", "-C", "../helper-ipc", "libhelper-probe.dylib")
}
val bundleHelperProbeBridge = tasks.register<Copy>("bundleHelperProbeBridge") {
    dependsOn(buildHelperProbeBridge)
    from(file("../helper-ipc/libhelper-probe.dylib"))
    into(nativeResources.map { it.dir("macos-arm64") })
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
        jvmArgs += listOf("--enable-native-access=ALL-UNNAMED")
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
    dependsOn(bundleSmcReader, bundleStatusItem, bundleLoginItem, bundleHelperProbeBridge)
}

tasks.matching { it.name == "run" }.configureEach {
    dependsOn(buildSmcReader, buildStatusItem)
}

tasks.matching { it.name == "createDistributable" }.configureEach {
    inputs.files(
        file("../smc-read/smc-read"),
        file("../menu-bar/status-item-bridge"),
        file("../login-item/liblogin-item.dylib"),
        file("../helper-ipc/libhelper-probe.dylib"),
    )
    val packagedApp = layout.buildDirectory.dir("compose/binaries/main/app/Ventilator.app")
    val packagedReader = packagedApp.map { it.file("Contents/app/resources/smc-read") }
    val packagedStatusItem = packagedApp.map { it.file("Contents/app/resources/status-item-bridge") }
    val packagedLoginItem = packagedApp.map { it.file("Contents/app/resources/liblogin-item.dylib") }
    val packagedHelperBridge = packagedApp.map { it.file("Contents/app/resources/libhelper-probe.dylib") }
    outputs.upToDateWhen {
        packagedReader.get().asFile.canExecute() && packagedStatusItem.get().asFile.canExecute() &&
            packagedLoginItem.get().asFile.isFile && packagedHelperBridge.get().asFile.isFile
    }
    // jpackage requires an empty destination when rebuilding a local app image.
    doFirst { delete(packagedApp) }
    doLast {
        val reader = packagedReader.get().asFile
        check(reader.setExecutable(true, false)) { "Bundled SMC reader is not executable" }
        val statusItem = packagedStatusItem.get().asFile
        check(statusItem.setExecutable(true, false)) { "Bundled status item is not executable" }
        check(packagedLoginItem.get().asFile.isFile) { "Bundled login item bridge is missing" }
        check(packagedHelperBridge.get().asFile.isFile) { "Bundled helper probe bridge is missing" }
    }
}
