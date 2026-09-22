---
id: settings-screen
title: Экран настроек Ventilator
type: client_screen
platform: [desktop]
status: draft
entry:
  desktop: "ventilator.desktop.DesktopMainKt: main → SettingsScreen"
parent_feature: login-at-startup
calls_api: []
source: prototype/desktop-app/src/main/kotlin/ventilator/desktop/settings/ui
---

# Экран настроек Ventilator

## 0a. Code anchors

| Компонент | Файл |
|---|---|
| Навигация и окно | `prototype/desktop-app/src/main/kotlin/ventilator/desktop/navigation/DesktopNavigation.kt`, `prototype/desktop-app/src/main/kotlin/ventilator/desktop/DesktopMain.kt` |
| Экран и состояния превью | `prototype/desktop-app/src/main/kotlin/ventilator/desktop/settings/ui/SettingsScreen.kt`, `prototype/desktop-app/src/main/kotlin/ventilator/desktop/settings/ui/SettingsPreviews.kt` |
| Карточка автозапуска | `prototype/desktop-app/src/main/kotlin/ventilator/desktop/login/ui/LoginItemSection.kt` |
| Статус и действия | `prototype/desktop-app/src/main/kotlin/ventilator/desktop/login/ui/LoginItemUiState.kt`, `prototype/desktop-app/src/main/kotlin/ventilator/desktop/login/ui/LoginItemViewModel.kt` |

## 0. Вход

- **Из основного окна:** кнопка с шестерёнкой справа вверху.
- **Из строки меню:** контекстный щелчок по значку → «Настройки». Окно открывается на экране настроек, даже если было скрыто.
- **Возврат:** кнопка со стрелкой «Назад к мониторингу».
- **Стиль:** та же тёмная схема Material 3 Expressive, что у мониторинга.

## 1. Состояния

- [x] **Disabled / enabled:** статус берётся из `SMAppService`, переключатель меняет регистрацию в macOS.
- [x] **Requires approval:** переключатель выключен; кнопка открывает системные настройки объектов входа.
- [x] **Unavailable:** в запуске без упакованного нативного моста переключатель недоступен и причина видна.
- [x] **Error:** ошибка регистрации отображается под карточкой без ложного изменения статуса.

## 2. Состав экрана

Сверху находятся кнопка возврата и заголовок «Настройки». Ниже раздел «Запуск» с карточкой «Запускать при входе в macOS», статусом, переключателем и условным действием для разрешения. Сервис и системные состояния описаны в [документе функции](../features/login-at-startup.md).

## 3. Навигация и жизненный цикл

Переходы не создают новый процесс или окно. Мониторинг SMC и обновление значка продолжаются, пока открыт экран настроек. Скрытие окна не завершает приложение; повторный обычный щелчок по значку возвращает текущее окно. Пункт меню «Настройки» всегда переводит его на этот экран.
