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

Снимок `FNum`, `F{n}Ac`, `F{n}Mn`, `F{n}Mx` и `TCMz` получают из локального AppleSMC без `sudo`. C-утилита выводит JSON, Kotlin/JVM запускает её и строит типизированный снимок. Это процессы прототипа на `Mac15,7`, не установленный системный сервис и не API управления вентиляторами. Запись в SMC не реализована.

## 2. API contracts

HTTP API нет. Локальный контракт — JSON схемы `1` из `smc-read --status-json`: `fan_count` может быть `null`, `fans` содержит индексы и nullable RPM/границы, `cpu_key` равен `TCMz`, а `cpu_temp_c` может быть `null`. Его формирует `prototype/smc-read/smc-read.c` и разбирает `prototype/kotlin-read/src/main/kotlin/ventilator/prototype/Main.kt`. Дополнительного протокола IPC пока нет.

## 2a. Code anchors

| Файл | Назначение |
|---|---|
| `prototype/smc-read/smc-read.c` | IOKit чтение и JSON схемы 1 |
| `prototype/kotlin-read/src/main/kotlin/ventilator/prototype/Main.kt` | запуск дочернего процесса с таймаутом и разбор JSON |
| `prototype/kotlin-read/src/main/kotlin/ventilator/prototype/Readings.kt` | состояние чтения и расчёт шкалы |
| `prototype/kotlin-read/src/test/kotlin/ventilator/prototype/ReadingsTest.kt` | граничные значения и отсутствие датчика |

## 3. How it is built

`smc-read` читает локальное SMC, проверяет тип и значение, выводит `null` при недоступности ключа. Kotlin запускает утилиту по переданному пути, ждёт до 5 секунд, проверяет код выхода и схему, затем фиксирует время обработки результата. При ошибке процесс завершается исключением, а не выдаёт `0 RPM`. Путь к утилите и размер вывода пока не защищены как в упакованном приложении.

## 4. Dependencies

| Вид | Имя | Назначение |
|---|---|---|
| Локальный драйвер | AppleSMC через IOKit | чтение ключей на macOS |
| Процесс | Kotlin/JVM 2.4.20 | парсинг JSON и модель |

## 5. Infrastructure and deploy

Ничего не устанавливается в систему. `smc-read` и JVM-процесс собираются и запускаются вручную; упаковка `.app`, подпись, нотарификация и helper запланированы отдельно.

## 6. Local setup

```bash
make -C prototype/smc-read build
cd prototype/kotlin-read
gradle --offline run --args=../smc-read/smc-read
gradle --offline test
```

## 7. Configuration

Единственный аргумент Kotlin-прототипа — путь к исполняемому `smc-read`; сетевых адресов, учётных данных и фонового сервиса нет.

## 8. Quirks

В песочнице Codex `IOServiceOpen` может отказать в доступе, тогда как обычный пользовательский запуск на этой машине работает без `sudo`. Время снимка назначается после завершения процесса; оно не показывает точный момент измерения в SMC.
