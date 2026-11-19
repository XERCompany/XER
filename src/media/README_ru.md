# Медиа-конвейер

[English](README_en.md) | [Русский](README_ru.md)

Каталог `src/media` содержит модульный стек воспроизведения и обработки
медиа проекта (`XER::Media::*`). Реализует конвейер из документа
архитектуры медиа: источники данных → демуксеры → декодеры → рендереры →
приёмники, с оркестрацией плеера, слоями сессии/сервиса, муксерами и
маршрутизацией.

## Структура

| Подмодуль | Назначение |
|---|---|
| `base/` | `SeekableBuffer`, лог медиа, утилиты времени |
| `demuxer/` | Интерфейс `DataSource`; `FileDataSource`; `UrlDataSource` (HTTP range-запросы через `src/content/network`); `BufferedDataSource` (байтовое окно опережающего чтения поверх любого источника); `MediaSource`/`ChunkDemuxer` (append/EOS в стиле MSE); мост демуксера FFmpeg |
| `decoders/` | Интерфейсы аудио/видео декодеров + адаптеры FFmpeg. Весь статический closure FFmpeg упакован в `YA.MediaDecoderUniversal.dll` (ggml/whisper/shaderc/vulkan — delay-load) |
| `renderers/` | `AudioRendererImpl`, `VideoRendererImpl`, синхронизация по аудио-часам, композитор кадров |
| `sinks/` | `WasapiOutputStream`, `FrameCacheSink`, pull-модель на потоке устройства |
| `muxers/` | Муксеры контейнеров (семейство EBML/MP4) |
| `player/` | `MediaPlayer`/`MediaPlayerImpl` — оркестрация конвейера |
| `session/` | Состояние медиа-сессии |
| `service/` | Внепроцессный медиа-сервис |
| `router/` | Маршрутизация медиа между источниками, рендерерами и приёмниками |
| `gpu/` | GPU-интеграция |
| `capture/` | Устройства захвата |
| `cdm/` | Заглушки модуля дешифрования контента |
| `filters/` | Граф фильтров |
| `metrics/` | Метрики конвейера |

## Источники данных

```cpp
// HTTP-источник с range-запросами и fallback на стриминг без размера.
XER::Media::UrlDataSource url;
url.Open("https://example.com/movie.mp4");

// Байтовое окно опережающего чтения поверх любого DataSource.
XER::Media::BufferedDataSource buffered(&url, forward_bytes, backward_bytes);
buffered.ReadAt(offset, dst, size);      // попадание в кэш → без I/O к источнику
```

- `UrlDataSource` определяет размер через `HEAD`, откатывается на
  range-`GET` и стримит, когда размер неизвестен.
- `BufferedDataSource` держит окно `[window_start_, +size)` с
  `backward_bytes` истории, дозаполняет вперёд до `forward_bytes` и
  откатывается на прямой `ReadAt` для доступа вне окна.

## Упаковка

Исходники рекурсивно собираются `src/media/CMakeLists.txt` и разбиваются
на общие YA-модули: `YA.MediaCore`, `YA.MediaPipeline`, `YA.MediaAudio`,
`YA.MediaRouter`, `YA.MediaMuxer`, `YA.MediaCapture`, `YA.MediaGpu`,
`YA.MediaDecoderUniversal`, `YA.MediaDecoder`, `YA.MediaDecoderZero`,
`YA.MediaEncoder`, `YA.DesginCSS` (CSS-движок UI).

## Тесты

`tests/media_smoke.cc` → `XER_Media_Smoke.exe`: конвейер
demux/decode/render, `BufferedDataSource` (чтение вперёд, назад по окну,
вне диапазона), матчинг/каскад/инвалидация CSS, парсер stylesheet. Запуск:

```bat
bin\XER_Media_Smoke.exe    :: работает без MSYS2-DLL в PATH
```
