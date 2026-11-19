# Демо-приложение и смоук-тесты

[English](README_en.md) | [Русский](README_ru.md)

Этот каталог содержит примеры скриптов, точки входа для валидации
рантайма и смоук-тесты, проверяющие модули и интеграции XER.

## Цели

- проверить поведение ядра движка
- убедиться в регистрации и интеграции модулей
- прогнать привязки audio, I/O, buffer, terminal и console
- предоставить простые примеры для разработки и валидации в CI

## Частые команды

Запуск демо мультимедиа-моста:

```bash
./build/EngineBuilder run demo_app/bridge_multimedia.js
```

Запуск общего смоук-скрипта модулей:

```bash
./out/build/default/EngineBuilder run demo_app/modules_smoke.js
```

Запуск смоук-теста Git-модуля:

```bash
./out/build/default/EngineBuilder run demo_app/git_module_smoke.js
```

Запуск смоук-теста crypto-модуля:

```bash
./out/build/default/EngineBuilder run demo_app/crypto_module_smoke.js
```

Запуск смоук-теста async I/O:

```bash
./out/build/default/EngineBuilder run demo_app/io_async_module_smoke.js
```

Запуск смоук-теста буфера:

```bash
./out/build/default/EngineBuilder run demo_app/buffer_smoke.js
```

Запуск смоук-теста консоли:

```bash
./out/build/default/EngineBuilder run demo_app/console_smoke.js
```

Запуск смоук-теста снимка терминала:

```bash
./out/build/default/EngineBuilder run demo_app/terminal_snapshot_smoke.js
```

Запуск отдельного смоук-теста менеджера терминала:

```bash
./out/build/default/FluxTerminalManagerSmoke
```

Запуск отдельного смоук-теста ANSI-рендерера:

```bash
./out/build/default/FluxTerminalAnsiSmoke
```

## Покрытие смоук-тестами

- OpenCV
- CUDA / cuDNN
- Skia
- FFmpeg
- ANGLE
- API `Buffer`
- функциональность рантайма консоли
- поведение снимка и рендеринга терминала
- async I/O и интеграция реестра

## Примечания

Эти скрипты предназначены для валидации и экспериментов. Они помогают
подтвердить, что рантайм-окружение, нативные мосты и JavaScript-модули
работают как ожидается во время рефакторинга и повседневной разработки.
