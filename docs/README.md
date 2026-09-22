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

### Research (1)

- [x] [research-architecture](research/research-architecture.md) — возможность Kotlin/KMP приложения, аппаратные ограничения и архитектурные риски.

### Features (3)

- [x] [monitoring-readings](features/monitoring-readings.md) — проверенная модель снимка вентиляторов и температуры для локального прототипа.
- [x] [login-at-startup](features/login-at-startup.md) — системная регистрация автозапуска и переходы к настройке.
- [ ] [manual-fan-control-trial](features/manual-fan-control-trial.md) — принятый протокол первой ограниченной записи; инструмент и аппаратная проба ещё не выполнены.

### Screens / Flows (2)

- [x] [monitor-screen](screens/monitor-screen.md) — окно мониторинга `Mac15,7` и его состояния.
- [x] [settings-screen](screens/settings-screen.md) — отдельный экран автозапуска с двумя входами.

### Services (2)

- [x] [smc-reader-prototype](services/smc-reader-prototype.md) — локальное чтение AppleSMC через C-утилиту и Kotlin/JVM процесс.
- [x] [login-item-bridge](services/login-item-bridge.md) — регистрация автозапуска и чтение его состояния через JNI и ServiceManagement.
