package ventilator.desktop.monitoring.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Button
import androidx.compose.material3.ElevatedCard
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp

@Composable
fun MonitorScreen(viewModel: MonitorViewModel, onOpenSettings: () -> Unit, statusItemError: String? = null) {
    val state by viewModel.uiState.collectAsState()
    MonitorContent(state, viewModel::onAction, onOpenSettings, statusItemError)
}

@Composable
fun MonitorContent(
    state: MonitorUiState,
    onAction: (MonitorUiAction) -> Unit,
    onOpenSettings: () -> Unit = {},
    statusItemError: String? = null,
) {
    val colors = MaterialTheme.colorScheme
    Surface(color = colors.background, modifier = Modifier.fillMaxSize()) {
        Column(
            modifier = Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(28.dp),
            verticalArrangement = Arrangement.spacedBy(22.dp),
        ) {
            Row(verticalAlignment = Alignment.CenterVertically, modifier = Modifier.fillMaxWidth()) {
                Column(modifier = Modifier.weight(1f)) {
                    Text("VENTILATOR  /  MAC15,7", style = MaterialTheme.typography.labelMedium, color = colors.primary)
                    Text("Мониторинг системы", style = MaterialTheme.typography.headlineLarge, fontWeight = FontWeight.Bold)
                    Text("Локальные показания AppleSMC · только чтение", style = MaterialTheme.typography.bodyMedium, color = colors.onSurfaceVariant)
                }
                Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(12.dp)) {
                    Button(onClick = { onAction(MonitorUiAction.Refresh) }, enabled = !state.refreshing) {
                        Text(if (state.refreshing) "Обновляем…" else "Обновить")
                    }
                    IconButton(onClick = onOpenSettings, modifier = Modifier.semantics { contentDescription = "Настройки" }) {
                        Text("⚙", style = MaterialTheme.typography.headlineMedium, color = colors.onSurface)
                    }
                }
            }

            state.error?.let { message ->
                Surface(color = colors.errorContainer, shape = RoundedCornerShape(20.dp), modifier = Modifier.fillMaxWidth()) {
                    Text(message, color = colors.onErrorContainer, modifier = Modifier.padding(18.dp))
                }
            }
            statusItemError?.let { message ->
                Surface(color = colors.errorContainer, shape = RoundedCornerShape(20.dp), modifier = Modifier.fillMaxWidth()) {
                    Text(message, color = colors.onErrorContainer, modifier = Modifier.padding(18.dp))
                }
            }

            Column(verticalArrangement = Arrangement.spacedBy(12.dp)) {
                SectionHeading("Температуры", state.updatedAt?.let { "Снимок обработан в $it" } ?: "Ожидаем первое чтение")
                Row(horizontalArrangement = Arrangement.spacedBy(16.dp), modifier = Modifier.fillMaxWidth()) {
                    state.temperatures.forEach { temperature ->
                        TemperatureCard(temperature, Modifier.weight(1f))
                    }
                }
            }

            Column(verticalArrangement = Arrangement.spacedBy(12.dp)) {
                SectionHeading("Вентиляторы", "Фактические обороты и диапазон каждого вентилятора")
                if (state.fans.isEmpty()) {
                    ElevatedCard(modifier = Modifier.fillMaxWidth(), shape = RoundedCornerShape(26.dp)) {
                        Text(
                            if (state.displayState == MonitorDisplayState.NO_FANS) "На этой машине вентиляторы не обнаружены"
                            else if (state.displayState == MonitorDisplayState.ERROR) "Данные вентиляторов не удалось получить"
                            else "Ожидаем показания вентиляторов",
                            modifier = Modifier.padding(24.dp),
                        )
                    }
                } else {
                    Row(horizontalArrangement = Arrangement.spacedBy(16.dp), modifier = Modifier.fillMaxWidth()) {
                        state.fans.forEach { fan -> FanCard(fan, Modifier.weight(1f)) }
                    }
                }
            }

            Text(
                "Скоростью вентиляторов управляет macOS. Нулевые обороты — показание датчика, а не предупреждение о поломке.",
                style = MaterialTheme.typography.bodySmall,
                color = colors.onSurfaceVariant,
            )
        }
    }
}

@Composable
private fun SectionHeading(title: String, subtitle: String) {
    Column(verticalArrangement = Arrangement.spacedBy(2.dp)) {
        Text(title, style = MaterialTheme.typography.titleLarge, fontWeight = FontWeight.SemiBold)
        Text(subtitle, style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
    }
}

@Composable
private fun FanCard(fan: FanUiItem, modifier: Modifier = Modifier) {
    val colors = MaterialTheme.colorScheme
    ElevatedCard(modifier = modifier, shape = RoundedCornerShape(28.dp)) {
        Column(modifier = Modifier.padding(22.dp), verticalArrangement = Arrangement.spacedBy(12.dp)) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Text("ВЕНТИЛЯТОР ${fan.index}", style = MaterialTheme.typography.labelLarge, modifier = Modifier.weight(1f))
                Text(fan.key, style = MaterialTheme.typography.labelSmall, color = colors.onSurfaceVariant)
            }
            Row(verticalAlignment = Alignment.Bottom) {
                Text(fan.rpm, style = MaterialTheme.typography.displaySmall, fontWeight = FontWeight.Bold)
                Text(" RPM", style = MaterialTheme.typography.titleMedium, modifier = Modifier.padding(bottom = 6.dp))
            }
            FanGauge(fan.level)
            Text(fan.state, style = MaterialTheme.typography.bodyMedium, color = colors.primary)
            Text("Диапазон: ${fan.range}", style = MaterialTheme.typography.bodySmall, color = colors.onSurfaceVariant)
        }
    }
}

@Composable
private fun FanGauge(level: Int?) {
    val colors = listOf(Color(0xFF42C985), Color(0xFF92D46E), Color(0xFFF2CB64), Color(0xFFF1A069), Color(0xFFED747C))
    Row(horizontalArrangement = Arrangement.spacedBy(6.dp), verticalAlignment = Alignment.Bottom, modifier = Modifier.height(42.dp)) {
        colors.forEachIndexed { index, color ->
            val shape = RoundedCornerShape(6.dp)
            Box(
                modifier = Modifier.width(22.dp).height((18 + index * 5).dp)
                    .background(if (level != null && index < level) color else Color.Transparent, shape)
                    .border(1.dp, MaterialTheme.colorScheme.outline, shape),
            )
        }
        Spacer(Modifier.width(8.dp))
        Text(level?.let { "$it/5" } ?: "—/5", color = MaterialTheme.colorScheme.onSurfaceVariant)
    }
}

@Composable
private fun TemperatureCard(temperature: TemperatureUiItem, modifier: Modifier = Modifier) {
    val colors = MaterialTheme.colorScheme
    ElevatedCard(modifier = modifier, shape = RoundedCornerShape(28.dp)) {
        Column(modifier = Modifier.padding(22.dp), verticalArrangement = Arrangement.spacedBy(10.dp)) {
            Text(temperature.component, style = MaterialTheme.typography.labelLarge, color = colors.primary)
            Row(verticalAlignment = Alignment.Bottom) {
                Text(
                    temperature.value,
                    style = MaterialTheme.typography.displaySmall,
                    fontWeight = FontWeight.Bold,
                    color = if (temperature.available) colors.onSurface else colors.onSurfaceVariant,
                )
                Text(" °C", style = MaterialTheme.typography.titleMedium, modifier = Modifier.padding(bottom = 6.dp))
            }
            Text("${temperature.description} · ${temperature.key}", style = MaterialTheme.typography.bodySmall, color = colors.onSurfaceVariant)
        }
    }
}
