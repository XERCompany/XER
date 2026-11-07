#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <ostream>

namespace XER::AsyncIO::IO::LogDebug {

class DumpHelper {
public:
    DumpHelper();
    ~DumpHelper();

    // Hex dump
    static std::string HexDump(const uint8_t* data, size_t size, size_t bytes_per_line = 16);
    static void HexDumpToStream(std::ostream& os, const uint8_t* data, size_t size);

    // Binary visualization
    static std::string BinaryDump(const uint8_t* data, size_t size);
    static std::string BitDump(uint32_t value);

    // Memory inspection
    static std::string InspectMemory(const void* ptr, size_t size);
    
    // String utilities
    static std::string RawStringToHex(const std::string& str);
    static std::string HexToRawString(const std::string& hex);

    // File dumping
    static bool DumpMemoryToFile(const std::string& filepath, const uint8_t* data, size_t size);
    static bool DumpObjectToFile(const std::string& filepath, const void* obj, size_t size);

    // Statistics
    static std::string GetDataStatistics(const uint8_t* data, size_t size);

private:
    static bool IsPrintable(uint8_t byte);
};

}  // namespace XER::AsyncIO::IO::LogDebug
