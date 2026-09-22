package ventilator.desktop.monitoring.data

import java.nio.file.Path
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.withContext
import ventilator.desktop.monitoring.domain.MonitorRepository
import ventilator.prototype.StatusSnapshot
import ventilator.prototype.readSnapshot

class SmcMonitorRepository(private val probe: Path) : MonitorRepository {
    private val _status = MutableStateFlow<StatusSnapshot?>(null)

    override val status: StateFlow<StatusSnapshot?> = _status

    override suspend fun refreshStatus() {
        _status.value = withContext(Dispatchers.IO) { readSnapshot(probe) }
    }
}
