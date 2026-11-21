#include "guikit/native.hpp"

int main(int /*argc*/, char* /*argv*/[]) {
    guikit::native::RegisterNativeWindowBackend();

    bool autopilot = true;
    bool darkMode = true;
    float thrustPct = 72.5f;
    keel::i32 pulseCount = 0;
    keel::String stationName = keel::String::from("ALPHA-07");

    guikit::MainWindow app({
        .title = "GuiKit Native (X11 + EGL + OpenGL ES 3.0 — Zero SDL3 / Zero ThorVG)",
        .width = 860,
        .height = 560
    });

    return app.Run([&](guikit::Context& ui) {
        if (auto root = ui.Column({
            .gap = 0.0f,
            .padding = guikit::Padding::All(0.0f),
            .width = guikit::SizeMode::Stretch,
            .height = guikit::SizeMode::Stretch
        })) {
            if (auto mb = ui.MenuBar({.style = guikit::MenuBarStyle::Header})) {
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

            if (auto center = ui.Column({
                .padding = guikit::Padding::All(24.0f),
                .align = guikit::Alignment::Center,
                .justify = guikit::Justify::Center,
                .width = guikit::SizeMode::Stretch,
                .height = guikit::SizeMode::Stretch
            })) {
                if (auto card = ui.Card({
                    .minWidth = 480.0f,
                    .width = guikit::SizeMode::Fit,
                    .gap = 12.0f,
                    .padding = guikit::Padding::All(20.0f),
                    .elevated = true
                })) {
                    ui.Heading({
                        .label = "GuiKit Native GPU Engine",
                        .description = "X11 + EGL + OpenGL ES 3.0 + FreeType Atlas (0 SDL3, 0 ThorVG)",
                        .icon = guikit::Icon::Play(),
                        .separator = guikit::HeadingSeparator::Bottom
                    });

                    if (auto row = ui.Row({.gap = 8.0f})) {
                        ui.Tag("OpenGL ES 3.0", {.variant = guikit::UiVariant::Success});
                        ui.Tag("X11 + EGL", {.variant = guikit::UiVariant::Info});
                        ui.Tag("Zero SDL3", {.variant = guikit::UiVariant::Primary});
                    }

                    if (auto el = ui.Element({
                        .label = "Station Callsign",
                        .description = "Try typing UTF-8 text directly in native X11"
                    })) {
                        ui.InputField(stationName, {.placeholder = "Enter callsign..."});
                    }

                    if (auto el = ui.Element({
                        .label = "Autopilot Loop",
                        .description = "Closed-loop attitude stabilization"
                    })) {
                        ui.Switch(autopilot);
                    }

                    if (auto el = ui.Element({.label = "Main Thruster Output"})) {
                        ui.Slider(thrustPct, 0.0f, 100.0f, {.valueFormat = "%.1f %%"});
                    }

                    if (auto row = ui.Row({.gap = 8.0f, .align = guikit::Alignment::Center})) {
                        if (ui.Button({
                            .text = "Trigger Pulse",
                            .icon = guikit::Icon::Flash(),
                            .color = guikit::StateColor::Primary()
                        })) {
                            ++pulseCount;
                        }
                        if (ui.Button({
                            .text = "Toggle Theme",
                            .color = guikit::StateColor::Secondary()
                        })) {
                            darkMode = !darkMode;
                            app.SetShellLightMode(!darkMode);
                        }
                        ui.Spacer();
                        ui.Text(ui.Format("Pulses: {}", pulseCount), {
                            .role = guikit::TextRole::CodeMedium,
                            .variant = guikit::UiVariant::Info
                        });
                    }
                }
            }

            if (auto sb = ui.StatusBar({})) {
                ui.Text(app.GetMetrics().GetStatusString(), {.role = guikit::TextRole::BodySmall});
                ui.Spacer();
                ui.Text(guikit::native::GetBackendSummary(), {.role = guikit::TextRole::BodySmall, .variant = guikit::UiVariant::Muted});
            }
        }
    });
}
