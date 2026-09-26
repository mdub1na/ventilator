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

`helper-status` в режиме `serve` создаёт именованный `NSXPCListener` с одним методом без входных аргументов. Режим `request` подключается через `NSXPCConnection`, проверяет точный ответ и выводит JSON. Оба режима запускаются от текущего пользователя во время smoke-пробы. Сервис не является частью приложения, не требует привилегий, не линкует writer и вообще не обращается к AppleSMC.

## 2. Contract

Метод `fetchStatusWithReply` возвращает четыре поля: `protocol_version: 1`, `state: read_only_prototype`, `smc_access: false`, `write_available: false`. Никаких параметров у метода нет. Ошибка XPC, другая версия, иное содержимое или ожидание более 5 секунд отклоняются клиентом. Это локальный протокол, не HTTP API.

## 3. Code anchors

| Файл | Назначение |
|---|---|
| `prototype/helper-ipc/HelperStatus.h` | ограниченный протокол |
| `prototype/helper-ipc/helper-status.m` | реализация listener и клиента |
| `prototype/helper-ipc/smoke.sh` | временный plist, регистрация и удаление |
| `prototype/helper-ipc/Makefile` | сборка и запуск пробы |

## 4. Local setup

На macOS с Xcode Command Line Tools выполнить `make -C prototype/helper-ipc smoke`. Скрипт использует пользовательский домен `gui/<uid>` и требует доступа к `launchctl bootstrap`; в песочнице Codex этот вызов может быть запрещён. После запроса скрипт вызывает `bootout` и проверяет отсутствие службы. При отказе удаления он сохраняет временный plist и печатает имя службы.

## 5. Limits

Имена временной службы служат только для smoke-пробы. Здесь нет проверки подписи клиента или сервиса, постоянной регистрации `SMAppService`, интеграции с UI либо доступа к SMC. Этот код нельзя устанавливать как root daemon или расширять командой записи до прохождения отдельных проверок [границы helper](../research/research-helper-boundary.md).
