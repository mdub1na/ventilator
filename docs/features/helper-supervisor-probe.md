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

1. Пять XPC методов без входных аргументов: `startSupervisorProbeWithReply`, `startSupervisorCrashProbeWithReply`, `resumeSupervisorProbeWithReply`, `fetchSupervisorProbeWithReply`, `cleanupSupervisorProbeWithReply`. Они доступны только прежнему проверенному клиенту. Обычный запуск UI не регистрирует службу и не начинает пробу.
2. Daemon определяет sibling `supervisor-probe` рядом с собственным executable. Security framework проверяет Apple anchor, identifier `com.ventilator.helper-ipc.signed-supervisor`, собственный Team ID daemon и целостность всех архитектур. Затем он копирует байты в приватное root состояние и заново проверяет подпись **копии** перед запуском. Копия имеет права `0500`; пользователь не может заменить запускаемый файл после проверки оригинала. Клиент не передаёт путь, argv, PID, SMC ключ или RPM. У runner пустое окружение, stdin — приватная pipe владения, stderr направлен в null; результат stdout ограничен 4096 байтами.
3. Runner требует real/effective UID 0 до доступа к состоянию. Помимо запуска есть только фиксированные `--cleanup`, `--crash-observer`, `--resume-pending`. Журнал и проверенная копия лежат в `/private/var/db/com.ventilator.supervisor-read-only`: root, `0700`, без симлинка. Родитель `/private/var/db` также проверяется: root, без group/world write. Отдельный наследуемый launch lock `0600` запрещает параллельные пробы, включая дочерние процессы после смерти runner.
4. Чистый старт сначала вызывает `worker_supervisor_observe_baseline`: новый ограниченный reader, 61 исходный снимок за минимум 60 секунд, отдельный PID собран. Это окно не создаёт `pending` или recovery proof. Проверки длительности чтения, свежести доставки, интервала и общего срока совпадают с recovery observer. При недостоверном снимке, зависании или сне допуск не выдаётся.
5. После окна обычный `control_lease_claim` проверяет последний свежий снимок и прежний порог температур ниже 75 °C. Capability подтверждает только read-only callback этой сборки; она не утверждает, что аппаратный возврат проверен. Затем supervisor сохраняет `pending`, запускает отдельные check-only operation и recovery, собирает их и начинает **новую** минуту observer. Только её подтверждение очищает маркер.
6. При исходном `pending` runner вызывает только `worker_supervisor_resume`, без операции и первого окна. Check-only recovery откажет при изменённом SMC; аппаратное восстановление отсутствует. Старый живой/неопределённый PID и lock запрещают takeover. Новый runner не отправляет сигнал сохранённому PID.
7. Состояния XPC: `idle`, `running`, `finished`, `failed`, `cleaned`, диагностический `interrupted`. `finished` содержит отдельный report: `verified`, `blocked` либо `pending`, UID/PID runner, отдельные PID фаз, число собранных процессов, число снимков/реальную длительность каждого окна, статус журнала и признак resume. Клиент проверяет типы, точный набор полей, согласованность runner и две минуты полного запуска; ложный успех и capability записи отвергаются. `verified` означает проверенный read-only цикл процессов.
8. Повторный обычный/crash start во время работы возвращает прежний статус только тому же владельцу; запрос другого соединения даёт `failed/busy`. После терминального результата новый start возвращает прежний статус до cleanup. Явный resume при отсутствии текущего task запускает только `--resume-pending`: чистое/неопределённое состояние не создаёт operation или начальную минуту, а корректный `pending` допускается прежней защитой владения. Cleanup/resume от постороннего соединения во время работы дают `failed/busy`. Завершённую пробу очищает тот же подписанный executable: свободный launch lock, чистый journal и подтверждённое отсутствие записанного worker обязательны. `pending` не удаляется для обхода отказа. Перед `unregister`/удалением пакета скрипт требует отсутствие root состояния.
9. Каждый принятый XPC клиент получает собственный объект владельца. Только соединение, запустившее runner, держит приватную pipe записи; чужой `status` может читать состояние, но его invalidation не закрывает эту pipe. Interruption и invalidation владельца закрывают её без сигнала по сохранённому PID. Отключение связи липкое: её автоматическое восстановление не даёт нового владения. JNI диагностического клиента держит **тот же** signed XPC канал, опрашивает до терминального результата в пределах 180 секунд и отвергает ответ другого daemon/runner. CLI команд start/crash/resume печатает начальный `running` и итоговую JSON-строку. Обычный UI не запускает supervisor.
10. Crash режим действует только на чистом запуске после обычного начального допуска. При создании recovery observer родитель получает внутренний `observer_started` после устойчивого PID record и разрешения callback. Диагностика проверяет `pending`, текущий parent PID/UID ребёнка через `proc_pidinfo`, пишет ограниченный marker в stdout и вызывает `raise(SIGKILL)` **в самом runner**. Входного PID/сигнала нет. Observer в этой диагностике ожидает monitor потери родителя, не читая SMC; `SIGALRM` через 10 секунд ограничивает отказ monitor. Daemon принимает `interrupted` только при фактическом `NSTaskTerminationReasonUncaughtSignal`/`SIGKILL`, точном root marker и закрытии pipe не позже двух секунд по `CLOCK_MONOTONIC_RAW`. Поздний fallback, другой PID, UID, будущая метка или лишние поля не дают подтверждения.
11. `interrupted` содержит только дополнительный `crash`: PID observer, `journal_pending=true`, `loss_duration_ns≤2 000 000 000`. Он не создаёт recovery proof. Старое crash evidence запрещено в `running`. Отдельный resume report с `resumed=true` требует нулевые admission/operation, два собранных новых процесса и 61 снимок за новую полную минуту.
12. Через stdin отдельного C runner передаётся единственный внутренний `G` из приватной pipe до `NSTask` запуска. Runner требует root UID, тип pipe и grant до открытия приватного состояния; после grant проверяет EOF/лишний байт перед каждой фазой, разрешением worker и потреблением recovery proof, а также не реже 10 мс при наблюдении. При потере upstream он завершает **себя** с кодом `125`, оставляя `pending`. Рабочие дочерние процессы закрывают наследованный stdout родителя и уже следят за его отдельным socket: смерть runner вызывает их выход при EOF. После гибели daemon новый daemon не имеет прежнего report; при сохранённом `pending` допустим только явный recovery-only restart и новое окно. Допуск для аппаратного writer этим не открыт.

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

### Scenario: Диагностическая авария сохраняет намерение и запрещает повторение операции
* **Given:** supervisor с подставными показаниями создал собственного recovery observer и устойчивый `pending`.
* **When:** тот же диагностический hook завершает самого родителя через `SIGKILL`, затем новый владелец выполняет resume с изменённым снимком.
* **Then:** прежний observer исчезает, cleanup отказывает, operation не повторяется; отказ нового окна сохраняет `pending` без старого proof. Неопределённый или чужой процесс не запускает диагностическую аварию.
* **Automated:** `prototype/helper-ipc/WorkerSupervisorTest.c::diagnostic_crash_keeps_intent_and_restart_never_repeats_operation`

### Scenario: Поздний выход или неверный marker не подтверждает аварию
* **Given:** marker имеет чужой UID/PID, будущую метку либо более двух секунд до EOF; ответ содержит неверный crash или старый crash в `running`.
* **When:** daemon и JNI проверяют диагностический результат.
* **Then:** `interrupted` не принимается; десятисекундный fallback не выглядит успешным monitor.
* **Automated:** `prototype/helper-ipc/HelperSupervisorValidationTest.m::interrupted_requires_bounded_root_evidence`

### Scenario: Resume подтверждается только новой минутой без operation
* **Given:** report утверждает `resumed=true`.
* **When:** клиент проверяет результат.
* **Then:** нулевые admission/operation, два новых собранных PID и полная 61-снимочная минута обязательны; повторная операция или короткое окно отклоняются.
* **Automated:** `prototype/helper-ipc/HelperSupervisorValidationTest.m::resume_requires_a_new_minute_and_no_repeated_operation`

### Scenario: Подписанный root путь проходит аварию и recovery-only restart
* **Given:** временный подписанный пакет UID 0, отсутствует прежнее root состояние, `Mac15,7`/macOS 27.0.
* **When:** `supervisor-crash-run` проверяет отказ resume на чистом состоянии, запускает фиксированную аварию, проверяет исчезновение старых процессов и отказ cleanup, затем запускает новый resume.
* **Then:** доверенный XPC сообщает `interrupted`, потом `verified/resumed=true`, без повторения operation; новый observer собран после полной минуты, журнал очищен. Свежий снимок остаётся системным; root состояние/служба/пакет удалены, фоновая активность восстановлена.
* **Manual:** `prototype/helper-ipc/integrated-probe.sh` — `supervisor-crash-run`, свежий `--helper-baseline`, затем `supervisor-cleanup`, `unregister`, `cleanup`.

### Scenario: Потеря XPC владельца закрывает только его канал
* **Given:** два действующих XPC соединения и read-only runner с собственным worker.
* **When:** чужое соединение завершается, затем владелец disconnect или его процесс гибнет.
* **Then:** первый раз канал runner остаётся открыт; при потере владельца runner и worker завершаются, а `pending`, если был, не удаляется. Старый XPC канал не может вновь стать владельцем.
* **Automated:** `prototype/helper-ipc/SupervisorProbeOwnerTest.m::xpc_invalidation_only_releases_its_own_connection`, `prototype/helper-ipc/WorkerSupervisorTest.c::upstream_loss_stops_runner_and_each_worker_phase`
* **Manual:** `prototype/helper-ipc/integrated-probe.sh supervisor-client-loss-run "$APP"` проверяет начальное окно на подписанном root пути; остальные фазы покрывает процессный тест с подставным оборудованием.

### Scenario: Гибель daemon завершает runner и не создаёт ложный proof
* **Given:** только отдельный тестовый процесс daemon держит upstream pipe runner, операция уже запустилась и `pending` сохранён.
* **When:** тест завершает именно этот собственный процесс.
* **Then:** runner и worker исчезают в пределах двух секунд; cleanup отказывает, новый resume не повторяет операцию и не принимает старое окно.
* **Automated:** `prototype/helper-ipc/WorkerSupervisorTest.c::daemon_process_death_closes_the_runner_pipe`, `prototype/helper-ipc/WorkerSupervisorTest.c::upstream_loss_stops_runner_and_each_worker_phase`

### Scenario: Потерянный XPC запрос не превращается в результат другого запуска
* **Given:** клиент удерживает соединение после `running`.
* **When:** оно прерывается либо status приходит от другого daemon/runner.
* **Then:** клиент отвергает терминальный результат; `failed/owner_lost` не содержит recovery proof, `failed/busy` не даёт второму клиенту владение.
* **Automated:** `prototype/helper-ipc/HelperSupervisorValidationTest.m::reconnect_or_different_run_is_not_a_terminal_result`

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

### 4.2. Подготовка проверки потери владельца

На `Mac15,7`/macOS 27.0 фиксированные crash/resume команды, C/Objective-C/JNI и Compose пакет собраны. Полный `worker-supervisor-test`, остальные helper контракты, изолированные writer/reader тесты и десять desktop тестов прошли. Отдельный процессный тест с подставными показаниями прошёл без root службы и SMC: диагностический родитель завершился через `SIGKILL`, observer исчез, cleanup отказал, recovery-only restart с изменённым снимком сохранил маркер и не повторил operation. В реализации родитель завершает **себя**, что устраняет отправку сигнала по номеру родителя из ребёнка и гонку повторного использования этого PID.

Пять безопасных мутаций обнаружены на временных копиях и собственных тестовых процессах: отключённый parent hook, игнорирование EOF владельца, принятие позднего маркера, принятие непривилегированного маркера и ненулевой operation при resume. Signature-only проба приняла доверенные оригинал/копию и отклонила другой identifier, ad hoc подпись, повреждение и отсутствие; root task в этой signature-only пробе не запускался. Последующая подписанная root проверка описана в §4.3; M2 остаётся открытым.

### 4.3. Реальная потеря root владельца, 2026-09-29

**Среда и источники:** `Mac15,7`, macOS 27.0; временный Apple Development подписанный Compose пакет, JNI/XPC ответы `integrated-probe.sh supervisor-crash-run`, отдельный свежий `--helper-baseline`, kernel проверки `helper-status process-path/process-absent`, системный `launchctl` и интерфейс системных настроек. Проверен код `660db481d1ca4fd284c2a9b22be03bc3729a4ed9`; SHA-256 подписанного sibling runner: `6e07a2237f60965262e8858183ebfee607499c33c48508eb0343a93530d60bb2`.

Включение через инструмент сначала показывало `on`, но служба сохраняла `requiresApproval`; после снятия временной регистрации повторная регистрация получила `Operation not permitted`/`notRegistered`. Пользователь вручную выключил и включил фоновую активность и подтвердил системный запрос. Следующая регистрация вернула `enabled`. Начальный подписанный запрос и SMC снимок прошли; два чужих клиента отклонены, daemon PID `78446` подтверждён с UID 0. Проба записи не выполнялась.

| Проверка | Фактический результат |
|---|---|
| Явный resume на чистом состоянии | runner `78596`: `finished/blocked`, `resumed=false`, `journal_clear=true`; все PID/метрики фаз и `reaped` равны нулю. Предварительный cleanup `78615` вернул `cleaned`. |
| Фиксированная авария | runner `78630` имел UID 0 и настоящий приватный root executable path. Daemon подтвердил фактический `SIGKILL`, `interrupted`, observer `78866`, `journal_pending=true`. От маркера до EOF stdout и сбора task — `3089083` нс (3,089083 мс), в пределах 2 секунд. |
| Исчезновение старых процессов и отказ очистки | `proc_pidinfo` подтвердил `ESRCH` для `78630` и `78866` до resume. Cleanup runner `78881` вернул `failed/exit`; root каталог сохранился. |
| Новый recovery-only runner | PID `78901`, UID 0 и настоящий executable path подтверждены отдельно. Итог `finished/verified`, `resumed=true`; admission/operation PID и метрики равны нулю, recovery `78912`, observer `78913`, `reaped=2`. |
| Новое окно и очистка намерения | 61 снимок за `62988355125` нс (62,988355125 с), `journal_clear=true`, `write_available=false`. Драйвер завершился с `supervisor-crash=recovered operation-repeated=false journal-clear=true`. |

Свежий XPC снимок после recovery: `Ftst=0`, режимы `[3,3]`, цели `[0,0]`, фактические RPM `[0,0]`, CPU/GPU/SSD `[56,67; 43,02; 30,52]` °C. Все записанные завершившиеся процессы отдельно проверены на отсутствие при ещё живом daemon. Финальный cleanup runner `79445` вернул `cleaned`; затем `unregister` подтвердил `notRegistered` и отсутствие system службы, временный пакет удалён. Независимая проверка не обнаружила root каталог, каталог пакета или записанные PID daemon/runner/observer/recovery/cleanup. Переключатель Ventilator вернулся в `off`; повторное открытие страницы настроек сохранило это значение.

Проверена смерть root родителя в диагностическом observer, ожидающем EOF с резервным alarm; read-only operation/recovery только проверяют исходный SMC снимок. Начальные admission/operation/recovery PID и длительность не входят в аварийный marker и отдельно здесь не измерялись. Проба не подтверждает гибель процесса внутри IOKit, отмену принятой SMC команды, аппаратный возврат после записи, питание/reboot с `pending`, сон при ручном управлении или связь будущего writer с жизнью daemon/client. M2-01/M2-02 остаются открытыми.

### 4.4. Локальная проверка привязки к XPC владельцу

На `Mac15,7`/macOS 27.0 полный `WorkerSupervisorTest` прошёл с подставными hardware callbacks и реальными процессами. Дополнительный короткий `--lifetime-only` подтвердил EOF в исходном окне, операции, recovery и observer: runner завершился кодом `125`, каждый worker исчез менее чем за две секунды; для трёх фаз после `pending` очистка отказала и повторная operation не запустилась. Отдельный тест завершил **собственный** тестовый процесс, единственный удерживавший writer pipe, и получил те же exit/pending результаты. Проверка не обращалась к SMC.

`SupervisorProbeOwnerTest` создал два настоящих анонимных XPC соединения и проверил тот же stdin API через `NSTask`. Invalidation второго не закрыла pipe первого; invalidation владельца дала EOF, и старое соединение не смогло открыть новый канал. Контракт отверг терминальный ответ с другим daemon или runner PID и принял только отказ `owner_lost` без proof. Две безопасные временные мутации были обнаружены: отсутствие EOF при invalidation и принятие другого runner. Signature-only проверка нового бинарника приняла точный Apple Development sibling и скопированные байты, отклонила другой identifier, ad hoc подпись, повреждение и отсутствие; root runner не запускала. `make check` прошёл, 117 BDD сценариев, 95 с автоматизацией. Подписанный root цикл новой версии ещё требует отдельного разрешения фоновой активности; эти локальные проверки не заменяют его и не открывают запись в SMC.

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
| `prototype/helper-ipc/SupervisorProbeCrash.c`, `prototype/helper-ipc/SupervisorProbeCrash.h` | фиксированная собственная авария после создания observer |
| `prototype/helper-ipc/SupervisorProbeOwner.m`, `prototype/helper-ipc/SupervisorProbeOwner.h`, `prototype/helper-ipc/SupervisorProbeOwnerTest.m` | состояние XPC владельца, закрытие upstream и настоящий анонимный XPC тест |
| `prototype/helper-ipc/SupervisorRunnerLifetime.c`, `prototype/helper-ipc/SupervisorRunnerLifetime.h` | проверка grant/EOF в однопоточном runner |
