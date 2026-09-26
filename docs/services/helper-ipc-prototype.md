---
id: helper-ipc-prototype
title: Пользовательский прототип XPC статуса helper
type: service
repo_url: https://github.com/mdub1na/ventilator
module: prototype/helper-ipc
tech_stack: [Objective-C, Foundation, NSXPCConnection, launchd, macOS]
owner: unassigned
depends_on: [Foundation, launchd]
publishes: [fixed local XPC status]
---

# Пользовательский прототип XPC статуса helper

## 1. Responsibility

`helper-status` в режиме `serve` создаёт именованный `NSXPCListener` с одним методом без входных аргументов. Режим `request` подключается через `NSXPCConnection`, проверяет точный ответ и выводит JSON. Обе стороны задают требование подписи к противоположному процессу до запуска XPC-соединения. Smoke-проба передаёт `cdhash` временных ad hoc копий. Оба режима запускаются от текущего пользователя. Сервис не является частью приложения, не требует привилегий, не линкует writer и вообще не обращается к AppleSMC.

## 2. Contract

Метод `fetchStatusWithReply` возвращает четыре поля: `protocol_version: 1`, `state: read_only_prototype`, `smc_access: false`, `write_available: false`. Никаких параметров у метода нет. Для `serve` и `request` CLI требует 40-значный ожидаемый `cdhash` peer; иной формат отклоняется до XPC. Ошибка XPC, другая версия, иное содержимое или ожидание более 5 секунд отклоняются клиентом. Это локальный протокол, не HTTP API.

## 3. Code anchors

| Файл | Назначение |
|---|---|
| `prototype/helper-ipc/HelperStatus.h` | ограниченный протокол |
| `prototype/helper-ipc/helper-status.m` | реализация listener и клиента |
| `prototype/helper-ipc/smoke.sh` | временный plist, регистрация и удаление |
| `prototype/helper-ipc/Makefile` | сборка и запуск пробы |
| `prototype/helper-ipc/agent-registration.m` | вызовы `SMAppService.agent` из тестового `.app` |
| `prototype/helper-ipc/package-smoke.sh` | сборка временного пакета, регистрация, XPC и удаление |

## 4. Local setup

На macOS с Xcode Command Line Tools выполнить `make -C prototype/helper-ipc smoke`. Скрипт создаёт две ad hoc подписанные копии с разными идентификаторами и `cdhash`, регистрирует временные службы в пользовательском домене `gui/<uid>`, проверяет успешный статус и оба отказа по несовпадению подписи. `launchctl bootstrap` в песочнице Codex может быть запрещён. После запросов скрипт вызывает `bootout` и проверяет отсутствие обеих служб. При отказе удаления он сохраняет временный plist и печатает имя службы. На `Mac15,7`/macOS 27.0 эта проба прошла; клиент получил `4102` для другой копии сервиса, другая копия клиента — `4097`, delegate доверенного сервиса не принял её, а доверенное соединение после этого продолжило работать.

`make -C prototype/helper-ipc package-smoke` строит отдельный ad hoc подписанный `HelperProbe.app`. В нём helper находится в `Contents/Resources`, а plist с относительным `BundleProgram` — в `Contents/Library/LaunchAgents`, как описывает [Apple для SMAppService](https://developer.apple.com/documentation/servicemanagement/updating-helper-executables-from-earlier-versions-of-macos). Тестовый app вызывает `SMAppService.agent(plistName:)`, регистрирует и отменяет регистрацию, затем скрипт подтверждает `notRegistered` и отсутствие службы. На `Mac15,7`/macOS 27.0 наблюдались `notFound → enabled → notRegistered`, успешный XPC ответ и удаление пакета. Это не упаковка `Ventilator.app` и не системный LaunchDaemon.

## 5. Limits

Имена временных служб служат только для проб. `cdhash` подтверждает конкретный код в текущем запуске, но копия того же бинарника имеет тот же хеш; ad hoc подпись не задаёт доверенного автора или устойчивую идентичность для обновления. Локально не найдено ни одной действующей code-signing identity. Пакет создаётся во временном каталоге и удаляется после теста; нет интеграции с UI либо доступа к SMC. Код отказывает в запуске `serve` с правами root; его нельзя устанавливать как root daemon или расширять командой записи до прохождения отдельных проверок [границы helper](../research/research-helper-boundary.md).
