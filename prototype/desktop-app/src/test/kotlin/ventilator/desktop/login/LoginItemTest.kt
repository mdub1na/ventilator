package ventilator.desktop.login

import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.runBlocking
import kotlinx.coroutines.withTimeout
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertTrue
import ventilator.desktop.login.domain.LaunchMode
import ventilator.desktop.login.domain.LoginItemRepository
import ventilator.desktop.login.domain.LoginItemStatus
import ventilator.desktop.login.ui.LoginItemUiAction
import ventilator.desktop.login.ui.LoginItemUiMapper
import ventilator.desktop.login.ui.LoginItemViewModel

class LoginItemTest {
    @Test
    fun `system approval is distinct from enabled state`() {
        val awaitingApproval = LoginItemUiMapper.map(LoginItemStatus.REQUIRES_APPROVAL, false, null)
        assertFalse(awaitingApproval.enabled)
        assertFalse(awaitingApproval.toggleAvailable)
        assertTrue(awaitingApproval.requiresApproval)

        val firstRegistration = LoginItemUiMapper.map(LoginItemStatus.NOT_FOUND, false, null)
        assertFalse(firstRegistration.enabled)
        assertTrue(firstRegistration.toggleAvailable)
    }

    @Test
    fun `toggle uses actual system status and recovers after failure`() = runBlocking {
        val repository = FakeLoginItemRepository()
        val scope = CoroutineScope(SupervisorJob() + Dispatchers.Default)
        try {
            val viewModel = LoginItemViewModel(repository, scope)
            viewModel.onAction(LoginItemUiAction.SetEnabled(true))
            val enabled = withTimeout(5_000) { viewModel.uiState.first { it.enabled && !it.busy } }
            assertEquals(LoginItemStatus.ENABLED, repository.status.value)
            assertTrue(enabled.toggleAvailable)

            repository.failNext = true
            viewModel.onAction(LoginItemUiAction.SetEnabled(false))
            val failed = withTimeout(5_000) { viewModel.uiState.first { it.error != null && !it.busy } }
            assertTrue(failed.enabled)
            assertEquals(LoginItemStatus.ENABLED, repository.status.value)

            viewModel.onAction(LoginItemUiAction.SetEnabled(false))
            withTimeout(5_000) { viewModel.uiState.first { !it.enabled && !it.busy } }
            assertEquals(LoginItemStatus.DISABLED, repository.status.value)
        } finally {
            scope.cancel()
        }
    }

    private class FakeLoginItemRepository : LoginItemRepository {
        private val mutableStatus = MutableStateFlow(LoginItemStatus.DISABLED)
        override val status: StateFlow<LoginItemStatus> = mutableStatus
        override val canDetectLaunch = false
        var failNext = false

        override fun launchMode() = LaunchMode.PENDING
        override suspend fun refreshStatus() = Unit
        override suspend fun setEnabled(enabled: Boolean) {
            if (failNext) {
                failNext = false
                error("registration denied")
            }
            mutableStatus.value = if (enabled) LoginItemStatus.ENABLED else LoginItemStatus.DISABLED
        }
        override fun openSystemSettings() = Unit
    }
}
