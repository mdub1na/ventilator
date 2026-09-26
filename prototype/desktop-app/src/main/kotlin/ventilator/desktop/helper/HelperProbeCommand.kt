package ventilator.desktop.helper

import java.nio.file.Path

/** Manual read-only integration probe; normal UI startup never registers the daemon. */
object HelperProbeCommand {
    fun run(command: String, resources: Path?): Int {
        val library = resources?.resolve("libhelper-probe.dylib")
        if (library == null) {
            System.err.println("Helper probe requires a packaged Ventilator.app")
            return 2
        }
        return runCatching {
            val native = HelperProbeNative(library)
            when (command) {
                "--helper-registration-status" -> {
                    println(native.registrationStatusNative())
                    0
                }
                "--helper-register" -> {
                    val attempt = runCatching { native.setRegisteredNative(true) }
                    attempt.exceptionOrNull()?.let { System.err.println("register failed: ${it.message}") }
                    println("status=${native.registrationStatusNative()}")
                    if (attempt.isSuccess) 0 else 1
                }
                "--helper-request" -> {
                    println(native.requestStatusNative())
                    0
                }
                "--helper-baseline" -> {
                    println(native.requestBaselineNative())
                    0
                }
                "--helper-watch-start" -> {
                    println(native.startWatchNative())
                    0
                }
                "--helper-watch-status" -> {
                    println(native.watchStatusNative())
                    0
                }
                "--helper-startup-audit" -> {
                    println(native.startupAuditNative())
                    0
                }
                "--helper-unregister" -> {
                    val status = native.registrationStatusNative()
                    if (status != "notRegistered" && status != "notFound") native.setRegisteredNative(false)
                    println("status=${native.registrationStatusNative()}")
                    0
                }
                else -> error("Unknown helper probe command: $command")
            }
        }.getOrElse {
            System.err.println("Helper probe failed: ${it.message}")
            1
        }
    }
}

internal class HelperProbeNative(path: Path) {
    init {
        System.load(path.toAbsolutePath().normalize().toString())
    }

    external fun registrationStatusNative(): String
    external fun setRegisteredNative(registered: Boolean): String
    external fun requestStatusNative(): String
    external fun requestBaselineNative(): String
    external fun startWatchNative(): String
    external fun watchStatusNative(): String
    external fun startupAuditNative(): String
}
