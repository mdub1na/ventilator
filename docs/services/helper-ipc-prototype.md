---
id: helper-ipc-prototype
title: Прототип XPC статуса helper
type: service
repo_url: https://github.com/mdub1na/ventilator
module: prototype/helper-ipc
tech_stack: [Kotlin/JVM, Objective-C, JNI, Foundation, Security, ServiceManagement, NSXPCConnection, launchd, macOS]
owner: unassigned
depends_on: [Foundation, launchd]
publishes: [fixed local XPC status, fixed-key read-only SMC snapshot]
---

# Прототип XPC статуса helper

## 1. Responsibility

`helper-status` в режиме `serve` создаёт именованный `NSXPCListener` с одним методом без входных аргументов. Режим `request` подключается через `NSXPCConnection`, проверяет точный ответ и выводит JSON. Smoke-проба передаёт `cdhash` временных ad hoc копий. `serve` отказывает в работе от root.

Отдельный `daemon-status` предоставляет фиксированный статус и запрос снимка по заранее заданным SMC-ключам; он принимает только клиента с Apple anchor, точным code identifier и Team ID. Подписанный `helper-status request-signed` требует такую же идентичность daemon. Прежняя проверка системного процесса с UID 0 относилась к версии без доступа к AppleSMC. Новая версия линкует IOKit, но в её модуле `SmcBaselineRead.c` есть только команды чтения `5` и `9`, а SMC writer не включён. Она не входит в основной установленный `Ventilator.app`.

Для проверки интеграции `HelperProbeBridge.m` загружается из основного JVM-процесса упакованного `Ventilator.app`. Мост читает Team ID собственной подписи через Security framework, требует точный identifier daemon и предоставляет только статус регистрации, явную регистрацию/отмену, `fetchStatusWithReply` и `fetchBaselineWithReply`. Команды доступны диагностическому CLI; обычный запуск окна не обращается к daemon. `integrated-probe.sh` добавляет daemon в отдельную подписанную копию приложения, не в установленный пользовательский пакет.

## 2. Contract

Метод `fetchStatusWithReply` возвращает четыре поля: `protocol_version: 1`, `state: read_only_prototype`, `smc_access` и `write_available: false`. Для ad hoc сервера `smc_access=false`, для подписанного daemon с read-only reader — `true`. Метод `fetchBaselineWithReply` не принимает параметров; daemon читает только `FNum`, `Ftst`, режимы, цели и фактические RPM двух вентиляторов, `TCMz`, `Tg0D`, `TH0a`. При ошибке он возвращает `available=false` и ограниченный код причины, а клиент отвергает снимок. При успехе ответ содержит `available=true`, модель/ОС, монотонное время чтения, исходное состояние и фиксированные массивы значений. Исходное состояние: `Ftst=0`, режимы `[3,3]`, обе цели не выше 1 RPM. Для ad hoc `serve` и `request` CLI требует 40-значный ожидаемый `cdhash` peer. Для подписанного клиента и daemon нужен 10-значный Team ID и точные идентификаторы. Ошибка XPC, другая версия, иное содержимое или ожидание более 5 секунд отклоняются клиентом. Это локальный протокол, не HTTP API.

Дополнительные методы `startBaselineWatchWithReply` и `fetchBaselineWatchWithReply` также не принимают параметров. UID 0 daemon хранит состояние 61 последовательного read-only снимка после выхода клиента и отдаёт `idle`, `running`, `stable`, `changed` или `read_failed`. Подписанный клиент проверяет число снимков, последнюю отметку и пересчитывает `baseline`. На `Mac15,7`/macOS 27.0 наблюдение дошло до `stable` за 66,09 секунды от одного PID daemon, когда первый JVM-процесс уже завершился. Подробности и ограничения — в [сценариях watcher](../features/helper-baseline-watch.md).

Новый `fetchStartupAuditWithReply` возвращает неизменяемое read-only чтение, захваченное до запуска listener в текущем процессе daemon. Состояния: `system_at_start`, `changed_at_start`, `read_failed`; во всех случаях `control_allowed=false`. Подписанный клиент повторно проверяет снимок и состояние. Этот audit отделён от минутного watcher и от свежего `fetchBaselineWithReply`; [сценарии](../features/helper-startup-audit.md) фиксируют его ограничения.

Подписанная root проба на `Mac15,7`/macOS 27.0 вернула `system_at_start` от PID `49002` с исходными `Ftst=0`, режимами `[3,3]` и целями `[0,0]`; отметка audit предшествовала отдельному свежему чтению. После `SIGKILL` новый PID `49648` вернул собственный, более поздний audit с теми же исходными показаниями и `control_allowed=false`. Временная служба и пакет удалены, фоновая активность возвращена в «выкл.».

Для проверки полной перезагрузки `prepare-reboot` сохранил временный пакет вне очищаемого macOS каталога. `reboot-before` записал `kern.boottime` и startup audit, а `reboot-after` после перезагрузки потребовал новый boot time, ответ от UID 0 и свежий исходный SMC снимок. На `Mac15,7`/macOS 27.0 проверены boot time `1790359888 → 1790441319`, PID `57992 → 1701`, `system_at_start` и `control_allowed=false`. Служба снята с регистрации, пакет удалён, системная фоновая запись Ventilator исчезла. До входа пользователя запуск daemon этой пробой не подтверждён.

Следующая read-only копия добавила `RunAtLoad=true` только в сохраняемый LaunchDaemon plist. `eager-check` сначала прочитал UID 0 PID `6923` из `launchctl`, затем получил startup audit от того же процесса. После полной перезагрузки `reboot-after` обнаружил UID 0 PID `298` **до** первого XPC-запроса и получил его audit; `kern.boottime` изменился `1790441319 → 1790442502`. На `Mac15,7`/macOS 27.0 оба audit вернули исходные ключи управления и `control_allowed=false`. В раннем audit температура `Tg0D=-1,95 °C` была неправдоподобной, а новое чтение через ~63 секунды вернуло `49,48 °C`; поля температур стартового снимка нельзя без проверки использовать для UI. Служба и пакет удалены, запись Ventilator исчезла из системного списка. Момент проверки был после входа пользователя; запуск до входа не подтверждён.

## 3. Code anchors

| Файл | Назначение |
|---|---|
| `prototype/helper-ipc/HelperStatus.h` | ограниченный протокол |
| `prototype/helper-ipc/helper-status.m` | реализация listener и клиента |
| `prototype/helper-ipc/smoke.sh` | временный plist, регистрация и удаление |
| `prototype/helper-ipc/Makefile` | сборка и запуск пробы |
| `prototype/helper-ipc/agent-registration.m` | вызовы `SMAppService.agent` из тестового `.app` |
| `prototype/helper-ipc/package-smoke.sh` | сборка временного пакета, регистрация, XPC и удаление |
| `prototype/helper-ipc/daemon-status.m` | отдельный ограниченный read-only daemon |
| `prototype/helper-ipc/SmcBaselineRead.c` | фиксированный SMC reader и критерий исходного состояния |
| `prototype/helper-ipc/HelperBaselineValidation.h` | проверка формы XPC снимка и независимый пересчёт `baseline` на стороне клиентов |
| `prototype/helper-ipc/BaselineWatch.c`, `prototype/helper-ipc/BaselineWatchController.m` | sticky state machine и таймер внутри daemon |
| `prototype/helper-ipc/HelperWatchValidation.h` | проверка XPC статуса watcher на стороне клиента |
| `prototype/helper-ipc/StartupAuditController.m`, `prototype/helper-ipc/HelperStartupAuditValidation.h` | одно чтение перед listener и проверка ответа у клиента |
| `prototype/helper-ipc/daemon-registration.m` | вызовы `SMAppService.daemon` из тестового `.app` |
| `prototype/helper-ipc/daemon-probe.sh` | подписанный пакет, регистрация, проверка и удаление |
| `prototype/helper-ipc/signed-ipc-smoke.sh` | проверка подписанного IPC в пользовательском домене |
| `prototype/helper-ipc/HelperProbeBridge.m` | JNI-мост основного процесса к ServiceManagement и XPC |
| `prototype/helper-ipc/integrated-probe.sh` | подписанная копия Compose app и проверка её daemon |
| `prototype/helper-ipc/root-inflight-smoke.sh` | отдельная проба прерванного запроса к системному read-only daemon |
| `prototype/desktop-app/src/main/kotlin/ventilator/desktop/helper/HelperProbeCommand.kt` | диагностические команды основного JVM-процесса |
| `prototype/desktop-app/build.gradle.kts` | упаковка JNI-библиотеки |

## 4. Local setup

На macOS с Xcode Command Line Tools выполнить `make -C prototype/helper-ipc smoke`. Скрипт создаёт две ad hoc подписанные копии с разными идентификаторами и `cdhash`, регистрирует временные службы в пользовательском домене `gui/<uid>`, проверяет успешный статус и оба отказа по несовпадению подписи. `launchctl bootstrap` в песочнице Codex может быть запрещён. После запросов скрипт вызывает `bootout` и проверяет отсутствие обеих служб. При отказе удаления он сохраняет временный plist и печатает имя службы. На `Mac15,7`/macOS 27.0 эта проба прошла; клиент получил `4102` для другой копии сервиса, другая копия клиента — `4097`, delegate доверенного сервиса не принял её, а доверенное соединение после этого продолжило работать.

`make -C prototype/helper-ipc package-smoke` строит отдельный ad hoc подписанный `HelperProbe.app`. В нём helper находится в `Contents/Resources`, а plist с относительным `BundleProgram` — в `Contents/Library/LaunchAgents`, как описывает [Apple для SMAppService](https://developer.apple.com/documentation/servicemanagement/updating-helper-executables-from-earlier-versions-of-macos). Тестовый app вызывает `SMAppService.agent(plistName:)`, регистрирует и отменяет регистрацию, затем скрипт подтверждает `notRegistered` и отсутствие службы. На `Mac15,7`/macOS 27.0 наблюдались `notFound → enabled → notRegistered`, успешный XPC ответ и удаление пакета. Это не упаковка `Ventilator.app` и не системный LaunchDaemon.

`make -C prototype/helper-ipc daemon-prepare` строит отдельный `HelperDaemonProbe.app` с LaunchDaemon plist. Скрипт выбирает локальный Apple Development сертификат, который проходит `codesign --verify --strict`; имя и Team ID в репозитории не хранятся. Он проверяет подписи пакета и клиентов и выдаёт путь к пакету. Дальше `daemon-probe.sh register APP`, `status APP`, `check APP`, `unregister APP` и `cleanup APP` выполняются явно. `check` запрашивает статус и фиксированный SMC снимок доверенным клиентом, проверяет отказ ad hoc клиента и клиента с тем же Team ID, но другим identifier, а также наличие системной службы. `cleanup` отказывает в удалении пакета, пока служба зарегистрирована или видна в `launchctl`. `signed-ipc-smoke.sh APP` использует тот же подписанный daemon в пользовательском домене и автоматически удаляет временный LaunchAgent.

На `Mac15,7`/macOS 27.0 первый `register` вернул `Operation not permitted` и `requiresApproval`. После разрешения macOS статус стал `enabled`, системный daemon работал с UID 0, оба доверенных запроса прошли, ad hoc клиент был отклонён. Повторный запуск пробы также отклонил другого подписанного клиента. `unregister` оба раза вернул `notRegistered`; служба отсутствовала в `launchctl`, оба пакета удалены. Это не проверка обновления, распространения или основного приложения.

Интеграционный скрипт собирает Compose app через `createDistributable`, копирует пакет во временный каталог, добавляет read-only daemon с требованием к `ventilator.desktop` и подписывает всю копию Apple Development сертификатом. `codesign --verify --strict --deep` прошёл на `Mac15,7`/macOS 27.0. Команда `--helper-registration-status` из главного JVM-процесса загрузила JNI-мост и сначала показала `notFound`; после `--helper-register` macOS вернула `requiresApproval`. Запрос `--helper-request` до разрешения завершился ошибкой, а не ложным статусом. После разрешения статус стал `enabled`, главный JVM-процесс дважды получил точный четырёхпольный статус. `check` отклонил другого подписанного клиента с тем же Team ID и ad hoc клиента; `launchctl` подтвердил UID 0. `unregister` вернул `notRegistered` и отсутствие системной службы; временный пакет удалён, фоновый переключатель возвращён в исходное положение «выкл.».

Дополнительные команды `restart-before/restart-after` и `sleep-before/sleep-after` относятся только к временной read-only копии. Первая пара записывает PID, после отдельной административной команды `sudo launchctl kill SIGKILL` проверяет новый root PID и XPC ответ. Вторая пара требует подтверждённого системой цикла сна/пробуждения в пределах одной загрузки, действующей регистрации и ответа после пробуждения. На `Mac15,7`/macOS 27.0 обе пробы прошли: после `SIGKILL` новый PID с UID 0 ответил доверенному приложению при статусе `enabled`; после сна счётчик `pmset` увеличился без перезагрузки, а root daemon вернул тот же статус. Непривилегированный `launchctl kill` получил отказ. `unregister` вернул `notRegistered`, системная служба исчезла, пакет удалён, фоновый переключатель возвращён в «выкл.». Эти пробы не подтверждают восстановление SMC или ответ на прерванный запрос.

`client-lifecycle-smoke.sh` проверяет выбор системного bootstrap до регистрации root daemon. Временный пользовательский LaunchAgent рекламирует **то же** имя службы и использует тот же подписанный бинарник. Контрольный клиент с пользовательским соединением получает статус, а JNI-мост с `NSXPCConnectionPrivileged` его отклоняет. Первый запуск этой пробы обнаружил устаревший JNI-бинарник внутри `.app`: исходный код был исправлен, но `createDistributable` пропустил пересборку пакета. После добавления нативных файлов во входы Gradle-задачи новая копия содержала флаг `0x1000` и прошла отрицательный тест. Скрипт также приостанавливает пользовательский daemon во время запроса, завершает его, требует ошибку старого запроса и ответ после явного перезапуска. Он удаляет LaunchAgent и не выдаёт этот результат за прерывание root XPC.

Новая системная read-only проба той же копии после разрешения фоновой активности снова потребовала `unregister`, паузу и повторный `register` для статуса `enabled`. Команда `check` подтвердила два ответа из главного JVM-процесса от root daemon с UID 0 и отказ двух чужих клиентов. `ui-crash` завершила только запущенный из временного пакета UI: его дочерний значок вышел, PID daemon не изменился, новый XPC запрос получил статус. После `unregister` служба отсутствовала в системном `launchctl`, пакет удалён, фоновый переключатель возвращён в «выкл.». Это не проверка обработки прерванного запроса к root daemon и не восстановление SMC.

`root-inflight-smoke.sh` затем проверил прерванный запрос к системному read-only daemon на `Mac15,7`/macOS 27.0. Пользователь выполнил скрипт в Terminal с `sudo`; он остановил только тестовый daemon, начал запрос и завершил этот процесс. Старый запрос завершился ошибкой связи без принятого статуса, а новый автоматически получил фиксированный ответ от другого PID с UID 0 (`85675 → 86052`). Независимая проверка подтвердила `enabled`, новый root PID и отсутствие старого. После пробы `unregister` подтвердил отсутствие службы, пакет удалён, фон Ventilator возвращён в «выкл.». Доступ к SMC и восстановление режима вентиляторов здесь не проверялись.

`integrated-probe.sh watch-crash-run` отдельно проверил аварию daemon во время read-only наблюдения на той же машине. После предварительного `sudo -v` скрипт подтвердил `running`, остановил только временную системную службу и потребовал от нового UID 0 PID `idle`/`samples=0`. Новый 61-снимочный интервал завершился `stable`; независимый запрос подтвердил `Ftst=0`, режимы `[3,3]`, цели `[0,0]`. Служба и пакет удалены, фон возвращён в «выкл.». Это не испытание восстановления после записи SMC.

## 5. Проверка и ограничения

Новая fixed-key read-only версия прошла signed XPC сначала в пользовательском домене, затем через главное приложение к UID 0 daemon на `Mac15,7`/macOS 27.0. Ответ содержал `Ftst=0`, режимы `[3,3]`, цели `[0,0]`, RPM `[0,0]`, `TCMz`/`Tg0D`/`TH0a`, `available=true`, `baseline=true`; оба вида чужих клиентов отклонены. До системного разрешения запрос завершался ошибкой. После проверки `unregister` подтвердил отсутствие службы, тестовый пакет удалён, фоновый переключатель возвращён в «выкл.». Источник и шаги: [протокол](../features/helper-status-ipc.md#3-проверка-на-устройстве), `integrated-probe.sh check/unregister/cleanup`. Записи SMC не выполнялись.

Имена служб служат только для проб. `cdhash` подтверждает конкретный код в текущем запуске, но копия того же бинарника имеет тот же хеш; ad hoc подпись не задаёт доверенного автора. Действующий Apple Development сертификат найден вне песочницы; он подходит для локальной пробы, а Developer ID и нотарификация не проверены. Тестовые пакеты создаются во временном каталоге и удаляются после подтверждённого `unregister`; daemon умеет только читать фиксированные ключи SMC. `helper-status serve` по-прежнему отказывает при root. Отдельный `daemon-status` нельзя расширять командой записи до проверки [границы helper](../research/research-helper-boundary.md).
