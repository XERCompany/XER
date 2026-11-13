#include "io_dump_helper.h"

#include "content/hex/hex.h"

#include <sstream>
#include <iomanip>
#include <fstream>
#include <cstring>
#include <algorithm>

namespace XER::AsyncIO::IO::LogDebug {

DumpHelper::DumpHelper() {}

DumpHelper::~DumpHelper() {}

std::string DumpHelper::HexDump(const uint8_t* data, size_t size, size_t bytes_per_line) {
    return XER::Content::Hex::Dump(data, size, static_cast<int>(bytes_per_line));
}

void DumpHelper::HexDumpToStream(std::ostream& os, const uint8_t* data, size_t size) {
    os << XER::Content::Hex::Dump(data, size);
}

std::string DumpHelper::BinaryDump(const uint8_t* data, size_t size) {
    std::ostringstream oss;
    
    for (size_t i = 0; i < size; ++i) {
        if (i % 8 == 0 && i > 0) oss << " ";
        oss << ((data[i] & 0x80) ? '1' : '0')
            << ((data[i] & 0x40) ? '1' : '0')
            << ((data[i] & 0x20) ? '1' : '0')
            << ((data[i] & 0x10) ? '1' : '0')
            << ((data[i] & 0x08) ? '1' : '0')
            << ((data[i] & 0x04) ? '1' : '0')
            << ((data[i] & 0x02) ? '1' : '0')
            << ((data[i] & 0x01) ? '1' : '0');
        
        if (i % 4 == 3) oss << " ";
    }
    
    return oss.str();
}

std::string DumpHelper::BitDump(uint32_t value) {
    std::ostringstream oss;
    for (int i = 31; i >= 0; --i) {
        oss << ((value >> i) & 1);
        if (i % 8 == 0 && i > 0) oss << " ";
    }
    return oss.str();
}

std::string DumpHelper::InspectMemory(const void* ptr, size_t size) {
    std::ostringstream oss;
    const uint8_t* data = static_cast<const uint8_t*>(ptr);
    
    oss << "Memory at " << std::hex << std::setfill('0') << std::setw(16) << reinterpret_cast<uintptr_t>(ptr) << ":\n";
    oss << std::dec;
    oss << "Size: " << size << " bytes\n";
    oss << HexDump(data, std::min(size, size_t(256)));
    
    return oss.str();
}

std::string DumpHelper::RawStringToHex(const std::string& str) {
    return XER::Content::Hex::Encode(str);
}

std::string DumpHelper::HexToRawString(const std::string& hex) {
    std::string result;
    XER::Content::Hex::Decode(hex, &result);
    return result;
}

bool DumpHelper::DumpMemoryToFile(const std::string& filepath, const uint8_t* data, size_t size) {
    std::ofstream file(filepath, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }
    
    file.write(reinterpret_cast<const char*>(data), size);
    return file.good();
}

bool DumpHelper::DumpObjectToFile(const std::string& filepath, const void* obj, size_t size) {
    return DumpMemoryToFile(filepath, static_cast<const uint8_t*>(obj), size);
}

std::string DumpHelper::GetDataStatistics(const uint8_t* data, size_t size) {
    std::ostringstream oss;
    
    if (size == 0) return "Empty data";
    
    uint32_t zero_count = 0, nonzero_count = 0;
    uint32_t byte_freq[256] = {0};
    
    for (size_t i = 0; i < size; ++i) {
        if (data[i] == 0) zero_count++;
        else nonzero_count++;
        byte_freq[data[i]]++;
    }
    
    oss << "Statistics:\n";
    oss << "Total bytes: " << size << "\n";
    oss << "Zero bytes: " << zero_count << " (" << (100.0 * zero_count / size) << "%)\n";
    oss << "Non-zero bytes: " << nonzero_count << " (" << (100.0 * nonzero_count / size) << "%)\n";
    
    uint8_t most_common = 0;
    uint32_t max_freq = 0;
    for (int i = 0; i < 256; ++i) {
        if (byte_freq[i] > max_freq) {
            max_freq = byte_freq[i];
            most_common = static_cast<uint8_t>(i);
        }
    }
    
    oss << "Most common byte: 0x" << std::hex << std::setw(2) << std::setfill('0') 
        << static_cast<int>(most_common) << " (count: " << std::dec << max_freq << ")\n";
    
    return oss.str();
}

bool DumpHelper::IsPrintable(uint8_t byte) {
    return XER::Content::Hex::IsPrintable(byte);
}

}  // namespace XER::AsyncIO::IO::LogDebug
