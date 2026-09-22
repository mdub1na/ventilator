package ventilator.desktop.monitoring.ui

import java.time.ZoneId
import java.time.format.DateTimeFormatter
import java.util.Locale
import ventilator.desktop.menubar.TrayReading
import ventilator.prototype.DiagnosticsSnapshot
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
    val key: String,
    val value: String,
    val available: Boolean,
)

data class MonitorUiState(
    val displayState: MonitorDisplayState = MonitorDisplayState.LOADING,
    val cpuValue: String = "—",
    val cpuAvailable: Boolean = false,
    val fans: List<FanUiItem> = emptyList(),
    val selectedTemperatures: List<TemperatureUiItem> = emptyList(),
    val diagnostics: List<TemperatureUiItem> = emptyList(),
    val diagnosticCount: Int = 0,
    val diagnosticsUnavailable: Boolean = false,
    val diagnosticsExpanded: Boolean = false,
    val diagnosticsRefreshing: Boolean = false,
    val diagnosticsError: String? = null,
    val query: String = "",
    val refreshing: Boolean = true,
    val error: String? = null,
    val updatedAt: String? = null,
    val trayReading: TrayReading = TrayReading(),
)

object MonitorUiMapper {
    private val timeFormat = DateTimeFormatter.ofPattern("HH:mm:ss").withZone(ZoneId.systemDefault())

    fun map(
        status: StatusSnapshot?,
        diagnostics: DiagnosticsSnapshot?,
        refreshing: Boolean,
        error: String?,
        diagnosticsExpanded: Boolean,
        diagnosticsRefreshing: Boolean,
        diagnosticsError: String?,
        query: String,
    ): MonitorUiState {
        val readings = diagnostics?.temperatures.orEmpty()
        val visibleReadings = readings.filter { it.rawKey.contains(query.trim(), ignoreCase = true) }
        return MonitorUiState(
            displayState = displayState(status, error),
            cpuValue = status?.cpuTemperature?.displayValue() ?: "—",
            cpuAvailable = status?.cpuTemperature?.availability == ReadingAvailability.AVAILABLE,
            fans = status?.fans.orEmpty().map(::fanItem),
            selectedTemperatures = status?.selectedTemperatures.orEmpty().map(::temperatureItem),
            diagnostics = visibleReadings.map(::temperatureItem),
            diagnosticCount = readings.size,
            diagnosticsUnavailable = diagnostics != null && diagnostics.temperatures == null,
            diagnosticsExpanded = diagnosticsExpanded,
            diagnosticsRefreshing = diagnosticsRefreshing,
            diagnosticsError = diagnosticsError,
            query = query,
            refreshing = refreshing,
            error = error,
            updatedAt = status?.cpuTemperature?.measuredAt?.let(timeFormat::format),
            trayReading = TrayReading.from(status.takeIf { error == null }),
        )
    }

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

    private fun temperatureItem(reading: TemperatureReading) = TemperatureUiItem(
        key = reading.rawKey,
        value = reading.displayValue(),
        available = reading.availability == ReadingAvailability.AVAILABLE,
    )

    private fun TemperatureReading.displayValue(): String =
        if (availability == ReadingAvailability.AVAILABLE) String.format(Locale.US, "%.1f", celsius) else "—"

    private fun number(value: Double?): String =
        if (value != null && value.isFinite()) String.format(Locale.US, "%.0f", value) else "—"
}
