#include "xray/collector.hpp"
#include "xray/config_manager.hpp"
#include "xray/llm_client.hpp"
#include "xray/ui.hpp"
#include "xray/types.hpp"
#include "xray/version.hpp"

#include <iostream>
#include <string>
#include <vector>
#include <tuple>
#include <chrono>
#include <thread>
#include <iomanip>
#include <sstream>

#ifndef NOMINMAX
#define NOMINMAX
#endif

#if defined(_WIN32)
#include <windows.h>
#endif

namespace xray {

// ---------------------------------------------------------------------------
// Args parsing
// ---------------------------------------------------------------------------

struct AppArgs {
    bool show_help = false;
    bool show_version = false;
    bool diagnose = false;
    bool offline = false;
    bool json_output = false;
    int delay_seconds = 0;
    std::string api_key;
    std::string model;
};

static void print_usage(const char* prog) {
    std::cout << "\nUsage: " << prog << " [OPTIONS]\n\n";
    std::cout << "  X-RAY — AI Hardware Diagnostic Agent\n\n";
    std::cout << "  --diagnose           Run full diagnosis (default)\n";
    std::cout << "  --offline            Run built-in diagnostics only\n";
    std::cout << "  --api-key <key>      Override OpenRouter API key\n";
    std::cout << "  --model <name>       Override LLM model\n";
    std::cout << "  --json               Output results as JSON\n";
    std::cout << "  --version, -v        Show version and exit\n";
    std::cout << "  --help, -h           Show this help and exit\n\n";
}

static AppArgs parse_args(int argc, char* argv[]) {
    AppArgs args;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "--help" || arg == "-h") {
            args.show_help = true;
        } else if (arg == "--version" || arg == "-v") {
            args.show_version = true;
        } else if (arg == "--diagnose") {
            args.diagnose = true;
        } else if (arg == "--offline") {
            args.offline = true;
        } else if (arg == "--json") {
            args.json_output = true;
        } else if (arg == "--api-key" && i + 1 < argc) {
            args.api_key = argv[++i];
        } else if (arg == "--model" && i + 1 < argc) {
            args.model = argv[++i];
        } else if (arg.size() > 10 && arg.substr(0, 10) == "--api-key=") {
            args.api_key = arg.substr(10);
        } else if (arg.size() > 8 && arg.substr(0, 8) == "--model=") {
            args.model = arg.substr(8);
        } else if (arg.size() > 8 && arg.substr(0, 8) == "--delay=") {
            args.delay_seconds = std::stoi(arg.substr(8));
        }
    }

    if (!args.show_help && !args.show_version) {
        args.diagnose = true;
    }
    return args;
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

int run(const AppArgs& args) {
    ConsoleUI ui(!args.json_output);

    if (args.show_version) {
        std::cout << XRAY_PROJECT_NAME << " v" << XRAY_VERSION_STRING << "\n";
        return 0;
    }

    if (args.show_help) {
        print_usage("xray");
        return 0;
    }

    if (args.delay_seconds > 0) {
        std::this_thread::sleep_for(std::chrono::seconds(args.delay_seconds));
    }

    ui.clear_screen();
    ui.print_banner();

    if (args.offline) {
        ui.print_step("Offline Mode", "Using built-in diagnostics only.");
    } else {
        ui.print_step("Configuring OpenRouter LLM API", "Loading API key from environment or config file.");
    }

#if defined(_WIN32)
    HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    bool com_initialized = SUCCEEDED(hr);
    if (!com_initialized) {
        ui.print_warning("COM initialization failed. WMI-based features unavailable.");
    }
#endif

    ui.print_step("Collecting hardware metrics...", "Reading PDH, WMI, and system power info.");
    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    HardwareData hw = collect_all();
    if (hw.cpu.temp_celsius < 0.0) {
        ui.print_warning("CPU temperature unavailable via PDH; using estimated value.");
    }

    ui.print_status("Hardware data collection complete.");

    ConfigManager cfg("C:\\Users\\paude\\OneDrive\\Documents\\MUX switch prototype\\X-Ray\\Config\\config.json");
    bool cfg_loaded = cfg.load();
    DiagnosticThresholds thresholds;
    if (cfg_loaded) {
        auto t_opt = cfg.get_thresholds();
        if (t_opt.has_value()) thresholds = *t_opt;
    } else {
        thresholds = DiagnosticThresholds{85.0, 75.0, 90.0, 80.0, 2.0,
                                          {"BAD_SECTORS", "FAILURE", "CRITICAL"}, 70.0};
    }

    ui.print_step("Analyzing metrics for anomalies...", "Built-in rule-based pass.");
    DiagnosticReport local_report = run_builtin_diagnostics(hw, thresholds);

    ui.print_status("Built-in analysis complete. Severity: " + severity_to_string(local_report.overall_severity));

    if (args.json_output) {
        std::cout << "{\n";
        std::cout << "  \"severity\": \"" << severity_to_string(local_report.overall_severity) << "\",\n";
        std::cout << "  \"findings\": [\n";
        for (size_t i = 0; i < local_report.findings.size(); ++i) {
            const auto& f = local_report.findings[i];
            std::cout << "    {\n";
            std::cout << "      \"severity\": \"" << severity_to_string(f.severity) << "\",\n";
            std::cout << "      \"component\": \"" << f.component << "\",\n";
            std::cout << "      \"description\": \"" << f.description.substr(0, 200) << "\",\n";
            std::cout << "      \"safe_fixes\": [\n";
            for (size_t j = 0; j < f.safe_fixes.size(); ++j) {
                std::cout << "        \"" << f.safe_fixes[j] << "\"";
                if (j + 1 < f.safe_fixes.size()) std::cout << ",";
                std::cout << "\n";
            }
            std::cout << "      ],\n";
            std::cout << "      \"scam_warning\": \"" << f.scam_warning.substr(0, 200) << "\"\n";
            std::cout << "    }";
            if (i + 1 < local_report.findings.size()) std::cout << ",";
            std::cout << "\n";
        }
        std::cout << "  ]\n";
        std::cout << "}\n";
    } else {
        ui.print_diagnostic_report(local_report);
    }

    // Step 3: LLM enrichment
    std::optional<LLMResponse> llm_response;
    if (!args.offline) {
        OpenRouterConfig or_cfg;
        or_cfg.model = args.model.empty() ? "nvidia/nemotron-3-nano-omni-30b-a3b-reasoning:free" : args.model;

        if (cfg_loaded) {
            auto oc = cfg.get_openrouter_config();
            if (oc.has_value()) {
                or_cfg = *oc;
                if (!args.model.empty()) or_cfg.model = args.model;
            }
        }

        if (!args.api_key.empty()) {
            or_cfg.api_key = args.api_key;
        }

        ui.print_step("Analyzing with AI Agent (OpenRouter)", "Model: " + or_cfg.model);

        OpenRouterClient client(or_cfg);

        if (!client.is_configured()) {
            std::cout << "\n";
            ui.print_warning(
                "OpenRouter API key not configured. Skipping LLM enrichment.\n"
                "  Set env var XRAY_OPENROUTER_API_KEY or OPENROUTER_API_KEY,\n"
                "  or run: xray --offline\n"
            );
        } else {
            ui.print_status("LLM client initialized.");

            std::stringstream payload_ss;
            payload_ss << "{\"cpu\":{\"usage_percent\":" << (int)hw.cpu.usage_percent
                       << ",\"temp_celsius\":" << (int)hw.cpu.temp_celsius
                       << "}, \"ram\":{\"available_gb\":" << (int)(hw.ram.available_gb * 10) / 10.0
                       << "}, \"battery\":{\"health_percent\":" << (int)hw.battery.health_percent
                       << ",\"is_charging\":" << (hw.battery.is_charging ? "true" : "false") << "}}";

            LLMResponse r = client.diagnose(payload_ss.str());
            llm_response = r;

            if (args.json_output) {
                std::cout << "{\n  \"llm_response\": {\n";
                std::cout << "    \"success\": " << (r.success ? "true" : "false") << ",\n";
                std::cout << "    \"error\": \"" << r.error_message << "\",\n";
                std::cout << "    \"raw_response\": \"" << r.raw_response.substr(0, 500) << "\"\n";
                std::cout << "  }\n}\n";
            } else {
                ui.print_llm_response(r.raw_response, r.error_message);
            }
        }
    }

#if defined(_WIN32)
    if (com_initialized) {
        CoUninitialize();
    }
#endif

    return 0;
}

}

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------

int main(int argc, char* argv[]) {
    xray::AppArgs args = xray::parse_args(argc, argv);
    return xray::run(args);
}
