---
id: monitoring-readings
title: Снимок показаний вентиляторов и температуры
type: feature
status: active
owner: unassigned
involved_services: [smc-reader-prototype]
client_entries: [monitor-screen]
api: []
tags: [monitoring, macos, prototype]
---

# Снимок показаний вентиляторов и температуры

## 1. Overview

Локальный прототип на `Mac15,7` читает обороты вентиляторов и температуру CPU без изменения управления ими. [Compose окно](../screens/monitor-screen.md) показывает два вентилятора и температуры, отделяет остановленный вентилятор от недоступного датчика и даёт диагностический список сырых температурных ключей. Единый AppKit значок получает тот же снимок через локальный канал и показывает максимальный относительный уровень двух вентиляторов с температурой `TCMz`.

## 2. Business rules

- `FanSnapshot` хранит `F{index}Ac`, ключи собственного диапазона `F{index}Mn`/`F{index}Mx`, источник AppleSMC, RPM, время обработки снимка и доступность. Физическое расположение вентиляторов по номеру ключа не установлено, поэтому подпись остаётся по сырому ID.
- `TemperatureReading` хранит `TCMz`, AppleSMC, °C, время обработки, доступность и уверенность `OBSERVED_ON_MAC15_7`. Это рабочая подпись максимума температуры кристалла CPU для данной модели, не универсальный контракт Apple.
- `0 RPM` — доступное показание с нулём заполненных делений; оно само по себе не означает поломку. Отсутствующее чтение — `UNAVAILABLE` и неизвестный уровень. Положительные RPM требуют корректного диапазона для шкалы.
- Уровень 1–5 рассчитывается по фактическим RPM и диапазону именно этого вентилятора, ограничивается по краям и сводится в максимум. Неизвестный уровень хотя бы одного из обнаруженных вентиляторов даёт неизвестный общий уровень. Неизвестное количество вентиляторов и известное отсутствие вентиляторов различаются в модели; в обоих случаях шкала не вычисляется.
- Время `measuredAt` проставляется при разборе результата локальной утилиты; SMC не передаёт временную метку измерения. Ошибка запуска/разбора утилиты не превращается в нулевые показания.
- Некорректные числовые значения (`RPM < 0`, температура вне рабочего фильтра 10–115 °C) при разборе становятся недоступными, а не отображаются как измерение.
- Короткий снимок дополнительно содержит `TAOL`, `TB0T`, `TCMb` с сырыми именами и без названий компонентов. Полный список температур запрашивается отдельно при открытии диагностики, фильтруется по SMC-ключу и обновляется по требованию; `null` означает недоступное перечисление, пустой список — успешное перечисление без ключей.
- Kotlin опрашивает короткий снимок каждые 2 секунды независимо от видимости окна. Окно удерживает последнее достоверное значение при ошибке и показывает сообщение с возможностью ручной повторной попытки; значок при ошибке показывает неизвестное состояние, не старые RPM.
- `TrayReading` передаёт AppKit уровень, `TCMz` и оба фактических RPM; нуль и отсутствие значения различаются в локальном протоколе. AppKit не пересчитывает шкалу и не читает SMC. Пункт «Выход» завершает оба процесса, закрытие окна лишь скрывает его.

## 4. Code anchors

| Компонент | Код |
|---|---|
| Чтение ключей и JSON | `prototype/smc-read/smc-read.c` |
| Модель и расчёт уровня | `prototype/kotlin-read/src/main/kotlin/ventilator/prototype/Readings.kt` |
| Разбор JSON и запуск утилиты | `prototype/kotlin-read/src/main/kotlin/ventilator/prototype/Main.kt` |
| Проверка модели | `prototype/kotlin-read/src/test/kotlin/ventilator/prototype/ReadingsTest.kt` |
| AppKit значок и рисунок | `prototype/menu-bar/StatusItemBridge.swift`, `prototype/menu-bar/StatusArtwork.swift` |
| Локальный протокол и команды меню | `prototype/desktop-app/src/main/kotlin/ventilator/desktop/menubar/TrayReading.kt`, `prototype/desktop-app/src/main/kotlin/ventilator/desktop/menubar/MenuBarBridge.kt` |
| Репозиторий и преобразование в состояние окна | `prototype/desktop-app/src/main/kotlin/ventilator/desktop/monitoring/data/SmcMonitorRepository.kt`, `prototype/desktop-app/src/main/kotlin/ventilator/desktop/monitoring/ui/MonitorUiState.kt` |
| Опрос и действия окна | `prototype/desktop-app/src/main/kotlin/ventilator/desktop/monitoring/ui/MonitorViewModel.kt` |
| Композиция экрана | `prototype/desktop-app/src/main/kotlin/ventilator/desktop/monitoring/ui/MonitorScreen.kt` |
| Проверка состояний | `prototype/desktop-app/src/test/kotlin/ventilator/desktop/monitoring/MonitorUiTest.kt` |

## 5. Scenarios (BDD / test cases)

### Scenario: Оба вентилятора остановлены
* **Given:** `F0Ac = F1Ac = 0`, а один диапазон `Mn`/`Mx` недоступен.
* **When:** Kotlin разбирает снимок AppleSMC.
* **Then:** оба показания доступны и общий уровень равен 0.
* **Automated:** `prototype/kotlin-read/src/test/kotlin/ventilator/prototype/ReadingsTest.kt` — `zero RPM is available and has no filled segments`

### Scenario: Датчик недоступен
* **Given:** `F0Ac = 0`, `F1Ac = null`, `TCMz = null`.
* **When:** Kotlin разбирает снимок AppleSMC.
* **Then:** второй вентилятор и температура недоступны, общий уровень неизвестен.
* **Automated:** `prototype/kotlin-read/src/test/kotlin/ventilator/prototype/ReadingsTest.kt` — `missing fan or CPU temperature stays unavailable`

### Scenario: Разные относительные скорости
* **Given:** оба вентилятора имеют одинаковые фактические RPM и разные аппаратные диапазоны.
* **When:** рассчитывается общий уровень строки меню.
* **Then:** берётся максимум уровней, вычисленных независимо по каждому диапазону; граничные значения ограничены 1–5.
* **Automated:** `prototype/kotlin-read/src/test/kotlin/ventilator/prototype/ReadingsTest.kt` — `tray uses the higher level calculated with each fan range`, `positive RPM clamps to one through five and invalid range is unknown`

### Scenario: Некорректное значение датчика
* **Given:** JSON содержит отрицательные RPM и температуру `TCMz` вне допустимого диапазона.
* **When:** Kotlin разбирает снимок.
* **Then:** оба значения недоступны, общий уровень неизвестен.
* **Automated:** `prototype/kotlin-read/src/test/kotlin/ventilator/prototype/ReadingsTest.kt` — `invalid numbers in JSON become unavailable readings`

### Scenario: Окно при остановке и недоступности
* **Given:** оба RPM равны нулю либо второй датчик и CPU недоступны.
* **When:** окно преобразует очередной снимок в `MonitorUiState`.
* **Then:** оно показывает состояние остановки с нулём делений либо состояние недоступности с «—», сохраняя сырые ID ключей.
* **Automated:** `prototype/desktop-app/src/test/kotlin/ventilator/desktop/monitoring/MonitorUiTest.kt` — тесты состояний остановки и недоступности.

### Scenario: Диагностика и ошибка чтения
* **Given:** перечисление температур возвращает список либо запуск reader завершается ошибкой.
* **When:** пользователь открывает диагностику или обновляет показания.
* **Then:** список фильтруется по ключу без подмены названий; ошибка чтения не превращается в нулевые RPM и допускает повторное обновление.
* **Automated:** `prototype/desktop-app/src/test/kotlin/ventilator/desktop/monitoring/MonitorUiTest.kt` — фильтрация и повторная попытка.

### Scenario: Общий снимок для окна и строки меню
* **Given:** два вентилятора имеют разные диапазоны и уровни, один из них сообщает 0 RPM или недоступный датчик.
* **When:** Kotlin формирует `TrayReading` из снимка для окна.
* **Then:** в строку меню уходит максимум рассчитанных индивидуальных уровней, отдельные реальные RPM и `TCMz`; 0 остаётся нулём, отсутствие чтения — неизвестным.
* **Automated:** `prototype/desktop-app/src/test/kotlin/ventilator/desktop/menubar/TrayReadingTest.kt` — сериализация общего снимка и различие нуля/недоступности.

### Scenario: Окно скрыто
* **Given:** пользователь закрыл окно или выбрал «Скрыть окно» в значке.
* **When:** проходит очередной интервал опроса.
* **Then:** процесс и значок остаются, короткий снимок обновляется; обычный щелчок по значку возвращает окно и повторный щелчок скрывает его, контекстное меню открывается нажатием двумя пальцами, «Открыть окно» возвращает окно, «Выход» завершает приложение.
* **Automated:** `prototype/desktop-app/src/test/kotlin/ventilator/desktop/monitoring/MonitorUiTest.kt` — опрос без подписчика экрана. Пункты меню требуют проверки в работающем `.app`.

## 6. Out of scope

- Подписанный дистрибутив `.app`, автозапуск при входе и управление скоростью вентиляторов.
- Интерпретация физического расположения вентиляторов и температурных ключей для других моделей Mac.

## 7. Quirks

SMC может сообщать `0 RPM` при системном управлении. Положительные RPM без доступного диапазона позволяют показать число оборотов, но не позволяют достоверно заполнить шкалу. Диагностический список считывается в другой момент, чем периодический снимок, поэтому одинаковые ключи могут временно иметь разные значения. Старый изолированный AppKit прототип по-прежнему умеет читать JSON сам, но в составе `.app` запускается отдельный `status-item-bridge` без доступа к reader.
