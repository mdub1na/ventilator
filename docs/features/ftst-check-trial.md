---
id: ftst-check-trial
title: Короткая проверка Ftst и независимого возврата
type: feature
status: draft
owner: unassigned
involved_services: [smc-write-trial]
client_entries: []
api: []
tags: [macos, smc, fan-control, safety, hardware-trial]
---

# Короткая проверка Ftst и независимого возврата

## 1. Overview

Прямая запись `F0Md` на `Mac15,7`/macOS 27.0 получила ответ SMC `0x82`. Следующий отдельный опыт проверяет только короткий переход `Ftst: 0 → 1 → 0` и чтение результата. Он **не задаёт обороты** и не доказывает, что вентиляторы можно перевести в ручной режим. Команда `ftst-check` существует только в отдельном CLI `prototype/smc-write-trial/`, не в Ventilator.app.

Открытая [реализация macfan](https://github.com/raminsharifi/MacFanControl/blob/main/src/control.rs) использует `Ftst` после отказа прямого перехода и очищает его после освобождения вентиляторов. Это основание для гипотезы, а не контракт Apple и не свидетельство работоспособности на нашем Mac.

## 2. Business rules

1. Доступны только точные команды §3, модель `Mac15,7`, macOS 27.0, `FNum=2`, runtime тип `Ftst=ui8 /1`, режимы `[3, 3]`, `Ftst=0` и цели `[0, 0]` в каждом из пяти снимков preflight за 10 секунд. Температуры `TCMz`, `Tg0D`, `TH0a` доступны, ниже 75 °C, рост от первого до последнего не более 5 °C. Прямо перед записью состояние и температурный предел проверяются ещё одним чтением. Остальные проверки и границы RPM наследуются из [прямого протокола](manual-fan-control-trial.md).
2. До записи нужен отдельный ревью PR, известные commit и SHA-256 бинарника, подготовленная во втором Terminal команда `restore-unlock` и новое явное решение пользователя о пробе. После интерактивной фразы preflight повторяется полностью. При несовпадении запись запрещена.
3. `ftst-check` записывает только `Ftst=1`, читает режимы, `Ftst`, цели, фактические RPM и три температуры, затем немедленно возвращает `Ftst=0`. Успех требует подтверждённого `Ftst=1` при неизменных `[3, 3]`/`[0, 0]`, времени до контрольного чтения менее пяти секунд и подтверждённого возврата к `[3, 3]`, `Ftst=0`, целям `[0, 0]`.
4. Если режим или цель неожиданно изменились, восстановление сначала пытается снять ручной режим и обнулить цели обоих вентиляторов. `Ftst=0` записывается лишь после чтения режимов `0`/`3` и нулевых целей. Если это нельзя подтвердить, результат критический: не считать восстановление завершённым, применить независимую команду или перезагрузить Mac и проверить режимы чтением.
5. Если первая запись отклонена и чтение подтверждает исходный снимок, дополнительная запись `Ftst=0` не нужна. Ошибка команды IOKit/SMC не заменяет чтение после неё: запись могла изменить состояние несмотря на код отказа.
6. Другой контроллер вентиляторов не должен работать во время пробы. `restore-unlock` предназначен только для восстановления этой ограниченной пробы, поскольку он может очистить `Ftst=1`.

## 3. Local contract

```text
smc-write-trial ftst-check --dry-run
smc-write-trial restore-unlock --dry-run
sudo smc-write-trial ftst-check --apply --confirm FTST-CHECK-Mac15,7-27.0
sudo smc-write-trial restore-unlock --apply --confirm RESTORE-UNLOCK-Mac15,7-27.0
```

Обе команды `--dry-run` только читают SMC. `ftst-check --apply` требует TTY и точной фразы `APPLY FTST CHECK`; `restore-unlock --apply` можно запустить независимо из второго Terminal. Оба пути повторно проверяют модель, версию ОС, число вентиляторов и типы ключей. Никакой произвольный SMC-ключ или RPM CLI не принимает.

После подтверждённого возврата журнал содержит монотонное время, ключ, значение, успех каждой попытки и код IOKit/SMC, если команда дошла до транспорта. Контрольное чтение после `Ftst=1` содержит режимы, `Ftst`, цели, фактические RPM и три температуры. События держатся в памяти до завершения возврата, чтобы вывод в Terminal не задерживал `Ftst=0`; финальная строка содержит режимы, `Ftst` и цели. При критическом отказе приоритет имеет немедленное сообщение о независимом восстановлении, поэтому буфер событий может не появиться в выводе. Серийный номер, UUID и полный дамп ключей не сохраняются.

## 4. Verification state

Код и тесты подготовлены после первой прямой пробы. Аппаратная команда `ftst-check --apply` **не запускалась**, эффект записи `Ftst` и восстановление из его фактического значения `1` **не проверены**. `restore-unlock --dry-run` на `Mac15,7`/macOS 27.0 прочитал `[3, 3]`, `Ftst=0`, цели `[0, 0]`. Первый `ftst-check --dry-run` безопасно остановился на preflight: `TCMz` поднялся с 49,73 до 57,55 °C за 10 секунд, превысив допуск 5 °C. Повторный dry-run прошёл: `TCMz` 52,03 → 53,94 °C, `[3, 3]`, `Ftst=0`, цели `[0, 0]`; записи не было. Порог не менялся.

Обычный выход, обработанные сигналы и независимая команда не защищают от `SIGKILL`, потери питания и сбоя ядра. Проверка этих случаев относится к M2-02. После успешной короткой проверки следующей отдельной задачей станет ограниченная пара «ручной режим → повышенная цель RPM» при `Ftst=1`.

## 5. Code anchors

| Назначение | Код |
|---|---|
| Фиксированный transport, CLI и readback | `prototype/smc-write-trial/smc-write-trial.c` |
| Проверка исходного состояния и восстановление | `prototype/smc-write-trial/trial_actions.c`, `prototype/smc-write-trial/trial_actions.h` |
| Тесты отказов и порядка восстановления | `prototype/smc-write-trial/trial_actions_test.c` |
| Ограничения preflight | `prototype/smc-write-trial/trial_logic.c`, `prototype/smc-write-trial/trial_logic_test.c` |

## 6. Scenarios (BDD / test cases)

### Scenario: Ftst кратковременно меняется и возвращается
* **Given:** подтверждены модель, пять снимков preflight, исходные режимы и нулевые цели.
* **When:** `Ftst=1` принят и прочитан обратно.
* **Then:** режимы и цели не меняются; инструмент записывает `Ftst=0` и подтверждает исходное состояние чтением.
* **Automated:** `prototype/smc-write-trial/trial_actions_test.c#ftst_check_round_trip_does_not_write_fan_keys`

### Scenario: Первая запись Ftst отклонена
* **Given:** `Ftst=0`, режимы `[3, 3]`, цели `[0, 0]`.
* **When:** запись `Ftst=1` отвергнута без изменения состояния.
* **Then:** отказ зафиксирован, дополнительных записей нет.
* **Automated:** `prototype/smc-write-trial/trial_actions_test.c#ftst_rejection_leaves_baseline_without_cleanup_writes`, `prototype/smc-write-trial/trial_actions_test.c#ftst_error_with_changed_readback_still_clears_unlock`

### Scenario: Очистка Ftst не подтверждается
* **Given:** чтение подтвердило `Ftst=1` и неизменные режимы.
* **When:** попытки записать `Ftst=0` не принимаются.
* **Then:** результат критический; независимое восстановление остаётся доступным.
* **Automated:** `prototype/smc-write-trial/trial_actions_test.c#ftst_release_failure_requires_independent_recovery`, `prototype/smc-write-trial/trial_actions_test.c#independent_restore_clears_ftst_after_unchanged_modes`, `prototype/smc-write-trial/trial_actions_test.c#interruption_after_unlock_still_restores_ftst`

### Scenario: Режим неожиданно изменился
* **Given:** после `Ftst=1` один вентилятор оказался в ручном режиме.
* **When:** начинается восстановление.
* **Then:** оба вентилятора освобождаются прежде очистки `Ftst`; если освобождение не удалось, `Ftst` не очищается и выдаётся критическая ошибка.
* **Automated:** `prototype/smc-write-trial/trial_actions_test.c#ftst_unexpected_manual_mode_releases_fans_first`, `prototype/smc-write-trial/trial_actions_test.c#ftst_is_not_cleared_while_a_fan_remains_manual`

### Scenario: Исходное состояние изменилось до записи
* **Given:** повторный readback перед записью больше не подтверждает нулевые цели.
* **When:** инструмент готов перейти к `Ftst=1`.
* **Then:** он завершается до первой записи.
* **Automated:** `prototype/smc-write-trial/trial_actions_test.c#ftst_check_rejects_nonbaseline_before_first_write`, `prototype/smc-write-trial/trial_actions_test.c#ftst_check_rejects_hot_reading_before_first_write`
