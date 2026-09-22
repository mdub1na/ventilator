package ventilator.desktop.monitoring.domain

import kotlinx.coroutines.flow.StateFlow
import ventilator.prototype.StatusSnapshot

interface MonitorRepository {
    val status: StateFlow<StatusSnapshot?>

    suspend fun refreshStatus()
}
