import kotlinx.cinterop.*
import platform.IOKit.IOObjectRelease
import platform.IOKit.IOServiceClose
import platform.IOKit.IOServiceGetMatchingService
import platform.IOKit.IOServiceMatching
import platform.IOKit.IOServiceOpen
import platform.IOKit.io_connect_tVar
import platform.darwin.mach_task_self_

// Read-only interop smoke test: there are no SMC key calls or write selectors.
@OptIn(ExperimentalForeignApi::class)
fun main() {
    val service = IOServiceGetMatchingService(0u, IOServiceMatching("AppleSMC"))
    check(service != 0u) { "AppleSMC service is unavailable" }
    try {
        val (openStatus, closeStatus) = memScoped {
            val connection = alloc<io_connect_tVar>()
            val opened = IOServiceOpen(service, mach_task_self_, 0u, connection.ptr)
            val closed = if (opened == 0) IOServiceClose(connection.value) else null
            opened to closed
        }
        check(openStatus == 0) { "IOServiceOpen failed: $openStatus" }
        check(closeStatus == 0) { "IOServiceClose failed: $closeStatus" }
        println("AppleSMC user client opened and closed (read-only)")
    } finally {
        IOObjectRelease(service)
    }
}
