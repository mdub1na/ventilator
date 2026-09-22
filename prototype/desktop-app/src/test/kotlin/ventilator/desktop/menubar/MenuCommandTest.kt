package ventilator.desktop.menubar

import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertNull

class MenuCommandTest {
    @Test
    fun `settings command from status item is parsed`() {
        assertEquals(MenuCommand.SETTINGS, MenuCommand.fromWire("settings"))
        assertNull(MenuCommand.fromWire("settings\textra"))
    }
}
