#ifndef XRAY_CONFIG_HPP
#define XRAY_CONFIG_HPP

#include "types.hpp"
#include <string>
#include <optional>

namespace xray {

class ConfigManager {
public:
    ConfigManager(const std::string& config_path = "C:\\Users\\paude\\.config\\xray\\config.json");

    bool load();

    bool is_configured() const;
    std::optional<OpenRouterConfig> get_openrouter_config() const;
    std::optional<DiagnosticThresholds> get_thresholds() const;
    std::string get_api_key() const;

    bool save(const OpenRouterConfig& config);

private:
    std::string config_path_;
    bool configured_;
    OpenRouterConfig openrouter_;
    DiagnosticThresholds thresholds_;

    bool create_config_directory() const;
    std::string get_api_key_from_env() const;
    std::string get_api_key_from_file() const;
};

}

#endif
