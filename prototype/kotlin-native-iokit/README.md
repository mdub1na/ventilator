# Kotlin/Native → IOKit: проба соединения

Минимальная проверка `macosArm64`: Kotlin/Native обращается к системным bindings `platform.IOKit`, находит AppleSMC, открывает и сразу закрывает user client. Она **не читает SMC-ключи и не содержит записи**. Системный IOKit уже входит в набор platform libraries Kotlin/Native, поэтому отдельный `.def` для этих функций не нужен.

```bash
gradle linkDebugExecutableMacosArm64
./build/bin/macosArm64/debugExecutable/iokit-smoke.kexe
```

Нужны macOS на Apple Silicon, Xcode, JDK 25 и совместимый Gradle; первый запуск Gradle загрузит Kotlin/Native toolchain и потребует сеть. После этого сборку можно повторять с `--offline`. На `Mac15,7` с macOS 27.0 программа открыла и закрыла AppleSMC без `sudo` вне песочницы Codex. Из этого **не следует**, что Kotlin/Native уже умеет читать показатели или управлять вентиляторами в этом проекте: фактическое чтение реализовано только в [C-прототипе](../smc-read/README.md).

Проба нужна для [решения M0-03](../../docs/research/research-architecture.md), а не для основной сборки приложения. Compose Desktop работает на JVM, поэтому нативный код всё равно потребовал бы границы процесса или JNI.
