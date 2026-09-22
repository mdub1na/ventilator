# Ограниченная проба записи SMC

Отдельный инструмент M2-01 для точного сочетания `Mac15,7` + macOS 27.0. Он не входит в Ventilator.app и не изменяет read-only прототип `prototype/smc-read/`.

## Безопасные команды разработки

```bash
make build
make test
./smc-write-trial trial --dry-run
./smc-write-trial restore --dry-run
```

Обе команды `--dry-run` только читают SMC. `trial` собирает пять снимков за 10 секунд и печатает рассчитанные цели. `restore` проверяет фиксированный план возврата для двух вентиляторов. Они не требуют `sudo`.

## Команды с записью

```bash
sudo ./smc-write-trial restore --apply --confirm RESTORE-Mac15,7-27.0
sudo ./smc-write-trial trial --apply --confirm TRIAL-Mac15,7-27.0
```

Не запускайте их как обычную проверку сборки. Первая аппаратная проба выполняется отдельно после ревью PR, со вторым Terminal, в котором заранее подготовлена команда `restore`. `trial --apply` дополнительно требует TTY и ввод фразы после показа свежего preflight.

Перед аппаратной пробой после слияния зафиксируйте точный исходник и бинарник в журнале:

```bash
git rev-parse HEAD
shasum -a 256 prototype/smc-write-trial/smc-write-trial
```

Текущая реализация проверяет только прямой переход `F0Md`/`F1Md` в ручной режим и **никогда не записывает `Ftst` или `FS! `**. Если thermal manager отклонит прямой переход, инструмент сразу выполняет восстановление и завершает опыт. Unlock через `Ftst` будет отдельным изменением только после сохранённого результата прямой пробы.

Полные ограничения, пороги и критерии результата описаны в [`docs/features/manual-fan-control-trial.md`](../../docs/features/manual-fan-control-trial.md).
