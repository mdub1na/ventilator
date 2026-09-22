package ventilator.desktop.navigation

import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow

enum class DesktopDestination { MONITOR, SETTINGS }

sealed interface DesktopNavigationAction {
    data object OpenMonitor : DesktopNavigationAction
    data object OpenSettings : DesktopNavigationAction
}

/** Keeps navigation shared between Compose controls and commands from the status item. */
class DesktopNavigation {
    private val mutableDestination = MutableStateFlow(DesktopDestination.MONITOR)
    val destination: StateFlow<DesktopDestination> = mutableDestination

    fun onAction(action: DesktopNavigationAction) {
        mutableDestination.value = when (action) {
            DesktopNavigationAction.OpenMonitor -> DesktopDestination.MONITOR
            DesktopNavigationAction.OpenSettings -> DesktopDestination.SETTINGS
        }
    }
}
