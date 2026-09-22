package ventilator.desktop.menubar

import java.nio.file.Path
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.launch
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import kotlinx.coroutines.withContext

enum class MenuCommand { SHOW, HIDE, TOGGLE, QUIT }

/** Owns one accessory AppKit process. Only Kotlin reads AppleSMC. */
class MenuBarBridge(
    private val executable: Path,
    private val scope: CoroutineScope,
    private val onCommand: (MenuCommand) -> Unit,
    private val onLost: (String) -> Unit,
) {
    private val writeMutex = Mutex()
    private var process: Process? = null
    private var readerJob: Job? = null
    @Volatile private var stopped = false

    fun start() {
        check(process == null) { "Status item already started" }
        try {
            val child = ProcessBuilder(executable.toString(), "--stdio")
                .redirectError(ProcessBuilder.Redirect.INHERIT)
                .start()
            process = child
            readerJob = scope.launch(Dispatchers.IO) {
                try {
                    child.inputStream.bufferedReader().useLines { lines ->
                        lines.forEach { line ->
                            val command = when (line) {
                                "show" -> MenuCommand.SHOW
                                "hide" -> MenuCommand.HIDE
                                "toggle" -> MenuCommand.TOGGLE
                                "quit" -> MenuCommand.QUIT
                                else -> null
                            }
                            if (command != null) scope.launch { onCommand(command) }
                        }
                    }
                } finally {
                    if (!stopped) scope.launch { onLost("Значок строки меню завершился. Перезапустите приложение.") }
                }
            }
        } catch (error: Exception) {
            onLost("Не удалось запустить значок строки меню: ${error.message ?: "неизвестная ошибка"}")
        }
    }

    suspend fun send(reading: TrayReading, windowVisible: Boolean) {
        val child = process ?: return
        try {
            writeMutex.withLock {
                withContext(Dispatchers.IO) {
                    child.outputStream.write(reading.wireLine(windowVisible).toByteArray(Charsets.UTF_8))
                    child.outputStream.flush()
                }
            }
        } catch (error: Exception) {
            if (!stopped) onLost("Связь со значком строки меню прервана. Перезапустите приложение.")
        }
    }

    fun stop() {
        stopped = true
        readerJob?.cancel()
        process?.outputStream?.close()
        process?.destroy()
    }
}
