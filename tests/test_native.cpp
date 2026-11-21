#include "guikit/native.hpp"
#include "keel/keel.h"
#include "keel/math.h"

static keel::i32 g_failures = 0;

#define CHECK(cond)                                                                  \
    do {                                                                             \
        if (!(cond)) {                                                               \
            keel::eprintln("[FAIL] {} ({}:{})", #cond, __FILE__, __LINE__);          \
            ++g_failures;                                                            \
        }                                                                            \
    } while (0)

int main() {
    using namespace guikit;
    using namespace guikit::native;

    keel::println("[RUN] Testing GuiKit Native backend module...");

    // 1. Compile-time capability probes
    CHECK(IsPlatformCompiled(NativePlatform::Headless));
    CHECK(IsRendererCompiled(NativeRenderer::Null));
    CHECK(IsRendererCompiled(NativeRenderer::OpenGLES3));
    CHECK(!GetBackendSummary().empty());

    // 2. GlEs3Canvas handles null procLoader gracefully without crashing
    {
        GlEs3Canvas uninitCanvas(nullptr, false);
        CHECK(!uninitCanvas.IsInitialized());
        CHECK(!uninitCanvas.UsesCpuFramebuffer());
        uninitCanvas.Begin(640, 480, 1.0f);
        uninitCanvas.Clear(Color(10, 14, 20));
        uninitCanvas.DrawRect(Rect(0, 0, 100, 50), Color(255, 0, 0));
        uninitCanvas.DrawRoundRect(Rect(10, 10, 80, 30), 6.0f, Color(0, 128, 255), Color(255, 255, 255), 1.0f);
        uninitCanvas.DrawLine({0, 0}, {50, 50}, Color(255, 255, 255), 2.0f);
        uninitCanvas.End();
    }

    // 3. Live X11 + EGL + OpenGL ES 3.0 smoke test when an X11 display & EGL driver are available
    if (X11PlatformWindow::IsRuntimeAvailable()) {
        keel::println("  -> Live X11 + EGL runtime detected; verifying GlEs3Canvas shader compile & full UI frame...");
        auto x11Win = CreateX11PlatformWindow();
        CHECK(x11Win != nullptr);

        Context ui;
        PlatformWindowDesc desc{};
        desc.title = "GuiKit Native ES3 Test";
        desc.width = 400;
        desc.height = 300;
        desc.vsync = false;
        desc.graphicsApi = GraphicsApi::OpenGLES3;

        if (x11Win && x11Win->Create(desc, ui, {})) {
            CHECK(x11Win->MakeContextCurrent());
            CHECK(x11Win->GetGlProcAddressFn() != nullptr);

            GlEs3Canvas glCanvas(x11Win->GetGlProcAddressFn(), true);
            CHECK(glCanvas.IsInitialized());
            CHECK(!glCanvas.GetGlVersionString().empty());
            keel::println("     GL Version:  {}", glCanvas.GetGlVersionString());
            keel::println("     GL Renderer: {}", glCanvas.GetGlRendererString());
            keel::println("     Text Engine: {}", glCanvas.GetTextEngineName());

            // Measure text & render full immediate-mode UI frame + custom path + pixels
            const Vec2 tm = glCanvas.MeasureText("Hello ES3", 16.0f);
            CHECK(tm.x > 20.0f && tm.y > 10.0f);

            keel::Array<keel::u32> samplePixels(16u * 16u, 0xFF38BDF8u);
            VectorPath poly;
            poly.MoveTo({20.0f, 20.0f});
            poly.LineTo({60.0f, 20.0f});
            poly.CubicTo({70.0f, 35.0f}, {70.0f, 55.0f}, {50.0f, 65.0f});
            poly.LineTo({20.0f, 60.0f});
            poly.Close();

            ui.SetDirtyRectEnabled(false);
            for (keel::i32 f = 0; f < 2; ++f) {
                ui.BeginFrame({400.0f, 300.0f}, 1.0f);
                if (auto col = ui.Column({.gap = 8.0f, .padding = Padding::All(16.0f)})) {
                    ui.Heading({.label = "Native ES3 Smoke Test"});
                    (void)ui.Button({.text = "Click Me", .color = StateColor::Primary()});
                    ui.Tag("ES3 OK", {.variant = UiVariant::Success});
                }
                ui.EndFrame(glCanvas);

                glCanvas.Begin(400, 300, 1.0f);
                glCanvas.DrawPath(poly, PathPaint{
                    .fillColor = Color(56, 189, 248, 200),
                    .strokeColor = Color(255, 255, 255, 255),
                    .strokeWidthPx = 1.5f,
                    .renderFill = true,
                    .renderStroke = true
                });
                CHECK(glCanvas.DrawPixels({samplePixels, 16u, 16u}, Rect(100.0f, 100.0f, 48.0f, 48.0f), 6.0f, 255));
                glCanvas.End();
                x11Win->SwapBuffers();
            }
            glCanvas.TrimCaches();
            x11Win->Destroy();
        } else {
            keel::println("  -> X11 display unreachable in current environment; skipping live GL test.");
        }
    }

    if (g_failures == 0) {
        keel::println("[PASS] All GuiKit Native tests passed successfully!");
        return 0;
    }
    keel::eprintln("[FAIL] Total failures: {}", g_failures);
    return 1;
}
