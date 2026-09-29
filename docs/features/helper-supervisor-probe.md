---
id: helper-supervisor-probe
title: Подписанный read-only supervisor из root helper
type: feature
status: active
owner: unassigned
involved_services: [helper-ipc-prototype]
client_entries: []
api: []
tags: [macos, smc, fan-control, safety]
---

# Подписанный read-only supervisor из root helper

## 1. Назначение

Следующий шаг M2-02 подключает [supervisor](worker-supervisor.md) к временному подписанному `Ventilator.app`. Compose CLI → JNI → проверенный XPC daemon с UID 0 → отдельный подписанный C executable → отдельные процессы чтения, операции, recovery и observer. Операция и recovery в этой версии только читают фиксированный снимок и проверяют системный режим. Writer не линкуется, `write_available=false` сохраняется во всех ответах.

`NSTask` запускает новый executable из многопоточного daemon; `fork` выполняется уже в его однопоточном C родителе. Эта граница позволяет использовать существующий процессный supervisor без `fork` внутри Foundation/XPC daemon.

## 2. Правила

1. Три XPC метода без входных аргументов: `startSupervisorProbeWithReply`, `fetchSupervisorProbeWithReply`, `cleanupSupervisorProbeWithReply`. Они доступны только прежнему проверенному клиенту. Обычный запуск UI не регистрирует службу и не начинает пробу.
2. Daemon определяет sibling `supervisor-probe` рядом с собственным executable. Security framework проверяет Apple anchor, identifier `com.ventilator.helper-ipc.signed-supervisor`, собственный Team ID daemon и целостность всех архитектур. Затем он копирует байты в приватное root состояние и заново проверяет подпись **копии** перед запуском. Копия имеет права `0500`; пользователь не может заменить запускаемый файл после проверки оригинала. Клиент не передаёт путь, argv, PID, SMC ключ или RPM. У runner пустое окружение, stdin/stderr направлены в null; результат ограничен 4096 байтами.
3. Runner требует real/effective UID 0 до доступа к состоянию. Единственный режим кроме запуска — фиксированный `--cleanup`. Журнал и проверенная копия лежат в `/private/var/db/com.ventilator.supervisor-read-only`: root, `0700`, без симлинка. Родитель `/private/var/db` также проверяется: root, без group/world write. Отдельный наследуемый launch lock `0600` запрещает параллельные пробы, включая дочерние процессы после смерти runner.
4. Чистый старт сначала вызывает `worker_supervisor_observe_baseline`: новый ограниченный reader, 61 исходный снимок за минимум 60 секунд, отдельный PID собран. Это окно не создаёт `pending` или recovery proof. Проверки длительности чтения, свежести доставки, интервала и общего срока совпадают с recovery observer. При недостоверном снимке, зависании или сне допуск не выдаётся.
5. После окна обычный `control_lease_claim` проверяет последний свежий снимок и прежний порог температур ниже 75 °C. Capability подтверждает только read-only callback этой сборки; она не утверждает, что аппаратный возврат проверен. Затем supervisor сохраняет `pending`, запускает отдельные check-only operation и recovery, собирает их и начинает **новую** минуту observer. Только её подтверждение очищает маркер.
6. При исходном `pending` runner вызывает только `worker_supervisor_resume`, без операции и первого окна. Check-only recovery откажет при изменённом SMC; аппаратное восстановление отсутствует. Старый живой/неопределённый PID и lock запрещают takeover. Новый runner не отправляет сигнал сохранённому PID.
7. Состояния XPC: `idle`, `running`, `finished`, `failed`, `cleaned`. `finished` содержит отдельный report: `verified`, `blocked` либо `pending`, UID/PID runner, отдельные PID фаз, число собранных процессов, число снимков/реальную длительность каждого окна, статус журнала и признак resume. Клиент проверяет типы, точный набор полей, согласованность runner и две минуты полного запуска; ложный успех и capability записи отвергаются. `verified` означает проверенный read-only цикл процессов.
8. Повторный start во время работы или после терминального результата возвращает прежний статус. Cleanup во время работы также не запускает другой процесс. Завершённую пробу очищает тот же подписанный executable: свободный launch lock, чистый journal и подтверждённое отсутствие записанного worker обязательны. `pending` не удаляется для обхода отказа. Перед `unregister`/удалением пакета скрипт требует отсутствие root состояния.
9. Обычный выход диагностического JVM клиента не останавливает эту фиксированную read-only пробу. При смерти daemon C runner может продолжить свой ограниченный цикл; новый daemon не получает старый report в память. Launch lock защищает от параллельного запуска, журнал сохраняет незавершённость. Привязка будущего writer к жизни daemon/клиента ещё требует отдельного протокола. Диагностическое root состояние сохраняется до разрешённой очистки; production восстановление через reboot с ним не проверено.

## 3. Сценарии

### Scenario: Начальное наблюдение ограничено и не заменяет recovery
* **Given:** чистый приватный журнал и reader с подставным исходным снимком либо отказом/остановкой.
* **When:** supervisor собирает начальное окно.
* **Then:** только полная реальная минута даёт `SUPERVISOR_BASELINE_READY`; операции и recovery не запускаются, proof не создаётся, отказный reader собран в пределах срока.
* **Automated:** `prototype/helper-ipc/WorkerSupervisorTest.c::initial_baseline_is_bounded_and_never_recovery_proof`

### Scenario: Неопределённое владение запрещает очистку root состояния
* **Given:** journal `pending` либо record существующего процесса.
* **When:** cleanup запрашивает право очистки.
* **Then:** файлы сохраняются; чистый журнал с отсутствующим worker разрешает удаление фиксированных файлов.
* **Automated:** `prototype/helper-ipc/SupervisorProbeStorageTest.c::pending_or_recorded_process_prohibits_cleanup`

### Scenario: Не приватное состояние или второй запуск отклоняются
* **Given:** каталог/lock с неверными правами, симлинком, лишней ссылкой либо занятым lock.
* **When:** runner получает launch lock.
* **Then:** запуск запрещён, существующий владелец не остановлен.
* **Automated:** `prototype/helper-ipc/SupervisorProbeStorageTest.c::private_state_and_exclusive_lock_are_required`

### Scenario: Клиент не принимает ложное подтверждение минут
* **Given:** report утверждает `verified`, но имеет меньше 61 снимка, меньше 60 секунд, не root UID, повторный PID, неполный reap или неочищенный journal.
* **When:** клиент проверяет report.
* **Then:** успех отклонён.
* **Automated:** `prototype/helper-ipc/HelperSupervisorValidationTest.m::false_success_is_rejected`

### Scenario: Статус не разрешает writer и не принимает чужой report
* **Given:** ответ содержит write capability, другой backend, несовпадающий runner PID или старый report в `running`.
* **When:** JNI проверяет XPC ответ.
* **Then:** весь ответ отклонён.
* **Automated:** `prototype/helper-ipc/HelperSupervisorValidationTest.m::xpc_write_or_mismatched_runner_is_rejected`

### Scenario: Подписанный daemon проверяет sibling executable до запуска
* **Given:** trusted Apple Development runner либо другой identifier, ad hoc подпись, повреждение или отсутствие файла.
* **When:** `supervisor-signature-smoke.sh` вызывает только signature diagnostic того же daemon.
* **Then:** принят только trusted runner, в том числе после копирования в приватный fixture; ни один task не запущен.
* **Manual:** `prototype/helper-ipc/supervisor-signature-smoke.sh` на Mac с действующим локальным Apple Development сертификатом.

### Scenario: Настоящий root helper выполняет две независимые минуты
* **Given:** подписанный временный пакет, разрешённая служба UID 0 и чистое состояние на `Mac15,7`/macOS 27.0.
* **When:** Compose CLI запрашивает supervisor через JNI/XPC и затем завершается.
* **Then:** отдельный root runner проходит начальное окно, check-only operation/recovery и новую минуту observer; возвращает `verified`, четыре собранных PID, чистый journal и `write_available=false`. Cleanup удаляет root состояние до отмены регистрации и удаления пакета.
* **Manual:** `prototype/helper-ipc/integrated-probe.sh` — `supervisor-start`, `supervisor-status`, `supervisor-cleanup`, `unregister`, `cleanup`.

### Scenario: Имя argv не подменяет executable процесса
* **Given:** обычный процесс запущен с `argv[0]`, совпадающим с длинным путём root runner.
* **When:** локальный диагностический клиент читает путь по PID через `proc_pidpath`.
* **Then:** возвращается настоящий executable; скрипт не принимает argv или усечённый `ps comm` за путь runner. Некорректный или недоступный PID отклоняется.
* **Automated:** `prototype/helper-ipc/process-path-test.py::test_spoofed_runner_argv_does_not_pass_as_the_executable`

## 4. Проверка и ограничения

На `Mac15,7`/macOS 27.0 сборка C/Objective-C/JNI и Compose пакета прошла. Полный процессный набор прошёл с новой реальной начальной минутой и отказными reader; остальные helper и изолированные writer/reader тесты также прошли. Signature-only проба проверила оригинал **и скопированные байты**: приняла точную подпись и отклонила чужой identifier, ad hoc, повреждение и отсутствие executable. Она не запускала runner и не читала SMC. Пять мутаций временных копий обнаружены: короткое recovery окно, принятие write capability, очистка `pending`, допуск после отказного начального observer и отключение проверки подписи скопированного executable. Мутанты используют подставные callbacks либо signature-only diagnostic, отдельные временные каталоги и ограниченные группы собственных процессов; writer не подключён. Предки приватного root каталога локально проверены: `/private`, `/private/var` и `/private/var/db` имеют root-владельца и права `0755`; runtime дополнительно проверяет родителя `/private/var/db`.

### 4.1. Реальный root цикл, 2026-09-29

Источник: `integrated-probe.sh check/supervisor-start/supervisor-status/supervisor-cleanup/unregister/cleanup` из временной подписанной копии Compose приложения на `Mac15,7`/macOS 27.0, независимые `ps`, `proc_pidpath`, `launchctl` и интерфейс системных настроек. После Touch ID регистрация стала `enabled`; daemon PID `57929` работал с UID 0. Доверенный JNI/XPC клиент получил снимок; другой подписанный identifier и ad hoc клиент отклонены.

Runner PID `58023` завершил полный цикл с `resumed=false`, `state=verified`, `runner_uid=0`, `journal_clear=true`, `reaped=4` и `write_available=false`:

| Фаза | Отдельный PID | Снимки / длительность |
|---|---|---|
| Начальный reader | `58033` | 61 / 62,910562542 с |
| Check-only operation | `58204` | процесс собран |
| Check-only recovery | `58205` | процесс собран |
| Новый recovery observer | `58206` | 61 / 65,540649042 с |

Первый `supervisor-start` ошибочно завершил **скрипт проверки** с `runner path differs`: macOS `ps comm` обрезал длинное имя до `/var/db/com.vent`. Сам runner продолжил ограниченный цикл. Пока он работал, независимый `proc_pidpath(58023)` вернул точный `/private/var/db/com.ventilator.supervisor-read-only/supervisor-executable-v1`, а `ps` подтвердил UID 0. Скрипт исправлен: локальный `helper-status process-path PID` использует тот же системный API, без передачи PID в XPC; тест с ложным `argv[0]` возвращает настоящий `/bin/sleep` и отклоняет подмену. Исправленный диагностический клиент и отказные PID проверены локально; второй root цикл ради этой проверки не запускался.

Свежий снимок после цикла подтвердил `Ftst=0`, режимы `[3,3]`, цели `[0,0]`, фактические RPM `[0,0]`; CPU/GPU/SSD — `[52,84; 46,22; 32,18]` °C. Cleanup вернул `cleaned` и удалил root каталог. Затем `unregister` подтвердил `notRegistered` и отсутствие system службы; временный пакет удалён. Независимая проверка не обнаружила каталог, пакет или PID daemon/runner/фаз/cleanup. В системных настройках `background-switch-Ventilator` вернулся в `off`. Запись в SMC не выполнялась.

Этот этап не пишет `Ftst`, режимы или цели. Возврат из ручного управления, зависание в ядре, root crash в аппаратной операции, сон после записи, обновление постоянного helper и восстановление через reboot не подтверждены. M2-01/M2-02 остаются открытыми; допуски записи закрыты.

### 4.2. План следующей проверки потери владельца

**Цель, ещё не реализована:** отдельная команда без входных аргументов запускает фиксированную пробу аварии во втором observer. Она не получает PID или сигнал от клиента. Read-only observer сообщает собственный PID и монотонное время, завершает только своего проверенного родителя через `SIGKILL` и остаётся под существующим monitor потери родителя. Диагностический аварийный предел гарантирует выход тестового observer при отказе monitor; такой поздний выход не считается успехом. Новый явный resume должен принимать только `pending`, не повторять operation и требовать новую полную минуту. Cleanup при `pending` обязан отказать. Прежде чем считать root путь проверенным, нужны отдельные процессные/контрактные тесты, настоящая подписанная проба, исходный снимок и штатное удаление состояния/службы/пакета.

## 5. Code anchors

| Path | Назначение |
|---|---|
| `prototype/helper-ipc/supervisor-probe.c` | фиксированный root read-only backend и отчёт двух окон |
| `prototype/helper-ipc/SupervisorProbeController.m`, `prototype/helper-ipc/SupervisorProbeController.h` | проверка подписи, запуск executable и асинхронный статус |
| `prototype/helper-ipc/SupervisorProbeStorage.c`, `prototype/helper-ipc/SupervisorProbeStorage.h` | наследуемый launch lock и запрет небезопасной очистки |
| `prototype/helper-ipc/SupervisorProbeDirectory.h` | фиксированный root каталог и проверка родителя без group/world write |
| `prototype/helper-ipc/HelperSupervisorValidation.h`, `prototype/helper-ipc/HelperSupervisorValidationTest.m` | строгий контракт результата и отказные тесты |
| `prototype/helper-ipc/SupervisorProbeStorageTest.c`, `prototype/helper-ipc/WorkerSupervisorTest.c` | приватность состояния и ограниченное начальное чтение |
| `prototype/helper-ipc/HelperStatus.h`, `prototype/helper-ipc/daemon-status.m`, `prototype/helper-ipc/HelperProbeBridge.m` | XPC и JNI команды без входных параметров |
| `prototype/desktop-app/src/main/kotlin/ventilator/desktop/helper/HelperProbeCommand.kt` | диагностический CLI настоящего приложения |
| `prototype/helper-ipc/integrated-probe.sh`, `prototype/helper-ipc/supervisor-signature-smoke.sh`, `prototype/helper-ipc/Makefile` | упаковка, подпись и проверки |
| `prototype/helper-ipc/helper-status.m`, `prototype/helper-ipc/process-path-test.py` | настоящий executable path вместо argv или усечённого имени |
