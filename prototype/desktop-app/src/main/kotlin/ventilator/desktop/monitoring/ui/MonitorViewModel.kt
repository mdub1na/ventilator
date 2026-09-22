package ventilator.desktop.monitoring.ui

import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharingStarted
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.combine
import kotlinx.coroutines.flow.stateIn
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import ventilator.desktop.monitoring.domain.MonitorRepository

sealed interface MonitorUiAction {
    data object Refresh : MonitorUiAction
    data object DiagnosticsToggle : MonitorUiAction
    data object DiagnosticsRefresh : MonitorUiAction
    data class SearchChanged(val query: String) : MonitorUiAction
}

private data class UiFlags(
    val refreshing: Boolean = true,
    val error: String? = null,
    val diagnosticsExpanded: Boolean = false,
    val diagnosticsRefreshing: Boolean = false,
    val diagnosticsError: String? = null,
    val query: String = "",
)

class MonitorViewModel(
    private val repository: MonitorRepository,
    private val scope: CoroutineScope,
) {
    private val flags = MutableStateFlow(UiFlags())
    private val statusMutex = Mutex()
    private val diagnosticsMutex = Mutex()

    val uiState: StateFlow<MonitorUiState> = combine(repository.status, repository.diagnostics, flags) { status, diagnostics, currentFlags ->
        MonitorUiMapper.map(
            status = status,
            diagnostics = diagnostics,
            refreshing = currentFlags.refreshing,
            error = currentFlags.error,
            diagnosticsExpanded = currentFlags.diagnosticsExpanded,
            diagnosticsRefreshing = currentFlags.diagnosticsRefreshing,
            diagnosticsError = currentFlags.diagnosticsError,
            query = currentFlags.query,
        )
    }.stateIn(scope, SharingStarted.Eagerly, MonitorUiState())

    private val polling: Job = scope.launch {
        while (isActive) {
            refreshStatus()
            delay(2_000)
        }
    }

    fun onAction(action: MonitorUiAction) {
        when (action) {
            MonitorUiAction.Refresh -> scope.launch { refreshStatus() }
            MonitorUiAction.DiagnosticsToggle -> {
                val expanded = !flags.value.diagnosticsExpanded
                flags.update { it.copy(diagnosticsExpanded = expanded) }
                if (expanded && repository.diagnostics.value == null) scope.launch { refreshDiagnostics() }
            }
            MonitorUiAction.DiagnosticsRefresh -> scope.launch { refreshDiagnostics() }
            is MonitorUiAction.SearchChanged -> flags.update { it.copy(query = action.query) }
        }
    }

    private suspend fun refreshStatus() = statusMutex.withLock {
        flags.update { it.copy(refreshing = true) }
        try {
            repository.refreshStatus()
            flags.update { it.copy(error = null) }
        } catch (cancelled: CancellationException) {
            throw cancelled
        } catch (error: Exception) {
            flags.update { it.copy(error = "Не удалось прочитать датчики. Повторите попытку.") }
        } finally {
            flags.update { it.copy(refreshing = false) }
        }
    }

    private suspend fun refreshDiagnostics() = diagnosticsMutex.withLock {
        flags.update { it.copy(diagnosticsRefreshing = true, diagnosticsError = null) }
        try {
            repository.refreshDiagnostics()
        } catch (cancelled: CancellationException) {
            throw cancelled
        } catch (error: Exception) {
            flags.update { it.copy(diagnosticsError = "Не удалось загрузить список датчиков.") }
        } finally {
            flags.update { it.copy(diagnosticsRefreshing = false) }
        }
    }

    fun stop() {
        polling.cancel()
    }
}
