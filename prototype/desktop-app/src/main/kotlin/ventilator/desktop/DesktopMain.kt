package ventilator.desktop

import androidx.compose.material3.ExperimentalMaterial3ExpressiveApi
import androidx.compose.material3.MaterialExpressiveTheme
import androidx.compose.material3.darkColorScheme
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.unit.DpSize
import androidx.compose.ui.unit.dp
import androidx.compose.ui.window.Window
import androidx.compose.ui.window.application
import androidx.compose.ui.window.rememberWindowState
import java.nio.file.Path
import ventilator.desktop.monitoring.data.SmcMonitorRepository
import ventilator.desktop.monitoring.ui.MonitorScreen
import ventilator.desktop.monitoring.ui.MonitorViewModel

private val colors = darkColorScheme(
    primary = Color(0xFF9CE7C4),
    onPrimary = Color(0xFF103927),
    secondaryContainer = Color(0xFF31465B),
    onSecondaryContainer = Color(0xFFD9EBFF),
    background = Color(0xFF10191D),
    surface = Color(0xFF1B262B),
    surfaceContainerLow = Color(0xFF1A252A),
    onSurface = Color(0xFFE5F0EC),
    onSurfaceVariant = Color(0xFFA7B9B5),
    outline = Color(0xFF7D928D),
)

@OptIn(ExperimentalMaterial3ExpressiveApi::class)
fun main(args: Array<String>) = application {
    val packagedReader = System.getProperty("compose.application.resources.dir")?.let { Path.of(it, "smc-read") }
    val probe = (args.firstOrNull()?.let(Path::of) ?: packagedReader ?: Path.of("../smc-read/smc-read"))
        .toAbsolutePath().normalize()
    val scope = rememberCoroutineScope()
    val viewModel = remember(probe) { MonitorViewModel(SmcMonitorRepository(probe), scope) }
    DisposableEffect(viewModel) {
        onDispose {
            viewModel.stop()
        }
    }

    Window(
        onCloseRequest = ::exitApplication,
        title = "Ventilator · Мониторинг",
        state = rememberWindowState(size = DpSize(1040.dp, 780.dp)),
    ) {
        MaterialExpressiveTheme(colorScheme = colors) {
            MonitorScreen(viewModel)
        }
    }
}
