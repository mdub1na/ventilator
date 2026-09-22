package ventilator.desktop.monitoring.ui

import androidx.compose.material3.ExperimentalMaterial3ExpressiveApi
import androidx.compose.material3.MaterialExpressiveTheme
import androidx.compose.runtime.Composable
import androidx.compose.ui.tooling.preview.Preview
import ventilator.desktop.login.domain.LoginItemStatus
import ventilator.desktop.login.ui.LoginItemUiMapper

object MonitorPreviews {
    val running = MonitorUiState(
        displayState = MonitorDisplayState.RUNNING,
        temperatures = listOf(
            TemperatureUiItem("CPU", "TCMz", "58.6", true, "Максимум кристалла"),
            TemperatureUiItem("GPU", "Tg0D", "49.2", true, "Датчик GPU · проверен нагрузкой"),
            TemperatureUiItem("SSD", "TH0a", "33.4", true, "Предварительная привязка"),
        ),
        fans = listOf(
            FanUiItem(0, "1354", 1, "Вращается", "1350–5349 RPM", "F0Ac"),
            FanUiItem(1, "2450", 2, "Вращается", "1458–5777 RPM", "F1Ac"),
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
        temperatures = running.temperatures.map { it.copy(value = "—", available = false) },
        fans = listOf(running.fans.first(), running.fans.last().copy(rpm = "—", level = null, state = "Показание недоступно")),
    )
    val error = MonitorUiState(displayState = MonitorDisplayState.ERROR, error = "Не удалось прочитать датчики. Повторите попытку.", refreshing = false)
    val loading = MonitorUiState()
}

@OptIn(ExperimentalMaterial3ExpressiveApi::class)
@Composable
private fun Fixture(state: MonitorUiState) {
    MaterialExpressiveTheme {
        MonitorContent(state, onAction = {}, loginItemState = LoginItemUiMapper.map(LoginItemStatus.DISABLED, false, null))
    }
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
