package ventilator.desktop.monitoring.data

import java.nio.file.Path
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.withContext
import ventilator.desktop.monitoring.domain.MonitorRepository
import ventilator.prototype.DiagnosticsSnapshot
import ventilator.prototype.StatusSnapshot
import ventilator.prototype.readDiagnostics
import ventilator.prototype.readSnapshot

class SmcMonitorRepository(private val probe: Path) : MonitorRepository {
    private val _status = MutableStateFlow<StatusSnapshot?>(null)
    private val _diagnostics = MutableStateFlow<DiagnosticsSnapshot?>(null)

    override val status: StateFlow<StatusSnapshot?> = _status
    override val diagnostics: StateFlow<DiagnosticsSnapshot?> = _diagnostics

    override suspend fun refreshStatus() {
        _status.value = withContext(Dispatchers.IO) { readSnapshot(probe) }
    }

    override suspend fun refreshDiagnostics() {
        _diagnostics.value = withContext(Dispatchers.IO) { readDiagnostics(probe) }
    }
}
