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

Отдельный процесс отвечает на запрос статуса фиксированным снимком: версия протокола `1`, состояние `read_only_prototype`, `smc_access=false`, `write_available=false`. Это проверка формы XPC-контракта и обмена между процессами. Первый пользовательский прототип и отдельный подписанный системный daemon не читают и не записывают AppleSMC и не входят в `Ventilator.app`. Системный daemon работает с UID 0, но не предоставляет привилегированных операций.

## 2. Правила

1. XPC-интерфейс содержит только `fetchStatusWithReply`; клиент не передаёт ключ SMC, RPM, путь, команду или иной параметр.
2. Клиент принимает ответ только с четырьмя ожидаемыми полями и версией `1`; ошибка или таймаут за 5 секунд означает недоступность сервиса.
3. Smoke-проба регистрирует отдельный пользовательский Mach service во временном plist и удаляет его через `launchctl bootout`. Она не регистрирует LaunchDaemon из приложения и не требует `sudo`.
4. Если удаление временной службы не подтверждено, скрипт завершает работу с ошибкой и сохраняет plist для проверки. Такой результат нельзя считать успешной пробой.
5. Во второй smoke-пробе обе стороны XPC требуют точный `cdhash` противоположного бинарника. Это проверяет механизм отклонения другого кода в текущем запуске, но не удостоверяет автора: исходный ad hoc бинарник можно скопировать. Сервер по-прежнему не запускается с правами root.
6. Упаковочная проба создаёт отдельный временный `.app` с read-only бинарником и plist пользовательского LaunchAgent. `SMAppService.agent` вызывается только из этого тестового пакета. Скрипт удаляет пакет лишь после `unregister`, статуса `notRegistered` и отсутствия службы в `launchctl`; основное `Ventilator.app` он не меняет.
7. Системная проба создаёт второй временный `.app` с отдельным read-only daemon. Обе стороны XPC требуют Apple anchor, точный code identifier и Team ID. До одобрения macOS статус `requiresApproval`; после запроса и отрицательного теста клиентом регистрацию снимают. Удалять пакет можно лишь после `notRegistered` и отсутствия службы в системном `launchctl`.

## 3. Проверка на устройстве

На `Mac15,7`, macOS 27.0, `make -C prototype/helper-ipc smoke` собрал Objective-C бинарник и выполнил XPC запрос от отдельного процесса. Ответ: `{"protocol_version":1,"smc_access":false,"state":"read_only_prototype","write_available":false}`. `launchctl print` после `bootout` не нашёл временную службу. Доступ к SMC и привилегии администратора не использовались. Внутри песочницы Codex `launchctl bootstrap` вернул ошибку 5; проверка прошла с разрешённым пользовательским запуском среды.

Повторная проверка подписала две временные копии ad hoc с разными `cdhash`. Доверенная пара дважды получила статус. Копия с другим хешем получила XPC ошибку `4097`; delegate доверенного сервиса не принял её соединение, а доверенная пара после отказа продолжила работать. Доверенный клиент отказался принимать другую копию сервиса с XPC ошибкой подписи `4102`. Обе временные службы были удалены с проверкой отсутствия в `launchctl print`. В песочнице `security find-identity -v -p codesigning` показал `0 valid identities found`; поздняя проверка вне песочницы обнаружила действующий Apple Development сертификат.

`make -C prototype/helper-ipc package-smoke` на том же Mac создал `HelperProbe.app` с `Contents/Resources/helper-status` и plist `com.ventilator.helper-ipc.package-test.plist` в `Contents/Library/LaunchAgents`. `plutil` и `codesign --verify --strict` прошли. До регистрации `SMAppService.status` был `notFound`; после `register` — `enabled`. Отдельный XPC клиент получил фиксированный статус. `unregister` вернул `notRegistered`, `launchctl print` не обнаружил службу, после чего временный пакет был удалён. Root, SMC и основной `Ventilator.app` не участвовали. Дополнительный запрос `sfltool dumpbtm` завис и был остановлен; отсутствие исторической записи в системном списке фоновых объектов этим тестом не подтверждено.

`make -C prototype/helper-ipc daemon-prepare` на `Mac15,7`/macOS 27.0 собрал `HelperDaemonProbe.app` с отдельным LaunchDaemon и подписал пакет, daemon, регистратор и два клиента действующим Apple Development сертификатом. `codesign --verify --strict` и локальная проверка требований к подписи прошли; ad hoc клиент и подписанный клиент с другим identifier требованию не удовлетворили. Во временном пользовательском домене тот же daemon ответил доверенному клиенту, отклонил оба чужих клиента, затем снова ответил доверенному. Временный LaunchAgent был удалён.

Первый вызов `SMAppService.daemon.register` вернул `Operation not permitted`, а последующее чтение показало `requiresApproval` и отсутствие системной службы. После разрешения в настройках macOS статус стал `enabled`; системный `launchctl` показал работающий процесс с UID 0. Подписанный клиент дважды получил точный статус, ad hoc клиент был отклонён. Повторная регистрация после одобрения прошла сразу: дополнительно отклонён подписанный клиент с тем же Team ID и другим identifier. `unregister` оба раза вернул `notRegistered`, `launchctl print system/...` подтвердил отсутствие службы, затем тестовые пакеты удалены. Записи в SMC не было. Это результат отдельного пакета, не интеграции с `Ventilator.app`.

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

### Scenario: Другая копия клиента не проходит требование сервиса
* **Given:** сервис требует `cdhash` доверенной ad hoc копии.
* **When:** подключается копия с другим `cdhash`.
* **Then:** запрос завершается ошибкой, delegate сервиса не принимает соединение, а доверенный клиент по-прежнему получает статус.
* **Manual:** `make -C prototype/helper-ipc smoke` на `Mac15,7`/macOS 27.0, §3.

### Scenario: Клиент отклоняет другую копию сервиса
* **Given:** клиент требует `cdhash` доверенной ad hoc копии.
* **When:** он подключается к сервису, запущенному из копии с другим `cdhash`.
* **Then:** запрос завершается XPC ошибкой подписи `4102` без принятого статуса.
* **Manual:** `make -C prototype/helper-ipc smoke` на `Mac15,7`/macOS 27.0, §3.

### Scenario: Временный пакет регистрирует и удаляет LaunchAgent
* **Given:** ad hoc подписанный тестовый `.app` содержит helper и plist с `BundleProgram`.
* **When:** пакет вызывает `SMAppService.agent.register`, клиент запрашивает статус, затем пакет вызывает `unregister`.
* **Then:** между вызовами статус `enabled` и XPC отвечает; в конце статус `notRegistered`, службы в `launchctl` нет, пакет удаляется.
* **Manual:** `make -C prototype/helper-ipc package-smoke` на `Mac15,7`/macOS 27.0, §3.

### Scenario: Подписанный read-only daemon регистрируется и удаляется
* **Given:** отдельный пакет и XPC peer подписаны действующим Apple Development сертификатом.
* **When:** пользователь разрешает системный фоновый объект, доверенный и ad hoc клиенты запрашивают статус, затем выполняется `unregister`.
* **Then:** статус проходит через `requiresApproval` и `enabled`; доверенный клиент получает ровно четыре поля, ad hoc клиент и подписанный клиент с другим identifier отклонены; в конце `notRegistered`, системной службы нет, временный пакет удалён.
* **Manual:** `daemon-probe.sh prepare/register/check/unregister/cleanup`, `Mac15,7`/macOS 27.0, §3.

## 5. Граница результата

Этап проверил упаковку отдельного пользовательского agent и отдельного подписанного root daemon, двусторонние требования к подписи и удаление обеих служб. Не проверены интеграция с настоящим `Ventilator.app`, JNI-мост, обновление пакета, чтение или запись SMC из daemon и аппаратное восстановление. Успешный XPC ответ не закрывает M2-02. [Исследование границы](../research/research-helper-boundary.md) описывает следующий порядок.

## 6. Code anchors

| Назначение | Код |
|---|---|
| XPC протокол | `prototype/helper-ipc/HelperStatus.h` |
| Listener и клиент | `prototype/helper-ipc/helper-status.m` |
| Временная регистрация и удаление | `prototype/helper-ipc/smoke.sh` |
| Сборка | `prototype/helper-ipc/Makefile` |
| Тестовый пакет и регистрация LaunchAgent | `prototype/helper-ipc/package-smoke.sh`, `prototype/helper-ipc/agent-registration.m` |
| Подписанный read-only daemon и его жизненный цикл | `prototype/helper-ipc/daemon-status.m`, `prototype/helper-ipc/daemon-registration.m`, `prototype/helper-ipc/daemon-probe.sh` |
| Проба подписанного IPC в пользовательском домене | `prototype/helper-ipc/signed-ipc-smoke.sh` |
