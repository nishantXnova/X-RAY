#include "xray/collector.hpp"
#include "xray/smart_analyzer.hpp"
#include "xray/config_manager.hpp"
#include "xray/llm_client.hpp"
#include "xray/types.hpp"
#include "xray/ui.hpp"
#include "xray/version.hpp"

#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>
#include <ctime>
#include <cctype>
#include <regex>
#include <thread>
#include <chrono>
#include <mutex>

#ifndef NOMINMAX
#define NOMINMAX
#endif

#if defined(_WIN32)
#include <windows.h>
#include <pdh.h>
#include <pdhmsg.h>
#include <comdef.h>
#include <ObjBase.h>
#include <winhttp.h>
#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "pdh.lib")
#pragma comment(lib, "ole32.lib")
#endif

namespace xray {

// ---------------------------------------------------------------------------
// Timestamp collection
// ---------------------------------------------------------------------------

std::chrono::system_clock::time_point collect_timestamp() {
    return std::chrono::system_clock::now();
}

// ---------------------------------------------------------------------------
// PDH CPU Temperature
// ---------------------------------------------------------------------------

double pdh_read_cpu_temp() {
    PDH_HQUERY query = NULL;
    PDH_HCOUNTER counter = NULL;
    PDH_STATUS status;

    const char* counter_path = R"(\Thermal Zone Information\_OS\_Temperature (C))";

    if (PdhOpenQuery(NULL, 0, &query) != ERROR_SUCCESS) {
        return -1.0;
    }
    if (PdhAddCounterA(query, counter_path, 0, &counter) != ERROR_SUCCESS) {
        PdhCloseQuery(query);
        return -1.0;
    }
    if (PdhCollectQueryData(query) != ERROR_SUCCESS) {
        PdhCloseQuery(query);
        return -1.0;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    PDH_FMT_COUNTERVALUE counterVal;
    if (PdhGetFormattedCounterValue(counter, PDH_FMT_DOUBLE, NULL, &counterVal) != ERROR_SUCCESS) {
        PdhCloseQuery(query);
        return -1.0;
    }

    PdhCloseQuery(query);
    return counterVal.doubleValue;
}

// ---------------------------------------------------------------------------
// Helper: get CPU usage via PDH
// ---------------------------------------------------------------------------

static double get_cpu_usage_pdh() {
    static ULONGLONG last_idle = 0, last_total = 0;
    FILETIME idle_time, kernel_time, user_time;

    if (!GetSystemTimes(&idle_time, &kernel_time, &user_time)) {
        return 0.0;
    }

    ULONGLONG idle = ((ULONGLONG)idle_time.dwHighDateTime << 32) | idle_time.dwLowDateTime;
    ULONGLONG kernel = ((ULONGLONG)kernel_time.dwHighDateTime << 32) | kernel_time.dwLowDateTime;
    ULONGLONG user = ((ULONGLONG)user_time.dwHighDateTime << 32) | user_time.dwLowDateTime;
    ULONGLONG total = kernel + user;

    if (last_total == 0) {
        last_idle = idle;
        last_total = total;
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        return 0.0;
    }

    ULONGLONG idle_d = idle - last_idle;
    ULONGLONG total_d = total - last_total;
    last_idle = idle;
    last_total = total;

    if (total_d == 0) return 0.0;
    double idle_pct = (double)idle_d / (double)total_d * 100.0;
    return std::max(0.0, std::min(100.0, 100.0 - idle_pct));
}

// ---------------------------------------------------------------------------
// Helper: get available RAM in GB
// ---------------------------------------------------------------------------

static double get_available_ram_gb() {
    MEMORYSTATUSEX mem = {};
    mem.dwLength = sizeof(mem);
    if (!GlobalMemoryStatusEx(&mem)) {
        return 0.0;
    }
    return (double)mem.ullAvailPhys / (1024.0 * 1024.0 * 1024.0);
}

static double get_total_ram_gb() {
    MEMORYSTATUSEX mem = {};
    mem.dwLength = sizeof(mem);
    if (!GlobalMemoryStatusEx(&mem)) {
        return 0.0;
    }
    return (double)mem.ullTotalPhys / (1024.0 * 1024.0 * 1024.0);
}

// ---------------------------------------------------------------------------
// Helper: get disk read speed estimate
// ---------------------------------------------------------------------------

static double get_disk_read_speed_mb() {
    PDH_HQUERY query = NULL;
    PDH_HCOUNTER counter = NULL;
    if (PdhOpenQuery(NULL, 0, &query) != ERROR_SUCCESS) return 0.0;

    if (PdhAddCounterA(query, R"(\PhysicalDisk(_Total)\Disk Read Bytes/sec)", 0, &counter) != ERROR_SUCCESS) {
        PdhCloseQuery(query);
        return 0.0;
    }

    PdhCollectQueryData(query);
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    PDH_FMT_COUNTERVALUE val;
    if (PdhGetFormattedCounterValue(counter, PDH_FMT_DOUBLE, NULL, &val) != ERROR_SUCCESS) {
        PdhCloseQuery(query);
        return 0.0;
    }
    PdhCloseQuery(query);
    return val.doubleValue / (1024.0 * 1024.0);
}

// ---------------------------------------------------------------------------
// Helper: escape string for JSON
// ---------------------------------------------------------------------------

static std::string json_escape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 10);
    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            default:   out += c; break;
        }
    }
    return out;
}

// ---------------------------------------------------------------------------
// Read critical system logs via Windows Event Log API
// ---------------------------------------------------------------------------

std::vector<std::string> read_critical_logs() {
    std::vector<std::string> logs;

    HANDLE hEventLog = OpenEventLogA(NULL, "Application");
    if (!hEventLog) return logs;

    DWORD flags = EVENTLOG_BACKWARDS_READ | EVENTLOG_SEQUENTIAL_READ;
    BYTE buffer[8192];
    DWORD bytes_read = 0, min_bytes = 0, record_count = 0;
    int max_count = 30;

    while (max_count-- > 0 && GetOldestEventLogRecord(hEventLog, &record_count) &&
           ReadEventLogA(hEventLog, flags, 0, buffer, sizeof(buffer), &bytes_read, &min_bytes)) {

        DWORD offset = 0;
        while (offset < bytes_read && max_count > 0) {
            EVENTLOGRECORD* pRec = (EVENTLOGRECORD*)(buffer + offset);

            if (pRec->EventType == EVENTLOG_ERROR_TYPE ||
                pRec->EventType == EVENTLOG_WARNING_TYPE) {

                char time_buf[64];
                std::stringstream ts;
                ts << pRec->TimeGenerated;

                char* msg = (char*)(buffer + pRec->DataOffset);
                if (pRec->DataOffset > 0 && pRec->DataOffset < bytes_read &&
                    pRec->DataOffset + 1 < bytes_read) {
                    std::stringstream ss;
                    ss << (pRec->EventType == EVENTLOG_ERROR_TYPE ? "Error" : "Warning")
                       << " " << ts.str() << ": " << msg;
                    logs.push_back(ss.str());
                }
            }

            offset += pRec->Length;
        }
    }

    CloseEventLog(hEventLog);
    return logs;
}

// ---------------------------------------------------------------------------
// SMART analysis is handled by smart_analyzer.cpp / smart_analyzer.hpp
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Main data collection
// ---------------------------------------------------------------------------

HardwareData collect_all(const std::optional<std::string>&) {
    HardwareData hw;

    // CPU
    hw.cpu.usage_percent = get_cpu_usage_pdh();
    hw.cpu.temp_celsius = pdh_read_cpu_temp();
    if (hw.cpu.temp_celsius < 0.0) {
        hw.cpu.temp_celsius = 40.0 + hw.cpu.usage_percent * 0.55;
    }
    hw.cpu.frequency_ghz = 3.4;

    // RAM
    hw.ram.total_gb = get_total_ram_gb();
    hw.ram.available_gb = get_available_ram_gb();
    double available_pct = hw.ram.total_gb > 0.0 ? (hw.ram.available_gb / hw.ram.total_gb) * 100.0 : 100.0;
    hw.ram.possible_leak = available_pct < 10.0;

    // GPU (PDH)
    PDH_HQUERY g_query = NULL;
    PDH_HCOUNTER g_counter = NULL;
    PDH_HCOUNTER ded_counter = NULL;
    double gpu_load = 0.0;
    double vram_mb = 1024.0;

    if (PdhOpenQuery(NULL, 0, &g_query) == ERROR_SUCCESS) {
        const char* gpu_counter = R"(\GPU Engine(*)\Utilization Percentage)";
        if (PdhAddCounterA(g_query, gpu_counter, 0, &g_counter) == ERROR_SUCCESS) {
            PdhCollectQueryData(g_query);
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            PDH_FMT_COUNTERVALUE gv;
            PdhGetFormattedCounterValue(g_counter, PDH_FMT_DOUBLE, NULL, &gv);
            gpu_load = gv.doubleValue;
        }

        if (PdhAddCounterA(g_query, R"(\GPU Adapter Memory(*)\Dedicated Usage)", 0, &ded_counter) == ERROR_SUCCESS) {
            PdhCollectQueryData(g_query);
            std::this_thread::sleep_for(std::chrono::milliseconds(300));
            PDH_FMT_COUNTERVALUE dv;
            PdhGetFormattedCounterValue(ded_counter, PDH_FMT_DOUBLE, NULL, &dv);
            vram_mb = dv.doubleValue / (1024.0 * 1024.0);
        }

        PdhCloseQuery(g_query);
    }

    hw.gpu.load_percent = gpu_load;
    hw.gpu.vram_used_mb = vram_mb;
    hw.gpu.temp_celsius = -1.0;

    // Disks
    ULONG drives = GetLogicalDrives();
    char drive_char = 'A';
    while (drives) {
        if (drives & 1) {
            std::string letter(1, drive_char);
            std::string path = letter + ":\\";
            UINT type = GetDriveTypeA(path.c_str());

            if (type == DRIVE_FIXED || type == DRIVE_REMOVABLE) {
                HardwareData::Disk d;
                d.drive_letter = letter;
                d.smart_status = "UNKNOWN";
                d.read_speed_mb = 0.0;

                ULARGE_INTEGER free, total, avail;
                if (GetDiskFreeSpaceExA(path.c_str(), &free, &total, &avail)) {
                    d.read_speed_mb = get_disk_read_speed_mb();
                }
                hw.disks.push_back(d);
            }
        }
        drives >>= 1;
        drive_char++;
    }

    // Battery
    SYSTEM_POWER_STATUS sps;
    if (GetSystemPowerStatus(&sps)) {
        hw.battery.is_charging = (sps.ACLineStatus == 1);
        hw.battery.health_percent = (sps.BatteryLifePercent == 255) ? 100.0 : (double)sps.BatteryLifePercent;
        hw.battery.remaining_minutes = (sps.BatteryLifeTime == (DWORD)-1) ? 0 : (int)(sps.BatteryLifeTime / 60);
    }

    // Critical logs
    hw.critical_logs = read_critical_logs();

    return hw;
}

// ---------------------------------------------------------------------------
// Built-in diagnostics (offline, no LLM)
// ---------------------------------------------------------------------------

DiagnosticReport run_builtin_diagnostics(const HardwareData& d,
                                         const DiagnosticThresholds& t) {
    DiagnosticReport report;
    report.overall_severity = DiagnosticSeverity::HEALTHY;

    // CPU Temp check
    if (d.cpu.temp_celsius >= t.cpu_temp_critical_c) {
        report.overall_severity = DiagnosticSeverity::CRITICAL;
        report.findings.push_back(DiagnosticFinding{
            DiagnosticSeverity::CRITICAL,
            "CPU",
            "CPU temperature is critically high at " + std::to_string((int)d.cpu.temp_celsius) +
            "C (threshold: " + std::to_string((int)t.cpu_temp_critical_c) + "C). "
            "Thermal throttling is active or imminent. Immediate action required.",
            {"Clean dust from vents and heatsink fans.",
             "Check that thermal paste is applied correctly (re-paste if older than 2 years).",
             "Consider a laptop cooling pad or improved case airflow."},
            "Do NOT download any software claiming to 'Fix CPU Temperature in One Click' "
            "or pay for fake 'PC Optimization' tools. Temperature issues are physical. "
            "Legitimate solutions require hardware maintenance only."
        });
    } else if (d.cpu.temp_celsius >= t.cpu_temp_warning_c) {
        if (report.overall_severity < DiagnosticSeverity::WARNING)
            report.overall_severity = DiagnosticSeverity::WARNING;
        report.findings.push_back(DiagnosticFinding{
            DiagnosticSeverity::WARNING,
            "CPU",
            "CPU temperature is elevated at " + std::to_string((int)d.cpu.temp_celsius) +
            "C. Watch for sustained high loads.",
            {"Close unused background applications to reduce CPU load.",
             "Check for dust buildup in cooling vents."},
            "Do NOT pay for 'PC Cleaner' or 'Speed Booster' apps; these do not reduce temperature."
        });
    }

    // RAM check
    if (d.ram.available_gb < t.ram_available_min_gb) {
        if (report.overall_severity < DiagnosticSeverity::WARNING)
            report.overall_severity = DiagnosticSeverity::WARNING;
        report.findings.push_back(DiagnosticFinding{
            DiagnosticSeverity::WARNING,
            "RAM",
            "Available memory critically low: " +
            std::to_string((int)(d.ram.available_gb * 10) / 10.0) + " GB of " +
            std::to_string((int)(d.ram.total_gb * 10) / 10.0) + " GB remaining.",
            {"Close unused browser tabs and memory-heavy applications.",
             "Consider upgrading RAM or reducing startup program count."},
            "Do NOT use 'RAM Booster' or 'Memory Optimizer' third-party tools; "
            "they typically degrade performance. Use Windows built-in Task Manager to manage processes."
        });
    }

    // Battery health check
    if (!d.battery.is_charging && d.battery.health_percent < t.battery_health_warning_pct) {
        if (report.overall_severity < DiagnosticSeverity::WARNING)
            report.overall_severity = DiagnosticSeverity::WARNING;
        report.findings.push_back(DiagnosticFinding{
            DiagnosticSeverity::WARNING,
            "Battery",
            "Battery health at " + std::to_string((int)d.battery.health_percent) +
            "%. Replacement recommended when below " + std::to_string((int)t.battery_health_warning_pct) + "%.",
            {"Reduce screen brightness and enable power-saving mode.",
             "Schedule a battery replacement through the manufacturer's official service."},
            "Do NOT buy cheap third-party replacement batteries from unverified marketplaces. "
            "Use OEM or manufacturer-certified replacements only."
        });
    }

    // Critical logs check
    for (const auto& log : d.critical_logs) {
        if (log.find("Error") != std::string::npos || log.find("disk") != std::string::npos) {
            report.overall_severity = DiagnosticSeverity::CRITICAL;
            report.findings.push_back(DiagnosticFinding{
                DiagnosticSeverity::CRITICAL,
                "System Logs",
                "Critical system event detected: " + log.substr(0, 120),
                {"Open Event Viewer and review full event details.",
                 "Backup critical data immediately to an external or cloud drive.",
                 "Check disk health using chkdsk and manufacturer diagnostic tools."},
                "Do NOT install 'Registry Fixer' or 'Disk Repair Wizard' software advertised online. "
                "These are almost always unwanted or harmful. Use only manufacturer or Windows built-in tools."
            });
            break;
        }
    }

    return report;
}

// ---------------------------------------------------------------------------
// Severity helpers
// ---------------------------------------------------------------------------

std::string severity_to_string(DiagnosticSeverity sev) {
    switch (sev) {
        case DiagnosticSeverity::HEALTHY:  return "HEALTHY";
        case DiagnosticSeverity::WARNING:  return "WARNING";
        case DiagnosticSeverity::CRITICAL: return "CRITICAL";
        default: return "UNKNOWN";
    }
}

std::string severity_to_emoji(DiagnosticSeverity sev) {
    switch (sev) {
        case DiagnosticSeverity::HEALTHY:  return "🟢";
        case DiagnosticSeverity::WARNING:  return "🟡";
        case DiagnosticSeverity::CRITICAL: return "🔴";
        default: return "⚪";
    }
}

}
