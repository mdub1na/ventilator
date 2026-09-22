package ventilator.desktop.login.domain

import kotlinx.coroutines.flow.StateFlow

enum class LoginItemStatus { DISABLED, ENABLED, REQUIRES_APPROVAL, NOT_FOUND, UNAVAILABLE }

enum class LaunchMode { PENDING, MANUAL, LOGIN_ITEM }

interface LoginItemRepository {
    val status: StateFlow<LoginItemStatus>
    val canDetectLaunch: Boolean
    fun launchMode(): LaunchMode
    suspend fun refreshStatus()
    suspend fun setEnabled(enabled: Boolean)
    fun openSystemSettings()
}
