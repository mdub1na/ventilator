package ventilator.desktop.monitoring

import java.time.Instant
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.runBlocking
import kotlinx.coroutines.withTimeout
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertTrue
import ventilator.desktop.monitoring.domain.MonitorRepository
import ventilator.desktop.monitoring.ui.MonitorDisplayState
import ventilator.desktop.monitoring.ui.MonitorUiAction
import ventilator.desktop.monitoring.ui.MonitorUiMapper
import ventilator.desktop.monitoring.ui.MonitorViewModel
import ventilator.prototype.FanSnapshot
import ventilator.prototype.StatusSnapshot
import ventilator.prototype.TemperatureReading

class MonitorUiTest {
    private val at = Instant.parse("2026-09-22T12:00:00Z")

    /** Zero RPM must describe a stopped fan without alarming the user. */
    @Test
    fun `a stopped fan appears as stopped with zero segments`() {
        val state = MonitorUiMapper.map(
            status = snapshot(0.0, 0.0), refreshing = false, error = null,
        )

        assertEquals(MonitorDisplayState.STOPPED, state.displayState)
        assertEquals(listOf(0, 0), state.fans.map { it.level })
        assertEquals(listOf("Остановлен", "Остановлен"), state.fans.map { it.state })
        assertEquals("TCMz", snapshot(0.0, 0.0).cpuTemperature.rawKey)
    }

    /** Missing component sensors must remain unknown without borrowing another temperature. */
    @Test
    fun `unavailable readings remain visibly unknown`() {
        val state = MonitorUiMapper.map(
            status = snapshot(0.0, null, cpu = null, gpu = null, ssd = null), refreshing = false, error = null,
        )

        assertEquals(MonitorDisplayState.UNAVAILABLE, state.displayState)
        assertEquals("—", state.fans.last().rpm)
        assertEquals(null, state.fans.last().level)
        assertEquals(listOf("CPU", "GPU", "SSD"), state.temperatures.map { it.component })
        assertEquals(listOf("—", "—", "—"), state.temperatures.map { it.value })
    }

    /** The dashboard uses the GPU and SSD keys observed under separate loads on this model. */
    @Test
    fun `component cards map their own raw keys`() {
        val state = MonitorUiMapper.map(snapshot(1350.0, 1460.0), refreshing = false, error = null)
        assertEquals(listOf("TCMz", "Tg0D", "TH0a"), state.temperatures.map { it.key })
        assertEquals(listOf("60.0", "45.0", "33.0"), state.temperatures.map { it.value })

        val unrelated = snapshot(1350.0, 1460.0).copy(
            selectedTemperatures = listOf(TemperatureReading("TAOL", 30.0, at)),
        )
        val missing = MonitorUiMapper.map(unrelated, refreshing = false, error = null)
        assertEquals(listOf("60.0", "—", "—"), missing.temperatures.map { it.value })
    }

    /** A failed first read is an error state and a successful retry restores live data. */
    @Test
    fun `a read failure does not invent zero RPM and retry recovers`() = runBlocking {
        val repository = FakeRepository(snapshot(0.0, 0.0)).apply { fail = true }
        val scope = CoroutineScope(SupervisorJob() + Dispatchers.Default)
        try {
            val viewModel = MonitorViewModel(repository, scope)
            val error = withTimeout(5_000) { viewModel.uiState.first { it.displayState == MonitorDisplayState.ERROR } }
            assertTrue(error.fans.isEmpty())
            repository.fail = false
            viewModel.onAction(MonitorUiAction.Refresh)
            val recovered = withTimeout(5_000) { viewModel.uiState.first { it.displayState == MonitorDisplayState.STOPPED } }
            assertEquals(listOf("0", "0"), recovered.fans.map { it.rpm })
        } finally {
            scope.cancel()
        }
    }

    /** The application owns polling so a hidden window cannot stop updates to the status item. */
    @Test
    fun `status polling continues without a screen collector`() = runBlocking {
        val repository = FakeRepository(snapshot(1350.0, 1460.0))
        val scope = CoroutineScope(SupervisorJob() + Dispatchers.Default)
        try {
            MonitorViewModel(repository, scope)
            withTimeout(5_000) {
                while (repository.statusLoads < 2) delay(50)
            }
            assertTrue(repository.statusLoads >= 2)
        } finally {
            scope.cancel()
        }
    }

    private fun snapshot(fan0: Double?, fan1: Double?, cpu: Double? = 60.0, gpu: Double? = 45.0, ssd: Double? = 33.0) = StatusSnapshot(
        declaredFanCount = 2,
        fans = listOf(
            FanSnapshot(0, fan0, 1350.0, 5349.0, at),
            FanSnapshot(1, fan1, 1458.0, 5777.0, at),
        ),
        cpuTemperature = TemperatureReading("TCMz", cpu, at),
        selectedTemperatures = listOf(
            TemperatureReading("Tg0D", gpu, at),
            TemperatureReading("TH0a", ssd, at),
        ),
    )

    private class FakeRepository(private val initial: StatusSnapshot) : MonitorRepository {
        private val statusFlow = MutableStateFlow<StatusSnapshot?>(null)
        override val status: StateFlow<StatusSnapshot?> = statusFlow
        var fail = false
        @Volatile var statusLoads = 0

        override suspend fun refreshStatus() {
            statusLoads++
            if (fail) error("SMC unavailable")
            statusFlow.value = initial
        }

    }
}
