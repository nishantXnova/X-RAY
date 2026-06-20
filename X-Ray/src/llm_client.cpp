#include "xray/llm_client.hpp"
#include "xray/collector.hpp"
#include "xray/types.hpp"
#include "xray/ui.hpp"

#include <string>
#include <vector>
#include <optional>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <thread>

#ifndef NOMINMAX
#define NOMINMAX
#endif

#if defined(_WIN32)
#include <windows.h>
#include <winhttp.h>
#include <shellapi.h>
#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "shell32.lib")
#endif

namespace xray {

// ---------------------------------------------------------------------------
// OpenRouterClient
// ---------------------------------------------------------------------------

OpenRouterClient::OpenRouterClient(const OpenRouterConfig& config)
    : config_(config) {}

OpenRouterClient::~OpenRouterClient() = default;

bool OpenRouterClient::is_configured() const {
    bool has_placeholder =
        config_.api_key.size() >= 6 &&
        config_.api_key.substr(0, 6) == "sk-...";
    bool has_template =
        config_.api_key.size() >= 5 &&
        config_.api_key.substr(0, 5) == "YOUR_";
    return !config_.api_key.empty() && !has_placeholder && !has_template;
}

std::optional<std::string> OpenRouterClient::get_api_key() const {
    if (api_key_override_.has_value() && !api_key_override_->empty()) {
        return api_key_override_;
    }
    if (!config_.api_key.empty()) {
        return config_.api_key;
    }
    return std::nullopt;
}

std::optional<std::string> OpenRouterClient::get_model() const {
    return config_.model;
}
void OpenRouterClient::set_model(const std::string& model) {
    config_.model = model;
}

int OpenRouterClient::get_timeout_ms() const {
    return config_.timeout_ms;
}
void OpenRouterClient::set_timeout_ms(int ms) {
    config_.timeout_ms = ms;
}

int OpenRouterClient::get_max_tokens() const {
    return config_.max_tokens;
}
void OpenRouterClient::set_max_tokens(int tokens) {
    config_.max_tokens = tokens;
}

std::string OpenRouterClient::get_system_prompt() const {
    return config_.system_prompt;
}
void OpenRouterClient::set_system_prompt(const std::string& prompt) {
    config_.system_prompt = prompt;
}

// ---------------------------------------------------------------------------
// JSON body builder (no external library)
// ---------------------------------------------------------------------------

std::string OpenRouterClient::build_request_body(const std::string& user_message) const {
    std::string body;

    auto esc = [](const std::string& s) -> std::string {
        std::string o;
        o.reserve(s.size() + 10);
        for (char c : s) {
            if (c == '"')  o += "\\\"";
            else if (c == '\\') o += "\\\\";
            else if (c == '\n') o += "\\n";
            else if (c == '\r') o += "\\r";
            else if (c == '\t') o += "\\t";
            else o += c;
        }
        return o;
    };

    body += "{\n";
    body += "  \"model\": \"" + esc(config_.model) + "\",\n";
    body += "  \"max_tokens\": " + std::to_string(config_.max_tokens) + ",\n";

    body += "  \"messages\": [\n";
    body += "    {\"role\": \"system\", \"content\": \"" + esc(config_.system_prompt) + "\"},\n";
    body += "    {\"role\": \"user\", \"content\": \"";
    body += esc(user_message);
    body += "\"}\n";
    body += "  ]\n";
    body += "}\n";

    return body;
}

std::string OpenRouterClient::build_api_url() const {
    return config_.api_base + "/chat/completions";
}

// ---------------------------------------------------------------------------
// Parse raw JSON response to extract content from last assistant message
// ---------------------------------------------------------------------------

std::string OpenRouterClient::extract_response_body(const std::string& raw) const {
    size_t msg_pos = raw.rfind("\"role\":\"assistant\"");
    if (msg_pos != std::string::npos) {
        size_t content_pos = raw.find("\"content\":\"", msg_pos);
        if (content_pos != std::string::npos) {
            content_pos += std::string("\"content\":\"").size();
            std::string result;
            bool esc = false;
            for (size_t i = content_pos; i < raw.size(); ++i) {
                char c = raw[i];
                if (esc) {
                    if (c == 'n') result += '\n';
                    else if (c == 't') result += '\t';
                    else if (c == '\\') result += '\\';
                    else if (c == '"') result += '"';
                    else result += c;
                    esc = false;
                } else if (c == '\\') {
                    esc = true;
                } else if (c == '"') {
                    break;
                } else {
                    result += c;
                }
            }
            return result;
        }
    }

    size_t cpos = raw.find("\"content\":\"");
    if (cpos != std::string::npos) {
        cpos += std::string("\"content\":\"").size();
        std::string result;
        bool esc = false;
        for (size_t i = cpos; i < raw.size(); ++i) {
            char c = raw[i];
            if (esc) {
                if (c == 'n') result += '\n';
                else if (c == 't') result += '\t';
                else if (c == '\\') result += '\\';
                else if (c == '"') result += '"';
                else result += c;
                esc = false;
            } else if (c == '\\') {
                esc = true;
            } else if (c == '"') {
                break;
            } else {
                result += c;
            }
        }
        return result;
    }
    return "";
}

// ---------------------------------------------------------------------------
// Low-level WinHTTP call (public: forward declared in llm_client.hpp)
// ---------------------------------------------------------------------------

LLMResponse OpenRouterClient::send_winhttp(const std::string& url, const std::string& body) {
    LLMResponse resp;
    resp.success = false;

    auto api_key_opt = get_api_key();
    if (!api_key_opt.has_value() || api_key_opt->empty()) {
        resp.error_message = "No OpenRouter API key configured.";
        return resp;
    }

    std::string api_key = *api_key_opt;

    std::string host, path;
    bool https = true;
    std::string stripped = url;

    if (stripped.size() >= 8 && stripped.substr(0, 8) == "https://") {
        host = stripped.substr(8);
        https = true;
    } else if (stripped.size() >= 7 && stripped.substr(0, 7) == "http://") {
        host = stripped.substr(7);
        https = false;
    } else {
        resp.error_message = "Invalid URL (must be http/https): " + url;
        return resp;
    }

    size_t slash_pos = host.find('/');
    if (slash_pos != std::string::npos) {
        path = host.substr(slash_pos);
        host = host.substr(0, slash_pos);
    } else {
        path = "/";
    }

    HINTERNET session = WinHttpOpen(
        L"X-Ray-Diagnostic-Agent/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS, 0);

    if (!session) {
        resp.error_message = "WinHttpOpen failed.";
        return resp;
    }

    int timeout_ms = config_.timeout_ms > 0 ? config_.timeout_ms : 60000;
    WinHttpSetTimeouts(session, timeout_ms, timeout_ms, timeout_ms, timeout_ms);

    HINTERNET connect = WinHttpConnect(
        session,
        std::wstring(host.begin(), host.end()).c_str(),
        https ? INTERNET_DEFAULT_HTTPS_PORT : INTERNET_DEFAULT_HTTP_PORT,
        0);

    if (!connect) {
        resp.error_message = "WinHttpConnect failed. Error: " + std::to_string(GetLastError());
        WinHttpCloseHandle(session);
        return resp;
    }

    HINTERNET request = WinHttpOpenRequest(
        connect,
        L"POST",
        std::wstring(path.begin(), path.end()).c_str(),
        NULL,
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        https ? WINHTTP_FLAG_SECURE : 0);

    if (!request) {
        resp.error_message = "WinHttpOpenRequest failed. Error: " + std::to_string(GetLastError());
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return resp;
    }

    std::wstring auth_header = L"Authorization: Bearer " +
                              std::wstring(api_key.begin(), api_key.end()) + L"\r\n";
    std::wstring content_header = L"Content-Type: application/json\r\n";

    BOOL result = WinHttpSendRequest(
        request,
        (auth_header + content_header).c_str(),
        (DWORD)(auth_header.length() + content_header.length()),
        (LPVOID)body.data(),
        (DWORD)body.length(),
        (DWORD)body.length(),
        0);

    if (!result) {
        resp.error_message = "WinHttpSendRequest failed. Error: " + std::to_string(GetLastError());
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return resp;
    }

    result = WinHttpReceiveResponse(request, NULL);
    if (!result) {
        resp.error_message = "WinHttpReceiveResponse failed. Error: " + std::to_string(GetLastError());
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return resp;
    }

    std::string response_data;
    DWORD bytes_avail = 0, bytes_read = 0;

    do {
        bytes_avail = 0;
        if (!WinHttpQueryDataAvailable(request, &bytes_avail)) break;
        if (bytes_avail == 0) break;

        std::vector<char> buf(bytes_avail + 1);
        if (WinHttpReadData(request, buf.data(), bytes_avail, &bytes_read)) {
            buf[bytes_read] = '\0';
            response_data.append(buf.data(), bytes_read);
        }
    } while (bytes_avail > 0);

    DWORD status_code = 0;
    DWORD status_len = sizeof(status_code);
    WinHttpQueryHeaders(request,
                       WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                       WINHTTP_HEADER_NAME_BY_INDEX,
                       &status_code, &status_len,
                       WINHTTP_NO_HEADER_INDEX);

    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);

    if (status_code != 200) {
        resp.error_message = "HTTP " + std::to_string(status_code) + ": " +
                            response_data.substr(0, 500);
        return resp;
    }

    if (response_data.empty()) {
        resp.error_message = "Received empty response from API.";
        return resp;
    }

    resp.raw_response = extract_response_body(response_data);
    resp.success = !resp.raw_response.empty();

    if (!resp.success) {
        resp.error_message = "Could not parse content from response. Raw: " +
                            response_data.substr(0, 300);
    }

    return resp;
}

// ---------------------------------------------------------------------------
// High-level diagnose call (from hardware data)
// ---------------------------------------------------------------------------

LLMResponse OpenRouterClient::diagnose(const HardwareData& hw,
                                       const std::vector<DiagnosticFinding>& local_findings,
                                       const DiagnosticSeverity& local_severity) {
    std::stringstream payload;
    payload << "Hardware Diagnostic Data:\n\n";
    payload << "{";
    payload << "\"cpu\":{\"usage_percent\":" << (int)hw.cpu.usage_percent
            << ",\"temp_celsius\":" << (int)hw.cpu.temp_celsius
            << ",\"frequency_ghz\":" << (int)hw.cpu.frequency_ghz << "},";
    payload << "\"gpu\":{\"load_percent\":" << (int)hw.gpu.load_percent
            << ",\"vram_used_mb\":" << (int)hw.gpu.vram_used_mb << "},";
    payload << "\"ram\":{\"total_gb\":" << (int)(hw.ram.total_gb * 10) / 10.0
            << ",\"available_gb\":" << (int)(hw.ram.available_gb * 10) / 10.0
            << ",\"possible_leak\":" << (hw.ram.possible_leak ? "true" : "false") << "},";
    if (!hw.disks.empty()) {
        payload << "\"disk\":{\"smart_status\":\"" << hw.disks[0].smart_status
                << "\",\"read_speed_mb\":" << (int)hw.disks[0].read_speed_mb << "},";
    }
    payload << "\"battery\":{\"health_percent\":" << (int)hw.battery.health_percent
            << ",\"is_charging\":" << (hw.battery.is_charging ? "true" : "false") << "}";
    payload << "}";

    std::string endpoint = build_api_url();
    std::string body = build_request_body(payload.str());
    return send_winhttp(endpoint, body);
}

LLMResponse OpenRouterClient::diagnose(const std::string& json_payload) {
    std::string endpoint = build_api_url();
    std::string body = build_request_body(json_payload);
    return send_winhttp(endpoint, body);
}

}
