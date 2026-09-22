package ventilator.desktop.settings.ui

import androidx.compose.material3.ExperimentalMaterial3ExpressiveApi
import androidx.compose.material3.MaterialExpressiveTheme
import androidx.compose.runtime.Composable
import androidx.compose.ui.tooling.preview.Preview
import ventilator.desktop.login.domain.LoginItemStatus
import ventilator.desktop.login.ui.LoginItemUiMapper

@OptIn(ExperimentalMaterial3ExpressiveApi::class)
@Composable
private fun Fixture(status: LoginItemStatus, error: String? = null) {
    MaterialExpressiveTheme {
        SettingsContent(LoginItemUiMapper.map(status, false, error))
    }
}

@Preview
@Composable
private fun DisabledPreview() = Fixture(LoginItemStatus.DISABLED)

@Preview
@Composable
private fun EnabledPreview() = Fixture(LoginItemStatus.ENABLED)

@Preview
@Composable
private fun ApprovalPreview() = Fixture(LoginItemStatus.REQUIRES_APPROVAL)

@Preview
@Composable
private fun UnavailablePreview() = Fixture(LoginItemStatus.UNAVAILABLE)

@Preview
@Composable
private fun ErrorPreview() = Fixture(LoginItemStatus.DISABLED, "Не удалось изменить автозапуск")
