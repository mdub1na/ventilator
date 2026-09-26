---
id: helper-ipc-prototype
title: Прототип XPC статуса helper
type: service
repo_url: https://github.com/mdub1na/ventilator
module: prototype/helper-ipc
tech_stack: [Objective-C, Foundation, ServiceManagement, NSXPCConnection, launchd, macOS]
owner: unassigned
depends_on: [Foundation, launchd]
publishes: [fixed local XPC status]
---

# Прототип XPC статуса helper

## 1. Responsibility

`helper-status` в режиме `serve` создаёт именованный `NSXPCListener` с одним методом без входных аргументов. Режим `request` подключается через `NSXPCConnection`, проверяет точный ответ и выводит JSON. Smoke-проба передаёт `cdhash` временных ad hoc копий. `serve` отказывает в работе от root.

Отдельный `daemon-status` предоставляет тот же фиксированный статус и принимает только клиента с Apple anchor, точным code identifier и Team ID. Подписанный `helper-status request-signed` требует такую же идентичность daemon. Этот процесс проверен в системном домене с UID 0, но не линкует IOKit или writer и не обращается к AppleSMC. Он не входит в основной `Ventilator.app`.

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

## 4. Local setup

На macOS с Xcode Command Line Tools выполнить `make -C prototype/helper-ipc smoke`. Скрипт создаёт две ad hoc подписанные копии с разными идентификаторами и `cdhash`, регистрирует временные службы в пользовательском домене `gui/<uid>`, проверяет успешный статус и оба отказа по несовпадению подписи. `launchctl bootstrap` в песочнице Codex может быть запрещён. После запросов скрипт вызывает `bootout` и проверяет отсутствие обеих служб. При отказе удаления он сохраняет временный plist и печатает имя службы. На `Mac15,7`/macOS 27.0 эта проба прошла; клиент получил `4102` для другой копии сервиса, другая копия клиента — `4097`, delegate доверенного сервиса не принял её, а доверенное соединение после этого продолжило работать.

`make -C prototype/helper-ipc package-smoke` строит отдельный ad hoc подписанный `HelperProbe.app`. В нём helper находится в `Contents/Resources`, а plist с относительным `BundleProgram` — в `Contents/Library/LaunchAgents`, как описывает [Apple для SMAppService](https://developer.apple.com/documentation/servicemanagement/updating-helper-executables-from-earlier-versions-of-macos). Тестовый app вызывает `SMAppService.agent(plistName:)`, регистрирует и отменяет регистрацию, затем скрипт подтверждает `notRegistered` и отсутствие службы. На `Mac15,7`/macOS 27.0 наблюдались `notFound → enabled → notRegistered`, успешный XPC ответ и удаление пакета. Это не упаковка `Ventilator.app` и не системный LaunchDaemon.

`make -C prototype/helper-ipc daemon-prepare` строит отдельный `HelperDaemonProbe.app` с LaunchDaemon plist. Скрипт выбирает локальный Apple Development сертификат, который проходит `codesign --verify --strict`; имя и Team ID в репозитории не хранятся. Он проверяет подписи пакета и клиентов и выдаёт путь к пакету. Дальше `daemon-probe.sh register APP`, `status APP`, `check APP`, `unregister APP` и `cleanup APP` выполняются явно. `check` делает два запроса доверенным клиентом, проверяет отказ ad hoc клиента и клиента с тем же Team ID, но другим identifier, а также наличие системной службы. `cleanup` отказывает в удалении пакета, пока служба зарегистрирована или видна в `launchctl`. `signed-ipc-smoke.sh APP` использует тот же подписанный daemon в пользовательском домене и автоматически удаляет временный LaunchAgent.

На `Mac15,7`/macOS 27.0 первый `register` вернул `Operation not permitted` и `requiresApproval`. После разрешения macOS статус стал `enabled`, системный daemon работал с UID 0, оба доверенных запроса прошли, ad hoc клиент был отклонён. Повторный запуск пробы также отклонил другого подписанного клиента. `unregister` оба раза вернул `notRegistered`; служба отсутствовала в `launchctl`, оба пакета удалены. Это не проверка обновления, распространения или основного приложения.

## 5. Limits

Имена служб служат только для проб. `cdhash` подтверждает конкретный код в текущем запуске, но копия того же бинарника имеет тот же хеш; ad hoc подпись не задаёт доверенного автора. Действующий Apple Development сертификат найден вне песочницы; он подходит для локальной пробы, а Developer ID и нотарификация не проверены. Оба тестовых пакета создаются во временном каталоге и удаляются после подтверждённого `unregister`; интеграции с UI и доступа к SMC нет. `helper-status serve` по-прежнему отказывает при root. Отдельный `daemon-status` нельзя расширять командой записи до проверки [границы helper](../research/research-helper-boundary.md).
