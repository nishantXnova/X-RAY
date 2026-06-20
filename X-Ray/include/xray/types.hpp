#ifndef XRAY_TYPES_HPP
#define XRAY_TYPES_HPP

#include <string>
#include <vector>
#include <cstdint>
#include <chrono>
#include <optional>

namespace xray {

// --- Hardware Metrics ---

struct HardwareData {
    struct CPU {
        double usage_percent = 0.0;
        double temp_celsius = 0.0;
        double frequency_ghz = 0.0;
    };
    struct GPU {
        double load_percent = 0.0;
        double vram_used_mb = 0.0;
        double temp_celsius = 0.0;
    };
    struct RAM {
        double total_gb = 0.0;
        double available_gb = 0.0;
        bool possible_leak = false;
    };
    struct Disk {
        std::string smart_status;
        double read_speed_mb = 0.0;
        std::string model;
        std::string drive_letter;
    };
    struct Battery {
        double health_percent = 100.0;
        bool is_charging = false;
        int remaining_minutes = 0;
    };

    CPU cpu;
    GPU gpu;
    RAM ram;
    std::vector<Disk> disks;
    Battery battery;
    std::vector<std::string> critical_logs;
};

// --- Diagnostic Result ---

enum class DiagnosticSeverity {
    HEALTHY,
    WARNING,
    CRITICAL
};

struct DiagnosticFinding {
    DiagnosticSeverity severity;
    std::string component;
    std::string description;
    std::vector<std::string> safe_fixes;
    std::string scam_warning;
};

struct DiagnosticReport {
    DiagnosticSeverity overall_severity;
    std::vector<DiagnosticFinding> findings;
};

// --- OpenRouter Config ---

struct OpenRouterConfig {
    std::string api_key;
    std::string model;
    std::string api_base;
    int timeout_ms;
    int max_tokens;
    std::string system_prompt;
};

// --- Thresholds ---

struct DiagnosticThresholds {
    double cpu_temp_critical_c;
    double cpu_temp_warning_c;
    double gpu_temp_critical_c;
    double gpu_temp_warning_c;
    double ram_available_min_gb;
    std::vector<std::string> disk_smart_bad_statuses;
    double battery_health_warning_pct;
};

}

#endif
