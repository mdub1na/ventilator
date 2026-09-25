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

Одноразовый CLI для этапа M2-01 проверяет точное сочетание `Mac15,7` + macOS 27.0. Прямая команда строит цель выше текущего охлаждения для двух вентиляторов и выполняет пару «ручной режим → цель» с возвратом системного режима `3`. Отдельная короткая команда `ftst-check` проверяет только `Ftst: 0 → 1 → 0`; `restore` и `restore-unlock` доступны второму Terminal независимо от процесса пробы.

Инструмент не входит в Ventilator.app, не является привилегированным helper и не принимает произвольные ключи, типы, байты или RPM. Прямая команда не записывает `Ftst`; короткая команда не записывает положительные цели RPM. `FS! ` не записывается никогда. Прямая аппаратная проба 2026-09-25 дошла до `F0Md`, которую SMC отклонил `0x82`; переход в ручной режим не был подтверждён. Команда `ftst-check --apply` ещё не запускалась.

## 2. Local contract

HTTP API нет. CLI принимает только восемь точных форм:

```text
smc-write-trial trial --dry-run
smc-write-trial restore --dry-run
smc-write-trial ftst-check --dry-run
smc-write-trial restore-unlock --dry-run
smc-write-trial trial --apply --confirm TRIAL-Mac15,7-27.0
smc-write-trial restore --apply --confirm RESTORE-Mac15,7-27.0
smc-write-trial ftst-check --apply --confirm FTST-CHECK-Mac15,7-27.0
smc-write-trial restore-unlock --apply --confirm RESTORE-UNLOCK-Mac15,7-27.0
```

`--apply` требует root; `trial --apply` и `ftst-check --apply` дополнительно требуют интерактивный TTY и повторный ввод разных фраз после первого preflight. Ожидание ввода может быть сколь угодно долгим, поэтому затем инструмент повторяет полное 10-секундное наблюдение непосредственно перед записью. Все пять снимков должны содержать системные режимы, `Ftst=0` и нулевые цели. Неизвестный аргумент прекращает процесс до открытия пути записи.

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

`make build` собирает отдельный macOS binary. В live transport команда записи AppleSMC `6` доступна только закрытым функциям для найденных `F0Md`/`F1Md`, `F0Tg`/`F1Tg` и точного `Ftst`. Перед каждой записью повторно проверяются type/size. State machine допускает для `Ftst` только байты `0`/`1`; операция `ftst-check` не пишет цели при штатном ходе, а операция `trial` не пишет `Ftst`. Операции для `FS! ` или произвольного ключа нет.

Прямой переход каждого вентилятора записывает цель не позднее 250 мс после принятой команды ручного режима. Через пять секунд фактические RPM должны вырасти и попасть в допуск. При изменившемся или неизвестном состоянии state machine записывает Auto и нулевую цель обоим вентиляторам, затем до пяти секунд ждёт фактические режимы `[3, 3]`, `Ftst = 0` и нулевые цели.

После первой аппаратной пробы cleanup сначала читает оба режима и цели. Если SMC отклонил переход и исходные `[3, 3]`, `Ftst = 0`, цели `0` сохранились, команда завершает отказ без дополнительных записей. Если состояние менялось или чтение не удалось, выполняется прежняя процедура восстановления. Подтверждение безопасного состояния требует также нулевых целей, поэтому сообщение CLI больше не называет нетронутый режим «восстановленным».

Для `ftst-check` выполняется отдельная state machine из [протокола](../features/ftst-check-trial.md): после чтения `Ftst=1` команда без выдержки записывает `0`. Если режимы или цели изменились, сначала пытается освободить оба вентилятора и только затем очищает `Ftst`. Для независимого `restore-unlock` действует тот же порядок. Ошибки освобождения или финального чтения возвращают критический статус; другой контроллер не должен удерживать `Ftst` одновременно.

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
- на эту дату ни одна команда `--apply`, selector `6` или запись SMC не запускалась.

Первая `trial --apply` 2026-09-25, commit `82ea370` и SHA-256 `b24bec38e7ac8d1f830e23b4ed66ee47aab8e5eaa975e1b353ee358ba09d0aa8`, прошла preflight, но SMC вернул `0x82` на `F0Md`. Положительные цели RPM не записывались. Независимое чтение после завершения показало `[3, 3]`, `Ftst = 0`, цели `[0, 0]`; фактическое восстановление из ручного режима не испытано. Подробные значения и ограничения приведены в [протоколе](../features/manual-fan-control-trial.md#51-результат-первой-прямой-пробы-2026-09-25).

После добавления короткой пробы unit-тесты проходят, `restore-unlock --dry-run` на той же машине прочитал `[3, 3]`, `Ftst=0`, цели `[0, 0]`. Первый `ftst-check --dry-run` остановился на температурном тренде до записи; повторный прошёл. `ftst-check --apply` и фактический возврат из `Ftst=1` пока не проверены.

## 6. Local setup

```bash
make -C prototype/smc-write-trial clean build test
prototype/smc-write-trial/smc-write-trial restore --dry-run
prototype/smc-write-trial/smc-write-trial trial --dry-run
prototype/smc-write-trial/smc-write-trial restore-unlock --dry-run
prototype/smc-write-trial/smc-write-trial ftst-check --dry-run
```

Команды `--apply` не относятся к проверке сборки. Их порядок допуска описан в [прямом протоколе](../features/manual-fan-control-trial.md) и [протоколе `Ftst`](../features/ftst-check-trial.md).

## 7. Limits

Обработчики `SIGINT`/`SIGTERM`/`SIGQUIT` направляют контролируемое завершение в восстановление, но процесс не может защититься от `SIGKILL`, аварии ядра или потери питания. Сон, crash и внешний watchdog относятся к M2-02. Если прямой переход отклонён, инструмент проверяет сохранность системного состояния и завершает опыт; он не пробует `Ftst` автоматически. Отдельная проверка `Ftst` ещё не является проверкой ручного управления RPM. Причина `0x82` на этой машине не установлена.
