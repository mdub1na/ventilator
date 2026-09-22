package ventilator.prototype

import java.time.Instant
import kotlin.math.floor

enum class ReadingSource { APPLE_SMC }

enum class ReadingUnit { RPM, CELSIUS }

enum class ReadingAvailability { AVAILABLE, UNAVAILABLE }

enum class LabelConfidence { OBSERVED_ON_MAC15_7, RAW_KEY_ONLY }

data class FanSnapshot(
    val index: Int,
    val actualRpm: Double?,
    val minRpm: Double?,
    val maxRpm: Double?,
    val measuredAt: Instant,
    val source: ReadingSource = ReadingSource.APPLE_SMC,
    val unit: ReadingUnit = ReadingUnit.RPM,
    val labelConfidence: LabelConfidence = LabelConfidence.RAW_KEY_ONLY,
) {
    init {
        require(index in 0..9) { "SMC fan keys support indices 0 through 9" }
    }

    val actualKey: String get() = "F${index}Ac"
    val minKey: String get() = "F${index}Mn"
    val maxKey: String get() = "F${index}Mx"

    val availability: ReadingAvailability
        get() = if (actualRpm != null && actualRpm.isFinite() && actualRpm >= 0) {
            ReadingAvailability.AVAILABLE
        } else {
            ReadingAvailability.UNAVAILABLE
        }

    fun level(): Int? {
        if (availability != ReadingAvailability.AVAILABLE) return null
        val actual = actualRpm ?: return null
        // A stopped fan has no filled segments even if its running range is unavailable.
        if (actual == 0.0) return 0
        val minimum = minRpm ?: return null
        val maximum = maxRpm ?: return null
        if (!minimum.isFinite() || !maximum.isFinite() ||
            minimum < 0 || maximum <= minimum
        ) return null
        val fraction = ((actual - minimum) / (maximum - minimum)).coerceIn(0.0, 1.0)
        return (1 + floor(5 * fraction).toInt()).coerceAtMost(5)
    }
}

data class TemperatureReading(
    val rawKey: String,
    val celsius: Double?,
    val measuredAt: Instant,
    val source: ReadingSource = ReadingSource.APPLE_SMC,
    val unit: ReadingUnit = ReadingUnit.CELSIUS,
    val labelConfidence: LabelConfidence = LabelConfidence.RAW_KEY_ONLY,
) {
    val availability: ReadingAvailability
        get() = if (celsius != null && celsius.isFinite() && celsius in 10.0..115.0) {
            ReadingAvailability.AVAILABLE
        } else {
            ReadingAvailability.UNAVAILABLE
        }
}

data class StatusSnapshot(
    val declaredFanCount: Int?,
    val fans: List<FanSnapshot>,
    val cpuTemperature: TemperatureReading,
) {
    fun trayLevel(): Int? {
        val count = declaredFanCount ?: return null
        if (count == 0 || count != fans.size || fans.map { it.index }.toSet() != (0 until count).toSet()) return null
        val levels = fans.map { it.level() }
        if (levels.any { it == null }) return null
        return levels.filterNotNull().maxOrNull()
    }
}
