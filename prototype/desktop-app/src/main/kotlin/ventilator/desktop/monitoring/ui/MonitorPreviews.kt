package ventilator.desktop.monitoring.ui

import androidx.compose.material3.ExperimentalMaterial3ExpressiveApi
import androidx.compose.material3.MaterialExpressiveTheme
import androidx.compose.runtime.Composable
import androidx.compose.ui.tooling.preview.Preview

object MonitorPreviews {
    val running = MonitorUiState(
        displayState = MonitorDisplayState.RUNNING,
        cpuValue = "58.6",
        cpuAvailable = true,
        fans = listOf(
            FanUiItem(0, "1354", 1, "Вращается", "1350–5349 RPM", "F0Ac"),
            FanUiItem(1, "2450", 2, "Вращается", "1458–5777 RPM", "F1Ac"),
        ),
        selectedTemperatures = listOf(
            TemperatureUiItem("TAOL", "29.8", true),
            TemperatureUiItem("TB0T", "33.8", true),
            TemperatureUiItem("TCMb", "55.4", true),
        ),
        updatedAt = "12:34:56",
        refreshing = false,
    )
    val stopped = running.copy(
        displayState = MonitorDisplayState.STOPPED,
        fans = running.fans.map { it.copy(rpm = "0", level = 0, state = "Остановлен") },
    )
    val unavailable = running.copy(
        displayState = MonitorDisplayState.UNAVAILABLE,
        cpuValue = "—",
        cpuAvailable = false,
        fans = listOf(running.fans.first(), running.fans.last().copy(rpm = "—", level = null, state = "Показание недоступно")),
        selectedTemperatures = running.selectedTemperatures.map { it.copy(value = "—", available = false) },
    )
    val error = MonitorUiState(displayState = MonitorDisplayState.ERROR, error = "Не удалось прочитать датчики. Повторите попытку.", refreshing = false)
    val loading = MonitorUiState()
}

@OptIn(ExperimentalMaterial3ExpressiveApi::class)
@Composable
private fun Fixture(state: MonitorUiState) {
    MaterialExpressiveTheme { MonitorContent(state, onAction = {}) }
}

@Preview
@Composable
private fun RunningPreview() = Fixture(MonitorPreviews.running)

@Preview
@Composable
private fun StoppedPreview() = Fixture(MonitorPreviews.stopped)

@Preview
@Composable
private fun UnavailablePreview() = Fixture(MonitorPreviews.unavailable)

@Preview
@Composable
private fun ErrorPreview() = Fixture(MonitorPreviews.error)

@Preview
@Composable
private fun LoadingPreview() = Fixture(MonitorPreviews.loading)
