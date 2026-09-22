package ventilator.desktop.monitoring.ui

import java.time.ZoneId
import java.time.format.DateTimeFormatter
import java.util.Locale
import ventilator.desktop.menubar.TrayReading
import ventilator.prototype.FanSnapshot
import ventilator.prototype.ReadingAvailability
import ventilator.prototype.StatusSnapshot
import ventilator.prototype.TemperatureReading

enum class MonitorDisplayState { LOADING, RUNNING, STOPPED, UNAVAILABLE, NO_FANS, ERROR }

data class FanUiItem(
    val index: Int,
    val rpm: String,
    val level: Int?,
    val state: String,
    val range: String,
    val key: String,
)

data class TemperatureUiItem(
    val component: String,
    val key: String,
    val value: String,
    val available: Boolean,
    val description: String,
)

data class MonitorUiState(
    val displayState: MonitorDisplayState = MonitorDisplayState.LOADING,
    val temperatures: List<TemperatureUiItem> = MonitorUiMapper.emptyTemperatures(),
    val fans: List<FanUiItem> = emptyList(),
    val refreshing: Boolean = true,
    val error: String? = null,
    val updatedAt: String? = null,
    val trayReading: TrayReading = TrayReading(),
)

object MonitorUiMapper {
    private val timeFormat = DateTimeFormatter.ofPattern("HH:mm:ss").withZone(ZoneId.systemDefault())

    fun emptyTemperatures(): List<TemperatureUiItem> = temperatures(null)

    fun map(status: StatusSnapshot?, refreshing: Boolean, error: String?): MonitorUiState = MonitorUiState(
        displayState = displayState(status, error),
        temperatures = temperatures(status),
        fans = status?.fans.orEmpty().map(::fanItem),
        refreshing = refreshing,
        error = error,
        updatedAt = status?.cpuTemperature?.measuredAt?.let(timeFormat::format),
        trayReading = TrayReading.from(status.takeIf { error == null }),
    )

    private fun temperatures(status: StatusSnapshot?): List<TemperatureUiItem> = listOf(
        temperatureItem("CPU", "TCMz", "Максимум кристалла", status?.cpuTemperature),
        temperatureItem("GPU", "Tg0D", "Датчик GPU · проверен нагрузкой", status?.selectedTemperatures?.firstOrNull { it.rawKey == "Tg0D" }),
        temperatureItem("SSD", "TH0a", "Датчик SSD · проверен нагрузкой", status?.selectedTemperatures?.firstOrNull { it.rawKey == "TH0a" }),
    )

    private fun temperatureItem(component: String, key: String, description: String, reading: TemperatureReading?): TemperatureUiItem =
        TemperatureUiItem(
            component = component,
            key = key,
            value = if (reading?.rawKey == key && reading.availability == ReadingAvailability.AVAILABLE) numberOneDecimal(reading.celsius) else "—",
            available = reading?.rawKey == key && reading.availability == ReadingAvailability.AVAILABLE,
            description = description,
        )

    private fun displayState(status: StatusSnapshot?, error: String?): MonitorDisplayState = when {
        status == null && error != null -> MonitorDisplayState.ERROR
        status == null -> MonitorDisplayState.LOADING
        status.declaredFanCount == 0 -> MonitorDisplayState.NO_FANS
        status.declaredFanCount == null || status.fans.size != status.declaredFanCount ||
            status.fans.any { it.availability == ReadingAvailability.UNAVAILABLE } -> MonitorDisplayState.UNAVAILABLE
        status.fans.all { it.actualRpm == 0.0 } -> MonitorDisplayState.STOPPED
        else -> MonitorDisplayState.RUNNING
    }

    private fun fanItem(fan: FanSnapshot): FanUiItem {
        val minimum = fan.minRpm
        val maximum = fan.maxRpm
        return FanUiItem(
            index = fan.index,
            rpm = if (fan.availability == ReadingAvailability.AVAILABLE) number(fan.actualRpm) else "—",
            level = fan.level(),
            state = when {
                fan.availability == ReadingAvailability.UNAVAILABLE -> "Показание недоступно"
                fan.actualRpm == 0.0 -> "Остановлен"
                else -> "Вращается"
            },
            range = if (minimum != null && maximum != null && maximum > minimum) {
                "${number(minimum)}–${number(maximum)} RPM"
            } else "Диапазон недоступен",
            key = fan.actualKey,
        )
    }

    private fun numberOneDecimal(value: Double?): String =
        if (value != null && value.isFinite()) String.format(Locale.US, "%.1f", value) else "—"

    private fun number(value: Double?): String =
        if (value != null && value.isFinite()) String.format(Locale.US, "%.0f", value) else "—"
}
