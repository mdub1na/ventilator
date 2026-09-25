# Ограниченная проба записи SMC

Отдельный инструмент M2-01 для точного сочетания `Mac15,7` + macOS 27.0. Он не входит в Ventilator.app и не изменяет read-only прототип `prototype/smc-read/`.

## Безопасные команды разработки

```bash
make build
make test
./smc-write-trial trial --dry-run
./smc-write-trial restore --dry-run
./smc-write-trial ftst-check --dry-run
./smc-write-trial restore-unlock --dry-run
```

Все команды `--dry-run` только читают SMC. `trial` и `ftst-check` собирают пять снимков за 10 секунд; `trial` печатает рассчитанные цели. `restore` и `restore-unlock` проверяют фиксированный план возврата. Они не требуют `sudo`.

## Команды с записью

```bash
sudo ./smc-write-trial restore --apply --confirm RESTORE-Mac15,7-27.0
sudo ./smc-write-trial ftst-check --apply --confirm FTST-CHECK-Mac15,7-27.0
sudo ./smc-write-trial restore-unlock --apply --confirm RESTORE-UNLOCK-Mac15,7-27.0
sudo ./smc-write-trial trial --apply --confirm TRIAL-Mac15,7-27.0
```

Не запускайте их как обычную проверку сборки. Каждая аппаратная проба выполняется отдельно после ревью PR, со вторым Terminal, в котором заранее подготовлена соответствующая команда восстановления. `trial --apply` и `ftst-check --apply` дополнительно требуют TTY и разные фразы после первого preflight. После ввода инструмент повторяет полное 10-секундное наблюдение. Если повторная проверка не прошла, записи не будет.

Перед аппаратной пробой после слияния зафиксируйте точный исходник и бинарник в журнале:

```bash
git rev-parse HEAD
shasum -a 256 prototype/smc-write-trial/smc-write-trial
```

`trial` проверяет только прямой переход `F0Md`/`F1Md` в ручной режим и **никогда не записывает `Ftst`**. Прямая проба 2026-09-25 получила `smc=0x82` при записи `F0Md`; переход в ручной режим не был подтверждён, итоговые `[3, 3]` и нулевые цели совпали с исходными. Не повторяйте этот опыт в расчёте на другой результат без изменения протокола. Новая отдельная команда `ftst-check` проверяет только короткий `Ftst: 0 → 1 → 0`, не задавая RPM; она ещё не запускалась с `--apply`. `restore-unlock` является независимым восстановлением именно этой пробы. Ни одна команда не пишет `FS! `.

Полные ограничения, пороги и критерии результата описаны в [`docs/features/manual-fan-control-trial.md`](../../docs/features/manual-fan-control-trial.md).
Протокол короткой пробы и её ограничения — в [`docs/features/ftst-check-trial.md`](../../docs/features/ftst-check-trial.md).
