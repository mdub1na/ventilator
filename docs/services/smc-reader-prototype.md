---
id: smc-reader-prototype
title: Локальный прототип чтения AppleSMC
type: service
repo_url: https://github.com/mdub1na/ventilator
module: prototype/kotlin-read
tech_stack: [Kotlin/JVM, C, IOKit, macOS]
owner: unassigned
depends_on: [AppleSMC]
publishes: [local JSON snapshot]
---

# Локальный прототип чтения AppleSMC

## 1. Responsibility

Снимок `FNum`, `F{n}Ac`, `F{n}Mn`, `F{n}Mx`, `TCMz` и трёх выбранных температур получают из локального AppleSMC без `sudo`. C-утилита выводит JSON, Kotlin/JVM запускает её и строит типизированный снимок. Отдельная команда перечисляет все температурные ключи для диагностики. Это процессы прототипа на `Mac15,7`, не установленный системный сервис и не API управления вентиляторами. Запись в SMC не реализована.

## 2. API contracts

HTTP API нет. Локальный контракт — JSON схемы `1` из `smc-read --status-json`: `fan_count` может быть `null`, `fans` содержит индексы и nullable RPM/границы, `cpu_key` равен `TCMz`, `cpu_temp_c` может быть `null`, `selected_temperatures` содержит пары `key`/nullable `celsius` для `TAOL`, `TB0T`, `TCMb`. Команда `--temperatures-json` выводит `{ "schema": 1, "temperatures": [{ "key": "...", "celsius": ... }] }`; `temperatures: null` означает невозможность перечисления ключей. Некорректное/отсутствующее значение отдельного ключа становится `celsius: null`. Формирует JSON `prototype/smc-read/smc-read.c`, разбирает `prototype/kotlin-read/src/main/kotlin/ventilator/prototype/Main.kt`. Дополнительного протокола IPC пока нет.

## 2a. Code anchors

| Файл | Назначение |
|---|---|
| `prototype/smc-read/smc-read.c` | IOKit чтение и JSON схемы 1 |
| `prototype/kotlin-read/src/main/kotlin/ventilator/prototype/Main.kt` | запуск дочернего процесса с таймаутом и разбор JSON |
| `prototype/kotlin-read/src/main/kotlin/ventilator/prototype/Readings.kt` | состояние чтения и расчёт шкалы |
| `prototype/kotlin-read/src/test/kotlin/ventilator/prototype/ReadingsTest.kt` | граничные значения и отсутствие датчика |
| `prototype/desktop-app/src/main/kotlin/ventilator/desktop/monitoring/data/SmcMonitorRepository.kt` | обновление потоков для окна |
| `prototype/desktop-app/build.gradle.kts` | упаковка reader и AppKit значка в локальный `.app` |
| `prototype/desktop-app/src/main/kotlin/ventilator/desktop/menubar/MenuBarBridge.kt` | дочерний процесс значка и команды меню |

## 3. How it is built

`smc-read` читает локальное SMC, проверяет тип и значение, выводит `null` при недоступности ключа. Kotlin запускает утилиту по переданному или включённому в `.app` пути, ждёт до 5 секунд для короткого снимка и до 20 секунд для полной диагностики, проверяет код выхода и схему, затем фиксирует время обработки результата. При ошибке процесс завершается исключением, а не выдаёт `0 RPM`. Подписанное распространение пакета и защита от подмены reader ещё не проверены.

## 4. Dependencies

| Вид | Имя | Назначение |
|---|---|---|
| Локальный драйвер | AppleSMC через IOKit | чтение ключей на macOS |
| Процесс | Kotlin/JVM 2.4.20 | парсинг JSON и модель |

## 5. Infrastructure and deploy

Ничего не устанавливается в систему. Reader и JVM-процесс могут работать вручную; Compose Gradle сборка также создаёт локальный `.app` с вложенными исполняемыми `smc-read` и `status-item-bridge` плюс Java runtime. Kotlin/JVM один опрашивает короткий снимок и передаёт его AppKit значку по локальному stdin/stdout; полная диагностика запрашивается отдельно. На `Mac15,7` запуск, чтение и наличие обоих процессов проверены. Подпись, нотарификация, устойчивое распространение и привилегированный helper остаются отдельными этапами.

## 6. Local setup

```bash
make -C prototype/smc-read build
cd prototype/kotlin-read
gradle --offline run --args=../smc-read/smc-read
gradle --offline test
cd ../desktop-app
gradle :test
gradle createDistributable -Pcompose.desktop.packaging.checkJdkVendor=false
```

## 7. Configuration

Аргумент CLI Kotlin-прототипа — путь к исполняемому `smc-read`; Compose окно принимает явный путь при разработке или берёт вложенный reader из пакета. Сетевых адресов, учётных данных и фонового сервиса нет.

## 8. Quirks

В песочнице Codex `IOServiceOpen` может отказать в доступе, тогда как обычный пользовательский запуск на этой машине работает без `sudo`. Время снимка назначается после завершения процесса; оно не показывает точный момент измерения в SMC.
