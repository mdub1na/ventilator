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
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.ElevatedCard
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp

@Composable
fun MonitorScreen(viewModel: MonitorViewModel) {
    val state by viewModel.uiState.collectAsState()
    MonitorContent(state, viewModel::onAction)
}

@Composable
fun MonitorContent(state: MonitorUiState, onAction: (MonitorUiAction) -> Unit) {
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
                Button(onClick = { onAction(MonitorUiAction.Refresh) }, enabled = !state.refreshing) {
                    Text(if (state.refreshing) "Обновляем…" else "Обновить")
                }
            }

            state.error?.let { message ->
                Surface(color = colors.errorContainer, shape = RoundedCornerShape(20.dp), modifier = Modifier.fillMaxWidth()) {
                    Text(message, color = colors.onErrorContainer, modifier = Modifier.padding(18.dp))
                }
            }

            Row(horizontalArrangement = Arrangement.spacedBy(16.dp), modifier = Modifier.fillMaxWidth()) {
                ElevatedCard(modifier = Modifier.weight(1.5f), shape = RoundedCornerShape(32.dp)) {
                    Column(modifier = Modifier.padding(26.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                        Text("ТЕМПЕРАТУРА CPU", style = MaterialTheme.typography.labelMedium, color = colors.primary)
                        Row(verticalAlignment = Alignment.Bottom) {
                            Text(state.cpuValue, style = MaterialTheme.typography.displayLarge, fontWeight = FontWeight.Bold)
                            Text(" °C", style = MaterialTheme.typography.headlineSmall, modifier = Modifier.padding(bottom = 8.dp))
                        }
                        Text("Максимум кристалла · TCMz", color = colors.onSurfaceVariant)
                        Text("Подпись проверена на этой модели", style = MaterialTheme.typography.labelSmall, color = colors.onSurfaceVariant)
                    }
                }
                ElevatedCard(modifier = Modifier.weight(1f), shape = RoundedCornerShape(32.dp)) {
                    Column(modifier = Modifier.padding(26.dp), verticalArrangement = Arrangement.spacedBy(10.dp)) {
                        Text("СОСТОЯНИЕ", style = MaterialTheme.typography.labelMedium, color = colors.primary)
                        Text(state.displayState.label(), style = MaterialTheme.typography.titleLarge, fontWeight = FontWeight.SemiBold)
                        Text(
                            state.updatedAt?.let { "Снимок обработан в $it" } ?: "Ожидаем первое чтение",
                            style = MaterialTheme.typography.bodySmall,
                            color = colors.onSurfaceVariant,
                        )
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

            Column(verticalArrangement = Arrangement.spacedBy(12.dp)) {
                SectionHeading("Другие температуры", "Сырые имена SMC · назначение компонентов пока не подтверждено")
                Row(horizontalArrangement = Arrangement.spacedBy(12.dp), modifier = Modifier.fillMaxWidth()) {
                    state.selectedTemperatures.forEach { temperature ->
                        TemperatureCard(temperature, Modifier.weight(1f))
                    }
                    if (state.selectedTemperatures.isEmpty()) {
                        Text("Ожидаем показания датчиков", color = colors.onSurfaceVariant)
                    }
                }
            }

            DiagnosticsCard(state, onAction)
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
    ElevatedCard(modifier = modifier, shape = RoundedCornerShape(22.dp)) {
        Column(modifier = Modifier.padding(18.dp), verticalArrangement = Arrangement.spacedBy(5.dp)) {
            Text(temperature.key, style = MaterialTheme.typography.labelLarge, color = MaterialTheme.colorScheme.primary)
            Text(if (temperature.available) "${temperature.value} °C" else "— °C", style = MaterialTheme.typography.titleLarge)
        }
    }
}

@Composable
private fun DiagnosticsCard(state: MonitorUiState, onAction: (MonitorUiAction) -> Unit) {
    val colors = MaterialTheme.colorScheme
    ElevatedCard(modifier = Modifier.fillMaxWidth(), shape = RoundedCornerShape(28.dp)) {
        Column(modifier = Modifier.padding(22.dp), verticalArrangement = Arrangement.spacedBy(14.dp)) {
            Row(verticalAlignment = Alignment.CenterVertically, modifier = Modifier.fillMaxWidth()) {
                Column(modifier = Modifier.weight(1f)) {
                    Text("Диагностика датчиков", style = MaterialTheme.typography.titleLarge, fontWeight = FontWeight.SemiBold)
                    Text("Все температурные ключи в исходном виде", color = colors.onSurfaceVariant)
                }
                Button(
                    onClick = { onAction(MonitorUiAction.DiagnosticsToggle) },
                    colors = ButtonDefaults.buttonColors(containerColor = colors.secondaryContainer, contentColor = colors.onSecondaryContainer),
                ) { Text(if (state.diagnosticsExpanded) "Скрыть" else "Открыть") }
            }
            if (state.diagnosticsExpanded) {
                Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(12.dp)) {
                    OutlinedTextField(
                        value = state.query,
                        onValueChange = { onAction(MonitorUiAction.SearchChanged(it)) },
                        label = { Text("Поиск по SMC-ключу") },
                        singleLine = true,
                        modifier = Modifier.weight(1f),
                    )
                    Button(onClick = { onAction(MonitorUiAction.DiagnosticsRefresh) }, enabled = !state.diagnosticsRefreshing) {
                        Text("Обновить список")
                    }
                }
                if (state.diagnosticsRefreshing) CircularProgressIndicator(modifier = Modifier.size(22.dp))
                state.diagnosticsError?.let { Text(it, color = colors.error) }
                if (state.diagnosticsUnavailable) Text("Перечисление температурных ключей недоступно", color = colors.onSurfaceVariant)
                else Text("Найдено ключей: ${state.diagnosticCount}", style = MaterialTheme.typography.labelMedium, color = colors.onSurfaceVariant)
                LazyColumn(modifier = Modifier.fillMaxWidth().height(228.dp), verticalArrangement = Arrangement.spacedBy(2.dp)) {
                    items(state.diagnostics, key = { it.key }) { item ->
                        Row(modifier = Modifier.fillMaxWidth().padding(vertical = 5.dp), verticalAlignment = Alignment.CenterVertically) {
                            Text(item.key, modifier = Modifier.weight(1f), style = MaterialTheme.typography.bodyMedium)
                            Text(if (item.available) "${item.value} °C" else "—", style = MaterialTheme.typography.bodyMedium)
                        }
                    }
                }
            }
        }
    }
}

private fun MonitorDisplayState.label(): String = when (this) {
    MonitorDisplayState.LOADING -> "Ожидаем данные"
    MonitorDisplayState.RUNNING -> "Вентиляторы работают"
    MonitorDisplayState.STOPPED -> "Вентиляторы остановлены"
    MonitorDisplayState.UNAVAILABLE -> "Часть данных недоступна"
    MonitorDisplayState.NO_FANS -> "Вентиляторов нет"
    MonitorDisplayState.ERROR -> "Ошибка чтения"
}
