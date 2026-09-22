package ventilator.prototype

import java.nio.file.Path
import java.util.concurrent.TimeUnit
import kotlin.math.floor
import kotlinx.serialization.json.Json
import kotlinx.serialization.json.doubleOrNull
import kotlinx.serialization.json.int
import kotlinx.serialization.json.jsonArray
import kotlinx.serialization.json.jsonObject
import kotlinx.serialization.json.jsonPrimitive

data class FanReading(
    val index: Int,
    val actualRpm: Double?,
    val minRpm: Double?,
    val maxRpm: Double?,
) {
    fun level(): Int? {
        val actual = actualRpm ?: return null
        val minimum = minRpm ?: return null
        val maximum = maxRpm ?: return null
        if (!actual.isFinite() || !minimum.isFinite() || !maximum.isFinite() ||
            actual < 0 || minimum < 0 || maximum <= minimum
        ) return null
        if (actual == 0.0) return 0
        val fraction = ((actual - minimum) / (maximum - minimum)).coerceIn(0.0, 1.0)
        return (1 + floor(5 * fraction).toInt()).coerceAtMost(5)
    }
}

data class StatusSnapshot(val fans: List<FanReading>, val cpuTempC: Double?) {
    fun trayLevel(): Int? {
        val levels = fans.map { it.level() }
        if (levels.any { it == null }) return null
        return levels.filterNotNull().maxOrNull()
    }
}

fun parseSnapshot(json: String): StatusSnapshot {
    val root = Json.parseToJsonElement(json).jsonObject
    require(root.getValue("schema").jsonPrimitive.int == 1) { "Unsupported SMC snapshot schema" }
    require(root.getValue("cpu_key").jsonPrimitive.content == "TCMz") { "Unexpected CPU sensor" }
    val fans = root.getValue("fans").jsonArray.map { element ->
        val fan = element.jsonObject
        FanReading(
            index = fan.getValue("index").jsonPrimitive.int,
            actualRpm = fan.getValue("actual_rpm").jsonPrimitive.doubleOrNull,
            minRpm = fan.getValue("min_rpm").jsonPrimitive.doubleOrNull,
            maxRpm = fan.getValue("max_rpm").jsonPrimitive.doubleOrNull,
        )
    }
    val cpuTempC = root.getValue("cpu_temp_c").jsonPrimitive.doubleOrNull
    return StatusSnapshot(fans, cpuTempC)
}

fun readSnapshot(probe: Path): StatusSnapshot {
    val process = ProcessBuilder(probe.toString(), "--status-json").start()
    try {
        if (!process.waitFor(5, TimeUnit.SECONDS)) {
            process.destroyForcibly()
            error("SMC probe timed out")
        }
        val output = process.inputStream.bufferedReader().readText()
        val error = process.errorStream.bufferedReader().readText()
        check(process.exitValue() == 0) { "SMC probe failed: $error" }
        return parseSnapshot(output)
    } finally {
        process.destroy()
    }
}

fun main(args: Array<String>) {
    require(args.size == 1) { "Usage: kotlin-read <path-to-smc-read>" }
    val snapshot = readSnapshot(Path.of(args[0]).toAbsolutePath())
    println("CPU die max (TCMz): ${snapshot.cpuTempC ?: "unavailable"} °C")
    snapshot.fans.forEach { fan ->
        println("Fan ${fan.index}: ${fan.actualRpm ?: "unavailable"} RPM; level ${fan.level() ?: "unknown"}")
    }
    println("Combined tray level: ${snapshot.trayLevel() ?: "unknown"}")
}
