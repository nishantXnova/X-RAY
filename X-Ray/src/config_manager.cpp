#include "xray/config_manager.hpp"
#include "xray/types.hpp"

#include <fstream>
#include <sstream>
#include <iostream>
#include <cstring>

#ifndef NOMINMAX
#define NOMINMAX
#endif

#if defined(_WIN32)
#include <windows.h>
#include <shlobj.h>
#endif

namespace xray {

ConfigManager::ConfigManager(const std::string& config_path)
    : config_path_(config_path), configured_(false) {

    openrouter_.api_base = "https://openrouter.ai/api/v1";
    openrouter_.model = "nvidia/nemotron-3-nano-omni-30b-a3b-reasoning:free";
    openrouter_.timeout_ms = 60000;
    openrouter_.max_tokens = 1500;
    openrouter_.system_prompt =
        "You are SysAnalyst AI, an elite, trustworthy local hardware diagnostic agent. "
        "You will receive a JSON object containing raw PC hardware metrics and critical system logs. "
        "YOUR RULES: "
        "1. Analyze the JSON for critical anomalies. "
        "2. If problems are found, format using Scenario A template. "
        "3. If healthy, use Scenario B template. "
        "4. Do not make up fake diagnostics. "
        "5. Keep responses concise and actionable.";

    thresholds_ = DiagnosticThresholds{
        85.0, 75.0,   // cpu_temp_critical_c, cpu_temp_warning_c
        90.0, 80.0,   // gpu_temp_critical_c, gpu_temp_warning_c
        2.0,          // ram_available_min_gb
        {"BAD_SECTORS", "FAILURE", "CRITICAL"},  // disk_smart_bad_statuses
        70.0          // battery_health_warning_pct
    };
}

bool ConfigManager::create_config_directory() const {
    std::string dir = config_path_;
    size_t pos = dir.find_last_of("\\/");
    if (pos != std::string::npos) {
        dir = dir.substr(0, pos);
    }

    DWORD attr = GetFileAttributesA(dir.c_str());
    if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY)) {
        return true;
    }

    return CreateDirectoryA(dir.c_str(), NULL) != 0 ||
           GetLastError() == ERROR_ALREADY_EXISTS;
}

std::string ConfigManager::get_api_key_from_env() const {
    const char* env_key = std::getenv("XRAY_OPENROUTER_API_KEY");
    if (env_key && std::strlen(env_key) > 0) {
        return std::string(env_key);
    }
    const char* env_key2 = std::getenv("OPENROUTER_API_KEY");
    if (env_key2 && std::strlen(env_key2) > 0) {
        return std::string(env_key2);
    }
    return "";
}

std::string ConfigManager::get_api_key_from_file() const {
    std::ifstream f(config_path_);
    if (!f.is_open()) return "";
    std::stringstream ss;
    ss << f.rdbuf();
    std::string line;
    while (std::getline(ss, line)) {
        auto pos = line.find("api_key");
        if (pos != std::string::npos) {
            auto first_q = line.find('"', pos + 7);
            if (first_q != std::string::npos) {
                auto second_q = line.find('"', first_q + 1);
                if (second_q != std::string::npos) {
                    return line.substr(first_q + 1, second_q - first_q - 1);
                }
            }
        }
    }
    return "";
}

bool ConfigManager::load() {
    std::string key = get_api_key_from_env();
    if (key.empty()) {
        key = get_api_key_from_file();
    }

    if (key.empty()) {
        configured_ = false;
        return false;
    }

    openrouter_.api_key = key;
    configured_ = true;
    return true;
}

bool ConfigManager::is_configured() const {
    return configured_;
}

std::optional<OpenRouterConfig> ConfigManager::get_openrouter_config() const {
    if (!configured_) return std::nullopt;
    return openrouter_;
}

std::optional<DiagnosticThresholds> ConfigManager::get_thresholds() const {
    if (!configured_) return std::nullopt;
    return thresholds_;
}

std::string ConfigManager::get_api_key() const {
    return openrouter_.api_key;
}

bool ConfigManager::save(const OpenRouterConfig& config) {
    if (!create_config_directory()) {
        return false;
    }

    std::ofstream f(config_path_);
    if (!f.is_open()) {
        return false;
    }

    f << "{\n";
    f << "  \"api_key\": \"" << config.api_key << "\",\n";
    f << "  \"model\": \"" << config.model << "\",\n";
    f << "  \"api_base\": \"" << config.api_base << "\",\n";
    f << "  \"timeout_ms\": " << config.timeout_ms << ",\n";
    f << "  \"max_tokens\": " << config.max_tokens << "\n";
    f << "}\n";
    f.close();

    openrouter_ = config;
    configured_ = true;
    return true;
}

}
