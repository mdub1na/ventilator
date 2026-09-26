---
id: research-helper-boundary
title: Ventilator — граница привилегированного helper для M2-02
type: research
status: active
date: 2026-09-26
---

# Граница привилегированного helper для M2-02

Этот документ отделяет проверенные свойства платформы и нашей сборки от проекта следующего этапа. **Отдельный подписанный read-only LaunchDaemon проверен и удалён; новый этап проверяет его во временной подписанной копии настоящего `Ventilator.app`. Запись в SMC остаётся заблокированной.** [Первая проба `Ftst`](../features/ftst-check-trial.md#41-первая-запись-ftst-2026-09-25) показала изменение уже после немедленного readback; независимое программное восстановление не подтвердилось. Даже работающий daemon сам по себе не устраняет этот аппаратный пробел.

## 1. Проверенные основания

| Наблюдение или контракт | Источник | Практическое следствие |
|---|---|---|
| `SMAppService` регистрирует LaunchDaemon из основного `.app`; plist располагается в `Contents/Library/LaunchDaemons`. После `register()` daemon запускается только после одобрения администратора. | [Apple: SMAppService](https://developer.apple.com/documentation/servicemanagement/smappservice), [daemon(plistName:)](https://developer.apple.com/documentation/servicemanagement/smappservice/daemon%28plistname%3A%29), [register()](https://developer.apple.com/documentation/servicemanagement/smappservice/register%28%29) | Регистрацию, разрешение и фактическую доступность XPC проверять отдельно; существующий автозапуск основного приложения через `mainAppService` не регистрирует daemon. |
| При `unregister()` macOS останавливает запущенный helper. | [Apple: unregister()](https://developer.apple.com/documentation/servicemanagement/smappservice/unregister%28%29) | После будущего включения управления нельзя удалять или обновлять helper, пока не подтверждён возврат исходного состояния. |
| `NSXPCListener` может требовать подпись соединяющегося процесса; `NSXPCConnection` может требовать подпись другой стороны. | [Apple: listener requirement](https://developer.apple.com/documentation/foundation/nsxpclistener/setconnectioncodesigningrequirement%28_%3A%29), [NSXPCConnection](https://developer.apple.com/documentation/foundation/nsxpcconnection), [TN3127](https://developer.apple.com/documentation/technotes/tn3127-inside-code-signing-requirements) | Будущий daemon должен проверять клиента по подписанной идентичности, клиент — daemon. PID, имя процесса и знание Mach service name не являются достаточной проверкой. Конкретные требования к подписи нужно испытать на собранном пакете. |
| `IORegisterForSystemPower` даёт уведомления о сне/пробуждении, но не о shutdown/restart. | [Apple: IOPMLib](https://developer.apple.com/documentation/iokit/iopmlib_h), [IODeregisterForSystemPower](https://developer.apple.com/documentation/iokit/1557132-ioderegisterforsystempower) | Обработчик сна полезен, но не закрывает потерю питания, `SIGKILL` или выключение. Проверка состояния при следующем запуске обязательна. |
| Локальный `Ventilator.app` сейчас имеет `Signature=adhoc` и `TeamIdentifier=not set`; `LoginItemBridge.m` использует JNI и `SMAppService.mainAppService`. | `codesign -dv --verbose=4` на локальном пакете, собранном из `prototype/desktop-app/build.gradle.kts`; `prototype/login-item/LoginItemBridge.m`, проверка 2026-09-26 | Эту сборку нельзя выдавать за проверку будущего правила допуска по Team ID. Нужна отдельная стадия подписанного пакета и отрицательный тест чужого клиента. |
| Минутный наблюдатель на `Mac15,7`/macOS 27.0 читает исходное состояние; симуляция ловит отложенное изменение на третьем снимке. | [Протокол](../features/baseline-observer.md), `prototype/smc-write-trial/trial_actions_test.c` | Есть основа для обнаружения изменения. Она не умеет восстанавливать режим и не защищает от сбоя самого наблюдателя. |
| На `Mac15,7`/macOS 27.0 отдельные пользовательские процессы обменялись фиксированным статусом через именованный XPC сервис. Временная регистрация `launchctl bootstrap` и удаление `bootout` прошли; `launchctl print` после удаления не нашёл службу. | [Протокол](../features/helper-status-ipc.md), `prototype/helper-ipc/helper-status.m`, `prototype/helper-ipc/smoke.sh`; проверка 2026-09-26 | Межпроцессный контракт доступен для дальнейшей работы, но нет прав root, регистрации через `SMAppService`, SMC доступа или моста к UI. |
| Две ad hoc подписанные копии с разными `cdhash` на `Mac15,7`/macOS 27.0 проверили требования подписи в обе стороны: доверенная пара обменялась статусом, другая копия клиента не дошла до delegate, а клиент отклонил другую копию сервиса с XPC ошибкой `4102`. Поиск identity внутри песочницы показал `0`, но вне неё найден действующий Apple Development сертификат; ещё один сертификат отозван. | [Протокол](../features/helper-status-ipc.md), `prototype/helper-ipc/helper-status.m`, `prototype/helper-ipc/smoke.sh`; проверки 2026-09-26 | Ad hoc `cdhash` можно повторить копированием бинарника. Первоначальный вывод об отсутствии подписи был ограничением среды проверки. |
| Временный ad hoc подписанный `HelperProbe.app` на `Mac15,7`/macOS 27.0 зарегистрировал read-only LaunchAgent через `SMAppService.agent`, получил `enabled` и XPC ответ; `unregister` вернул `notRegistered`, `launchctl print` не нашёл службу, после чего пакет удалён. | [Протокол](../features/helper-status-ipc.md), `prototype/helper-ipc/agent-registration.m`, `prototype/helper-ipc/package-smoke.sh`; проверка 2026-09-26 | Структура пакета и жизненный цикл **пользовательского тестового agent** подтверждены. Это не регистрация root LaunchDaemon и не интеграция с `Ventilator.app`. |
| Отдельный `HelperDaemonProbe.app` и XPC peer подписаны действующим Apple Development сертификатом; строгая проверка подписей и требований `anchor apple generic`, identifier и Team ID прошла. Тот же daemon бинарник в пользовательском LaunchAgent ответил доверенному клиенту, отклонил ad hoc клиента и клиента с тем же Team ID, но другим identifier; LaunchAgent удалён. `SMAppService.daemon.register` сначала вернул `Operation not permitted` со статусом `requiresApproval`. После одобрения macOS статус стал `enabled`; системный процесс работал с UID 0, два запроса доверенного клиента прошли, оба чужих клиента были отклонены. `unregister` вернул `notRegistered`, `launchctl print` показал отсутствие системной службы, временный пакет удалён. Повторная регистрация после одобрения прошла без запроса, и удаление проверено вновь. | `prototype/helper-ipc/daemon-probe.sh`, `prototype/helper-ipc/signed-ipc-smoke.sh`; `Mac15,7`/macOS 27.0, 2026-09-26 | Проверены подпись и жизненный цикл отдельного read-only root daemon. Клиентом пока выступает отдельный подписанный бинарник, не `Ventilator.app`; аппаратных операций нет. |
| Временная копия настоящего Compose `Ventilator.app` с JVM, JNI-мостом, LaunchDaemon plist и read-only daemon подписана Apple Development; `codesign --verify --strict --deep` и требования идентичности обеих сторон прошли. Диагностическая команда запущена из главного JVM-процесса, прочитала `notFound`; регистрация дала `requiresApproval`. XPC запрос до разрешения завершился ошибкой. | `prototype/helper-ipc/integrated-probe.sh`, `prototype/helper-ipc/HelperProbeBridge.m`, `prototype/desktop-app/src/main/kotlin/ventilator/desktop/helper/HelperProbeCommand.kt`; `Mac15,7`/macOS 27.0, 2026-09-26 | Подписанная упаковка и fail-closed путь до разрешения проверены. Успешный XPC из главного приложения, UID daemon и удаление этой регистрации ещё ожидают системного разрешения. Обычный запуск UI службу не регистрирует. |

## 2. Проект границы процессов — целевое поведение

```text
Compose/JVM Ventilator.app
    → узкий JNI мост (read-only статус встроен в временную копию)
    → XPC с проверкой подписей в обе стороны
    → LaunchDaemon внутри подписанного .app
    → фиксированный адаптер AppleSMC (только после отдельного допуска)
```

На первом этапе daemon предоставляет только фиксированный снимок состояния и собственную версию протокола. Он не принимает сырой SMC-ключ, байты, произвольный RPM, путь к исполняемому файлу или команду оболочки. Writer не линкуется в первую read-only сборку daemon. Установка и удаление службы должны быть явными операциями с проверкой статуса; текущий переключатель «Запускать при входе» управляет только основным `.app` и не должен молча включать privileged daemon.

Будущий контракт записи должен появиться отдельным изменением после проверки аппаратного восстановления. Запрашивать следует семантическую операцию с точной моделью, диапазонами и ограничением времени, а не общий метод `writeKey`. Число управляющих клиентов — не более одного; при разрыве связи, истечении срока владения или ошибке чтения helper переходит к восстановлению **только по проверенному протоколу**. Пока такого протокола нет, никакого метода управления в XPC не будет.

## 3. Порядок проверки

1. **Read-only пакет и IPC.** Отдельный подписанный LaunchDaemon прошёл `requiresApproval → enabled → notRegistered`, root XPC ответ и проверенное удаление. Временная подписанная копия настоящего `Ventilator.app` уже собрана и получила `requiresApproval`; после системного разрешения проверить XPC из основного процесса и `unregister`/отсутствие службы. Это ещё не M2-02 целиком.
2. **Проверка клиента.** В отдельном пакете обе стороны требуют Apple anchor, точный identifier и Team ID; ad hoc клиент и клиент с тем же Team ID, но другим identifier, отклонены. Во временной копии приложения требование daemon закрепляет `ventilator.desktop`; после разрешения проверить положительный запрос от приложения и оба отрицательных запроса. Developer сертификат подходит для локальной разработки; распространение и нотарификация здесь не проверены.
3. **Жизненный цикл без записи.** Прервать UI, daemon и соединение; проверить повторный запуск, исчезновение владельца, события сна/пробуждения и чтение исходного состояния после них. Не предполагать, что launchd или обработчик сигнала гарантирует мгновенный возврат SMC.
4. **Протокол восстановления.** Отдельно выяснить принятые SMC-команды и задержки на `Mac15,7`/macOS 27.0, испытать восстановление из реально изменённого состояния и независимое чтение после процесса. Существующая `restore-unlock --apply` завершилась `CRITICAL`; до нового проверенного решения дальнейшая запись запрещена.
5. **Только после §4 — управление.** Проверить выход клиента, аварийный выход helper, сон/пробуждение, обновление/удаление пакета и повторный запуск. Каждый путь требует фактического чтения `Ftst=0`, режимов `[3,3]`, целей `[0,0]` после достаточного окна. Длительность достаточного окна пока неизвестна.

## 4. Открытые вопросы и ограничения

- Достаточно ли текущего правила подписи для постоянной установки `Ventilator.app` и распространения. Локальный Apple Development сертификат и временная копия основного пакета проверены; Developer ID, распространение и нотарификация ещё нет. Личный Team ID не хранить в репозитории.
- JNI мост на Objective-C собран и вызван из главного процесса, но успешный XPC из него ещё требует разрешения службы. Позднее решить, остаётся ли этот узкий мост или нужен Swift; выбор языка сам по себе не меняет границу доступа.
- Может ли восстановление вообще быть подтверждено программно на этой версии SMC. Daemon, повторный запуск и 60-секундное наблюдение не заменяют этого доказательства.
- На сон можно реагировать по уведомлению, но на внезапную потерю питания — нет. При следующем запуске сначала читать SMC и блокировать управление при неопределённом состоянии; даже такая проверка не даёт гарантии на время, пока Mac выключен.

## 5. Code anchors

| Текущая граница | Код |
|---|---|
| Compose/JVM и пакет `.app` | `prototype/desktop-app/build.gradle.kts`, `prototype/desktop-app/src/main/kotlin/ventilator/desktop/DesktopMain.kt` |
| Уже проверенный JNI-вызов ServiceManagement для автозапуска UI | `prototype/login-item/LoginItemBridge.m` |
| Отдельный исследовательский SMC transport, не включённый в `.app` | `prototype/smc-write-trial/smc-write-trial.c` |
| Read-only наблюдатель и его тесты | `prototype/smc-write-trial/trial_actions.c`, `prototype/smc-write-trial/trial_actions_test.c` |
| Пользовательская XPC проба с фиксированным статусом | `prototype/helper-ipc/HelperStatus.h`, `prototype/helper-ipc/helper-status.m`, `prototype/helper-ipc/smoke.sh` |
| Тестовая упаковка с `SMAppService.agent` | `prototype/helper-ipc/agent-registration.m`, `prototype/helper-ipc/package-smoke.sh` |
| Отдельный подписанный read-only LaunchDaemon | `prototype/helper-ipc/daemon-status.m`, `prototype/helper-ipc/daemon-registration.m`, `prototype/helper-ipc/daemon-probe.sh` |
| Пользовательская проба подписанного IPC | `prototype/helper-ipc/signed-ipc-smoke.sh` |
| Интеграционная подписанная копия и JNI-мост | `prototype/helper-ipc/integrated-probe.sh`, `prototype/helper-ipc/HelperProbeBridge.m`, `prototype/desktop-app/src/main/kotlin/ventilator/desktop/helper/HelperProbeCommand.kt` |

Привилегированный бинарник предоставляет только фиксированный статус. JNI-мост в подписанной копии основного приложения реализован; аппаратных операций в daemon нет, а проба не доказывает безопасность будущего интерфейса записи.
