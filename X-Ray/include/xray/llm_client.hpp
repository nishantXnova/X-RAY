#ifndef XRAY_LLM_CLIENT_HPP
#define XRAY_LLM_CLIENT_HPP

#include "types.hpp"
#include <string>
#include <vector>
#include <optional>

namespace xray {

struct LLMResponse {
    bool success;
    std::string raw_response;
    std::string error_message;
};

class OpenRouterClient {
public:
    OpenRouterClient(const OpenRouterConfig& config);
    ~OpenRouterClient();

    bool is_configured() const;
    std::optional<std::string> get_api_key() const;

    std::optional<std::string> get_model() const;
    void set_model(const std::string& model);

    int get_timeout_ms() const;
    void set_timeout_ms(int ms);

    int get_max_tokens() const;
    void set_max_tokens(int tokens);

    std::string get_system_prompt() const;
    void set_system_prompt(const std::string& prompt);

    LLMResponse diagnose(const HardwareData& hw,
                         const std::vector<DiagnosticFinding>& local_findings,
                         const DiagnosticSeverity& local_severity);

    LLMResponse diagnose(const std::string& json_payload);

private:
    OpenRouterConfig config_;
    std::optional<std::string> api_key_override_;

    std::string build_api_url() const;
    std::string build_request_body(const std::string& user_message) const;
    std::string extract_response_body(const std::string& raw_json) const;
    LLMResponse send_winhttp(const std::string& url, const std::string& body);

    bool validate_json_output(const std::string& raw_response,
                              std::string& out_error) const;
};

}

#endif
