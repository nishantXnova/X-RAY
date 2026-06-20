#ifndef XRAY_SMART_ANALYZER_HPP
#define XRAY_SMART_ANALYZER_HPP

#include <string>
#include <vector>
#include <cstdint>

namespace xray {

struct SmartAttribute {
    uint8_t id;
    std::string name;
    uint16_t flags;
    uint8_t value;
    uint8_t worst;
    uint8_t threshold;
    uint64_t raw_data;
    std::string raw_hex;
};

struct SmartReadResult {
    bool success;
    std::string drive_model;
    std::string drive_serial;
    std::string error_message;
};

SmartReadResult read_smart_attributes(const std::string& drive_letter);
bool has_bad_sectors(const std::vector<SmartAttribute>& attributes);
std::string interpret_smart_status(const std::vector<SmartAttribute>& attributes);

}

#endif
