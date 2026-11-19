# Подсистема Async I/O

[English](README_en.md) | [Русский](README_ru.md)

Каталог `src/async_io` содержит примитивы асинхронного ввода-вывода и
слой аппаратной абстракции, используемые по всему проекту. Он даёт
строительные блоки буферизации, планирования, конкурентности и работы
с машинным окружением для рантайма, сетевых слоёв, нативных интеграций
и аппаратно-ориентированных оптимизаций.

## Что предоставляет

- асинхронные пути чтения и записи файлов
- переиспользуемые пулы буферов и хелперы управления памятью
- планирование задач и исполнение через очереди
- примитивы синхронизации
- определение аппаратных возможностей и мониторинг ресурсов
- опциональные бэкенд-интеграции для ускоренных нагрузок

## Типичные компоненты

- `async_file_reader.*` / `async_file_writer.*`
- `async_buffer_pool.*` / `io_buffer_pool.*`
- `async_task_queue.*`
- `io_file_map.*`
- `io_prefetch_manager.*`
- `io_priority_queue.*`
- `hardware_abstraction/`
- `dnn_backends/`

## Принципы дизайна

- по возможности держать I/O вне главного пути исполнения
- переиспользовать байтовые буферы и ресурсы планирования
- изолировать платформенное поведение за абстрактными интерфейсами
- допускать опциональное ускорение через CPU/GPU-ориентированные пути
- никогда не зависеть от вендорских SDK жёстко — грузить их динамически
  во время выполнения

## Аппаратная абстракция

Каталог `hardware_abstraction` покрывает всё машинное окружение:

| Провайдер                | Что сообщает                                          | Бэкенд Windows             | Бэкенд Linux               |
|--------------------------|-------------------------------------------------------|----------------------------|----------------------------|
| `CPUInfoProvider`        | вендор/модель, ядра, кэши (L1/L2/L3), SIMD, загрузка | `GetLogicalProcessorInformationEx`, `__cpuid`, `GetSystemTimes`, `NtQuerySystemInformation` | `/proc/cpuinfo`, `/sys/.../cache`, cpuid, `/proc/stat` |
| `GPUInfoProvider`        | имя GPU, память, температура, мощность, загрузка, частоты | NVML + опционально CUDA/Vulkan | NVML + опционально CUDA/Vulkan |
| `MemoryMonitor`          | системная + GPU память, трекинг пиков                 | `GlobalMemoryStatusEx`     | `sysconf` + NVML           |
| `TemperatureSensor`      | температуры CPU + GPU, алерты, фоновый поток          | WMI/ACPI (best-effort) + NVML | `/sys/class/thermal/*` + NVML |
| `HwPowerMonitor`         | мощность пакетов CPU + GPU, счётчик троттлинга        | NVML (мощность CPU не поддержана) | RAPL sysfs + NVML      |
| `HardwareTopologyMap`    | сокеты, NUMA-ноды, ядра, иерархия кэшей               | `GetLogicalProcessorInformationEx` | `/sys/devices/system/...` |
| `HwCapabilityScanner`    | SSE4/AVX2/AVX-512, RAM, наличие CUDA                  | `__cpuid` + NVML           | cpuid + sysinfo + NVML     |
| `DirectIOManager`        | выровненные буферы, O_DIRECT / FILE_FLAG_NO_BUFFERING | Win32 + утилиты выравнивания | `open(O_DIRECT)`           |
| `DiskInfoProvider`       | смонтированные диски, размеры, ФС, статистика I/O     | `GetLogicalDriveStrings` + `GetVolumeInformation` | `/proc/mounts` + `/proc/diskstats` |
| `NetworkInfoProvider`    | адаптеры, MAC, IPv4/IPv6, скорость линка, счётчики байт | `GetAdaptersAddresses`  | `getifaddrs` + `/sys/class/net` |
| `BatteryInfoProvider`    | батареи, процент, состояние, питание от сети, износ   | `GetSystemPowerStatus`     | `/sys/class/power_supply`  |
| `SystemInfoProvider`     | агрегированный снимок всего вышеперечисленного + экспорт в JSON | всё перечисленное | всё перечисленное |

### NVML (NVIDIA) — динамическая загрузка

`nvml_loader.*` предоставляет процесс-широкий синглтон, который при
первом использовании грузит `nvml.dll` (Windows) / `libnvidia-ml.so.1`
(Linux), однажды инициализирует NVML и держит его загруженным. Все запросы
температуры / мощности / загрузки / памяти GPU идут через этот синглтон,
а не грузят библиотеку на каждый вызов. **Нет зависимости на этапе
компиляции** от заголовков или библиотек NVIDIA: если драйвер отсутствует,
каждое поле GPU сообщается с явным `*_supported = false`.

### Температура CPU на Windows — честное замечание

Чтение температуры пакета CPU на Windows без драйвера ядра или прав
администратора ненадёжно. Мы используем `MSAcpi_ThermalZoneTemperature`
через WMI как best-effort источник и помечаем каждое показание флагом
`is_supported`. Когда WMI ничего не возвращает, мы выдаём единственную
запись `CPU` с `is_supported = false` вместо фейкового значения `0 °C`.
Для точных температур CPU установите LibreHardwareMonitor (с публикацией
в WMI) или вендорский драйвер.

### Неймспейс

Все классы железа живут в `AsyncIO::IO::Hardware` (старый алиас
`AsyncIO::IO::HW` объединён в него).

## Пример использования

```cpp
#include "async_io/hardware_abstraction/system_info.h"

using AsyncIO::IO::Hardware::SystemInfoProvider;

int main() {
    SystemInfoProvider sys;
    sys.Initialize();
    auto snap = sys.Refresh();

    // Красивый JSON всей машины.
    std::cout << sys.ToJson(snap, /*pretty=*/true) << "\n";

    // Или прямой доступ к типизированным полям.
    for (const auto& gpu : snap.gpus) {
        std::cout << gpu.name << "  " << gpu.temp_celsius << " °C  "
                  << gpu.gpu_util_percent << "% util\n";
    }
    std::cout << "Disks: " << snap.disks.size() << "\n";
    std::cout << "On AC: " << (snap.on_ac_power ? "yes" : "no") << "\n";
    return 0;
}
```

Потоковая телеметрия температур с фоновым потоком:

```cpp
using AsyncIO::IO::Hardware::TemperatureSensor;
TemperatureSensor t;
t.Initialize();
t.StartMonitoring(2000);  // обновление каждые 2с
// ... позже ...
const auto readings = t.GetAllReadings();
t.StopMonitoring();
```

## Примечания

Подсистема спроектирована как переиспользуемая, а не привязанная к
приложению. Она служит фундаментом для доступа к файлам, асинхронного
исполнения и аппаратно-ориентированных оптимизаций по всему проекту.
