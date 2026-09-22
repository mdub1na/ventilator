---
id: login-at-startup
title: Запуск Ventilator при входе в macOS
type: feature
status: draft
owner: unassigned
involved_services: [login-item-bridge]
client_entries: [settings-screen]
api: []
tags: [settings, macos, login]
---

# Запуск Ventilator при входе в macOS

## 1. Overview

Пользователь управляет автозапуском на отдельном [экране настроек](../screens/settings-screen.md). Его открывают шестерёнка в окне мониторинга и пункт «Настройки» в контекстном меню значка. Приложение читает реальную регистрацию macOS через [JNI мост](../services/login-item-bridge.md), не хранит локальную копию переключателя.

## 2. Business rules

- При первом запуске автозапуск выключен. `notFound` до первой регистрации допускает включение.
- `enabled` означает зарегистрированный и разрешённый объект входа. `requiresApproval` не показывается как включённое состояние: экран предлагает открыть системные настройки.
- При обычном запуске основное окно открывается. При запуске как объект входа окно должно оставаться скрытым, а значок строки меню — работать.
- Действия из обоих входов ведут на один экран. Контекстное меню показывает этот экран даже при скрытом окне; «Назад» возвращает мониторинг.

## 3. Code anchors

| Компонент | Код |
|---|---|
| Системная регистрация | `prototype/login-item/LoginItemBridge.m`, `prototype/desktop-app/src/main/kotlin/ventilator/desktop/login/data/NativeLoginItemRepository.kt` |
| Модель и действия | `prototype/desktop-app/src/main/kotlin/ventilator/desktop/login/domain/LoginItemRepository.kt`, `prototype/desktop-app/src/main/kotlin/ventilator/desktop/login/ui/LoginItemViewModel.kt` |
| Навигация | `prototype/desktop-app/src/main/kotlin/ventilator/desktop/navigation/DesktopNavigation.kt`, `prototype/desktop-app/src/main/kotlin/ventilator/desktop/DesktopMain.kt` |
| Значок и команда меню | `prototype/menu-bar/StatusItemBridge.swift`, `prototype/desktop-app/src/main/kotlin/ventilator/desktop/menubar/MenuBarBridge.kt` |
| Проверка команды меню | `prototype/desktop-app/src/test/kotlin/ventilator/desktop/menubar/MenuCommandTest.kt` |
| Проверка статусов | `prototype/desktop-app/src/test/kotlin/ventilator/desktop/login/LoginItemTest.kt` |

## 4. Scenarios (BDD / test cases)

### Scenario: Регистрация и разрешение
* **Given:** Ventilator открыт вручную, автозапуск выключен.
* **When:** пользователь включает настройку, отзывает и возвращает разрешение в macOS, затем выключает настройку.
* **Then:** статус последовательно показывает `enabled`, `requiresApproval`, `enabled`, `notRegistered`; при запросе разрешения есть действие для открытия системных настроек.
* **Automated:** `prototype/desktop-app/src/test/kotlin/ventilator/desktop/login/LoginItemTest.kt` — отображение статусов и восстановление после ошибки.
* **Manual:** на `Mac15,7` запись Ventilator появлялась и исчезала в «Открывать при входе», отзыв разрешения менял статус.

### Scenario: Следующий вход пользователя
* **Given:** Ventilator зарегистрирован и разрешён в macOS.
* **When:** пользователь перезагружает Mac и снова входит в учётную запись.
* **Then:** приложение запускается со значком в строке меню без автоматического показа окна.
* **Manual:** после включения настройки и появления записи в «Открывать при входе» пользователь подтвердил автоматический запуск на `Mac15,7`: до первого щелчка по значку было видно только значок, окно оставалось скрытым. Отсутствие запуска после выключения настройки ещё не проверено повторным входом.

### Scenario: Открытие настроек двумя путями
* **Given:** пользователь находится на экране мониторинга или скрыл окно.
* **When:** он нажимает шестерёнку или выбирает «Настройки» в контекстном меню значка.
* **Then:** открывается один экран настроек с актуальным состоянием автозапуска; «Назад» возвращает мониторинг.
* **Automated:** `prototype/desktop-app/src/test/kotlin/ventilator/desktop/menubar/MenuCommandTest.kt` — разбор команды `settings`.
* **Manual:** в собранном `.app` переход по шестерёнке и возврат проверены локально; пользователь подтвердил переход через пункт «Настройки» в контекстном меню значка на `Mac15,7`.

## 5. Out of scope

Подпись Developer ID, нотарификация и распространение относятся к M3. Запуск до входа пользователя в macOS не требуется.
