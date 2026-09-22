package ventilator.desktop.menubar

import java.time.Instant
import kotlin.test.Test
import kotlin.test.assertEquals
import ventilator.prototype.FanSnapshot
import ventilator.prototype.StatusSnapshot
import ventilator.prototype.TemperatureReading

class TrayReadingTest {
    private val at = Instant.parse("2026-09-22T12:00:00Z")

    @Test
    fun `tray uses the higher individual fan level from the same window snapshot`() {
        val status = StatusSnapshot(
            declaredFanCount = 2,
            fans = listOf(
                FanSnapshot(0, 1350.0, 1350.0, 5350.0, at),
                FanSnapshot(1, 5000.0, 1450.0, 5750.0, at),
            ),
            cpuTemperature = TemperatureReading("TCMz", 78.4, at),
        )

        val reading = TrayReading.from(status)
        assertEquals(status.trayLevel(), reading.level)
        assertEquals("snapshot\t5\t78.4\t1350.0\t5000.0\t0\n", reading.wireLine(windowVisible = false))
    }

    @Test
    fun `zero rpm and missing sensor remain different on the wire`() {
        val status = StatusSnapshot(
            declaredFanCount = 2,
            fans = listOf(FanSnapshot(0, 0.0, null, null, at), FanSnapshot(1, null, null, null, at)),
            cpuTemperature = TemperatureReading("TCMz", null, at),
        )

        assertEquals("snapshot\t-\t-\t0.0\t-\t1\n", TrayReading.from(status).wireLine(windowVisible = true))
        assertEquals("snapshot\t-\t-\t-\t-\t1\n", TrayReading().wireLine(windowVisible = true))
    }
}
