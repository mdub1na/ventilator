package ventilator.desktop.settings.ui

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import ventilator.desktop.login.ui.LoginItemSection
import ventilator.desktop.login.ui.LoginItemUiAction
import ventilator.desktop.login.ui.LoginItemUiState
import ventilator.desktop.login.ui.LoginItemViewModel

@Composable
fun SettingsScreen(viewModel: LoginItemViewModel, onBack: () -> Unit) {
    val state by viewModel.uiState.collectAsState()
    SettingsContent(state, viewModel::onAction, onBack)
}

@Composable
fun SettingsContent(
    state: LoginItemUiState,
    onAction: (LoginItemUiAction) -> Unit = {},
    onBack: () -> Unit = {},
) {
    val colors = MaterialTheme.colorScheme
    Surface(color = colors.background, modifier = Modifier.fillMaxSize()) {
        Column(
            modifier = Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(28.dp),
            verticalArrangement = Arrangement.spacedBy(24.dp),
        ) {
            Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(12.dp)) {
                IconButton(onClick = onBack, modifier = Modifier.semantics { contentDescription = "Назад к мониторингу" }) {
                    Text("←", style = MaterialTheme.typography.headlineMedium, color = colors.onSurface)
                }
                Column {
                    Text("VENTILATOR  /  НАСТРОЙКИ", style = MaterialTheme.typography.labelMedium, color = colors.primary)
                    Text("Настройки", style = MaterialTheme.typography.headlineLarge, fontWeight = FontWeight.Bold)
                }
            }
            Column(verticalArrangement = Arrangement.spacedBy(12.dp), modifier = Modifier.fillMaxWidth()) {
                Text("Запуск", style = MaterialTheme.typography.titleLarge, fontWeight = FontWeight.SemiBold)
                LoginItemSection(state, onAction)
            }
        }
    }
}
