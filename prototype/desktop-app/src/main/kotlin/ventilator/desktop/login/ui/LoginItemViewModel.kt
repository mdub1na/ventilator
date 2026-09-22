package ventilator.desktop.login.ui

import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.CoroutineScope
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
import ventilator.desktop.login.domain.LoginItemRepository

sealed interface LoginItemUiAction {
    data class SetEnabled(val enabled: Boolean) : LoginItemUiAction
    data object OpenSystemSettings : LoginItemUiAction
}

private data class LoginItemFlags(val busy: Boolean = false, val error: String? = null)

class LoginItemViewModel(private val repository: LoginItemRepository, private val scope: CoroutineScope) {
    private val flags = MutableStateFlow(LoginItemFlags())
    private val mutex = Mutex()

    val uiState: StateFlow<LoginItemUiState> = combine(repository.status, flags) { status, currentFlags ->
        LoginItemUiMapper.map(status, currentFlags.busy, currentFlags.error)
    }.stateIn(scope, SharingStarted.Eagerly, LoginItemUiMapper.map(repository.status.value, false, null))

    private val polling: Job = scope.launch {
        while (isActive) {
            refreshSafely()
            delay(2_000)
        }
    }

    fun onAction(action: LoginItemUiAction) {
        when (action) {
            is LoginItemUiAction.SetEnabled -> scope.launch {
                mutex.withLock {
                    flags.update { it.copy(busy = true, error = null) }
                    try {
                        repository.setEnabled(action.enabled)
                    } catch (cancelled: CancellationException) {
                        throw cancelled
                    } catch (error: Exception) {
                        flags.update { it.copy(error = "Не удалось изменить автозапуск: ${error.message ?: "ошибка macOS"}") }
                    } finally {
                        refreshSafely()
                        flags.update { it.copy(busy = false) }
                    }
                }
            }
            LoginItemUiAction.OpenSystemSettings -> repository.openSystemSettings()
        }
    }

    fun stop() {
        polling.cancel()
    }

    private suspend fun refreshSafely() {
        try {
            repository.refreshStatus()
        } catch (cancelled: CancellationException) {
            throw cancelled
        } catch (_: Exception) {
            flags.update { it.copy(error = "Не удалось проверить состояние автозапуска") }
        }
    }
}
