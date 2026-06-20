#include "xray/ui.hpp"
#include "xray/version.hpp"
#include "xray/collector.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif

#if defined(_WIN32)
#include <windows.h>
#endif

#include <iostream>
#include <iomanip>
#include <string>
#include <iomanip>
#include <algorithm>

namespace xray {

ConsoleUI::ConsoleUI(bool colored)
    : colored_(colored) {
#if defined(_WIN32)
    colored_ = colored && GetConsoleOutputCP() == CP_UTF8;
#endif
}

ConsoleUI::~ConsoleUI() = default;

void ConsoleUI::clear_screen() const {
#if defined(_WIN32)
    system("cls");
#else
    std::cout << "\033[2J\033[H";
#endif
}

void ConsoleUI::print_colored(const std::string& text, int code) const {
    if (!colored_) {
        std::cout << text;
        return;
    }
#if defined(_WIN32)
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    if (h == INVALID_HANDLE_VALUE) { std::cout << text; return; }
    CONSOLE_SCREEN_BUFFER_INFO info;
    GetConsoleScreenBufferInfo(h, &info);
    SetConsoleTextAttribute(h, (info.wAttributes & 0xF0) | (code & 0x0F));
    std::cout << text;
    SetConsoleTextAttribute(h, info.wAttributes);
#else
    std::cout << "\033[1;" << (30 + (code % 8)) << "m" << text << "\033[0m";
#endif
}

void ConsoleUI::reset_color() const {
#if defined(_WIN32)
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    if (h != INVALID_HANDLE_VALUE) {
        CONSOLE_SCREEN_BUFFER_INFO info;
        GetConsoleScreenBufferInfo(h, &info);
        SetConsoleTextAttribute(h, (info.wAttributes & 0xF0) | 7);
    }
#endif
}

void ConsoleUI::print_banner() const {
    std::cout << "\n";
    print_colored("=====================================================\n", 11);
    print_colored("   X-RAY AI Hardware Diagnostic Agent             \n", 11);
    print_colored("=====================================================\n", 11);
    print_version();
    std::cout << "\n";
}

void ConsoleUI::print_version() const {
    std::cout << "   Version " << XRAY_VERSION_STRING << " | ";
    std::cout << "DiagnosticAgent.cpp\n";
}

void ConsoleUI::print_step(const std::string& title, const std::string& detail) const {
    print_colored("[STEP] ", 11);
    std::cout << title << "\n";
    if (!detail.empty()) {
        std::cout << "       " << detail << "\n";
    }
}

void ConsoleUI::print_status(const std::string& message, bool success) const {
    const int code = success ? 10 : 12;
    print_colored((success ? "[OK]   " : "[FAIL] "), code);
    std::cout << message << "\n";
}

void ConsoleUI::print_warning(const std::string& message) const {
    print_colored("[WARN] ", 14);
    std::cout << message << "\n";
}

void ConsoleUI::print_error(const std::string& message) const {
    print_colored("[ERR]  ", 12);
    std::cout << message << "\n";
}

void ConsoleUI::print_diagnostic_report(const DiagnosticReport& report) const {
    std::cout << "\n";
    print_colored("=========== DIAGNOSTIC REPORT ===========\n", 11);
    std::cout << "\n";

    DiagnosticSeverity top = report.overall_severity;
    std::string emoji = severity_to_emoji(top);

    std::cout << emoji << " Overall Severity: ";
    int sev_color = (top == DiagnosticSeverity::HEALTHY) ? 10 :
                    (top == DiagnosticSeverity::WARNING ? 14 : 12);
    print_colored(severity_to_string(top), sev_color);
    std::cout << "\n\n";

    if (report.findings.empty()) {
        print_colored("No anomalies detected. System is healthy.\n", 10);
    } else {
        int idx = 1;
        for (const auto& f : report.findings) {
            std::cout << emoji << " Finding #" << idx++ << " [";
            print_colored(severity_to_string(f.severity), (int)f.severity + 10);
            std::cout << "] Component: " << f.component << "\n";
            std::cout << "    " << f.description << "\n\n";

            if (!f.safe_fixes.empty()) {
                print_colored("    Safe Fixes:\n", 10);
                int fix_i = 1;
                for (const auto& fix : f.safe_fixes) {
                    std::cout << "      " << fix_i++ << ". " << fix << "\n";
                }
                std::cout << "\n";
            }
            print_colored("    Avoid This: ", 12);
            std::cout << f.scam_warning << "\n";
            std::cout << "\n";
        }
    }

    std::cout << "\n";
    print_colored("==============================================\n", 11);
}

void ConsoleUI::print_llm_response(const std::optional<std::string>& response,
                                   const std::string& error) const {
    std::cout << "\n";
    print_colored("----------- AI Agent Response -----------\n", 13);

    if (error.empty() && response.has_value()) {
        std::cout << *response << "\n";
    } else if (!error.empty()) {
        print_error("LLM Error: " + error);
    } else {
        print_warning("No AI response available.");
    }

    print_colored("------------------------------------------\n\n", 13);
}

}
