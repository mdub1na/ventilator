package ventilator.desktop.login.data

import java.nio.file.Path
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.withContext
import ventilator.desktop.login.domain.LaunchMode
import ventilator.desktop.login.domain.LoginItemRepository
import ventilator.desktop.login.domain.LoginItemStatus

/** Calls ServiceManagement from the main JVM process, which owns the Ventilator.app bundle. */
class NativeLoginItemRepository private constructor(private val native: LoginItemNative?) : LoginItemRepository {
    private val mutableStatus = MutableStateFlow(readStatus())
    override val status: StateFlow<LoginItemStatus> = mutableStatus
    override val canDetectLaunch: Boolean get() = native != null

    override fun launchMode(): LaunchMode = when (native?.launchModeNative()) {
        1 -> LaunchMode.MANUAL
        2 -> LaunchMode.LOGIN_ITEM
        else -> LaunchMode.PENDING
    }

    override suspend fun refreshStatus() {
        mutableStatus.value = withContext(Dispatchers.IO) { readStatus() }
    }

    override suspend fun setEnabled(enabled: Boolean) {
        val bridge = native ?: error("Служба автозапуска недоступна в этой сборке")
        withContext(Dispatchers.IO) {
            bridge.setEnabledNative(enabled)?.let { error(it) }
        }
        refreshStatus()
    }

    override fun openSystemSettings() {
        native?.openSettingsNative()
    }

    private fun readStatus(): LoginItemStatus = when (runCatching { native?.statusNative() }.getOrNull()) {
        0 -> LoginItemStatus.DISABLED
        1 -> LoginItemStatus.ENABLED
        2 -> LoginItemStatus.REQUIRES_APPROVAL
        3 -> LoginItemStatus.NOT_FOUND
        else -> LoginItemStatus.UNAVAILABLE
    }

    companion object {
        fun fromPackagedResources(resources: Path?): NativeLoginItemRepository {
            val path = resources?.resolve("liblogin-item.dylib") ?: return NativeLoginItemRepository(null)
            val native = runCatching { LoginItemNative(path) }.getOrNull()
            return NativeLoginItemRepository(native)
        }
    }
}

internal class LoginItemNative(path: Path) {
    init {
        System.load(path.toAbsolutePath().normalize().toString())
        installLaunchMonitor()
    }

    private external fun installLaunchMonitor()
    external fun launchModeNative(): Int
    external fun statusNative(): Int
    external fun setEnabledNative(enabled: Boolean): String?
    external fun openSettingsNative()
}
