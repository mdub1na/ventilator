package ventilator.desktop.menubar

import ventilator.prototype.ReadingAvailability
import ventilator.prototype.StatusSnapshot

/** A single SMC snapshot supplies both the Compose window and the macOS status item. */
data class TrayReading(
    val level: Int? = null,
    val cpuCelsius: Double? = null,
    val fan0Rpm: Double? = null,
    val fan1Rpm: Double? = null,
) {
    fun wireLine(windowVisible: Boolean): String = listOf(
        "snapshot",
        level?.toString() ?: "-",
        cpuCelsius?.toString() ?: "-",
        fan0Rpm?.toString() ?: "-",
        fan1Rpm?.toString() ?: "-",
        if (windowVisible) "1" else "0",
    ).joinToString("\t", postfix = "\n")

    companion object {
        fun from(snapshot: StatusSnapshot?): TrayReading {
            if (snapshot == null) return TrayReading()
            fun rpm(index: Int): Double? = snapshot.fans.firstOrNull { it.index == index }
                ?.takeIf { it.availability == ReadingAvailability.AVAILABLE }?.actualRpm
            return TrayReading(
                level = snapshot.trayLevel(),
                cpuCelsius = snapshot.cpuTemperature.takeIf { it.availability == ReadingAvailability.AVAILABLE }?.celsius,
                fan0Rpm = rpm(0),
                fan1Rpm = rpm(1),
            )
        }
    }
}
