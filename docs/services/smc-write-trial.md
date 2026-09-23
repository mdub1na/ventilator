---
id: smc-write-trial
title: Ограниченный инструмент первой записи SMC
type: service
repo_url: https://github.com/mdub1na/ventilator
module: prototype/smc-write-trial
tech_stack: [C11, IOKit, macOS]
owner: unassigned
depends_on: [AppleSMC]
publishes: [local trial evidence]
---

# Ограниченный инструмент первой записи SMC

## 1. Responsibility

Одноразовый CLI для этапа M2-01 проверяет точное сочетание `Mac15,7` + macOS 27.0, строит цель выше текущего охлаждения для двух вентиляторов, выполняет только прямую пару «ручной режим → цель» и всегда заканчивает попыткой вернуть исходный системный режим `3`. Отдельная команда `restore` доступна второму Terminal независимо от процесса пробы.

Инструмент не входит в Ventilator.app, не является привилегированным helper и не принимает произвольные ключи, типы, байты или RPM. Текущая версия не записывает `Ftst`/`FS! ` и не реализует unlock thermal manager. Аппаратная запись ещё не запускалась.

## 2. Local contract

HTTP API нет. CLI принимает только четыре точные формы:

```text
smc-write-trial trial --dry-run
smc-write-trial restore --dry-run
smc-write-trial trial --apply --confirm TRIAL-Mac15,7-27.0
smc-write-trial restore --apply --confirm RESTORE-Mac15,7-27.0
```

`--apply` требует root; `trial --apply` дополнительно требует интерактивный TTY и повторный ввод фразы после первого preflight. Ожидание ввода может быть сколь угодно долгим, поэтому затем инструмент повторяет полное 10-секундное наблюдение и рассчитывает цели из последнего снимка непосредственно перед записью. Неизвестный аргумент прекращает процесс до открытия пути записи.

## 2a. Code anchors

| Файл | Назначение |
|---|---|
| `prototype/smc-write-trial/smc-write-trial.c` | фиксированный AppleSMC transport, live preflight и CLI |
| `prototype/smc-write-trial/trial_logic.c` | чистая проверка allowlist, температурного тренда, целей и кодировки RPM |
| `prototype/smc-write-trial/trial_actions.c` | тестируемая state machine прямой пробы и восстановления |
| `prototype/smc-write-trial/trial_logic_test.c` | границы preflight и runtime типов |
| `prototype/smc-write-trial/trial_actions_test.c` | порядок записей и восстановление после частичного отказа |
| `prototype/smc-write-trial/Makefile` | локальная сборка и тесты |

## 3. How it is built

`make build` собирает отдельный macOS binary. В live transport команда записи AppleSMC `6` доступна только закрытым функциям для найденных `F0Md`/`F1Md` и `F0Tg`/`F1Tg`. Перед каждой записью повторно проверяются type/size. State machine имеет callback только для режима конкретного вентилятора и его числовой цели; операции для `Ftst`, `FS! ` или произвольного ключа в этом интерфейсе нет.

Прямой переход каждого вентилятора записывает цель не позднее 250 мс после принятой команды ручного режима. Через пять секунд фактические RPM должны вырасти и попасть в допуск. При любом отклонении state machine записывает Auto и нулевую цель обоим вентиляторам, затем до пяти секунд ждёт фактические режимы `[3, 3]` и `Ftst = 0`.

## 4. Dependencies

| Вид | Имя | Назначение |
|---|---|---|
| Локальный драйвер | AppleSMC через IOKit | точечное чтение и будущая ограниченная запись |
| Среда | `sysctl` | точная проверка `hw.model` и `kern.osproductversion` |

## 5. Local verification

На `Mac15,7`, macOS 27.0, 2026-09-23:

- `make clean build test` собрал transport и выполнил тесты pure logic/state machine;
- `restore --dry-run` прочитал режимы `[3, 3]`, `Ftst = 0`, два `flt `/4 target key и вывел фиксированный план;
- `trial --dry-run` сделал пять снимков за 10 секунд и вычислил цели 1650/1758 RPM из свежих `Ac = 0`, `Mn = 1350/1458`;
- ни одна команда `--apply`, selector `6` или запись SMC не запускалась.

## 6. Local setup

```bash
make -C prototype/smc-write-trial clean build test
prototype/smc-write-trial/smc-write-trial restore --dry-run
prototype/smc-write-trial/smc-write-trial trial --dry-run
```

Команды `--apply` не относятся к проверке сборки. Их порядок допуска описан в [протоколе M2-01](../features/manual-fan-control-trial.md).

## 7. Limits

Обработчики `SIGINT`/`SIGTERM`/`SIGQUIT` направляют контролируемое завершение в восстановление, но процесс не может защититься от `SIGKILL`, аварии ядра или потери питания. Сон, crash и внешний watchdog относятся к M2-02. Если прямой переход отклонён thermal manager, текущий инструмент восстанавливает систему и завершает опыт; он не пробует `Ftst` автоматически.
