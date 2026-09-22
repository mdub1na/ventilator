package ventilator.desktop.login.ui

import ventilator.desktop.login.domain.LoginItemStatus

data class LoginItemUiState(
    val enabled: Boolean = false,
    val toggleAvailable: Boolean = false,
    val requiresApproval: Boolean = false,
    val message: String = "Проверяем состояние автозапуска…",
    val busy: Boolean = false,
    val error: String? = null,
)

object LoginItemUiMapper {
    fun map(status: LoginItemStatus, busy: Boolean, error: String?): LoginItemUiState = LoginItemUiState(
        enabled = status == LoginItemStatus.ENABLED,
        toggleAvailable = !busy && status in setOf(LoginItemStatus.DISABLED, LoginItemStatus.ENABLED, LoginItemStatus.NOT_FOUND),
        requiresApproval = status == LoginItemStatus.REQUIRES_APPROVAL,
        message = when (status) {
            LoginItemStatus.DISABLED -> "Автозапуск выключен"
            LoginItemStatus.ENABLED -> "Ventilator будет запускаться после входа в macOS"
            LoginItemStatus.REQUIRES_APPROVAL -> "Разрешите запуск Ventilator в системных настройках"
            LoginItemStatus.NOT_FOUND -> "Автозапуск ещё не зарегистрирован"
            LoginItemStatus.UNAVAILABLE -> "Автозапуск недоступен: откройте собранный Ventilator.app на macOS 13+"
        },
        busy = busy,
        error = error,
    )
}
