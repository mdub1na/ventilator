package ventilator.prototype

import java.nio.file.Path
import java.time.Instant
import java.util.concurrent.TimeUnit
import kotlinx.serialization.json.Json
import kotlinx.serialization.json.doubleOrNull
import kotlinx.serialization.json.int
import kotlinx.serialization.json.intOrNull
import kotlinx.serialization.json.jsonArray
import kotlinx.serialization.json.jsonObject
import kotlinx.serialization.json.jsonPrimitive

private fun parseTemperature(element: kotlinx.serialization.json.JsonElement, measuredAt: Instant): TemperatureReading {
    val entry = element.jsonObject
    val key = entry.getValue("key").jsonPrimitive.content
    require(key.length == 4 && key.startsWith('T')) { "Unexpected temperature key" }
    val celsius = entry.getValue("celsius").jsonPrimitive.doubleOrNull?.takeIf { it.isFinite() && it in 10.0..115.0 }
    return TemperatureReading(
        rawKey = key,
        celsius = celsius,
        measuredAt = measuredAt,
        labelConfidence = if (key == "Tg0D") LabelConfidence.OBSERVED_ON_MAC15_7 else LabelConfidence.RAW_KEY_ONLY,
    )
}

fun parseSnapshot(json: String, measuredAt: Instant = Instant.now()): StatusSnapshot {
    val root = Json.parseToJsonElement(json).jsonObject
    require(root.getValue("schema").jsonPrimitive.int == 1) { "Unsupported SMC snapshot schema" }
    require(root.getValue("cpu_key").jsonPrimitive.content == "TCMz") { "Unexpected CPU sensor" }
    val declaredFanCount = root.getValue("fan_count").jsonPrimitive.intOrNull
    val fans = root.getValue("fans").jsonArray.map { element ->
        val fan = element.jsonObject
        FanSnapshot(
            index = fan.getValue("index").jsonPrimitive.int,
            actualRpm = fan.getValue("actual_rpm").jsonPrimitive.doubleOrNull?.takeIf { it.isFinite() && it >= 0 },
            minRpm = fan.getValue("min_rpm").jsonPrimitive.doubleOrNull?.takeIf { it.isFinite() && it >= 0 },
            maxRpm = fan.getValue("max_rpm").jsonPrimitive.doubleOrNull?.takeIf { it.isFinite() && it >= 0 },
            measuredAt = measuredAt,
        )
    }
    val cpuTempC = root.getValue("cpu_temp_c").jsonPrimitive.doubleOrNull?.takeIf { it.isFinite() && it in 10.0..115.0 }
    return StatusSnapshot(
        declaredFanCount = declaredFanCount,
        fans = fans,
        cpuTemperature = TemperatureReading(
            rawKey = "TCMz",
            celsius = cpuTempC,
            measuredAt = measuredAt,
            labelConfidence = LabelConfidence.OBSERVED_ON_MAC15_7,
        ),
        selectedTemperatures = root["selected_temperatures"]?.jsonArray?.map { parseTemperature(it, measuredAt) }
            ?: emptyList(),
    )
}

fun parseDiagnostics(json: String, measuredAt: Instant = Instant.now()): DiagnosticsSnapshot {
    val root = Json.parseToJsonElement(json).jsonObject
    require(root.getValue("schema").jsonPrimitive.int == 1) { "Unsupported SMC snapshot schema" }
    return DiagnosticsSnapshot(
        temperatures = root.getValue("temperatures").takeUnless { it is kotlinx.serialization.json.JsonNull }
            ?.jsonArray?.map { parseTemperature(it, measuredAt) },
        measuredAt = measuredAt,
    )
}

fun readSnapshot(probe: Path): StatusSnapshot {
    return parseSnapshot(readProbe(probe, "--status-json", 5))
}

fun readDiagnostics(probe: Path): DiagnosticsSnapshot =
    parseDiagnostics(readProbe(probe, "--temperatures-json", 20))

private fun readProbe(probe: Path, argument: String, timeoutSeconds: Long): String {
    val process = ProcessBuilder(probe.toString(), argument).start()
    try {
        if (!process.waitFor(timeoutSeconds, TimeUnit.SECONDS)) {
            process.destroyForcibly()
            error("SMC probe timed out")
        }
        val output = process.inputStream.bufferedReader().readText()
        val error = process.errorStream.bufferedReader().readText()
        check(process.exitValue() == 0) { "SMC probe failed: $error" }
        return output
    } finally {
        process.destroy()
    }
}

fun main(args: Array<String>) {
    require(args.size == 1) { "Usage: kotlin-read <path-to-smc-read>" }
    val snapshot = readSnapshot(Path.of(args[0]).toAbsolutePath())
    println("CPU die max (TCMz): ${snapshot.cpuTemperature.celsius ?: "unavailable"} °C")
    snapshot.fans.forEach { fan ->
        println("Fan ${fan.index}: ${fan.actualRpm ?: "unavailable"} RPM; level ${fan.level() ?: "unknown"}")
    }
    println("Combined tray level: ${snapshot.trayLevel() ?: "unknown"}")
}
