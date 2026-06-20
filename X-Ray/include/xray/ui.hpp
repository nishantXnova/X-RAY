#ifndef XRAY_UI_HPP
#define XRAY_UI_HPP

#include "types.hpp"
#include <string>

namespace xray {

class ConsoleUI {
public:
    struct UIContext {
        bool running;
        bool colored_output;
    };

    ConsoleUI(bool colored = true);
    ~ConsoleUI();

    void clear_screen() const;
    void print_banner() const;
    void print_version() const;

    void print_step(const std::string& title, const std::string& detail) const;
    void print_status(const std::string& message, bool success = true) const;
    void print_warning(const std::string& message) const;
    void print_error(const std::string& message) const;
    void print_diagnostic_report(const DiagnosticReport& report) const;
    void print_llm_response(const std::optional<std::string>& response,
                            const std::string& error = "") const;

private:
    bool colored_;
    void print_colored(const std::string& text, int color_code) const;
    void reset_color() const;
};

}

#endif
