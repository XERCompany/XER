# Модуль XER UI

[English](README_en.md) | [Русский](README_ru.md)

Универсальный UI-фреймворк на WinAPI с виджетами в стиле ShadcnUI,
полной поддержкой ColorSpace и HDR-рендерингом.

## Обзор

Глубокий модуль, предоставляющий современный UI-фреймворк на чистом WinAPI:

- **Маленький интерфейс**: минимальный публичный API с максимально скрытой сложностью
- **Высокий рычаг**: сложные UI-операции через простые вызовы функций
- **Поддержка ColorSpace**: полная поддержка sRGB, Display P3, Rec.2020 и HDR (PQ/HLG)
- **HDR-рендеринг**: интеграция с Windows HDR API для HDR-дисплеев
- **Стиль ShadcnUI**: современный чистый дизайн виджетов по мотивам ShadcnUI
- **Глубокая архитектура**: вся сложность WinAPI скрыта в платформенном слое

## Архитектура

```
src/ui/
├── core/                  # Ядро фреймворка (слой интерфейса)
│   ├── application.h/cc   # Жизненный цикл приложения
│   └── window.h/cc        # Управление окнами
├── colorspace/            # Поддержка ColorSpace и HDR
│   ├── color_space.h      # Перечисления и утилиты цветовых пространств
│   ├── hdr_manager.h/cc   # Управление HDR-дисплеями
│   └── color_manager.h/cc # Единое управление цветом
├── widgets/               # Система виджетов
│   └── widget.h           # Базовый интерфейс виджета
├── layout/                # Система раскладки
│   └── geometry.h         # Геометрические примитивы (Rect, Size, Point)
├── theme/                 # Токены темы и провайдер светлой/тёмной
├── rendering/             # Абстракция рендерера и бэкенд Direct2D/DirectWrite
├── css/                   # CSS-движок: селекторы, каскад, инвалидация стилей
├── style/                 # Мост XER::Content::CSS: WidgetStyleHost отражает
│                          # дерево виджетов в style-движок, style_values маппит
│                          # CSS-значения на токены темы
├── debug/                 # DevTools: инспектор Elements/Render/Stats, XML-дамп
├── xml/                   # Декларативные деревья виджетов из XML-разметки
└── platform/              # Трансляция ввода Win32
```

### Пул процессов удалённого рендеринга

`Application::Initialize` заранее запускает пул скрытых процессов
`xer_engine_ui_render.exe` (по умолчанию 2; переопределяется
`Application::SetRenderWorkerCount(n)` до init или переменной
`XER_UI_RENDER_WORKERS` — `auto` масштабируется по ядрам, `0` отключает
разнесение рендеринга по процессам). Каждый воркер ждёт на своём
`Cache::SharedMemoryChannel`; `UiRenderHost::CreateRemoteRenderer`
мгновенно забирает воркер из пула (сверх пула — спавн по требованию) и
перенацеливает его на HWND вызывающего через потоковую операцию
`kSetTargetHwnd`. `Application::Shutdown` сигналит всем каналам, ждёт
воркеров и добивает зависших.

#### Общее хранилище ресурсов + поэлементная обработка

Пиксельные данные, которые переполняют канал команд, живут в
именованном общем сегменте — `Rendering::RenderStore`
(`xer.ui.res.<pid>`), принадлежит `UiRenderHost` и подключается к
каждому воркеру через `kAttachStore` при checkout. Воркеры читают
ресурсы напрямую из shared memory: кадр 4K стоит один memcpy с
seqlock, а не сообщение в 8 МБ:

- `UiRenderHost::UploadImage(id, bgra, w, h)` → статичный битмап;
  воркеры рисуют через `DrawResource(id, dst, opacity, radius)` и
  держат per-id кэш `ID2D1Bitmap`.
- `UiRenderHost::WriteVideoFrame(id, bgra, w, h)` → живой кадр
  видео/захвата; `DrawVideoFrame(id, ...)` перезаписывает per-id
  staging-битмап каждый кадр.
- `UiRenderHost::WriteSpectrum(id, levels, count)` → float-уровни
  аудио; `DrawSpectrum(id, dst, color, mode)` рисует VU/спектр
  на стороне воркера.
- `UiRenderHost::FreeResource(id)` освобождает слот и рассылает
  `kFreeResource` всем воркерам (сброс GPU-битмапа).
- Размер хранилища — 64 МБ по умолчанию (`XER_UI_STORE_MB`).

Поэлементные операции покрывают композитинг: `kPushLayer`/`kPopLayer`
(оффскрин-слой с прозрачностью), `kSetTransform`/`kResetTransform`
(аффинное 2x3). `WidgetTree::RenderSubtree` эмитит их автоматически
из computed style — `opacity`, `filter: blur()`, `transform:
translate()/scale()` — то есть эффекты каждого виджета вычисляются
внутри процесса-рендерера.

### CSS в XER::UI (мост XER::Content::CSS)

`CSS::WidgetStyleHost` (`style/widget_style_host.h`) гоняет каноничный
движок `XER::Content::CSS` (`src/content/css`, в `YA.DesginCSS.dll`)
по живому дереву виджетов:

- Виджеты несут CSS-идентичность: `GetStyleTag()` (имя элемента;
  fallback — lowercase-имя из RTTI), `SetStyleId`, `AddStyleClass`,
  `SetStyleAttribute`, `SetInlineStyle`; `disabled` синтезируется из
  `IsEnabled()`.
- `Update(root)` отражает поддерево виджетов в теневое дерево
  `CSSNode`, превращает диффы в вызовы `FooChangedForElement` +
  pending-инвалидации, проталкивает их и пересчитывает — записывая
  `GetComputedStyle()`/`StyleValue(prop)` на каждый виджет.
- Селекторы: tag, `.class`, `#id`, `[attr]`, `[attr=v]`,
  `:hover`/`:active`/`:focus`, `:first-child`/`:last-child`/
  `:nth-child(an+b)`, descendant и `>`, списки через запятую, `*`.
  Conditional-pseudo выставляет флаги `AffectedBy*`, поэтому первое же
  `:hover`-изменение пересчитывает только нужный виджет.
- `style/style_values.h` парсит цвета (`#rgb[a]`, `rgb()`, имена) и
  длины (`px`/`pt`/`em`/`rem`); `ApplyComputedToTokens` маппит
  токен-свойства (`accent-color`, `border-radius`, `font-size`,
  `spacing-*`...) на `Theme::Tokens`.

`Window::SetContent` размещает дерево виджетов, пересчитывает раскладку
при изменении клиентской области и инвалидирует окно для перерисовки.
Сообщения Win32 о клавиатуре, символах, мыши и фокусе становятся
событиями виджетов, при этом UTF-16 суррогатные пары кодируются в UTF-8.
`Window::SetTheme` выбирает светлые или тёмные токены. Ошибки нативного
рендеринга сохраняются в `GetLastRenderError()` и отправляются в отладочный
вывод Windows; пустое дерево контента рисует выбранный фон.

Виджеты сообщают о визуальных изменениях через внутренние dirty-флаги,
распространяемые к корню дерева; окно планирует перерисовку только когда
виджет действительно изменился, поэтому ввод в простое ничего не стоит.
`Window::Invalidate()` форсирует перерисовку, а
`Window::SetFrameCallback(cb, ms)` вызывает `cb` на UI-потоке по таймеру
для polling-обновлений (например, позиции воспроизведения) без принудительных
перерисовок.

## Принципы дизайна

### Стратегия глубоких модулей
- **Маленький интерфейс**: минимум публичных методов (у Application всего 3)
- **Глубокая реализация**: сложная логика скрыта в приватной реализации (PIMPL)
- **Высокий рычаг**: один вызов интерфейса запускает сложные операции
- **Локальность изменений**: правки содержатся внутри модуля, не затрагивая вызывающих

### Размещение швов
- **Внешний шов**: интерфейсы Application, Window, Widget
- **Внутренние швы**: адаптеры рендерера, стратегии раскладки, реализации темы
- **Платформенный шов**: WinAPI-специфичный код изолирован в platform/

## Быстрый старт

### Базовое окно

```cpp
#include "ui/core/application.h"
#include "ui/core/window.h"

using namespace XER::UI::Core;

int main() {
    // Инициализация приложения
    auto& app = Application::Instance();
    if (!app.Initialize()) {
        return -1;
    }

    // Создание окна
    Window window;
    window.Create("My Window", 800, 600);
    window.Show();

    // Запуск цикла событий
    return app.Run();
}
```

### ColorSpace и HDR

```cpp
#include "ui/core/application.h"
#include "ui/core/window.h"
#include "ui/colorspace/color_manager.h"

using namespace XER::UI;
using namespace XER::UI::ColorSpace;

int main() {
    auto& app = Application::Instance();
    app.Initialize();

    // Инициализация управления цветом
    auto& color_mgr = ColorManager::Instance();
    color_mgr.Initialize();

    // Проверка поддержки HDR
    if (color_mgr.IsHDRCapable()) {
        std::cout << "HDR поддерживается!" << std::endl;

        // Установить предпочитаемое цветовое пространство
        color_mgr.SetPreferredColorSpace(ColorSpace::Rec2100_PQ);

        // Получить метаданные HDR
        auto& hdr_mgr = color_mgr.GetHDRManager();
        HDRMetadata metadata = hdr_mgr.GetHDRMetadata();
        std::cout << "Макс. яркость: " << metadata.max_luminance << " нит" << std::endl;
    }

    Window window;
    window.Create("HDR Window", 800, 600);

    // Включить HDR для окна
    if (color_mgr.IsHDRCapable()) {
        color_mgr.GetHDRManager().EnableHDR(window.GetHWND());
    }

    window.Show();
    return app.Run();
}
```

### Преобразование цвета

```cpp
#include "ui/colorspace/color_manager.h"

using namespace XER::UI::ColorSpace;

// Преобразование между цветовыми пространствами
ColorManager& color_mgr = ColorManager::Instance();
color_mgr.Initialize();

std::vector<float> input_rgb(pixel_count * 3);  // цвета sRGB
std::vector<float> output_rgb(pixel_count * 3);  // выходной буфер

// Преобразовать sRGB в Rec.2020
color_mgr.ConvertToDisplaySpace(
    input_rgb.data(),
    output_rgb.data(),
    pixel_count,
    ColorSpace::sRGB,
    ColorSpace::Rec2020
);
```

## Основные модули

### Application

Главная точка входа с маленьким интерфейсом:

```cpp
class Application {
public:
    static Application& Instance();
    bool Initialize();  // Настройка платформы
    int Run();          // Запуск цикла событий
    void Shutdown();    // Очистка
};
```

**Глубокая реализация скрывает**:
- инициализацию WinAPI
- регистрацию common controls
- цикл сообщений
- управление приоритетом процесса

### Window

Управление окнами с маленьким интерфейсом:

```cpp
class Window {
public:
    bool Create(const std::string& title, int width, int height);
    void Show();
    void Hide();
    void Close();
    void SetContent(std::shared_ptr<Widget> root);
    HWND GetHWND() const;  // Доступ к платформе при необходимости
};
```

**Глубокая реализация скрывает**:
- регистрацию класса окна
- создание окна WinAPI
- обработку сообщений
- управление деревом виджетов

### Widget

Универсальный контракт геометрии раскладки для каждого виджета
(модель Qt `QWidget`/`QLayoutItem` + Chromium `ui/views`). Свойства
одинаковы для всех — хранятся в `Widget` и разрешаются централизованно
внутри `SetBounds` — единой точки, через которую проходят все
реализации `Layout()` и прямое размещение хостами, поэтому каждый
контейнер учитывает их для каждого размещаемого дочернего виджета
без собственного кода. Все значения по умолчанию — без изменений.

```cpp
class Widget {
public:
    virtual void Render(Renderer&, const Theme::Tokens&) = 0;
    virtual void Layout(const Layout::Rect& bounds) = 0;
    virtual bool HandleEvent(const Event&) = 0;
    virtual Layout::Size GetMinSize() const = 0;
    virtual Layout::Size GetPreferredSize() const = 0;
    virtual Layout::Size GetMaxSize() const = 0;
    Layout::Rect GetBounds() const;
    void SetBounds(const Layout::Rect& slot);  // принимает вычисленный rect

    // Выравнивание в слоте большего размера — флаги в стиле
    // Qt::Alignment, по одному биту на ось (Alignment::HCenter | Top).
    // Default заполняет слот целиком (прежнее поведение).
    void SetAlignment(Alignment alignment);
    Alignment alignment() const;

    // Внешний отступ (margin) + ведущий отступ с учётом направления
    // текста (отступ дерева/абзаца).
    void SetMargin(Layout::Insets margin);
    void SetIndent(int pixels);

    // Растяжение содержимого в стиле QLabel; учитывается
    // VideoPreviewWidget, Surface и любыми виджетами с собственным
    // содержимым.
    void SetScaledContents(bool scaled);
    bool hasScaledContents() const;

    // Алиасы Qt: sizeHint→GetPreferredSize, minimumSizeHint→GetMinSize.
    Layout::Size sizeHint() const;
    Layout::Size minimumSizeHint() const;

    // Алиасы геометрии в стиле Qt поверх GetBounds() (координаты
    // родителя).
    Layout::Rect rect() const;  Layout::Point pos() const;
    Layout::Size size() const;  float x(), y(), width(), height();
    void Move(const Layout::Rect&);  void Move(int x, int y);
    void Resize(int w, int h);       void Resize(const Layout::Size&);

    // Фиксированный размер в стиле Qt: переопределяет предпочтительную
    // величину по оси — даже при Fill виджет прижимается к началу
    // ячейки вместо растяжения. <= 0 или ClearFixedSize() — сброс.
    void SetFixedSize(const Layout::Size&);
    void SetFixedWidth(float);  void SetFixedHeight(float);
    void ClearFixedSize();
};
```

`widgets/surface.h` добавляет `Surface` — универсальный виджет
произвольной отрисовки:
`SetPaintCallback(std::function<void(Renderer&, Rect, const Tokens&)>)`
+ `SetEventCallback`, letterboxing по `ContentSize` с учётом
`hasScaledContents()`, настраиваемые size hints. Размещает произвольный
рендеринг приложения (видеомониторы, таймлайны, индикаторы) внутри
дерева виджетов.

### ColorSpace

Перечисление цветовых пространств и утилиты:

```cpp
enum class ColorSpace {
    sRGB,          // Стандартный sRGB
    DisplayP3,     // Широкий охват
    Rec2020,       // Сверхширокий охват
    Rec2100_PQ,    // HDR PQ
    Rec2100_HLG,   // HDR HLG
    ScRGB          // HDR линейный
};

enum class TransferFunction {
    sRGB, PQ, HLG, Linear, Gamma22, Gamma18
};
```

### HDRManager

Управление HDR-дисплеями:

```cpp
class HDRManager {
public:
    bool Initialize();
    bool IsHDRCapable() const;
    HDRMetadata GetHDRMetadata() const;
    bool EnableHDR(HWND hwnd);
    bool DisableHDR(HWND hwnd);
};
```

**Глубокая реализация скрывает**:
- Windows HDR API (IDXGIOutput)
- определение возможностей дисплея
- конфигурацию метаданных HDR
- определение Dolby Vision

### ColorManager

Единое управление цветом:

```cpp
class ColorManager {
public:
    void SetPreferredColorSpace(ColorSpace space);
    ColorSpace GetSystemColorSpace() const;
    void ConvertToDisplaySpace(...);
    void ApplyTransferFunction(...);
    HDRManager& GetHDRManager();
};
```

**Глубокая реализация скрывает**:
- матрицы преобразования цветовых пространств
- применение передаточных функций
- определение системного цветового пространства
- интеграцию с HDRManager

## Поддерживаемые цветовые пространства

| Цветовое пространство | Охват | HDR | Передаточная функция | Применение |
|-----------------------|-------|-----|----------------------|------------|
| sRGB | Стандартный | Нет | гамма sRGB | Стандартный UI |
| Display P3 | Широкий | Нет | гамма sRGB | Дисплеи Apple |
| Rec.2020 | Сверхширокий | Нет | гамма sRGB | Топовые дисплеи |
| Rec.2100 PQ | Сверхширокий | Да | PQ | HDR-видео |
| Rec.2100 HLG | Сверхширокий | Да | HLG | HDR-вещание |
| ScRGB | Сверхширокий | Да | Линейная | HDR-графика |

## Поддержка HDR

### Определение
Автоматическое определение возможностей HDR:
- проверка версии Windows (Windows 10 1803+)
- запрос возможностей дисплея через DXGI
- разбор EDID на предмет HDR
- определение Dolby Vision

### Конфигурация
Настраиваемые параметры HDR:
- максимальная яркость (ниты)
- минимальная яркость (уровень чёрного)
- максимальный средний уровень света кадра

### HDR на уровне окна
HDR можно включать для каждого окна отдельно:
```cpp
color_mgr.GetHDRManager().EnableHDR(window.GetHWND());
```

## Статус реализации

### Готово
- ✅ Ядро фреймворка (Application, Window)
- ✅ Перечисление и утилиты ColorSpace
- ✅ HDRManager (базовая реализация)
- ✅ ColorManager с преобразованием цвета
- ✅ Базовый интерфейс Widget + 80 виджетов с темами
- ✅ Геометрические примитивы (Rect, Size, Point)
- ✅ Рендерер Direct2D + путь canvas Skia/D3D12
- ✅ CSS-движок (`css/`): селекторы (tag/class/id/attr/pseudo/nth-child),
  каскад по origin+специфичности, наборы инвалидации + отложенные
  инвалидации + DFS-инвалидатор — `YA.DesginCSS.dll`
- ✅ Инспектор DevTools (Elements/Render/Stats) и XML-дамп
- ✅ Загрузчик XML-разметки UI (`xml/`)

### TODO
- ⏳ Система раскладки (Flex, Grid)
- ⏳ Полное покрытие тестами
- ⏳ Примеры приложений

## Сборка

### Требования
- Windows 10 или новее
- Visual Studio 2019 или новее (либо MSYS2 clang64 — пресет `windows-clang-msys2`)
- Поддержка C++23
- Windows SDK
- DirectX SDK (для Direct2D)

### Интеграция с CMake
Добавить в CMakeLists.txt:

```cmake
# Модуль UI
if(WIN32)
    file(GLOB_RECURSE UI_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/*.cc"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/**/*.cc"
    )
    file(GLOB_RECURSE UI_HEADERS
        "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/*.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/**/*.h"
    )

    add_library(XER_UI STATIC ${UI_SOURCES} ${UI_HEADERS})
    target_link_libraries(XER_UI
        PRIVATE
            user32
            gdi32
            d2d1
            dxgi
    )
endif()
```

## Преимущества глубоких модулей

### Рычаг
- `Application::Run()` обрабатывает весь жизненный цикл UI
- Один `Window::Create()` — регистрация, создание и настройка окна
- `ColorManager::ConvertToDisplaySpace()` — сложные преобразования цвета

### Локальность
- Вся сложность WinAPI в platform/
- Логика преобразования цвета в colorspace/
- Отрисовка виджетов в widgets/
- Изменения не растекаются по кодовой базе

### Тестируемость
- Интерфейс рендерера мокабелен для тестов
- Логика виджетов тестируется без WinAPI
- Преобразование цвета тестируется независимо
- Чистые швы для внедрения зависимостей

## Примеры

См. каталог `examples/`:
- пример базового окна
- пример HDR-окна
- пример преобразования цвета
- пример виджета (когда реализован)

## Документация

- [ARCHITECTURE.md](ARCHITECTURE.md) — подробная документация архитектуры
- [README_en.md](README_en.md) — английская версия
- [README_ru.md](README_ru.md) — этот файл
- встроенная документация в заголовках

## Участие в разработке

При добавлении новых возможностей:
1. Держать интерфейс маленьким
2. Скрывать сложность в реализации
3. Использовать PIMPL для стабильности ABI
4. Ясно документировать интерфейс
5. Добавлять тесты для интерфейса
6. Обновлять этот README

## Лицензия

Часть проекта XER. См. LICENSE основного проекта.
