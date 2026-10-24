#include "settings.hpp"
#include "korean.hpp"
#include "korean_controls.hpp"

#include "bool_button.hpp"
#include "controller_config.hpp"
#include "graphics_tuner.hpp"
#include "menu_bar.hpp"
#include "modal.hpp"
#include "number_button.hpp"
#include "pane.hpp"
#include "prelaunch.hpp"
#include "saves_window.hpp"
#include "touch_controls_editor.hpp"
#include "ui.hpp"

#include "dusk/app_info.hpp"
#include "dusk/audio/DuskAudioSystem.h"
#include "dusk/audio/DuskDsp.hpp"
#include "dusk/config.hpp"
#include "dusk/data.hpp"
#include "dusk/discord_presence.hpp"
#include "dusk/hotkeys.h"
#include "dusk/imgui/ImGuiEngine.hpp"
#include "dusk/language.hpp"
#include "dusk/livesplit.h"
#include "dusk/presentation.hpp"
#include "dusk/speedrun.h"

#include <aurora/gfx.h>
#include <aurora/lib/window.hpp>
#include <borealis/file_select.hpp>
#include <borealis/io.hpp>
#if BOREALIS_HAS_SENTRY
#include <borealis/sentry.hpp>
#endif
#include <fmt/format.h>
#include <SDL3/SDL_filesystem.h>

#include <algorithm>
#include <filesystem>

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

#if defined(TARGET_ANDROID) || defined(__ANDROID__) ||                                             \
    (defined(__APPLE__) && TARGET_OS_IOS && !TARGET_OS_MACCATALYST)
#define TOUCH_CONTROLS_AVAILABLE true
#else
#define TOUCH_CONTROLS_AVAILABLE false
#endif

namespace dusk::ui {
namespace {

constexpr std::array kCardFileTypes = {
    korean::text("Card Image"),
    korean::text("GCI Folder"),
};

constexpr std::array kFpsOverlayCornerNames = {
    korean::text("Top Left"),
    korean::text("Top Right"),
    korean::text("Bottom Left"),
    korean::text("Bottom Right"),
};

constexpr std::array kInterpolationModes = {
    korean::text("Off"),
    korean::text("Capped"),
    korean::text("Unlimited"),
};

constexpr std::array kAudioOutputModeNames = {
    korean::text("Stereo (Speakers)"),
    korean::text("Stereo (Headphones)"),
    korean::text("5.1 Surround"),
    korean::text("7.1 Surround"),
};

constexpr std::array kLetterboxModes = {
    korean::text("Off"),
    korean::text("On"),
    korean::text("Only During Gameplay"),
    korean::text("Only During Cutscenes"),
};

constexpr std::array kTouchTargetingLabels = {
    korean::text("Hybrid"),
    korean::text("Hold"),
    korean::text("Switch"),
};

constexpr std::array kTouchTargetingDescriptions = {
    korean::text("Tap once to lock on when a target is found. Double-tap when none is found to hold L."),
    korean::text("L stays held only while your finger is on the button."),
    korean::text("Tap L to keep it held. Tap again to release it."),
};

constexpr std::array kGyroInputModeLabels = {
    korean::text("Sensor"),
    korean::text("Mouse"),
};

constexpr std::array kMenuScalingModeLabels = {
    "GameCube",
    "Wii",
    "Dusklight",
};

constexpr std::array kAlwaysGreatspinModes = {
    korean::text("Off"),
    korean::text("After Learning Skill"),
    korean::text("Always"),
};

constexpr std::array kMagicArmorModes = {
    korean::text("Normal"),
    korean::text("On Damage"),
    korean::text("Double Defense"),
    korean::text("Invincible"),
    korean::text("Cosmetic"),
};

bool try_parse_backend(std::string_view backend, AuroraBackend& outBackend) {
    if (backend == "auto") {
        outBackend = BACKEND_AUTO;
        return true;
    }
    if (backend == "d3d11") {
        outBackend = BACKEND_D3D11;
        return true;
    }
    if (backend == "d3d12") {
        outBackend = BACKEND_D3D12;
        return true;
    }
    if (backend == "metal") {
        outBackend = BACKEND_METAL;
        return true;
    }
    if (backend == "vulkan") {
        outBackend = BACKEND_VULKAN;
        return true;
    }
    if (backend == "opengl") {
        outBackend = BACKEND_OPENGL;
        return true;
    }
    if (backend == "opengles") {
        outBackend = BACKEND_OPENGLES;
        return true;
    }
    if (backend == "webgpu") {
        outBackend = BACKEND_WEBGPU;
        return true;
    }
    if (backend == "null") {
        outBackend = BACKEND_NULL;
        return true;
    }

    return false;
}

std::string_view backend_name(AuroraBackend backend) {
    switch (backend) {
    default:
        return korean::text("Auto");
    case BACKEND_D3D12:
        return "D3D12";
    case BACKEND_D3D11:
        return "D3D11";
    case BACKEND_METAL:
        return "Metal";
    case BACKEND_VULKAN:
        return "Vulkan";
    case BACKEND_OPENGL:
        return "OpenGL";
    case BACKEND_OPENGLES:
        return "OpenGL ES";
    case BACKEND_WEBGPU:
        return "WebGPU";
    case BACKEND_NULL:
        return "Null";
    }
}

std::string_view backend_id(AuroraBackend backend) {
    switch (backend) {
    default:
        return "auto";
    case BACKEND_D3D12:
        return "d3d12";
    case BACKEND_D3D11:
        return "d3d11";
    case BACKEND_METAL:
        return "metal";
    case BACKEND_VULKAN:
        return "vulkan";
    case BACKEND_OPENGL:
        return "opengl";
    case BACKEND_OPENGLES:
        return "opengles";
    case BACKEND_WEBGPU:
        return "webgpu";
    case BACKEND_NULL:
        return "null";
    }
}

std::vector<AuroraBackend> available_backends() {
    std::vector<AuroraBackend> backends;
    backends.emplace_back(BACKEND_AUTO);
    size_t backendCount = 0;
    const AuroraBackend* raw = aurora_get_available_backends(&backendCount);
    for (size_t i = 0; i < backendCount; ++i) {
        // Do not expose NULL
        if (raw[i] != BACKEND_NULL) {
            backends.emplace_back(raw[i]);
        }
    }
    return backends;
}

AuroraBackend configured_backend() {
    AuroraBackend configuredBackend = BACKEND_AUTO;
    const auto configuredId = getSettings().backend.graphicsBackend.getValue();
    if (!try_parse_backend(configuredId, configuredBackend)) {
        configuredBackend = BACKEND_AUTO;
    }
    return configuredBackend;
}

bool is_graphics_backend_restart_pending() {
    return getSettings().backend.graphicsBackend.getValue() !=
           prelaunch_state().initialGraphicsBackend;
}

Rml::String graphics_backend_display_name() {
    if (is_graphics_backend_restart_pending()) {
        return Rml::String{backend_name(configured_backend())};
    }
    return Rml::String{backend_name(aurora_get_backend())};
}

Rml::String configured_data_path_display_name() {
    const auto path = data::abbreviated_path_string(data::configured_data_path());
    if (path.empty()) {
        return korean::text("(none)");
    }

    auto display = borealis::io::display_name(path);
    if (display.empty()) {
        return path;
    }
    return display;
}

class DataFolderPathText : public Component {
public:
    explicit DataFolderPathText(Rml::Element* parent)
        : Component(append(parent, "data-folder-path")) {
        append_text_element(mRoot, "small", korean::text("Current data folder:"));
        mPath = append(mRoot, "file-path");
    }

    void update() override {
        const Rml::String path = data::abbreviated_path_string(data::configured_data_path());
        if (path != mCurrentPath) {
            set_text_content(mPath, path);
            mCurrentPath = path;
        }
        Component::update();
    }

private:
    Rml::Element* mPath = nullptr;
    Rml::String mCurrentPath;
};

void show_data_folder_error_modal(std::string_view message) {
    auto dismiss = [](Modal& modal) {
        mDoAud_seStartMenu(kSoundWindowClose);
        modal.pop();
    };
    push_document(std::make_unique<Modal>(Modal::Props{
        .title = korean::text("Data Folder Not Changed"),
        .bodyText = Rml::String{message},
        .actions =
            {
                ModalAction{
                    .label = korean::text("OK"),
                    .onPressed = dismiss,
                },
            },
        .onDismiss = dismiss,
        .icon = "warning",
    }));
    if (auto* doc = top_document()) {
        doc->focus();
    }
}

void data_folder_dialog_callback(borealis::file_select::Result result) {
    if (result.status == borealis::file_select::Status::Canceled) {
        return;
    }
    if (result.status != borealis::file_select::Status::Selected || result.locations.empty()) {
        show_data_folder_error_modal(korean::text("Dusklight could not open the folder picker."));
        return;
    }

    std::string dataPathError;
    if (data::set_custom_data_path(result.locations.front(), &dataPathError)) {
        mDoAud_seStartMenu(kSoundItemChange);
        return;
    }

    if (dataPathError.empty()) {
        dataPathError =
            fmt::format(korean::text("{} could not use the selected folder as its data folder."), AppName);
    }
    show_data_folder_error_modal(dataPathError);
}

const Rml::String kInternalResolutionHelpText =
    korean::text("Configure the resolution used for rendering the game. Higher values are more demanding on "
    "your graphics hardware.");
const Rml::String kShadowResolutionHelpText =
    korean::text("Configure the shadow-map resolution. Higher values improve shadow quality but increase GPU "
    "and memory usage.");
const Rml::String kResamplerHelpText =
    korean::text("Configure the sampling method used when scaling the internal resolution for final presentation.");
const Rml::String kBloomHelpText =
    korean::text("Configure the post-processing bloom effect. Classic uses the original bloom pass; Dusklight uses "
    "a higher-quality bloom pass.");
const Rml::String kBloomBrightnessHelpText =
    korean::text("Configure bloom intensity. Higher values make bright areas glow more strongly.");
const Rml::String kDepthOfFieldHelpText =
    korean::text("Configure the post-processing depth-of-field effect. Classic uses the original depth-of-field pass;"
    " Dusklight uses a higher-quality depth-of-field pass.");
const Rml::String kUnlockFramerateHelpText =
    korean::text("<br/>Uses inter-frame interpolation to enable higher frame rates.<br/><br/>May introduce minor "
    "visual artifacts or animation glitches.");
const Rml::String kTextureReplacementHelpText =
    korean::text("Enable installed texture replacements.");

int float_setting_percent(ConfigVar<float>& var) {
    return static_cast<int>(var.getValue() * 100.0f + 0.5f);
}

bool gyro_enabled() {
    return getSettings().game.enableGyroAim || getSettings().game.enableGyroRollgoal;
}

Rml::String touch_targeting_label(TouchTargeting targeting) {
    const auto index = static_cast<size_t>(targeting);
    if (index >= kTouchTargetingLabels.size()) {
        return korean::text("Unknown");
    }
    return kTouchTargetingLabels[index];
}

struct ConfigBoolProps {
    Rml::String key;
    Rml::String icon;
    Rml::String helpText;
    std::function<void(bool)> onChange;
    std::function<bool()> isDisabled;
};

SelectButton& config_bool_select(
    Pane& leftPane, Pane& rightPane, ConfigVar<bool>& var, ConfigBoolProps props) {
    auto& button = leftPane.add_child<KoreanBoolButton>(BoolButton::Props{
        .key = std::move(props.key),
        .icon = std::move(props.icon),
        .getValue = [&var] { return var.getValue(); },
        .setValue =
            [&var, callback = std::move(props.onChange)](bool value) {
                if (value == var.getValue()) {
                    return;
                }
                var.setValue(value);
                config::save();
                if (callback) {
                    callback(value);
                }
            },
        .isDisabled = std::move(props.isDisabled),
        .isModified = [&var] { return var.getValue() != var.getDefaultValue(); },
    });
    leftPane.register_control(
        button, rightPane, [helpText = std::move(props.helpText)](Pane& pane) {
            pane.clear();
            pane.add_rml(helpText);
        });
    return button;
}

void add_speedrun_disabled_option(Pane& leftPane, Pane& rightPane, ConfigVar<bool>& var,
    const Rml::String& key, const Rml::String& helpText) {
    config_bool_select(leftPane, rightPane, var, {
        .key = key,
        .helpText = helpText,
        .isDisabled = [] { return speedrun::isActive(); },
    });
}

SelectButton& config_percent_select(Pane& leftPane, Pane& rightPane, ConfigVar<float>& var,
    Rml::String key, Rml::String helpText, int min, int max, int step = 5,
    std::function<bool()> isDisabled = {}) {
    auto& button = leftPane.add_child<NumberButton>(NumberButton::Props{
        .key = std::move(key),
        .getValue = [&var] { return float_setting_percent(var); },
        .setValue =
            [&var, min, max](int value) {
                var.setValue(std::clamp(value, min, max) / 100.0f);
                config::save();
            },
        .isDisabled = std::move(isDisabled),
        .isModified = [&var] { return var.getValue() != var.getDefaultValue(); },
        .min = min,
        .max = max,
        .step = step,
        .suffix = "%",
    });
    leftPane.register_control(button, rightPane, [helpText = std::move(helpText)](Pane& pane) {
        pane.clear();
        pane.add_rml(helpText);
    });
    return button;
}

SelectButton& config_int_select(Pane& leftPane, Pane& rightPane, ConfigVar<int>& var,
    Rml::String key, Rml::String helpText, int min, int max, int step = 5,
    std::function<bool()> isDisabled = {}, std::function<void(int)> onChange = {},
    std::string suffix = "") {
    auto& button = leftPane.add_child<NumberButton>(NumberButton::Props{
        .key = std::move(key),
        .getValue = [&var] { return var.getValue(); },
        .setValue =
            [&var, min, max, callback = std::move(onChange)](int value) {
                const int clampedValue = std::clamp(value, min, max);
                var.setValue(clampedValue);
                config::save();
                if (callback) {
                    callback(clampedValue);
                }
            },
        .isDisabled = std::move(isDisabled),
        .isModified = [&var] { return var.getValue() != var.getDefaultValue(); },
        .min = min,
        .max = max,
        .step = step,
        .suffix = suffix,
    });
    leftPane.register_control(button, rightPane, [helpText = std::move(helpText)](Pane& pane) {
        pane.clear();
        pane.add_text(helpText);
    });
    return button;
}

void graphics_tuner_control(Window& window, Pane& leftPane, Pane& rightPane,
    const GraphicsTunerProps& props) {
    const auto setting = GraphicsSetting::of(props.option);
    leftPane.register_control(
        leftPane
            .add_select_button({
                .key = props.title,
                .getValue = [setting] { return setting.text(); },
                .isModified = [setting] { return setting.isModified(); },
                .submit = false,
            })
            .on_nav_command([&window, props](Rml::Event&, NavCommand cmd) {
                if (cmd == NavCommand::Confirm || cmd == NavCommand::Left ||
                    cmd == NavCommand::Right) {
                    window.push(std::make_unique<GraphicsTuner>(props));
                    return true;
                }
                return false;
            }),
        rightPane, [helpText = props.helpText](Pane& pane) {
            pane.clear();
            pane.add_text(helpText);
        });
}

}  // namespace

SettingsWindow::SettingsWindow(bool prelaunch) : mPrelaunch(prelaunch) {
    if (prelaunch) {
        add_tab("Prelaunch", [this](Rml::Element* content) {
            auto& leftPane = add_child<Pane>(content, Pane::Type::Controlled);
            auto& rightPane = add_child<Pane>(content, Pane::Type::Uncontrolled);

            leftPane.register_control(
                leftPane
                    .add_select_button({
                        .key = korean::text("Disc Image"),
                        .getValue =
                            [] {
                                const auto& path = prelaunch_state().configuredDiscPath;
                                std::string display;
                                if (path.empty()) {
                                    display = korean::text("(none)");
                                } else {
                                    display = borealis::io::display_name(path);
                                    if (display.empty()) {
                                        display = path;
                                    }
                                }
                                return display;
                            },
                        .isModified =
                            [] {
                                const auto& state = prelaunch_state();
                                const auto& active = state.activeDiscPath;
                                return !active.empty() && state.configuredDiscPath != active;
                            },
                    })
                    .on_pressed([] { open_iso_picker(); }),
                rightPane, [](Pane& pane) {
                    pane.add_rml(korean::text("Set the disc image that Dusklight uses to launch the game.<br/><br/>"
                                 "Changes require a restart."));
                });
            if (data::manager().capabilities().canChangeLocation &&
                borealis::file_select::capabilities().canOpenFolder)
            {
                leftPane.register_control(
                    leftPane.add_select_button({
                        .key = korean::text("Data Folder"),
                        .getValue = [] { return configured_data_path_display_name(); },
                        .isModified = [] { return data::is_data_path_restart_pending(); },
                    }),
                    rightPane, [](Pane& pane) {
                        pane.add_text(korean::text("The data folder is where Dusklight stores settings, saves, "
                                      "logs, texture replacements, and other app data."));
                        pane.add_child<DataFolderPathText>();
#if DUSK_CAN_OPEN_DATA_FOLDER
                        pane.add_button(korean::text("Open Data Folder")).on_pressed([] {
                            if (data::open_data_path()) {
                                mDoAud_seStartMenu(kSoundClick);
                            }
                        });
#endif
                        pane.add_button(korean::text("Change Data Folder")).on_pressed([] {
                            const auto defaultLocation =
                                borealis::io::fs_path_to_string(data::configured_data_path());
                            borealis::file_select::open_folder(
                                {
                                    .parentWindow = aurora::window::get_sdl_window(),
                                    .defaultLocation = defaultLocation,
                                    .requireRealPath = true,
                                },
                                &data_folder_dialog_callback);
                        });
#if defined(_WIN32)
                        pane.add_button(korean::text("Portable Mode")).on_pressed([] {
                            if (data::set_portable_data_path()) {
                                mDoAud_seStartMenu(kSoundItemChange);
                            }
                        });
#endif
                        pane.add_button(
                                {
                                    .text = korean::text("Reset to Default"),
                                    .isDisabled = [] { return data::is_default_data_path(); },
                                })
                            .on_pressed([] {
                                if (data::reset_data_path()) {
                                    mDoAud_seStartMenu(kSoundItemChange);
                                }
                            });
                        pane.add_rml(korean::text("Data will be migrated automatically on restart."));
                    });
            }
            leftPane.register_control(
                leftPane.add_select_button({
                    .key = korean::text("Language"),
                    .getValue =
                        [] {
                            return language::language_name(getSettings().game.language.getValue());
                        },
                    .isDisabled =
                        [] {
                            const auto& state = prelaunch_state();
                            if (!state.configuredDiscCanLaunch) {
                                return true;
                            }
                            return language::available_languages(state.configuredDiscInfo).size() <= 1;
                        },
                    .isModified =
                        [] {
                            return getSettings().game.language.getValue() !=
                                   prelaunch_state().initialLanguage;
                        },
                }),
                rightPane, [](Pane& pane) {
                    const auto& state = prelaunch_state();
                    const auto languages = state.configuredDiscCanLaunch
                                               ? language::available_languages(state.configuredDiscInfo)
                                               : language::available_languages({});
                    for (const GameLanguage language : languages) {
                        pane.add_button({
                                            .text = language::language_name(language),
                                            .isSelected =
                                                [language] {
                                                    return getSettings().game.language.getValue() ==
                                                           language;
                                                },
                                        })
                            .on_pressed([language] {
                                mDoAud_seStartMenu(kSoundItemChange);
                                getSettings().game.language.setValue(language);
                                config::save();
                            });
                    }
                    pane.add_rml(korean::text("<br/>Changes require a restart."));
                });
            leftPane.register_control(
                leftPane.add_select_button({
                    .key = korean::text("Graphics Backend"),
                    .getValue = [] { return graphics_backend_display_name(); },
                    .isModified = [] { return is_graphics_backend_restart_pending(); },
                }),
                rightPane, [](Pane& pane) {
                    const auto availableBackends = available_backends();
                    for (const auto backend : availableBackends) {
                        pane
                            .add_button({
                                .text = Rml::String{backend_name(backend)},
                                .isSelected = [backend] { return configured_backend() == backend; },
                            })
                            .on_pressed([backend] {
                                mDoAud_seStartMenu(kSoundItemChange);
                                getSettings().backend.graphicsBackend.setValue(
                                    std::string{backend_id(backend)});
                                config::save();
                            });
                    }
                    pane.add_rml(korean::text("<br/>Changes require a restart."));
                });
            leftPane.register_control(
                leftPane.add_select_button({
                    .key = korean::text("Save File Type"),
                    .getValue =
                        [] {
                            return kCardFileTypes[getSettings().backend.cardFileType.getValue()];
                        },
                    .isModified =
                        [] {
                            return getSettings().backend.cardFileType.getValue() !=
                                   prelaunch_state().initialCardFileType;
                        },
                }),
                rightPane, [](Pane& pane) {
                    for (int i = 0; i < kCardFileTypes.size(); i++) {
                        pane
                            .add_button({
                                .text = kCardFileTypes[i],
                                .isSelected =
                                    [i] {
                                        return getSettings().backend.cardFileType.getValue() == i;
                                    },
                            })
                            .on_pressed([i] {
                                mDoAud_seStartMenu(kSoundItemChange);
                                getSettings().backend.cardFileType.setValue(i);
                                config::save();
                            });
                    }
                });
            add_save_files_control(leftPane, rightPane);
        });
    }

    add_tab("Video", [this](Rml::Element* content) {
        auto& leftPane = add_child<Pane>(content, Pane::Type::Controlled);
        auto& rightPane = add_child<Pane>(content, Pane::Type::Uncontrolled);

        leftPane.add_section(korean::text("Display"));

        leftPane.register_control(leftPane.add_button(korean::text("Toggle Fullscreen")).on_pressed([] {
            mDoAud_seStartMenu(kSoundItemChange);
            getSettings().video.enableFullscreen.setValue(!getSettings().video.enableFullscreen);
            VISetWindowFullscreen(getSettings().video.enableFullscreen);
            config::save();
        }),
            rightPane, [](Pane& pane) { pane.clear(); });
        leftPane.register_control(leftPane.add_button(korean::text("Restore Default Window Size")).on_pressed([] {
            mDoAud_seStartMenu(kSoundItemChange);
            getSettings().video.enableFullscreen.setValue(false);
            VISetWindowFullscreen(false);
            VISetWindowSize(FB_WIDTH * 2, FB_HEIGHT * 2);
            VICenterWindow();
        }),
            rightPane, [](Pane& pane) { pane.clear(); });
        config_bool_select(leftPane, rightPane, getSettings().video.enableVsync,
            {
                .key = korean::text("Enable VSync"),
                .helpText = korean::text("Synchronizes the frame rate to your monitor's refresh rate."),
                .onChange = [](bool value) { aurora_enable_vsync(value); },
            });
        config_bool_select(leftPane, rightPane, getSettings().video.lockAspectRatio,
            {
                .key = korean::text("Lock 4:3 Aspect Ratio"),
                .helpText = korean::text("Lock the game's aspect ratio to the original."),
                .onChange =
                    [](bool value) {
                        AuroraSetViewportPolicy(
                            value ? AURORA_VIEWPORT_FIT : AURORA_VIEWPORT_STRETCH);
                    },
            });
        config_bool_select(leftPane, rightPane, getSettings().game.pauseOnFocusLost,
            {
                .key = korean::text("Pause on Focus Lost"),
                .helpText = korean::text("Pause the game when window focus is lost."),
                .isDisabled = [] { return IsMobile || speedrun::isActive(); },
            });
        leftPane.register_control(
            leftPane.add_select_button({
                .key = korean::text("Show FPS Counter"),
                .getValue =
                    [] {
                        if (!getSettings().video.enableFpsOverlay.getValue()) {
                            return Rml::String{korean::text("Off")};
                        }
                        const int idx = getSettings().video.fpsOverlayCorner.getValue();
                        return Rml::String{kFpsOverlayCornerNames[idx]};
                    },
                .isModified =
                    [] {
                        const auto& enable = getSettings().video.enableFpsOverlay;
                        const auto& corner = getSettings().video.fpsOverlayCorner;
                        return enable.getValue() != enable.getDefaultValue() ||
                               (enable.getValue() && corner.getValue() != corner.getDefaultValue());
                    },
            }),
            rightPane, [](Pane& pane) {
                pane.add_button(
                        {
                            .text = korean::text("Off"),
                            .isSelected =
                                [] { return !getSettings().video.enableFpsOverlay.getValue(); },
                        })
                    .on_pressed([] {
                        mDoAud_seStartMenu(kSoundItemChange);
                        getSettings().video.enableFpsOverlay.setValue(false);
                        config::save();
                    });
                for (int i = 0; i < static_cast<int>(kFpsOverlayCornerNames.size()); ++i) {
                    pane.add_button(
                            {
                                .text = kFpsOverlayCornerNames[i],
                                .isSelected =
                                    [i] {
                                        return getSettings().video.enableFpsOverlay.getValue() &&
                                               getSettings().video.fpsOverlayCorner.getValue() == i;
                                    },
                            })
                        .on_pressed([i] {
                            mDoAud_seStartMenu(kSoundItemChange);
                            getSettings().video.enableFpsOverlay.setValue(true);
                            getSettings().video.fpsOverlayCorner.setValue(i);
                            config::save();
                        });
                }
                pane.add_rml(
                    korean::text("<br/>Display the current framerate in a corner of the screen while playing."));
            });
        config_bool_select(leftPane, rightPane, getSettings().video.rememberWindowSize,
            {
                .key = korean::text("Remember Window Size"),
                .helpText = korean::text("Save and restore the previous session's window size when opening Dusklight."),
                .onChange =
                    [](bool value) {
                        if (value && !getSettings().video.enableFullscreen) {
                            const auto windowSize = aurora::window::get_window_size();
                            getSettings().video.lastWindowWidth.setValue(windowSize.width);
                            getSettings().video.lastWindowHeight.setValue(windowSize.height);
                            config::save();
                        }
                    },
                .isDisabled = [] { return IsMobile; },
            });

        config_int_select(leftPane, rightPane, getSettings().video.uiScale,
            korean::text("UI Scale"),
            korean::text("Scales the Dusklight interface relative to the display's DPI scale. Has no effect on the game's UI and HUD."),
            50, 200, 25, {}, {}, "%");

        leftPane.add_section(korean::text("Resolution"));
        graphics_tuner_control(*this, leftPane, rightPane,
            GraphicsTunerProps{
                .option = GraphicsOption::InternalResolution,
                .title = korean::text("Internal Resolution"),
                .helpText = kInternalResolutionHelpText,
            });
        graphics_tuner_control(*this, leftPane, rightPane,
            GraphicsTunerProps{
                .option = GraphicsOption::ShadowResolution,
                .title = korean::text("Shadow Resolution"),
                .helpText = kShadowResolutionHelpText,
            });
        graphics_tuner_control(*this, leftPane, rightPane,
            GraphicsTunerProps{
                .option = GraphicsOption::Resampler,
                .title = korean::text("Output Resampling"),
                .helpText = kResamplerHelpText,
            });

        leftPane.add_section(korean::text("Post-Processing"));
        graphics_tuner_control(*this, leftPane, rightPane,
            GraphicsTunerProps{
                .option = GraphicsOption::BloomMode,
                .title = korean::text("Bloom"),
                .helpText = kBloomHelpText,
            });
        graphics_tuner_control(*this, leftPane, rightPane,
            GraphicsTunerProps{
                .option = GraphicsOption::BloomMultiplier,
                .title = korean::text("Bloom Brightness"),
                .helpText = kBloomBrightnessHelpText,
            });
        graphics_tuner_control(*this, leftPane, rightPane,
            GraphicsTunerProps{
                .option = GraphicsOption::DepthOfFieldMode,
                .title = korean::text("Depth of Field"),
                .helpText = kDepthOfFieldHelpText,
            });

        leftPane.add_section(korean::text("Rendering"));
        graphics_tuner_control(*this, leftPane, rightPane,
            GraphicsTunerProps{
                .option = GraphicsOption::TextureReplacements,
                .title = korean::text("Enable Texture Replacements"),
                .helpText = kTextureReplacementHelpText,
            });
        leftPane.register_control(
            leftPane.add_select_button({
                .key = korean::text("Unlock Framerate"),
                .getValue =
                    [] {
                        return kInterpolationModes[static_cast<u8>(
                            getSettings().game.enableFrameInterpolation.getValue())];
                    },
                .isModified =
                    [] {
                        return getSettings().game.enableFrameInterpolation.getValue() !=
                               getSettings().game.enableFrameInterpolation.getDefaultValue();
                    },
            }),
            rightPane, [](Pane& pane) {
                for (int i = 0; i < kInterpolationModes.size(); i++) {
                    pane.add_button({
                            .text = kInterpolationModes[i],
                            .isSelected =
                                [i] {
                                    return getSettings().game.enableFrameInterpolation.getValue() == static_cast<FrameInterpMode>(i);
                                },
                        })
                        .on_pressed([i] {
                            mDoAud_seStartMenu(kSoundItemChange);
                            getSettings().game.enableFrameInterpolation.setValue(static_cast<FrameInterpMode>(i));
                            presentation::update_frame_rate_preference();
                            config::save();
                        });
                }
                pane.add_rml(kUnlockFramerateHelpText);
            });
        config_int_select(leftPane, rightPane, getSettings().video.maxFrameRate,
            korean::text("Framerate Cap"), korean::text("Limit the framerate to the specified value."), 30, 540, 1,
            [] { return getSettings().game.enableFrameInterpolation.getValue() != FrameInterpMode::Capped; },
            [](int) { presentation::update_frame_rate_preference(); });
        config_bool_select(leftPane, rightPane, getSettings().game.enableMapBackground,
            {
                .key = korean::text("Enable Mini-Map Shadows"),
                .helpText = korean::text("Render a thick shadow around the mini-map. May impact performance.")
            });
        config_bool_select(leftPane, rightPane, getSettings().game.disableCutscenePillarboxing,
            {
                .key = korean::text("Disable Cutscene Pillarboxing"),
                .helpText = korean::text("Disable black bars on the left and right sides of the screen "
                            "during some cutscenes, particularly on ultra-wide displays. "
                            "Visuals beyond the original intended framing may appear buggy."),
            });
        leftPane.register_control(
            leftPane.add_select_button({
                .key = korean::text("Disable Letterboxing"),
                .getValue =
                    [] {
                        return kLetterboxModes[static_cast<u8>(getSettings().game.disableLetterboxing.getValue())];
                    },
                .isModified =
                    [] {
                        return getSettings().game.disableLetterboxing.getValue() !=
                               getSettings().game.disableLetterboxing.getDefaultValue();
                    },
            }),
            rightPane, [](Pane& pane) {
                for (int i = 0; i < static_cast<int>(kLetterboxModes.size()); i++) {
                    pane.add_button({
                            .text = kLetterboxModes[i],
                            .isSelected =
                                [i] {
                                    return getSettings().game.disableLetterboxing.getValue() == static_cast<LetterboxMode>(i);
                                },
                        })
                        .on_pressed([i] {
                            mDoAud_seStartMenu(kSoundItemChange);
                            getSettings().game.disableLetterboxing.setValue(static_cast<LetterboxMode>(i));
                            config::save();
                        });
                }
                pane.add_rml(
                    korean::text("<br/>Disable the top and bottom black bars during L-targeting, aiming, "
                    "cutscenes, dialogue, etc."));
            });
    });

    add_tab("Input", [this](Rml::Element* content) {
        auto& leftPane = add_child<Pane>(content, Pane::Type::Controlled);
        auto& rightPane = add_child<Pane>(content, Pane::Type::Uncontrolled);

        auto addOption = [&](const Rml::String& key, ConfigVar<bool>& value,
                             const Rml::String& helpText, std::function<bool()> isDisabled = {}) {
            config_bool_select(leftPane, rightPane, value,
                {
                    .key = key,
                    .helpText = helpText,
                    .isDisabled = std::move(isDisabled),
                });
        };

        leftPane.add_section(korean::text("Inputs"));
        leftPane.register_control(
            leftPane.add_group_button({.text = korean::text("Configure Inputs")}).on_pressed([this] {
                push(std::make_unique<ControllerConfigWindow>());
            }),
            rightPane, [](Pane& pane) {
                pane.clear();
                pane.add_text(korean::text("Open input binding configuration."));
            });
        config_bool_select(leftPane, rightPane, getSettings().game.allowBackgroundInput,
            {
                .key = korean::text("Allow Background Inputs"),
                .helpText = korean::text("Allow inputs even when the game window is not focused."),
                .onChange = [](bool value) { aurora_set_background_input(value); },
            });

#if TOUCH_CONTROLS_AVAILABLE
        leftPane.add_section(korean::text("Touch"));
        addOption(korean::text("Touch Controls"), getSettings().game.enableTouchControls,
            korean::text("Enables controls overlay for touch screens.<br/><br/>Press and drag on the left side "
            "of the screen to move, and on the right side of the screen to control the camera."));
        auto& customizeTouchLayout = leftPane.add_group_button(GroupButton::Props{
            .text = korean::text("Customize Layout"),
            .isDisabled = [] { return !getSettings().game.enableTouchControls; },
        });
        leftPane.register_control(customizeTouchLayout.on_pressed(
                                      [this] { push(std::make_unique<TouchControlsEditor>()); }),
            rightPane, [](Pane& pane) {
                pane.clear();
                pane.add_text(korean::text("Open the touch controls layout editor."));
            });
        leftPane.register_control(
            leftPane.add_select_button({
                .key = korean::text("Touch Targeting"),
                .getValue =
                    [] {
                        return touch_targeting_label(getSettings().game.touchTargeting.getValue());
                    },
                .isDisabled = [] { return !getSettings().game.enableTouchControls; },
                .isModified =
                    [] {
                        const auto& targeting = getSettings().game.touchTargeting;
                        return targeting.getValue() != targeting.getDefaultValue();
                    },
            }),
            rightPane, [](Pane& pane) {
                pane.clear();
                for (int i = 0; i < static_cast<int>(kTouchTargetingLabels.size()); ++i) {
                    pane.add_button({
                            .text = kTouchTargetingLabels[i],
                            .isSelected =
                                [i] {
                                    return getSettings().game.touchTargeting.getValue() ==
                                           static_cast<TouchTargeting>(i);
                                },
                        })
                        .on_pressed([i] {
                            mDoAud_seStartMenu(kSoundItemChange);
                            getSettings().game.touchTargeting.setValue(
                                static_cast<TouchTargeting>(i));
                            config::save();
                        });
                }
                pane.add_rml(fmt::format(korean::text("<br/>Hybrid: {}<br/>Hold: {}<br/>Switch: {}"),
                    kTouchTargetingDescriptions[0], kTouchTargetingDescriptions[1],
                    kTouchTargetingDescriptions[2]));
            });
        config_percent_select(leftPane, rightPane, getSettings().game.touchCameraXSensitivity,
            korean::text("Touch Camera X Sensitivity"),
            korean::text("Adjusts touch camera horizontal sensitivity.<br/><br/>Applies to touch input only."),
            25, 400, 5, [] { return !getSettings().game.enableTouchControls; });
        config_percent_select(leftPane, rightPane, getSettings().game.touchCameraYSensitivity,
            korean::text("Touch Camera Y Sensitivity"),
            korean::text("Adjusts touch camera vertical sensitivity.<br/><br/>Applies to touch input only."), 25,
            400, 5, [] { return !getSettings().game.enableTouchControls; });
#endif

        leftPane.add_section(korean::text("Camera"));
        addOption(korean::text("Free Camera"), getSettings().game.freeCamera,
            korean::text("Enables free camera control, letting you control the camera fully with the C-Stick."));
        config_percent_select(leftPane, rightPane, getSettings().game.freeCameraXSensitivity,
            korean::text("Free Camera X Sensitivity"),
            korean::text("Adjusts horizontal free camera sensitivity.<br/><br/>Applies to the control stick only."),
            50, 200, 5, [] { return !getSettings().game.freeCamera; });
        config_percent_select(leftPane, rightPane, getSettings().game.freeCameraYSensitivity,
            korean::text("Free Camera Y Sensitivity"),
            korean::text("Adjusts vertical free camera sensitivity.<br/><br/>Applies to the control stick only."),
            50, 200, 5, [] { return !getSettings().game.freeCamera; });
        addOption(korean::text("Invert Camera X Axis"), getSettings().game.invertCameraXAxis,
            korean::text("Invert horizontal camera movement.<br/><br/>Applies to the control stick only."));
        addOption(korean::text("Invert Camera Y Axis"), getSettings().game.invertCameraYAxis,
            korean::text("Invert vertical camera movement.<br/><br/>Applies to the control stick only."),
            [] { return !getSettings().game.freeCamera; });
        addOption(korean::text("Invert First Person X Axis"), getSettings().game.invertFirstPersonXAxis,
            korean::text("Invert horizontal movement while aiming with items or first person camera.<br/><br/>Applies to the control stick only."));
        addOption(korean::text("Invert First Person Y Axis"), getSettings().game.invertFirstPersonYAxis,
            korean::text("Invert vertical movement while aiming with items or first person camera.<br/><br/>Applies to the control stick only."));

        leftPane.add_section(korean::text("Gyro"));
        addOption(korean::text("Gyro Aim"), getSettings().game.enableGyroAim,
            korean::text("Enables gyro controls while in look mode, aiming a hawk, and aiming "
            "supported items.<br/><br/>Supported items include the Slingshot, Gale Boomerang, "
            "Hero's Bow, Clawshot(s), Ball and Chain, and Dominion Rod."));
        addOption(korean::text("Gyro Rollgoal"), getSettings().game.enableGyroRollgoal,
            korean::text("Enables gyro controls for Rollgoal in Hena's Cabin."));
        config_percent_select(leftPane, rightPane, getSettings().game.gyroSensitivityY,
            korean::text("Gyro Pitch Sensitivity"), korean::text("Controls vertical gyro aiming sensitivity."), 25, 400, 5,
            [] { return !gyro_enabled(); });
        config_percent_select(leftPane, rightPane, getSettings().game.gyroSensitivityX,
            korean::text("Gyro Yaw Sensitivity"), korean::text("Controls horizontal gyro aiming sensitivity."), 25, 400, 5,
            [] { return !gyro_enabled(); });
        config_percent_select(leftPane, rightPane, getSettings().game.gyroSensitivityRollgoal,
            korean::text("Rollgoal Sensitivity"), korean::text("Controls how strongly gyro input tilts the Rollgoal table."),
            25, 400, 5,
            [] { return !getSettings().game.enableGyroRollgoal; });
        config_percent_select(leftPane, rightPane, getSettings().game.gyroDeadband, korean::text("Gyro Deadband"),
            korean::text("Ignores small gyro movement to reduce drift and jitter."), 0, 50, 1,
            [] { return !gyro_enabled(); });
        config_percent_select(leftPane, rightPane, getSettings().game.gyroSmoothing,
            korean::text("Gyro Smoothing"), korean::text("Higher values smooth gyro input over time."), 0, 100, 1,
            [] { return !gyro_enabled(); });
        addOption(korean::text("Invert Gyro Pitch"), getSettings().game.gyroInvertPitch,
            korean::text("Invert vertical gyro aiming."), [] { return !gyro_enabled(); });
        addOption(korean::text("Invert Gyro Yaw"), getSettings().game.gyroInvertYaw,
            korean::text("Invert horizontal gyro aiming."), [] { return !gyro_enabled(); });

        leftPane.add_section(korean::text("Mouse"));
        addOption(korean::text("Mouse Aim"), getSettings().game.enableMouseAim,
            korean::text("Enables mouse input while in look mode, aiming a hawk, and aiming "
            "supported items.<br/><br/>Supported items include the Slingshot, Gale Boomerang, "
            "Hero's Bow, Clawshot(s), Ball and Chain, and Dominion Rod."));
        addOption(korean::text("Mouse Camera"), getSettings().game.enableMouseCamera,
            korean::text("Enables mouse input for controlling the third-person camera."));
        config_percent_select(leftPane, rightPane, getSettings().game.mouseAimSensitivity,
            korean::text("Mouse Aim Sensitivity"), korean::text("Controls mouse aim sensitivity."), 25, 400, 5,
            [] { return !getSettings().game.enableMouseAim; });
        config_percent_select(leftPane, rightPane, getSettings().game.mouseCameraSensitivity,
            korean::text("Mouse Camera Sensitivity"), korean::text("Controls mouse camera sensitivity."), 25, 400, 5,
            [] { return !getSettings().game.enableMouseCamera; });
        addOption(korean::text("Invert Mouse Y"), getSettings().game.invertMouseY,
            korean::text("Invert vertical mouse control for both aiming and camera."),
            [] { return !getSettings().game.enableMouseAim || !getSettings().game.enableMouseCamera; });

        leftPane.add_section(korean::text("Gameplay"));
        addOption(korean::text("Mouse/Touch in Menus"), getSettings().game.enableMenuPointer,
            korean::text("Enables mouse and touch input for supported in-game menus."));
        addOption(korean::text("Invert Air/Swim X Axis"), getSettings().game.invertAirSwimX,
            korean::text("Invert horizontal movement while flying or swimming."));
        addOption(korean::text("Invert Air/Swim Y Axis"), getSettings().game.invertAirSwimY,
            korean::text("Invert vertical movement while flying or swimming."));
        addOption(korean::text("Swap Direct Select Input"), getSettings().game.swapDirectSelect,
            korean::text("Swap the controls for using Direct Select on the item wheel, making Direct Select the default and holding L to scroll the wheel."));

        leftPane.add_section(korean::text("Tools"));
        addOption(korean::text("Turbo Key"), getSettings().game.enableTurboKeybind,
            korean::text("Hold Tab to increase game speed by up to 4x."),
            [] { return speedrun::isActive(); });
        addOption(korean::text("Reset Key (") + Rml::String{hotkeys::DO_RESET} + ")",
            getSettings().game.enableResetKeybind,
            korean::text("Press ") + Rml::String{hotkeys::DO_RESET} + korean::text(" to reset the game."));
    });

    add_tab("Audio", [this](Rml::Element* content) {
        auto& leftPane = add_child<Pane>(content, Pane::Type::Controlled);
        auto& rightPane = add_child<Pane>(content, Pane::Type::Uncontrolled);

        leftPane.add_section(korean::text("Output"));
        leftPane.register_control(
            leftPane.add_select_button({
                .key = korean::text("Output Mode"),
                .getValue = [] {
                    const auto idx = static_cast<int>(getSettings().audio.outputMode.getValue());
                    return Rml::String{kAudioOutputModeNames[idx]};
                },
                .isModified = [] {
                    const auto& setting = getSettings().audio.outputMode;
                    return setting.getValue() != setting.getDefaultValue();
                },
            }), rightPane, [](Pane& pane) {
                for (int i = 0; i < static_cast<int>(kAudioOutputModeNames.size()); ++i) {
                    pane.add_button({
                        .text = kAudioOutputModeNames[i],
                        .isSelected = [i] {
                            const auto& setting = getSettings().audio.outputMode;
                            return setting.getValue() == static_cast<AudioOutputMode>(i);
                        },
                    }).on_pressed([i] {
                        mDoAud_seStartMenu(kSoundItemChange);
                        getSettings().audio.outputMode.setValue(static_cast<AudioOutputMode>(i));
                        config::save();
                        audio::Reinitialize();
                    });
                }
            });

        // TODO: Individual sliders for Sub Music, Sound Effects, and Fanfare.
        leftPane.add_section(korean::text("Volume"));
        leftPane.register_control(
            leftPane.add_child<NumberButton>(NumberButton::Props{
                .key = korean::text("Master Volume"),
                .getValue = [] { return getSettings().audio.masterVolume.getValue(); },
                .setValue =
                    [](int value) {
                        getSettings().audio.masterVolume.setValue(value);
                        config::save();
                        audio::SetMasterVolume(audio::MasterVolumeToLinear(value / 100.0f));
                    },
                .isModified =
                    [] {
                        return getSettings().audio.masterVolume.getValue() !=
                               getSettings().audio.masterVolume.getDefaultValue();
                    },
                .max = 100,
                .suffix = "%",
            }),
            rightPane, [](Pane& pane) {
                pane.clear();
                pane.add_text(korean::text("Adjusts the volume of all sounds in the game."));
            });
        leftPane.register_control(
            leftPane.add_child<NumberButton>(NumberButton::Props{
                .key = korean::text("Main Music Volume"),
                .getValue = [] { return getSettings().audio.mainMusicVolume.getValue(); },
                .setValue =
                    [](int value) {
                        getSettings().audio.mainMusicVolume.setValue(value);
                        config::save();
                    },
                .isModified =
                    [] {
                        return getSettings().audio.mainMusicVolume.getValue() !=
                               getSettings().audio.mainMusicVolume.getDefaultValue();
                    },
                .max = 100,
                .suffix = "%",
            }),
            rightPane, [](Pane& pane) {
                pane.clear();
                pane.add_text(korean::text("Adjusts the volume of all music in the game."));
            });

        leftPane.add_section(korean::text("Effects"));
        config_bool_select(leftPane, rightPane, getSettings().audio.enableReverb,
            {
                .key = korean::text("Enable Reverb"),
                .helpText = korean::text("Enables the reverb effect in game audio."),
                .onChange = [](bool value) { audio::SetEnableReverb(value); },
            });
        config_bool_select(leftPane, rightPane, getSettings().audio.menuSounds,
            {
                .key = korean::text("Dusklight Menu Sounds"),
                .helpText = korean::text("Play sound effects when navigating the Dusklight menu."),
            });

        leftPane.add_section(korean::text("Tweaks"));
        config_bool_select(leftPane, rightPane, getSettings().game.noLowHpSound,
            {
                .key = korean::text("No Low HP Sound"),
                .helpText = korean::text("Disable the beeping sound when having low health."),
            });
        config_bool_select(leftPane, rightPane, getSettings().game.midnasLamentNonStop,
            {
                .key = korean::text("Non-Stop Midna's Lament"),
                .helpText = korean::text("Prevents enemy music while Midna's Lament is playing."),
            });
    });

    add_tab("Gameplay", [this](Rml::Element* content) {
        auto& leftPane = add_child<Pane>(content, Pane::Type::Controlled);
        auto& rightPane = add_child<Pane>(content, Pane::Type::Uncontrolled);

        auto addOption = [&](const Rml::String& key, ConfigVar<bool>& value,
                             const Rml::String& helpText) {
            config_bool_select(leftPane, rightPane, value,
                {
                    .key = key,
                    .helpText = helpText,
                });
        };
        auto addSpeedrunDisabledOption = [&](const Rml::String& key, ConfigVar<bool>& value,
                                             const Rml::String& helpText) {
            add_speedrun_disabled_option(leftPane, rightPane, value, key, helpText);
        };

        leftPane.add_section(korean::text("General"));
        addOption(korean::text("Mirror Mode"), getSettings().game.enableMirrorMode,
            korean::text("Mirrors the world horizontally, matching the Wii version of the game."));
        addOption(korean::text("Minimal HUD"), getSettings().game.minimalHUD,
            korean::text("Disables the elements of the main HUD of the game.<br/>Useful for a more immersive "
            "experience."));
        config_percent_select(leftPane, rightPane, getSettings().game.hudScale,
            korean::text("HUD Scale"),
            korean::text("Scales the size of the gameplay HUD (hearts, buttons, mini-map, etc.). Does not affect dialog boxes or menus."),
            50, 200, 5,
            [] { return getSettings().game.minimalHUD.getValue(); });
        addOption(korean::text("Restore Wii 1.0 Glitches"), getSettings().game.restoreWiiGlitches,
            korean::text("Restores patched glitches from Wii USA 1.0, the first released version."));
        addOption(korean::text("Enable Rotating Link Doll"), getSettings().game.enableLinkDollRotation,
            korean::text("Enables rotating Link in the collection menu with the C-Stick."));
        addOption(korean::text("Hide Owl Statue Markers"), getSettings().game.removeQuestMapMarkers,
            korean::text("Removes completed Owl Statue markers from the map and Minimap."));

        leftPane.add_section(korean::text("Difficulty"));
        leftPane.register_control(
            leftPane.add_child<NumberButton>(NumberButton::Props{
                .key = korean::text("Damage Multiplier"),
                .getValue = [] { return getSettings().game.damageMultiplier.getValue(); },
                .setValue =
                    [](int value) {
                        getSettings().game.damageMultiplier.setValue(value);
                        config::save();
                    },
                .isDisabled = [] { return speedrun::isActive(); },
                .isModified =
                    [] {
                        return getSettings().game.damageMultiplier.getValue() !=
                               getSettings().game.damageMultiplier.getDefaultValue();
                    },
                .min = 1,
                .max = 8,
                .suffix = "×",
            }),
            rightPane, [](Pane& pane) {
                pane.clear();
                pane.add_text(korean::text("Multiplies incoming damage."));
            });
        addSpeedrunDisabledOption(
            korean::text("Instant Death"), getSettings().game.instantDeath, korean::text("Any hit will instantly kill you."));
        addSpeedrunDisabledOption(korean::text("No Heart Drops"), getSettings().game.noHeartDrops,
            korean::text("Hearts will never drop from enemies, pots, and various other places."));

        leftPane.add_section(korean::text("Quality of Life"));
        addOption(korean::text("Bigger Wallets"), getSettings().game.biggerWallets,
            korean::text("Wallet sizes are like in the HD version. (500, 1000, 2000)"));
        addOption(korean::text("Disable Rupee Cutscenes"), getSettings().game.disableRupeeCutscenes,
            korean::text("Rupees will not play cutscenes after you have collected them the first time."));
        addSpeedrunDisabledOption(korean::text("Faster Scene Transitions"), getSettings().game.fastTransitions,
            korean::text("Reduces how long the transitions take when changing maps."));
        addOption(korean::text("Faster Climbing"), getSettings().game.fastClimbing,
            korean::text("Quicker climbing on ladders and vines like the HD version."));
        addOption(korean::text("Faster Tears of Light"), getSettings().game.fastTears,
            korean::text("Tears of Light dropped by Shadow Insects pop out faster like the HD version."));
        addSpeedrunDisabledOption(korean::text("Autosave"), getSettings().game.autoSave,
            korean::text("Autosaves the game when going to a new area or opening a dungeon door."));
        addOption(korean::text("Instant Saves"), getSettings().game.instantSaves,
            korean::text("Skips the delay when writing to the Memory Card."));
        addOption(korean::text("Hold B for Instant Text"), getSettings().game.instantText,
            korean::text("Makes text scroll immediately by holding B."));
        addSpeedrunDisabledOption(korean::text("Hold Button to Mash"), getSettings().game.holdToMash,
            korean::text("Hold the indicated button to mash automatically."));
        addOption(korean::text("No Climbing Miss Animation"), getSettings().game.noMissClimbing,
            korean::text("Prevents Link from playing a struggle animation when grabbing ledges or "
            "climbing on vines."));
        addOption(korean::text("No Rupee Returns"), getSettings().game.noReturnRupees,
            korean::text("Always collect Rupees even if your Wallet is too full."));
        addOption(korean::text("No Sword Recoil"), getSettings().game.noSwordRecoil,
            korean::text("Link will not recoil when his sword hits walls."));
        addOption(korean::text("No 2nd Fish for Cat"), getSettings().game.no2ndFishForCat,
            korean::text("Skip needing to catch a second fish for Sera's cat."));
        addOption(korean::text("Button Fishing"), getSettings().game.buttonFishing,
            korean::text("Allow fishing with the Fishing Rod using the button the item is assigned to."));
        addOption(korean::text("Show Poe Count on Map"), getSettings().game.enhancedMapMenus,
            korean::text("Displays collected/total number of Poe Souls for a region on the map."));
        addSpeedrunDisabledOption(korean::text("Sun's Song (R+X)"), getSettings().game.sunsSong,
            korean::text("Allows Wolf Link to howl and change the time of day."));
        addOption(korean::text("Quick Transform (R+Y)"), getSettings().game.enableQuickTransform,
            korean::text("Transform instantly by pressing R and Y simultaneously."));
        addOption(korean::text("Aiming Reticle"), getSettings().game.aimingReticle,
            korean::text("Shows the aiming reticle for bow and slingshot."));

        leftPane.add_section(korean::text("Speedrunning"));
        config_bool_select(leftPane, rightPane, getSettings().game.speedrunMode,
            {
                .key = korean::text("Speedrun Mode"),
                .helpText =
                    korean::text("Enables Speedrun game mode option in the Dusklight launch menu."),
                .onChange =
                    [this](bool enabled) {
                        if (enabled) {
                            speedrun::registerSpeedrunGameMode();
                        } else {
                            if (speedrun::isActive()) {
                                pop();
                            }
                            speedrun::unregisterSpeedrunGameMode();
                        }
                        MenuBar::refresh_tabs();
                    },
            });
        config_bool_select(leftPane, rightPane, getSettings().game.liveSplitEnabled,
            {
                .key = korean::text("LiveSplit Connection"),
                .helpText = korean::text("Connect to LiveSplit server on localhost:16834. For this to work you must right click LiveSplit, and turn on Control -> Start TCP Server."
                " To see IGT in LiveSplit you must change your comparison to Game Time."),
                .onChange =
                    [](bool enabled) {
                        if (enabled) {
                            speedrun::connectLiveSplit();
                        } else {
                            speedrun::disconnectLiveSplit();
                        }
                    },
                .isDisabled = [] { return IsMobile || !speedrun::isActive(); },
            });
        config_bool_select(leftPane, rightPane, getSettings().game.showSpeedrunRTATimer,
            {
                .key = korean::text("Show RTA"),
                .helpText = korean::text("Display the RTA timer. IGT is always visible."),
                .isDisabled = [] { return !speedrun::isActive(); },
            });
    });

    add_tab("Cheats", [this](Rml::Element* content) {
        auto& leftPane = add_child<Pane>(content, Pane::Type::Controlled);
        auto& rightPane = add_child<Pane>(content, Pane::Type::Uncontrolled);

        auto addCheat = [&](const Rml::String& key, ConfigVar<bool>& value,
                            const Rml::String& helpText) {
            add_speedrun_disabled_option(leftPane, rightPane, value, key, helpText);
        };

        leftPane.add_section(korean::text("Resources"));
        addCheat(korean::text("Infinite Hearts"), getSettings().game.infiniteHearts, korean::text("Keeps your health full."));
        addCheat(
            korean::text("Infinite Arrows"), getSettings().game.infiniteArrows, korean::text("Keeps your arrow count full."));
        addCheat(korean::text("Infinite Seeds"), getSettings().game.infiniteSeeds, korean::text("Keeps your slingshot pellets (seeds) full."));
        addCheat(korean::text("Infinite Bombs"), getSettings().game.infiniteBombs, korean::text("Keeps all bomb bags full."));
        addCheat(korean::text("Infinite Oil"), getSettings().game.infiniteOil, korean::text("Keeps your lantern oil full."));
        addCheat(korean::text("Infinite Oxygen"), getSettings().game.infiniteOxygen,
            korean::text("Keeps your underwater oxygen meter full."));
        addCheat(
            korean::text("Infinite Rupees"), getSettings().game.infiniteRupees, korean::text("Keeps your rupee count full."));
        addCheat(korean::text("No Item Timer"), getSettings().game.enableIndefiniteItemDrops,
            korean::text("Item drops such as rupees and hearts will never disappear after they drop."));

        leftPane.add_section(korean::text("Abilities"));

        addCheat(
            korean::text("Moon Jump (R+A)"), getSettings().game.moonJump, korean::text("Hold R and A to rise into the air."));
        addCheat(
            korean::text("Easy Quick Spin (R+B)"), getSettings().game.easyQuickSpin, korean::text("Hold R to always do a Quick Spin when attacking with B."));

        addCheat(korean::text("Super Clawshot"), getSettings().game.superClawshot,
            korean::text("Extends Clawshot behavior beyond the normal game rules."));
        leftPane.register_control(
            leftPane.add_select_button({
                .key = korean::text("Always Greatspin"),
                .getValue =
                    [] {
                        return kAlwaysGreatspinModes[static_cast<u8>(
                            getSettings().game.alwaysGreatspin.getValue())];
                    },
                .isDisabled = [] { return dusk::speedrun::isActive(); },
                .isModified =
                    [] {
                        return getSettings().game.alwaysGreatspin.getValue() !=
                               getSettings().game.alwaysGreatspin.getDefaultValue();
                    },
            }),
            rightPane, [](Pane& pane) {
                for (int i = 0; i < static_cast<int>(kAlwaysGreatspinModes.size()); i++) {
                    pane.add_button({
                            .text = kAlwaysGreatspinModes[i],
                            .isSelected =
                                [i] {
                                    return getSettings().game.alwaysGreatspin.getValue() ==
                                           static_cast<AlwaysGreatspinMode>(i);
                                },
                        })
                        .on_pressed([i] {
                            mDoAud_seStartMenu(kSoundItemChange);
                            getSettings().game.alwaysGreatspin.setValue(
                                static_cast<AlwaysGreatspinMode>(i));
                            config::save();
                        });
                }
                pane.add_rml(korean::text("<br/>Allows the Great Spin attack without requiring full health."));
            });
        addCheat(korean::text("Fast Iron Boots"), getSettings().game.enableFastIronBoots,
            korean::text("Speeds up movement while heavy, including wearing the Iron Boots, holding the Ball and Chain, wearing Magic Armor without rupees, etc."));
        addCheat(korean::text("Can Transform Anywhere"), getSettings().game.canTransformAnywhere,
            korean::text("Allows transforming even if NPCs are looking."));
        addCheat(korean::text("Fast Roll"), getSettings().game.fastRoll,
            korean::text("Makes Link's roll animation and movement twice as fast."));
        addCheat(korean::text("Fast Spinner"), getSettings().game.fastSpinner,
            korean::text("Speeds up Spinner movement while holding R."));
        leftPane.register_control(
            leftPane.add_select_button({
                .key = korean::text("Magic Armor Behavior"),
                .getValue =
                    [] {
                        return kMagicArmorModes[static_cast<u8>(
                            getSettings().game.armorRupeeDrain.getValue())];
                    },
                .isDisabled = [] { return speedrun::isActive(); },
                .isModified =
                    [] {
                        return getSettings().game.armorRupeeDrain.getValue() !=
                               getSettings().game.armorRupeeDrain.getDefaultValue();
                    },
            }),
            rightPane, [](Pane& pane) {
                for (int i = 0; i < kMagicArmorModes.size(); i++) {
                    pane.add_button({
                            .text = kMagicArmorModes[i],
                            .isSelected =
                                [i] {
                                    return getSettings().game.armorRupeeDrain.getValue() == static_cast<MagicArmorMode>(i);
                                },
                        })
                        .on_pressed([i] {
                            mDoAud_seStartMenu(kSoundItemChange);
                            getSettings().game.armorRupeeDrain.setValue(static_cast<MagicArmorMode>(i));
                            config::save();
                        });
                }
                pane.add_rml(
                    korean::text("<br/>Control the behavior of the Magic Armor."));
            });
        addCheat(korean::text("Invincible Enemies"), getSettings().game.invincibleEnemies,
            korean::text("Prevents enemies from taking damage."));
    });

    add_tab("Interface", [this](Rml::Element* content) {
        auto& leftPane = add_child<Pane>(content, Pane::Type::Controlled);
        auto& rightPane = add_child<Pane>(content, Pane::Type::Uncontrolled);

        leftPane.add_section("Dusklight");
#if DUSK_CAN_OPEN_DATA_FOLDER
        leftPane.register_control(
            leftPane.add_button(korean::text("Open Data Folder")).on_pressed([] {
                mDoAud_seStartMenu(kSoundClick);
                data::open_data_path();
            }),
            rightPane, [](Pane& pane) {
                pane.add_text(
                    korean::text("Open the folder where Dusklight stores settings, saves, logs, texture "
                    "replacements, and other app data."));
            });
#endif
        leftPane.register_control(leftPane.add_button(korean::text("Restart to Main Menu")).on_pressed([this] {
            mDoAud_seStartMenu(kSoundClick);
            pop();
            prelaunch_state().returnToPrelaunchOnReset = true;
            JUTGamePad::C3ButtonReset::sResetSwitchPushing = true;
        }),
            rightPane, [](Pane& pane) {
                pane.add_text(korean::text("Restart Dusklight to the pre-launch menu to change settings, game "
                              "modes, or mods."));
            });
        leftPane.register_control(
            leftPane.add_select_button({
                .key = korean::text("Notifications"),
                .getValue = [] {
                    const bool ach = getSettings().game.enableAchievementToasts.getValue();
                    const bool ctl = getSettings().game.enableControllerToasts.getValue();
                    if (!ach && !ctl) {
                        return Rml::String{korean::text("Off")};
                    }
                    if (ach && ctl) {
                        return Rml::String{korean::text("All")};
                    }
                    return Rml::String{korean::text("Some")};
                },
                .isModified = [] {
                    const auto& ach = getSettings().game.enableAchievementToasts;
                    const auto& ctl = getSettings().game.enableControllerToasts;
                    return ach.getValue() != ach.getDefaultValue() || ctl.getValue() != ctl.getDefaultValue();
                },
            }),
            rightPane, [](Pane& pane) {
                pane.clear();
                pane.add_button(korean::text("Select All")).on_pressed([] {
                    mDoAud_seStartMenu(kSoundItemChange);
                    getSettings().game.enableAchievementToasts.setValue(true);
                    getSettings().game.enableControllerToasts.setValue(true);
                    config::save();
                });
                pane.add_button(korean::text("Select None")).on_pressed([] {
                    mDoAud_seStartMenu(kSoundItemChange);
                    getSettings().game.enableAchievementToasts.setValue(false);
                    getSettings().game.enableControllerToasts.setValue(false);
                    config::save();
                });

                pane.add_section(korean::text("Types"));
                pane.add_button(
                    {
                        .text = korean::text("Achievements"),
                        .isSelected =
                        [] {
                            return getSettings().game.enableAchievementToasts.getValue();
                        },
                    })
                    .on_pressed([] {
                        mDoAud_seStartMenu(kSoundItemChange);
                        auto& v = getSettings().game.enableAchievementToasts;
                        v.setValue(!v.getValue());
                        config::save();
                    });
                pane.add_button(
                    {
                        .text = korean::text("Missing Device"),
                        .isSelected =
                            [] { return getSettings().game.enableControllerToasts.getValue(); },
                    })
                    .on_pressed([] {
                        mDoAud_seStartMenu(kSoundItemChange);
                        auto& v = getSettings().game.enableControllerToasts;
                        v.setValue(!v.getValue());
                        config::save();
                    });
                pane.add_rml(korean::text("<br/>Choose which notifications can be displayed."));
            });
#if BOREALIS_HAS_SENTRY
        auto& crashReporting = leftPane.add_child<KoreanBoolButton>(BoolButton::Props{
            .key = korean::text("Crash Reporting"),
            .getValue =
                [] { return borealis::sentry::get_consent() == borealis::sentry::Consent::Given; },
            .setValue = [](bool enabled) { borealis::sentry::set_consent(enabled); },
            .isDisabled =
                [] {
                    return borealis::sentry::get_consent() ==
                           borealis::sentry::Consent::Unavailable;
                },
            .isModified = [] { return false; },
        });
        leftPane.register_control(crashReporting, rightPane, [](Pane& pane) {
            pane.clear();
            pane.add_rml(korean::text("Dusklight can automatically send crash reports to the developers. Crash "
                         "reports contain the following:<br/>• Operating system version<br/>• CPU "
                         "architecture<br/>• GPU model & driver version<br/>• File paths (may "
                         "include account username)<br/>• Stack trace"));
        });
#endif
        config_bool_select(leftPane, rightPane, getSettings().backend.skipPreLaunchUI,
            {
                .key = korean::text("Skip Dusklight Main Menu"),
                .helpText =
                    korean::text("When starting Dusklight, skip the main menu and boot straight into the "
                    "game if a disc image is available.<br/><br/>Note: If any mods register game "
                    "modes, this option will be ignored."),
            });
        config_bool_select(leftPane, rightPane, getSettings().backend.showPipelineCompilation,
            {
                .key = korean::text("Show Shader Compilation"),
                .helpText = korean::text("Show an overlay when shaders are being compiled for your hardware."),
            });
        config_bool_select(leftPane, rightPane, getSettings().backend.checkForUpdates,
            {
                .key = korean::text("Check for Dusklight Updates"),
                .helpText = korean::text("Checks GitHub releases for a new Dusklight version on startup.<br/><br/>"
                            "No personal information is transmitted or collected."),
            });
#if BOREALIS_HAS_DISCORD
        config_bool_select(leftPane, rightPane, getSettings().game.enableDiscordPresence,
            {
                .key = korean::text("Enable Discord Rich Presence"),
                .helpText = korean::text("Enable Dusklight to integrate with Discord Rich Presence. This allows Discord to show your status in-game."),
                .onChange = [](bool enabled) {
                    if (enabled) {
                        discord::initialize();
                    } else {
                        discord::shutdown();
                    }
                },
            });
#endif
        config_bool_select(leftPane, rightPane, getSettings().backend.enableAdvancedSettings,
            {
                .key = korean::text("Enable Advanced Settings"),
                .icon = "warning",
                .helpText = korean::text("Show advanced settings and debugging tools with "
                            "Shift+F1.<br/><br/><icon class=\"warning\"/> WARNING: Debugging tools "
                            "can easily break your game. Do not use on a regular save!"),
                .onChange = [](bool) { MenuBar::refresh_tabs(); },
                .isDisabled = [] { return speedrun::isActive(); },
            });
        config_bool_select(leftPane, rightPane, getSettings().game.showInputViewer,
            {
                .key = korean::text("Show Input Viewer"),
                .helpText = korean::text("Display a controller input overlay while playing."),
            });
        config_bool_select(leftPane, rightPane, getSettings().game.showInputViewerGyro,
            {
                .key = korean::text("Show Gyro Input Viewer"),
                .helpText = korean::text("Show gyro sensor values in the input viewer."),
                .isDisabled = [] { return !getSettings().game.showInputViewer; },
            });
        leftPane.add_section(korean::text("Game"));
        leftPane.register_control(
            leftPane.add_select_button({
                .key = korean::text("Menu Scaling Mode"),
                .getValue =
                    [] {
                        return kMenuScalingModeLabels[static_cast<u8>(
                            getSettings().game.menuScalingMode.getValue())];
                    },
                .isModified =
                    [] {
                        const auto& mode = getSettings().game.menuScalingMode;
                        return mode.getValue() != mode.getDefaultValue();
                    },
            }),
            rightPane, [](Pane& pane) {
                for (int i = 0; i < static_cast<int>(kMenuScalingModeLabels.size()); ++i) {
                    pane
                        .add_button({
                            .text = kMenuScalingModeLabels[i],
                            .isSelected =
                                [i] {
                                    return getSettings().game.menuScalingMode.getValue() ==
                                           static_cast<MenuScaling>(i);
                                },
                        })
                        .on_pressed([i] {
                            mDoAud_seStartMenu(kSoundItemChange);
                            getSettings().game.menuScalingMode.setValue(
                                static_cast<MenuScaling>(i));
                            config::save();
                        });
                }
                pane.add_rml(korean::text("<br/>Changes how the Collection and File Select menus scale to your "
                             "aspect ratio."));
            });
        config_bool_select(leftPane, rightPane, getSettings().game.hideTvSettingsScreen,
            {
                .key = korean::text("Skip TV Settings Screen"),
                .helpText = korean::text("Skips the TV calibration screen shown when loading a save."),
            });
        add_speedrun_disabled_option(leftPane, rightPane, getSettings().game.recordingMode,
            korean::text("Recording Mode"),
            korean::text("Disables the game HUD and all background music.<br/><br/>Useful for recording footage."));
    });

    add_tab("Tools", [this](Rml::Element* content) {
        auto& leftPane = add_child<Pane>(content, Pane::Type::Controlled);
        auto& rightPane = add_child<Pane>(content, Pane::Type::Uncontrolled);

        leftPane.add_section(korean::text("Link"));
        add_speedrun_disabled_option(leftPane, rightPane, getSettings().game.enableMoveLinkCombo,
            korean::text("Move Link (L+R+Y)"),
            korean::text("Enables the L+R+Y button combo to toggle freely repositioning Link."));
        add_speedrun_disabled_option(leftPane, rightPane, getSettings().game.enableTeleportCombo,
            korean::text("Teleport (R+D-pad Up/Down)"),
            korean::text("R+D-pad Up stores Link's current position.<br/>"
            "R+D-pad Down teleports Link back to it."));
    });
    // Korean display labels; the original tab identifiers remain unchanged.
    mTabBar->set_tab_label("Prelaunch", korean::text("Prelaunch"));
    mTabBar->set_tab_label("Video", korean::text("Video"));
    mTabBar->set_tab_label("Input", korean::text("Input"));
    mTabBar->set_tab_label("Audio", korean::text("Audio"));
    mTabBar->set_tab_label("Gameplay", korean::text("Gameplay"));
    mTabBar->set_tab_label("Cheats", korean::text("Cheats"));
    mTabBar->set_tab_label("Interface", korean::text("Interface"));
    mTabBar->set_tab_label("Tools", korean::text("Tools"));
}

void SettingsWindow::update() {
    if (mPrelaunch && top_document() == this) {
        try_push_verification_modal(*this);
        try_push_language_unavailable_modal(*this);
    }

    Window::update();
}

void SettingsWindow::hide(bool close) {
    config::save();
    Window::hide(close);
}

}  // namespace dusk::ui
