# Документация Ventilator

Репозиторий находится на стадии исследования и локальных прототипов, без законченного приложения. Слой [исследования](research/research-architecture.md) описывает проверенные факты и решения, а [BACKLOG.md](../BACKLOG.md) — последовательность проверок. [Функция показаний](features/monitoring-readings.md) и [экран мониторинга](screens/monitor-screen.md) документируют реализованную Kotlin-модель и самостоятельное Compose Desktop окно. Документы о контрактах и других модулях появятся вместе с соответствующим кодом.

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

### Features (1)

- [x] [monitoring-readings](features/monitoring-readings.md) — проверенная модель снимка вентиляторов и температуры для локального прототипа.

### Screens / Flows (1)

- [x] [monitor-screen](screens/monitor-screen.md) — окно мониторинга `Mac15,7` и его состояния.

### Services (1)

- [x] [smc-reader-prototype](services/smc-reader-prototype.md) — локальное чтение AppleSMC через C-утилиту и Kotlin/JVM процесс.
