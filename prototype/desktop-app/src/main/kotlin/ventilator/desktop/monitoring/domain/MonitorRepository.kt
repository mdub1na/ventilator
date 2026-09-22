package ventilator.desktop.monitoring.domain

import kotlinx.coroutines.flow.StateFlow
import ventilator.prototype.DiagnosticsSnapshot
import ventilator.prototype.StatusSnapshot

interface MonitorRepository {
    val status: StateFlow<StatusSnapshot?>
    val diagnostics: StateFlow<DiagnosticsSnapshot?>

    suspend fun refreshStatus()
    suspend fun refreshDiagnostics()
}
