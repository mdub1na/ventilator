package ventilator.desktop

import androidx.compose.material3.ExperimentalMaterial3ExpressiveApi
import androidx.compose.material3.MaterialExpressiveTheme
import androidx.compose.material3.darkColorScheme
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.setValue
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.unit.DpSize
import androidx.compose.ui.unit.dp
import androidx.compose.ui.window.Window
import androidx.compose.ui.window.application
import androidx.compose.ui.window.rememberWindowState
import java.nio.file.Path
import ventilator.desktop.monitoring.data.SmcMonitorRepository
import ventilator.desktop.menubar.MenuBarBridge
import ventilator.desktop.menubar.MenuCommand
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
    val packagedResources = System.getProperty("compose.application.resources.dir")?.let(Path::of)
    val probe = (args.firstOrNull()?.let(Path::of) ?: packagedResources?.resolve("smc-read") ?: Path.of("../smc-read/smc-read"))
        .toAbsolutePath().normalize()
    val statusExecutable = (packagedResources?.resolve("status-item-bridge") ?: Path.of("../menu-bar/status-item-bridge"))
        .toAbsolutePath().normalize()
    val scope = rememberCoroutineScope()
    val viewModel = remember(probe) { MonitorViewModel(SmcMonitorRepository(probe), scope) }
    var windowVisible by remember { mutableStateOf(true) }
    var openRequest by remember { mutableIntStateOf(0) }
    var statusItemError by remember { mutableStateOf<String?>(null) }
    val bridge = remember(statusExecutable) {
        MenuBarBridge(
            executable = statusExecutable,
            scope = scope,
            onCommand = { command ->
                when (command) {
                    MenuCommand.SHOW -> {
                        windowVisible = true
                        openRequest++
                    }
                    MenuCommand.HIDE -> windowVisible = false
                    MenuCommand.TOGGLE -> {
                        if (windowVisible) windowVisible = false else {
                            windowVisible = true
                            openRequest++
                        }
                    }
                    MenuCommand.QUIT -> exitApplication()
                }
            },
            onLost = { message ->
                statusItemError = message
                windowVisible = true
            },
        )
    }
    DisposableEffect(viewModel, bridge) {
        bridge.start()
        onDispose {
            bridge.stop()
            viewModel.stop()
        }
    }
    val uiState by viewModel.uiState.collectAsState()
    LaunchedEffect(uiState.trayReading, windowVisible) {
        bridge.send(uiState.trayReading, windowVisible)
    }

    Window(
        onCloseRequest = { windowVisible = false },
        title = "Ventilator · Мониторинг",
        visible = windowVisible,
        state = rememberWindowState(size = DpSize(1040.dp, 650.dp)),
    ) {
        LaunchedEffect(windowVisible, openRequest) {
            if (windowVisible) {
                window.toFront()
                window.requestFocus()
            }
        }
        MaterialExpressiveTheme(colorScheme = colors) {
            MonitorScreen(viewModel, statusItemError)
        }
    }
}
