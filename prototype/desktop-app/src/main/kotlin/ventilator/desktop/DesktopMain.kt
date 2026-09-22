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
import kotlinx.coroutines.delay
import ventilator.desktop.login.data.NativeLoginItemRepository
import ventilator.desktop.login.domain.LaunchMode
import ventilator.desktop.login.ui.LoginItemViewModel
import ventilator.desktop.monitoring.data.SmcMonitorRepository
import ventilator.desktop.menubar.MenuBarBridge
import ventilator.desktop.menubar.MenuCommand
import ventilator.desktop.monitoring.ui.MonitorScreen
import ventilator.desktop.monitoring.ui.MonitorViewModel
import ventilator.desktop.navigation.DesktopDestination
import ventilator.desktop.navigation.DesktopNavigation
import ventilator.desktop.navigation.DesktopNavigationAction
import ventilator.desktop.settings.ui.SettingsScreen

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
fun main(args: Array<String>) {
    val packagedResources = System.getProperty("compose.application.resources.dir")?.let(Path::of)
    // Install the Apple Event observer before Compose initializes AppKit.
    val loginItemRepository = NativeLoginItemRepository.fromPackagedResources(packagedResources)
    val probe = (args.firstOrNull()?.let(Path::of) ?: packagedResources?.resolve("smc-read") ?: Path.of("../smc-read/smc-read"))
        .toAbsolutePath().normalize()
    val statusExecutable = (packagedResources?.resolve("status-item-bridge") ?: Path.of("../menu-bar/status-item-bridge"))
        .toAbsolutePath().normalize()
    application {
        val scope = rememberCoroutineScope()
        val viewModel = remember(probe) { MonitorViewModel(SmcMonitorRepository(probe), scope) }
        val loginItemViewModel = remember(loginItemRepository) { LoginItemViewModel(loginItemRepository, scope) }
        val navigation = remember { DesktopNavigation() }
        val destination by navigation.destination.collectAsState()
        var windowVisible by remember { mutableStateOf(!loginItemRepository.canDetectLaunch) }
        var openRequest by remember { mutableIntStateOf(0) }
        var statusItemError by remember { mutableStateOf<String?>(null) }
        LaunchedEffect(loginItemRepository) {
            if (loginItemRepository.canDetectLaunch) {
                repeat(30) {
                    when (loginItemRepository.launchMode()) {
                        LaunchMode.LOGIN_ITEM -> return@LaunchedEffect
                        LaunchMode.MANUAL -> {
                            windowVisible = true
                            return@LaunchedEffect
                        }
                        LaunchMode.PENDING -> delay(100)
                    }
                }
                // An absent launch event must not hide a manually opened app forever.
                windowVisible = true
            }
        }
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
                        MenuCommand.SETTINGS -> {
                            navigation.onAction(DesktopNavigationAction.OpenSettings)
                            windowVisible = true
                            openRequest++
                        }
                        MenuCommand.QUIT -> exitApplication()
                    }
                },
                onLost = { message ->
                    statusItemError = message
                    navigation.onAction(DesktopNavigationAction.OpenMonitor)
                    windowVisible = true
                },
            )
        }
        DisposableEffect(viewModel, loginItemViewModel, bridge) {
            bridge.start()
            onDispose {
                bridge.stop()
                viewModel.stop()
                loginItemViewModel.stop()
            }
        }
        val uiState by viewModel.uiState.collectAsState()
        LaunchedEffect(uiState.trayReading, windowVisible) {
            bridge.send(uiState.trayReading, windowVisible)
        }

        Window(
            onCloseRequest = { windowVisible = false },
            title = if (destination == DesktopDestination.MONITOR) "Ventilator · Мониторинг" else "Ventilator · Настройки",
            visible = windowVisible,
            state = rememberWindowState(size = DpSize(1040.dp, 860.dp)),
        ) {
            LaunchedEffect(windowVisible, openRequest) {
                if (windowVisible) {
                    window.toFront()
                    window.requestFocus()
                }
            }
            MaterialExpressiveTheme(colorScheme = colors) {
                when (destination) {
                    DesktopDestination.MONITOR -> MonitorScreen(
                        viewModel,
                        onOpenSettings = { navigation.onAction(DesktopNavigationAction.OpenSettings) },
                        statusItemError = statusItemError,
                    )
                    DesktopDestination.SETTINGS -> SettingsScreen(
                        loginItemViewModel,
                        onBack = { navigation.onAction(DesktopNavigationAction.OpenMonitor) },
                    )
                }
            }
        }
    }
}
