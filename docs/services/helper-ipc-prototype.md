---
id: helper-ipc-prototype
title: Прототип XPC статуса helper
type: service
repo_url: https://github.com/mdub1na/ventilator
module: prototype/helper-ipc
tech_stack: [Kotlin/JVM, Objective-C, JNI, Foundation, Security, ServiceManagement, NSXPCConnection, launchd, macOS]
owner: unassigned
depends_on: [Foundation, launchd]
publishes: [fixed local XPC status]
---

# Прототип XPC статуса helper

## 1. Responsibility

`helper-status` в режиме `serve` создаёт именованный `NSXPCListener` с одним методом без входных аргументов. Режим `request` подключается через `NSXPCConnection`, проверяет точный ответ и выводит JSON. Smoke-проба передаёт `cdhash` временных ad hoc копий. `serve` отказывает в работе от root.

Отдельный `daemon-status` предоставляет тот же фиксированный статус и принимает только клиента с Apple anchor, точным code identifier и Team ID. Подписанный `helper-status request-signed` требует такую же идентичность daemon. Этот процесс проверен в системном домене с UID 0, но не линкует IOKit или writer и не обращается к AppleSMC. Он не входит в основной `Ventilator.app`.

Для проверки интеграции `HelperProbeBridge.m` загружается из основного JVM-процесса упакованного `Ventilator.app`. Мост читает Team ID собственной подписи через Security framework, требует точный identifier daemon и предоставляет только статус регистрации, явную регистрацию/отмену и `fetchStatusWithReply`. Команды доступны диагностическому CLI; обычный запуск окна не обращается к daemon. `integrated-probe.sh` добавляет daemon в отдельную подписанную копию приложения, не в установленный пользовательский пакет.

## 2. Contract

Метод `fetchStatusWithReply` возвращает четыре поля: `protocol_version: 1`, `state: read_only_prototype`, `smc_access: false`, `write_available: false`. Никаких параметров у метода нет. Для ad hoc `serve` и `request` CLI требует 40-значный ожидаемый `cdhash` peer. Для `request-signed` и `daemon-status` нужен 10-значный Team ID; первый также закрепляет идентификатор daemon, второй — идентификатор клиента. Ошибка XPC, другая версия, иное содержимое или ожидание более 5 секунд отклоняются клиентом. Это локальный протокол, не HTTP API.

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

`make -C prototype/helper-ipc daemon-prepare` строит отдельный `HelperDaemonProbe.app` с LaunchDaemon plist. Скрипт выбирает локальный Apple Development сертификат, который проходит `codesign --verify --strict`; имя и Team ID в репозитории не хранятся. Он проверяет подписи пакета и клиентов и выдаёт путь к пакету. Дальше `daemon-probe.sh register APP`, `status APP`, `check APP`, `unregister APP` и `cleanup APP` выполняются явно. `check` делает два запроса доверенным клиентом, проверяет отказ ad hoc клиента и клиента с тем же Team ID, но другим identifier, а также наличие системной службы. `cleanup` отказывает в удалении пакета, пока служба зарегистрирована или видна в `launchctl`. `signed-ipc-smoke.sh APP` использует тот же подписанный daemon в пользовательском домене и автоматически удаляет временный LaunchAgent.

На `Mac15,7`/macOS 27.0 первый `register` вернул `Operation not permitted` и `requiresApproval`. После разрешения macOS статус стал `enabled`, системный daemon работал с UID 0, оба доверенных запроса прошли, ad hoc клиент был отклонён. Повторный запуск пробы также отклонил другого подписанного клиента. `unregister` оба раза вернул `notRegistered`; служба отсутствовала в `launchctl`, оба пакета удалены. Это не проверка обновления, распространения или основного приложения.

Интеграционный скрипт собирает Compose app через `createDistributable`, копирует пакет во временный каталог, добавляет read-only daemon с требованием к `ventilator.desktop` и подписывает всю копию Apple Development сертификатом. `codesign --verify --strict --deep` прошёл на `Mac15,7`/macOS 27.0. Команда `--helper-registration-status` из главного JVM-процесса загрузила JNI-мост и сначала показала `notFound`; после `--helper-register` macOS вернула `requiresApproval`. Запрос `--helper-request` до разрешения завершился ошибкой, а не ложным статусом. После разрешения статус стал `enabled`, главный JVM-процесс дважды получил точный четырёхпольный статус. `check` отклонил другого подписанного клиента с тем же Team ID и ad hoc клиента; `launchctl` подтвердил UID 0. `unregister` вернул `notRegistered` и отсутствие системной службы; временный пакет удалён, фоновый переключатель возвращён в исходное положение «выкл.».

Дополнительные команды `restart-before/restart-after` и `sleep-before/sleep-after` относятся только к временной read-only копии. Первая пара записывает PID, после отдельной административной команды `sudo launchctl kill SIGKILL` проверяет новый root PID и XPC ответ. Вторая пара требует подтверждённого системой цикла сна/пробуждения в пределах одной загрузки, действующей регистрации и ответа после пробуждения. На `Mac15,7`/macOS 27.0 обе пробы прошли: после `SIGKILL` новый PID с UID 0 ответил доверенному приложению при статусе `enabled`; после сна счётчик `pmset` увеличился без перезагрузки, а root daemon вернул тот же статус. Непривилегированный `launchctl kill` получил отказ. `unregister` вернул `notRegistered`, системная служба исчезла, пакет удалён, фоновый переключатель возвращён в «выкл.». Эти пробы не подтверждают восстановление SMC или ответ на прерванный запрос.

`client-lifecycle-smoke.sh` проверяет выбор системного bootstrap до регистрации root daemon. Временный пользовательский LaunchAgent рекламирует **то же** имя службы и использует тот же подписанный бинарник. Контрольный клиент с пользовательским соединением получает статус, а JNI-мост с `NSXPCConnectionPrivileged` его отклоняет. Первый запуск этой пробы обнаружил устаревший JNI-бинарник внутри `.app`: исходный код был исправлен, но `createDistributable` пропустил пересборку пакета. После добавления нативных файлов во входы Gradle-задачи новая копия содержала флаг `0x1000` и прошла отрицательный тест. Скрипт также приостанавливает пользовательский daemon во время запроса, завершает его, требует ошибку старого запроса и ответ после явного перезапуска. Он удаляет LaunchAgent и не выдаёт этот результат за прерывание root XPC.

Новая системная read-only проба той же копии после разрешения фоновой активности снова потребовала `unregister`, паузу и повторный `register` для статуса `enabled`. Команда `check` подтвердила два ответа из главного JVM-процесса от root daemon с UID 0 и отказ двух чужих клиентов. `ui-crash` завершила только запущенный из временного пакета UI: его дочерний значок вышел, PID daemon не изменился, новый XPC запрос получил статус. После `unregister` служба отсутствовала в системном `launchctl`, пакет удалён, фоновый переключатель возвращён в «выкл.». Это не проверка обработки прерванного запроса к root daemon и не восстановление SMC.

`root-inflight-smoke.sh` затем проверил прерванный запрос к системному read-only daemon на `Mac15,7`/macOS 27.0. Пользователь выполнил скрипт в Terminal с `sudo`; он остановил только тестовый daemon, начал запрос и завершил этот процесс. Старый запрос завершился ошибкой связи без принятого статуса, а новый автоматически получил фиксированный ответ от другого PID с UID 0 (`85675 → 86052`). Независимая проверка подтвердила `enabled`, новый root PID и отсутствие старого. После пробы `unregister` подтвердил отсутствие службы, пакет удалён, фон Ventilator возвращён в «выкл.». Доступ к SMC и восстановление режима вентиляторов здесь не проверялись.

## 5. Limits

Имена служб служат только для проб. `cdhash` подтверждает конкретный код в текущем запуске, но копия того же бинарника имеет тот же хеш; ad hoc подпись не задаёт доверенного автора. Действующий Apple Development сертификат найден вне песочницы; он подходит для локальной пробы, а Developer ID и нотарификация не проверены. Тестовые пакеты создаются во временном каталоге и удаляются после подтверждённого `unregister`; доступа к SMC в daemon нет. `helper-status serve` по-прежнему отказывает при root. Отдельный `daemon-status` нельзя расширять командой записи до проверки [границы helper](../research/research-helper-boundary.md).
