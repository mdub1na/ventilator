---
id: login-item-bridge
title: Локальный мост автозапуска macOS
type: service
repo_url: https://github.com/mdub1na/ventilator
module: prototype/desktop-app
tech_stack: [Kotlin/JVM, Objective-C, JNI, ServiceManagement, AppKit]
owner: unassigned
depends_on: [SMAppService]
publishes: [local login item status]
---

# Локальный мост автозапуска macOS

## 1. Responsibility

Вложенная `liblogin-item.dylib` вызывает `SMAppService.mainAppService` **внутри основного JVM-процесса Ventilator.app**. Она читает состояние службы, регистрирует и отменяет запуск после входа пользователя, открывает системные настройки при необходимости разрешения. При старте она наблюдает событие открытия приложения и различает обычный запуск и запуск как объект входа по `keyAELaunchedAsLogInItem`. Обычный запуск показывает окно, запуск при входе должен оставить только значок строки меню.

## 2. API contracts

HTTP API и отдельного привилегированного процесса нет. Внутренний JNI-контракт: целочисленные статусы `notRegistered`, `enabled`, `requiresApproval`, `notFound` преобразуются в `LoginItemStatus`; `register`/`unregister` возвращают текст ошибки только при отказе. `notFound` до первой регистрации может означать, что macOS ещё не знает эту службу, поэтому переключатель допускает включение. `requiresApproval` не выдаётся за включённый автозапуск: UI показывает отдельную кнопку перехода к настройкам. Недоступный нативный мост оставляет переключатель выключенным.

## 2a. Code anchors

| Файл | Назначение |
|---|---|
| `prototype/login-item/LoginItemBridge.m` | `SMAppService`, Apple Event, JNI |
| `prototype/login-item/Makefile` | сборка библиотеки для macOS |
| `prototype/desktop-app/src/main/kotlin/ventilator/desktop/login/data/NativeLoginItemRepository.kt` | загрузка библиотеки и отображение статуса в поток |
| `prototype/desktop-app/src/main/kotlin/ventilator/desktop/login/domain/LoginItemRepository.kt` | состояния и интерфейс |
| `prototype/desktop-app/src/main/kotlin/ventilator/desktop/login/ui/LoginItemViewModel.kt` | действия и обновление статуса |
| `prototype/desktop-app/src/main/kotlin/ventilator/desktop/login/ui/LoginItemSection.kt` | переключатель, разрешение, ошибка |
| `prototype/desktop-app/src/main/kotlin/ventilator/desktop/DesktopMain.kt` | выбор видимости окна при старте |
| `prototype/desktop-app/build.gradle.kts` | упаковка библиотеки в `.app` |

## 3. How it is built

`make -C prototype/login-item build` использует Objective-C и заголовки JDK 25. Gradle включает библиотеку в каталог ресурсов локального `.app`. При `gradle run` служба не загружается и настройка недоступна: этот путь не запускает установленный app bundle. Библиотека загружается перед инициализацией Compose/AppKit, чтобы успеть увидеть начальный Apple Event. Если признак запуска не пришёл за 3 секунды, приложение показывает окно, сохраняя доступность ручного запуска; поведение после реального входа в учётную запись требует отдельной проверки.

## 4. Local verification

На `Mac15,7`, macOS 27.0, локальный ad hoc подписанный `.app` открыл окно при ручном запуске. Включение переключателя перевело `SMAppService.status` в `enabled` и добавило Ventilator в системный список «Открывать при входе». Отключение фонового разрешения в настройках перевело состояние в `requiresApproval`; окно показало действие для открытия настроек. После возврата разрешения статус стал `enabled`. Выключение переключателя убрало Ventilator из списка объектов входа. Тестовая регистрация удалена. Запуск после фактического выхода и нового входа пользователя пока не проверен.

## 5. Limits

Служба запускается после входа пользователя, не до него. Подпись Developer ID, нотарификация и стабильная установка относятся к M3. Библиотека не получает привилегий и не записывает в SMC. [Apple: main app service](https://developer.apple.com/documentation/servicemanagement/smappservice/mainapp), [статусы](https://developer.apple.com/documentation/servicemanagement/smappservice/status-swift.enum), [признак запуска как объект входа](https://developer.apple.com/documentation/coreservices/keyaelaunchedasloginitem).
