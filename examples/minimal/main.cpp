#include "guikit/native.hpp"

int main(int /*argc*/, char* /*argv*/[]) {
    guikit::native::RegisterNativePlatform();

    bool autopilot = true;
    bool darkMode = true;
    keel::f32 thrustPct = 72.5f;
    keel::i32 pulseCount = 0;
    keel::String stationName = keel::String::from("ALPHA-07");

    guikit::MainWindow app({
        .title = "GuiKit Native (X11 + EGL + OpenGL ES 3.0 — Zero SDL3 / Zero ThorVG)",
        .width = 860,
        .height = 560
    });

    return app.Run([&](guikit::Context& ui) {
        if (auto root = guikit::Column(ui, {
            .gap = 0.0f,
            .padding = guikit::Padding::All(0.0f),
            .width = guikit::SizeMode::Stretch,
            .height = guikit::SizeMode::Stretch
        })) {
            if (auto mb = guikit::MenuBar(ui, {.style = guikit::MenuBarStyle::Header})) {
                if (auto file = mb.Menu("File")) {
                    if (file.Item("Reset Telemetry", {.shortcut = "Ctrl+R"})) {
                        thrustPct = 50.0f;
                        pulseCount = 0;
                    }
                    file.Separator();
                    if (file.Item("Exit", {.variant = guikit::UiVariant::Danger})) {
                        app.Close();
                    }
                }
                if (auto view = mb.Menu("View")) {
                    if (view.Checkbox("Dark Theme", darkMode, {.keepOpen = true})) {
                        app.SetShellLightMode(!darkMode);
                    }
                }
            }

            if (auto center = guikit::Column(ui, {
                .padding = guikit::Padding::All(24.0f),
                .align = guikit::Alignment::Center,
                .justify = guikit::Justify::Center,
                .width = guikit::SizeMode::Stretch,
                .height = guikit::SizeMode::Stretch
            })) {
                if (auto card = guikit::Card(ui, {
                    .minWidth = 480.0f,
                    .width = guikit::SizeMode::Fit,
                    .gap = 12.0f,
                    .padding = guikit::Padding::All(20.0f),
                    .surface = guikit::SurfaceRole::Raised
                })) {
                    guikit::Heading(ui, {
                        .label = "GuiKit Native GPU Engine",
                        .description = "X11 + EGL + OpenGL ES 3.0 + FreeType Atlas (0 SDL3, 0 ThorVG)",
                        .icon = guikit::Icon::Play(),
                        .separator = guikit::HeadingSeparator::Bottom
                    });

                    if (auto row = guikit::Row(ui, {.gap = 8.0f})) {
                        guikit::Tag(ui, "OpenGL ES 3.0", {.variant = guikit::UiVariant::Success});
                        guikit::Tag(ui, "X11 + EGL", {.variant = guikit::UiVariant::Info});
                        guikit::Tag(ui, "Zero SDL3", {.variant = guikit::UiVariant::Primary});
                    }

                    if (auto el = guikit::Element(ui, {
                        .label = "Station Callsign",
                        .description = "Try typing UTF-8 text directly in native X11"
                    })) {
                        guikit::InputField(ui, stationName, {.placeholder = "Enter callsign..."});
                    }

                    if (auto el = guikit::Element(ui, {
                        .label = "Autopilot Loop",
                        .description = "Closed-loop attitude stabilization"
                    })) {
                        guikit::Switch(ui, autopilot);
                    }

                    if (auto el = guikit::Element(ui, {.label = "Main Thruster Output"})) {
                        guikit::Slider(ui, thrustPct, 0.0f, 100.0f, {.valueFormat = "%.1f %%"});
                    }

                    if (auto row = guikit::Row(ui, {.gap = 8.0f, .align = guikit::Alignment::Center})) {
                        if (guikit::Button(ui, {
                            .text = "Trigger Pulse",
                            .icon = guikit::Icon::Flash(),
                            .variant = guikit::UiVariant::Primary
                        })) {
                            ++pulseCount;
                        }
                        if (guikit::Button(ui, {
                            .text = "Toggle Theme",
                            .variant = guikit::UiVariant::Secondary
                        })) {
                            darkMode = !darkMode;
                            app.SetShellLightMode(!darkMode);
                        }
                        guikit::Spacer(ui);
                        guikit::Text(ui, ui.Format("Pulses: {}", pulseCount), {
                            .role = guikit::TextRole::CodeMedium,
                            .variant = guikit::UiVariant::Info
                        });
                    }
                }
            }

            if (auto sb = guikit::StatusBar(ui, {})) {
                guikit::Text(ui, app.GetMetrics().GetStatusString(), {.role = guikit::TextRole::BodySmall});
                guikit::Spacer(ui);
                guikit::Text(ui, guikit::native::GetBackendSummary(), {.role = guikit::TextRole::BodySmall, .variant = guikit::UiVariant::Muted});
            }
        }
    });
}
