---
id: monitor-screen
title: Окно мониторинга Mac15,7
type: client_screen
platform: [desktop]
status: active
entry:
  desktop: "ventilator.desktop.DesktopMainKt: main → MonitorScreen"
parent_feature: monitoring-readings
calls_api: []
source: prototype/desktop-app/src/main/kotlin/ventilator/desktop/monitoring/ui
---

# Окно мониторинга Mac15,7

## 0a. Code anchors

| Компонент | Файл |
|---|---|
| View model | `prototype/desktop-app/src/main/kotlin/ventilator/desktop/monitoring/ui/MonitorViewModel.kt` |
| UI state | `prototype/desktop-app/src/main/kotlin/ventilator/desktop/monitoring/ui/MonitorUiState.kt` |
| Screen / Content и превью состояний | `prototype/desktop-app/src/main/kotlin/ventilator/desktop/monitoring/ui/MonitorScreen.kt`, `prototype/desktop-app/src/main/kotlin/ventilator/desktop/monitoring/ui/MonitorPreviews.kt` |
| Создание репозитория и окна | `prototype/desktop-app/src/main/kotlin/ventilator/desktop/DesktopMain.kt` |
| Реальный источник снимков | `prototype/desktop-app/src/main/kotlin/ventilator/desktop/monitoring/data/SmcMonitorRepository.kt` |

## 0. Вход и видимость

- **Вход:** самостоятельное Compose Desktop окно `Ventilator · Мониторинг` при запуске локального `.app` или `gradle :run`.
- **Видимость:** открывается сразу. Закрытие завершает этот процесс и его опрос; связь с отдельным AppKit прототипом строки меню предстоит в M1-03.
- **Стиль:** тёмная цветовая схема, `MaterialExpressiveTheme`, карточки Material 3; пользовательского дизайн-артборда и эталонного сравнения пока нет.

## 1. Состояния экрана

- [x] **Loading (`LOADING`):** до первого снимка температура «—», вентиляторы ожидают показаний; кнопка обновления занята.
- [x] **Running (`RUNNING`):** фактические RPM хотя бы одного вентилятора положительны, индивидуальные уровни 1–5, `TCMz` и выбранные сырые температуры доступны по мере чтения.
- [x] **Stopped (`STOPPED`):** каждый обнаруженный вентилятор сообщает 0 RPM, шкалы имеют 0 заполненных делений; это не диагностика поломки.
- [x] **Unavailable (`UNAVAILABLE`):** неизвестно число вентиляторов, не хватает строк или хотя бы один RPM недоступен; там, где нет значения, выводится «—» и неизвестная шкала. Температура `TCMz` может быть отдельно недоступна и при работающих вентиляторах.
- [x] **No fans (`NO_FANS`):** `FNum = 0`; вместо карточек пояснение об отсутствии вентиляторов.
- [x] **Error (`ERROR`):** ошибка первого запуска или разбора reader; сообщение и ручное обновление. После ошибки последующего опроса последние данные остаются видимыми вместе с сообщением об ошибке.
- [x] **Diagnostics:** по раскрытию запрашивается полный список температур, далее поиск по сырому ID и обновление по кнопке. `temperatures: null` означает недоступное перечисление, ошибка процесса показывает отдельное сообщение.

## 2. Интеграция

HTTP API нет. `SmcMonitorRepository` вызывает локальный `smc-read --status-json` и по запросу `--temperatures-json` через Kotlin/JVM `readSnapshot`/`readDiagnostics`. Контракт и таймауты находятся в [документе сервиса](../services/smc-reader-prototype.md). `MonitorViewModel` объединяет снимки и UI-флаги через `combine` и `stateIn`.

## 3. Инициализация

**Входные параметры:** явный путь к `smc-read` первым аргументом, иначе ресурс `.app`, иначе `../smc-read/smc-read` из Gradle проекта. Карту датчиков предполагаем проверенной только на `Mac15,7`.

| Вызов | Условие | Результат |
|---|---|---|
| `refreshStatus` | сразу при создании view model и затем каждые 2 секунды | снимок CPU, вентиляторов, трёх выбранных ключей |
| `refreshDiagnostics` | только при первом раскрытии списка или по кнопке | все сырые температурные ключи |

| Результат | Обработка | Состояние |
|---|---|---|
| Валидный снимок | заменить `status`, очистить ошибку | **Running**, **Stopped**, **Unavailable** или **No fans** |
| Недоступный ключ | отдельное значение «—», без ложного нуля | **Unavailable** для RPM; температура отдельно |
| Ошибка процесса или JSON | показать текст ошибки и оставить последний успешный снимок | **Error** без снимка; сообщение над прежним состоянием при наличии снимка |

## 4. Элементы сверху вниз

### 4.1. Заголовок и обновление

- **Поле:** `MonitorUiState.refreshing`, `error`.
- **Вид:** название, пометка «только чтение», кнопка «Обновить»/«Обновляем…», текст ошибки при наличии.
- **Действие:** `MonitorUiAction.Refresh` повторно читает короткий снимок; параллельные чтения сериализуются.

### 4.2. CPU и состояние

- **Поле:** `cpuValue`, `displayState`, `updatedAt`.
- **Вид:** `TCMz` как максимум температуры кристалла CPU только на этой модели; при недоступности «— °C». Время — момент обработки снимка, а не аппаратная метка.
- **Действие:** нет.

### 4.3. Две карточки вентиляторов

- **Поле:** `fans`: сырой `F0Ac`/`F1Ac`, `rpm`, `level`, `state`, `range`.
- **Вид:** RPM, индивидуальная пятисегментная зелёно-красная шкала по собственным `Mn`/`Mx`, состояние и диапазон. При 0 RPM шкала пуста, при неизвестном диапазоне «—/5».
- **Действие:** нет; запись и регуляторы не реализованы.

### 4.4. Другие температуры

- **Поле:** `selectedTemperatures`.
- **Вид:** `TAOL`, `TB0T`, `TCMb` с °C либо «—»; только сырые ID, компонентам имена не присвоены.
- **Действие:** нет.

### 4.5. Диагностика датчиков

- **Поле:** `diagnosticsExpanded`, `diagnosticsRefreshing`, `diagnosticsError`, `diagnosticsUnavailable`, `diagnosticCount`, `query`, `diagnostics`.
- **Вид:** раскрываемая панель с поиском по SMC-ключу и списком raw ID/°C. Число ключей относится к полному снимку; список — к текущему фильтру. Диагностика снимается отдельно от короткого опроса, её значение может отставать.
- **Действия:** `DiagnosticsToggle`, `SearchChanged`, `DiagnosticsRefresh`. Ошибка списка не изменяет короткий снимок.

## 5. Навигация

Маршрутов и переходов пока нет. Закрытие окна завершает самостоятельное приложение. Открытие/скрытие из совмещённого значка будет добавлено в M1-03, автозапуск при входе — в M1-04.
