package ventilator.prototype

import java.time.Instant
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertNull
import kotlin.test.assertFailsWith

class ReadingsTest {
    private val measuredAt = Instant.parse("2026-09-22T12:00:00Z")

    /** A real zero must remain visible even when the fan's running range cannot be read. */
    @Test
    fun `zero RPM is available and has no filled segments`() {
        val snapshot = parseSnapshot(
            """{"schema":1,"fan_count":2,"fans":[
                {"index":0,"actual_rpm":0,"min_rpm":null,"max_rpm":null},
                {"index":1,"actual_rpm":0,"min_rpm":1450,"max_rpm":5700}
            ],"cpu_key":"TCMz","cpu_temp_c":68}""",
            measuredAt,
        )

        assertEquals(ReadingAvailability.AVAILABLE, snapshot.fans[0].availability)
        assertEquals(0, snapshot.fans[0].level())
        assertEquals(0, snapshot.trayLevel())
        assertEquals("F0Ac", snapshot.fans[0].actualKey)
        assertEquals("F0Mn", snapshot.fans[0].minKey)
        assertEquals(ReadingSource.APPLE_SMC, snapshot.fans[0].source)
        assertEquals(ReadingUnit.RPM, snapshot.fans[0].unit)
        assertEquals(LabelConfidence.RAW_KEY_ONLY, snapshot.fans[0].labelConfidence)
        assertEquals(measuredAt, snapshot.fans[0].measuredAt)
        assertEquals("TCMz", snapshot.cpuTemperature.rawKey)
        assertEquals(LabelConfidence.OBSERVED_ON_MAC15_7, snapshot.cpuTemperature.labelConfidence)
        assertEquals(measuredAt, snapshot.cpuTemperature.measuredAt)
    }

    /** A missing SMC reading must not masquerade as a stopped fan or a zero-level icon. */
    @Test
    fun `missing fan or CPU temperature stays unavailable`() {
        val snapshot = parseSnapshot(
            """{"schema":1,"fan_count":2,"fans":[
                {"index":0,"actual_rpm":0,"min_rpm":1350,"max_rpm":5349},
                {"index":1,"actual_rpm":null,"min_rpm":1450,"max_rpm":5700}
            ],"cpu_key":"TCMz","cpu_temp_c":null}""",
            measuredAt,
        )

        assertEquals(ReadingAvailability.UNAVAILABLE, snapshot.fans[1].availability)
        assertNull(snapshot.fans[1].level())
        assertNull(snapshot.trayLevel())
        assertEquals(ReadingAvailability.UNAVAILABLE, snapshot.cpuTemperature.availability)
        assertNull(snapshot.cpuTemperature.celsius)
    }

    /** The same RPM means different levels when each fan has its own hardware range. */
    @Test
    fun `tray uses the higher level calculated with each fan range`() {
        val snapshot = parseSnapshot(
            """{"schema":1,"fan_count":2,"fans":[
                {"index":0,"actual_rpm":3000,"min_rpm":1000,"max_rpm":7000},
                {"index":1,"actual_rpm":3000,"min_rpm":2500,"max_rpm":3500}
            ],"cpu_key":"TCMz","cpu_temp_c":55}""",
            measuredAt,
        )

        assertEquals(2, snapshot.fans[0].level())
        assertEquals(3, snapshot.fans[1].level())
        assertEquals(3, snapshot.trayLevel())
        assertEquals(ReadingAvailability.AVAILABLE, snapshot.cpuTemperature.availability)
        assertEquals(ReadingUnit.CELSIUS, snapshot.cpuTemperature.unit)
    }

    /** Range boundaries and missing ranges must not turn a positive RPM into a false zero. */
    @Test
    fun `positive RPM clamps to one through five and invalid range is unknown`() {
        fun fan(actual: Double, minimum: Double?, maximum: Double?) =
            FanSnapshot(0, actual, minimum, maximum, measuredAt)

        assertEquals(1, fan(500.0, 1000.0, 6000.0).level())
        assertEquals(1, fan(1000.0, 1000.0, 6000.0).level())
        assertEquals(2, fan(2000.0, 1000.0, 6000.0).level())
        assertEquals(5, fan(6000.0, 1000.0, 6000.0).level())
        assertEquals(5, fan(6500.0, 1000.0, 6000.0).level())
        assertNull(fan(1500.0, null, 6000.0).level())
        assertNull(fan(1500.0, 6000.0, 1000.0).level())
        assertEquals(ReadingAvailability.UNAVAILABLE, fan(Double.NaN, 1000.0, 6000.0).availability)
    }

    /** A missing or inconsistent fan count is not a valid aggregate for the menu icon. */
    @Test
    fun `unknown or inconsistent fan count has no combined level`() {
        val fan = FanSnapshot(0, 0.0, 1350.0, 5349.0, measuredAt)
        val cpu = TemperatureReading("TCMz", 60.0, measuredAt)
        assertNull(StatusSnapshot(null, listOf(fan), cpu).trayLevel())
        assertNull(StatusSnapshot(2, listOf(fan), cpu).trayLevel())
        assertNull(StatusSnapshot(2, listOf(fan, fan), cpu).trayLevel())
        assertNull(StatusSnapshot(0, emptyList(), cpu).trayLevel())
        assertEquals(ReadingAvailability.UNAVAILABLE, TemperatureReading("TCMz", 500.0, measuredAt).availability)
    }

    /** The UI must never display an invalid sensor number as if it were a measured value. */
    @Test
    fun `invalid numbers in JSON become unavailable readings`() {
        val snapshot = parseSnapshot(
            """{"schema":1,"fan_count":1,"fans":[
                {"index":0,"actual_rpm":-1,"min_rpm":1350,"max_rpm":5349}
            ],"cpu_key":"TCMz","cpu_temp_c":500}""",
            measuredAt,
        )

        assertNull(snapshot.fans.single().actualRpm)
        assertEquals(ReadingAvailability.UNAVAILABLE, snapshot.fans.single().availability)
        assertNull(snapshot.trayLevel())
        assertNull(snapshot.cpuTemperature.celsius)
        assertEquals(ReadingAvailability.UNAVAILABLE, snapshot.cpuTemperature.availability)
    }

    /** The overview keeps tentative sensor names as raw keys while preserving the observed CPU label. */
    @Test
    fun `selected temperature keys remain unlabelled diagnostics`() {
        val snapshot = parseSnapshot(
            """{"schema":1,"fan_count":0,"fans":[],"cpu_key":"TCMz","cpu_temp_c":70,
                "selected_temperatures":[{"key":"TAOL","celsius":29.5},{"key":"TB0T","celsius":null}]}""",
            measuredAt,
        )

        assertEquals(listOf("TAOL", "TB0T"), snapshot.selectedTemperatures.map { it.rawKey })
        assertEquals(LabelConfidence.RAW_KEY_ONLY, snapshot.selectedTemperatures.first().labelConfidence)
        assertEquals(ReadingAvailability.UNAVAILABLE, snapshot.selectedTemperatures.last().availability)
        assertEquals(LabelConfidence.OBSERVED_ON_MAC15_7, snapshot.cpuTemperature.labelConfidence)
    }

    /** A failed enumeration is distinct from an empty list on a fanless or unfamiliar Mac. */
    @Test
    fun `diagnostics retain raw keys and distinguish failed enumeration`() {
        val readings = parseDiagnostics(
            """{"schema":1,"temperatures":[{"key":"TAOL","celsius":30.2},
                {"key":"TB0T","celsius":null},{"key":"TCMz","celsius":200}]}""",
            measuredAt,
        )

        assertEquals(3, readings.temperatures?.size)
        assertEquals(30.2, readings.temperatures?.first()?.celsius)
        assertNull(readings.temperatures?.last()?.celsius)
        assertEquals(measuredAt, readings.temperatures?.first()?.measuredAt)
        assertNull(parseDiagnostics("""{"schema":1,"temperatures":null}""", measuredAt).temperatures)
        assertEquals(emptyList(), parseDiagnostics("""{"schema":1,"temperatures":[]}""", measuredAt).temperatures)
        assertFailsWith<IllegalArgumentException> {
            parseDiagnostics("""{"schema":1,"temperatures":[{"key":"F0Ac","celsius":40}]}""", measuredAt)
        }
    }
}
