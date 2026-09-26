---
id: helper-status-ipc
title: Первый read-only XPC контракт helper
type: feature
status: active
owner: unassigned
involved_services: [helper-ipc-prototype]
client_entries: []
api: []
tags: [macos, xpc, safety]
---

# Первый read-only XPC контракт helper

## 1. Назначение

Отдельный процесс отвечает на запрос статуса фиксированным снимком: версия протокола `1`, состояние `read_only_prototype`, `smc_access=false`, `write_available=false`. Это проверка формы XPC-контракта и обмена между процессами. Процесс не читает и не записывает AppleSMC, не входит в `Ventilator.app` и не является привилегированным helper.

## 2. Правила

1. XPC-интерфейс содержит только `fetchStatusWithReply`; клиент не передаёт ключ SMC, RPM, путь, команду или иной параметр.
2. Клиент принимает ответ только с четырьмя ожидаемыми полями и версией `1`; ошибка или таймаут за 5 секунд означает недоступность сервиса.
3. Smoke-проба регистрирует отдельный пользовательский Mach service во временном plist и удаляет его через `launchctl bootout`. Она не регистрирует LaunchDaemon из приложения и не требует `sudo`.
4. Если удаление временной службы не подтверждено, скрипт завершает работу с ошибкой и сохраняет plist для проверки. Такой результат нельзя считать успешной пробой.

## 3. Проверка на устройстве

На `Mac15,7`, macOS 27.0, `make -C prototype/helper-ipc smoke` собрал Objective-C бинарник и выполнил XPC запрос от отдельного процесса. Ответ: `{"protocol_version":1,"smc_access":false,"state":"read_only_prototype","write_available":false}`. `launchctl print` после `bootout` не нашёл временную службу. Доступ к SMC и привилегии администратора не использовались. Внутри песочницы Codex `launchctl bootstrap` вернул ошибку 5; проверка прошла с разрешённым пользовательским запуском среды.

## 4. Сценарии

### Scenario: Клиент получает фиксированный статус
* **Given:** временная пользовательская служба запущена.
* **When:** клиент вызывает `fetchStatusWithReply`.
* **Then:** клиент принимает четыре поля версии `1` и печатает JSON; после пробы служба отсутствует.
* **Manual:** `make -C prototype/helper-ipc smoke` на `Mac15,7`/macOS 27.0, §3.

### Scenario: Служба не отвечает
* **Given:** соединение недоступно или ответ не пришёл в течение 5 секунд.
* **When:** клиент запрашивает статус.
* **Then:** клиент завершает работу с ненулевым кодом, не объявляя helper доступным.
* **Implementation:** `prototype/helper-ipc/helper-status.m`.

## 5. Граница результата

В этом этапе нет подписи сторон XPC, `SMAppService.daemon`, установки LaunchDaemon, JNI-моста, чтения SMC или управления вентиляторами. Даже успешный XPC ответ не закрывает M2-02: проверка чужого клиента, жизненного цикла и аппаратного восстановления остаётся открытой. [Исследование границы](../research/research-helper-boundary.md) описывает порядок следующих проверок.

## 6. Code anchors

| Назначение | Код |
|---|---|
| XPC протокол | `prototype/helper-ipc/HelperStatus.h` |
| Listener и клиент | `prototype/helper-ipc/helper-status.m` |
| Временная регистрация и удаление | `prototype/helper-ipc/smoke.sh` |
| Сборка | `prototype/helper-ipc/Makefile` |
