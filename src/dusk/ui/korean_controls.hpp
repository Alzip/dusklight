#pragma once

#include "bool_button.hpp"
#include "korean.hpp"

namespace dusk::ui {

// Opt-in display adapter for built-in settings only. The original getters,
// setters, overrides, disabled state and input handling remain in BoolButton.
class KoreanBoolButton final : public BoolButton {
public:
    using BoolButton::BoolButton;

protected:
    Rml::String format_value() override {
        auto value = BoolButton::format_value();
        if (value == "On") {
            return korean::text("On");
        }
        if (value == "Off") {
            return korean::text("Off");
        }
        return value;
    }
};

}  // namespace dusk::ui
