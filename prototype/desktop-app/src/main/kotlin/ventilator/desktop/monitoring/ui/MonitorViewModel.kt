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
}

private data class UiFlags(
    val refreshing: Boolean = true,
    val error: String? = null,
)

class MonitorViewModel(
    private val repository: MonitorRepository,
    private val scope: CoroutineScope,
) {
    private val flags = MutableStateFlow(UiFlags())
    private val statusMutex = Mutex()

    val uiState: StateFlow<MonitorUiState> = combine(repository.status, flags) { status, currentFlags ->
        MonitorUiMapper.map(
            status = status,
            refreshing = currentFlags.refreshing,
            error = currentFlags.error,
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

    fun stop() {
        polling.cancel()
    }
}
