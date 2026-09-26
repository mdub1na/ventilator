---
id: helper-startup-audit
title: Read-only аудит SMC при запуске helper
type: feature
status: active
owner: unassigned
involved_services: [helper-ipc-prototype]
client_entries: []
api: []
tags: [macos, smc, xpc, safety]
---

# Read-only аудит SMC при запуске helper

## 1. Назначение

После `SIGKILL` память минутного watcher исчезает: новый daemon отвечает `idle` и `samples=0`. Пустой watcher не сообщает, какое состояние SMC увидел процесс при старте. Отдельный audit читает фиксированные ключи **один раз до запуска XPC listener** и сохраняет результат на время жизни daemon. Он ничего не записывает и никогда не разрешает управление вентиляторами.

## 2. Правила

1. При каждом запуске подписанного daemon `StartupAuditController` вызывает тот же fixed-key reader, который проверен для `fetchBaselineWithReply`. Снимок и PID захватываются до `listener resume`; последующие XPC запросы не повторяют чтение startup audit.
2. Один исходный снимок даёт `state=system_at_start`, изменённый `Ftst`, режим или цель — `changed_at_start`. Ошибка чтения, неподдерживаемая модель/ОС или ошибка монотонных часов дают `read_failed` с ограниченным кодом причины. Ни одно состояние не обозначает устойчивое восстановление после записи.
3. Ответ содержит `control_allowed=false` при любом состоянии. Подписанный клиент проверяет точную форму ответа и сам пересчитывает `baseline`; ложное `system_at_start`, некорректный снимок или `control_allowed=true` отвергаются. Метод не принимает аргументов и не открывает произвольные SMC-ключи.
4. После аварии daemon новый PID создаёт собственный startup audit. Прежний снимок не переносится; разница PID и монотонных отметок проверяется отдельной пробой. Обычный UI сам службу не регистрирует.

## 3. Локальный контракт

`fetchStartupAuditWithReply` возвращает `protocol_version`, `daemon_pid`, `state`, `control_allowed`. При успешном чтении добавляются `available=true`, модель/версия ОС, монотонная отметка и фиксированный снимок `Ftst`, двух режимов, целей, RPM и трёх температур. При ошибке возвращаются `available=false` и `reason`, без выдуманных показаний. Диагностическая команда временной подписанной копии: `Ventilator --helper-startup-audit`. Её код 0 означает только валидный XPC ответ, а не разрешение на управление.

## 4. Проверка

`StartupAuditControllerTest.m` подставляет исходный, изменённый и ошибочный reader, проверяет единственное чтение на объект и валидацию XPC ответа.

Первый запуск временной Apple Development подписанной копии на `Mac15,7`/macOS 27.0 вернул из UID 0 daemon PID `49002` `state=system_at_start`, `control_allowed=false`, `Ftst=0`, режимы `[3,3]`, цели `[0,0]`, RPM `[0,0]`. Монотонная отметка startup audit `78494523338000` предшествовала отметке свежего `fetchBaseline` `78494651086000`. Оба чужих клиента были отклонены. После пользовательского запуска `startup-audit-crash-run` независимый запрос подтвердил новый PID `49648` и новый audit с отметкой `78734889463000`, тем же исходным SMC состоянием и `control_allowed=false`. Служба снята с регистрации (`notRegistered`, `system-service=absent`), тестовый пакет удалён, фоновая активность возвращена в «выкл.». Записи в SMC не было.

## 5. Сценарии

### Scenario: Исходное состояние при запуске не разрешает управление
* **Given:** первый снимок содержит `Ftst=0`, режимы `[3,3]` и нулевые цели.
* **When:** daemon создаёт startup audit до запуска listener.
* **Then:** ответ содержит `system_at_start`, один монотонный снимок и `control_allowed=false`; повторный запрос не читает SMC снова.
* **Automated:** `prototype/helper-ipc/StartupAuditControllerTest.m`.

### Scenario: Изменённое состояние или отказ чтения обнаружены при запуске
* **Given:** первый снимок изменён либо reader возвращает ошибку.
* **When:** daemon создаёт startup audit.
* **Then:** ответ содержит `changed_at_start` с фактическим снимком либо `read_failed` без него; клиент не принимает ложное исходное состояние.
* **Automated:** `prototype/helper-ipc/StartupAuditControllerTest.m`.

### Scenario: Перезапущенный daemon получает новый audit
* **Given:** подписанный UID 0 daemon вернул startup audit и собственный PID.
* **When:** администратор завершает только тестовую read-only службу через `SIGKILL`.
* **Then:** новый PID возвращает более поздний startup audit со свежим фиксированным снимком.
* **Manual:** `prototype/helper-ipc/integrated-probe.sh startup-audit-crash-run`; на `Mac15,7`/macOS 27.0 проверены PID `49002 → 49648` и более поздняя отметка нового audit.

## 6. Ограничения

Audit содержит один последовательный снимок; отложенное изменение может возникнуть после него. Минутный watcher остаётся отдельным наблюдением. Даже `system_at_start` не доказывает восстановление после записи, поэтому интерфейс управления остаётся заблокированным.
