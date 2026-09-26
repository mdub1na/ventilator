# Документация Ventilator

Репозиторий находится на стадии локального прототипа macOS-приложения. Слой [исследования](research/research-architecture.md) описывает проверенные факты и решения, а [BACKLOG.md](../BACKLOG.md) — последовательность проверок. [Функция показаний](features/monitoring-readings.md), [экран мониторинга](screens/monitor-screen.md) и документы сервисов описывают работающую сборку Ventilator.app и нативные мосты macOS. Документы о контрактах и других модулях появятся вместе с соответствующим кодом.

## Правила

- Язык документов — русский; имена API и типов сохраняются как в источниках.
- Каждый проверенный технический факт имеет адрес источника; гипотеза имеет этап проверки.
- `status: active` означает текущий проверенный документ, `status: draft` — намерение в рабочей ветке. На `main` нельзя выдавать намерение за уже работающее приложение.
- Форматы будущих слоёв находятся в [`templates/`](templates/); проверки структуры — в [`scripts/`](../scripts/).

## Проверка

```bash
python3 -m pip install -r requirements-docs.txt
make check
make report
```

## Coverage map

### Research (2)

- [x] [research-architecture](research/research-architecture.md) — возможность Kotlin/KMP приложения, аппаратные ограничения и архитектурные риски.
- [x] [research-helper-boundary](research/research-helper-boundary.md) — проверенные свойства macOS и граница read-only helper для M2-02; аппаратное восстановление ещё не реализовано.

### Features (7)

- [x] [monitoring-readings](features/monitoring-readings.md) — проверенная модель снимка вентиляторов и температуры для локального прототипа.
- [x] [login-at-startup](features/login-at-startup.md) — системная регистрация автозапуска и переходы к настройке.
- [ ] [manual-fan-control-trial](features/manual-fan-control-trial.md) — прямая проба записи отклонена SMC; ручные обороты и возврат из ручного режима не подтверждены.
- [ ] [ftst-check-trial](features/ftst-check-trial.md) — запись `Ftst` выявила отложенное изменение; восстановление не подтвердилось, новые пробы заблокированы.
- [x] [baseline-observer](features/baseline-observer.md) — независимое чтение исходного состояния в течение минуты после инцидента `Ftst`; проверено на `Mac15,7`.
- [x] [helper-status-ipc](features/helper-status-ipc.md) — пользовательский XPC обмен фиксированным read-only статусом и удаление временной службы.
- [x] [helper-baseline-watch](features/helper-baseline-watch.md) — минутное read-only наблюдение в UID 0 helper после выхода клиента, проверенное на `Mac15,7`.

### Screens / Flows (2)

- [x] [monitor-screen](screens/monitor-screen.md) — окно мониторинга `Mac15,7` и его состояния.
- [x] [settings-screen](screens/settings-screen.md) — отдельный экран автозапуска с двумя входами.

### Services (4)

- [x] [smc-reader-prototype](services/smc-reader-prototype.md) — локальное чтение AppleSMC через C-утилиту и Kotlin/JVM процесс.
- [x] [login-item-bridge](services/login-item-bridge.md) — регистрация автозапуска и чтение его состояния через JNI и ServiceManagement.
- [ ] [smc-write-trial](services/smc-write-trial.md) — отдельный инструмент M2-01: прямая запись отклонена, после пробы `Ftst` новые записи заблокированы.
- [x] [helper-ipc-prototype](services/helper-ipc-prototype.md) — отдельный пользовательский XPC прототип без SMC доступа и проверки подписи.
