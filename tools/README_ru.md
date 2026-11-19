# Утилиты

[English](README_en.md) | [Русский](README_ru.md)

Отдельные консольные утилиты и небольшие приложения, поставляемые с XER.

## Утилиты

| Каталог | Назначение |
|---|---|
| `EngineScanVST3/` | Сканер VST3-плагинов. Перечисляет `.vst3`-модули без загрузки, затем пробует каждый модуль в изолированном процессе `EngineScanVST3 --probe <module>`. На основе async/future, конкурентность ограничена числом ядер CPU. Выдаёт JSON-записи успеха/ошибок |
| `XERVirtualCable/` | CLI virtual audio cable: именованные межпроцессные PCM-каналы, мосты WASAPI, маршрутизация, встроенные эффекты и VST3-стадии на канал |
| `audioconvert/` | Конвертация аудиоформатов |
| `audioapplyeffect/` | Применение аудиоэффекта к файлу |
| `audioplayer/` | Консольное воспроизведение аудио |
| `audioprobe/` | Анализ аудиофайлов |
| `video_editor/` | Утилита видеомонтажа |
| `preview/` | Утилита предпросмотра |
| `XERUIRender/` | Стенд рендеринга UI |
| `XERVacUI/` | UI virtual audio cable |
| `ServerAudio/` | Инструмент аудио-сервера |
| `ArchiveManager/` | Управление архивами |
| `ControllAccountsWindows/` | Управление учётными записями Windows |
| `HubCivitAIBrowser/` | Хаб-браузер CivitAI |
| `SdGenerate/` | Инструмент генерации Stable Diffusion |
| `SecurityFlashHello/` | Инструмент security flash (Hello) |
| `SecurityFlashService/` | Сервис security flash |
| `tz/` | Утилита часовых поясов |

## Использование EngineScanVST3

```bat
bin\EngineScanVST3.exe [root ...] [--out file.json] [--pretty]
                       [--jobs N] [--timeout MS] [--in-process]
bin\EngineScanVST3.exe --probe <module.vst3>
```

`--jobs N` ограничивает число конкурентных probe-процессов (по умолчанию —
число ядер CPU). Каждый probe грузит ровно один модуль, поэтому падающий
плагин не может уронить всё сканирование.
