#ifndef XRAY_COLLECTOR_HPP
#define XRAY_COLLECTOR_HPP

#include "types.hpp"
#include <string>
#include <chrono>
#include <optional>

#ifndef NOMINMAX
#define NOMINMAX
#endif

#if defined(_WIN32)
#include <windows.h>
#include <pdh.h>
#include <comdef.h>
#endif

namespace xray {

std::chrono::system_clock::time_point collect_timestamp();
HardwareData collect_all(const std::optional<std::string>& wmi_namespace = std::nullopt);
DiagnosticReport run_builtin_diagnostics(const HardwareData& data,
                                         const DiagnosticThresholds& thresholds);
std::vector<std::string> read_critical_logs();
double pdh_read_cpu_temp();

std::string severity_to_string(DiagnosticSeverity sev);
std::string severity_to_emoji(DiagnosticSeverity sev);

inline bool starts_with(const std::string& s, const std::string& prefix) {
    return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

}

#endif
