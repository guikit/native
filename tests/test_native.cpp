#include "guikit/native.hpp"
#include "guikit/backend/vector_backend.hpp"
#include "guikit/core/assets.hpp"
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
#if defined(__EMSCRIPTEN__)
    CHECK(IsPlatformCompiled(NativePlatform::Emscripten));
#else
    CHECK(!IsPlatformCompiled(NativePlatform::Emscripten));
    CHECK(CreateEmscriptenPlatformWindow() == nullptr);
#endif

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

    // 2.5. Built-in keel::imagefile decoder in guikit::core (InspectImageFromMemory + DecodeImageFromMemory)
    {
        RegisterNativePlatform();
        const auto iconAsset = Assets::Find("assets/guikit-icon-inter850-1024.png");
        CHECK(iconAsset.has_value() && !iconAsset->Empty());
        if (iconAsset && !iconAsset->Empty()) {
            keel::u32 pw = 0u;
            keel::u32 ph = 0u;
            CHECK(InspectImageFromMemory(iconAsset->Bytes(), pw, ph));
            CHECK(pw == 1024u && ph == 1024u);

            keel::u32 w = 0u;
            keel::u32 h = 0u;
            keel::Array<keel::u32> rgba;
            CHECK(DecodeImageFromMemory(iconAsset->Bytes(), w, h, rgba));
            CHECK(w == 1024u && h == 1024u);
            CHECK(rgba.size() == 1024u * 1024u);
            keel::u32 nonZero = 0u;
            for (keel::usize i = 0u; i < rgba.size(); i += 257u) {
                if ((rgba[i] >> 24) > 0u) ++nonZero;
            }
            CHECK(nonZero > 100u);
        }
    }

    // 3. Live native platform window + OpenGL ES3 / GL3.3 Core smoke test when a display runtime is available
    const bool hasLiveRuntime = X11PlatformWindow::IsRuntimeAvailable() || Win32PlatformWindow::IsRuntimeAvailable();
    if (hasLiveRuntime) {
        auto nativeWin = CreateNativePlatformWindow(NativePlatform::Auto);
        CHECK(nativeWin != nullptr);
        if (nativeWin) {
            keel::println("  -> Live native runtime detected ({}); verifying GlEs3Canvas shader compile & full UI frame...",
                          nativeWin->GetPlatformName());
        }

        Context ui;
        PlatformWindowDesc desc{};
        desc.title = "GuiKit Native GPU Test";
        desc.width = 400;
        desc.height = 300;
        desc.vsync = false;
#if defined(_WIN32)
        desc.graphicsApi = GraphicsApi::OpenGL3;
#else
        desc.graphicsApi = GraphicsApi::OpenGLES3;
#endif

        if (nativeWin && nativeWin->Create(desc, ui, {})) {
            CHECK(nativeWin->MakeContextCurrent());
            CHECK(nativeWin->GetGlProcAddressFn() != nullptr);

            GlEs3Canvas glCanvas(nativeWin->GetGlProcAddressFn(), true);
            CHECK(glCanvas.IsInitialized());
            CHECK(!glCanvas.GetGlVersionString().empty());
            keel::println("     GL Version:  {}", glCanvas.GetGlVersionString());
            keel::println("     GL Renderer: {}", glCanvas.GetGlRendererString());
            keel::println("     Text Engine: {}", glCanvas.GetTextEngineName());
            keel::println("     Surface:     MSAA {}x, Stencil {} bits", glCanvas.GetMsaaSamples(), glCanvas.GetStencilBits());
            CHECK(glCanvas.GetStencilBits() >= 8);

            // Verify system clipboard round-trip (owned by this window)
            ui.SetClipboardText("GuiKit Native UTF-8 ✓");
            CHECK(ui.GetClipboardText() == "GuiKit Native UTF-8 ✓");

            // Measure text & render full immediate-mode UI frame + custom path + pixels + image + GPU shader pass
            const Vec2 tm = glCanvas.MeasureText("Hello ES3", 16.0f);
            CHECK(tm.x > 20.0f && tm.y > 10.0f);
            CHECK(glCanvas.SupportsGpuShaders());

            const Vec2 iconSize = glCanvas.GetImageSize("assets/guikit-icon-inter850-1024.png");
            CHECK(keel::abs(iconSize.x - 1024.0f) < 0.5f && keel::abs(iconSize.y - 1024.0f) < 0.5f);

            keel::Array<keel::u32> samplePixels(16u * 16u, 0xFF38BDF8u);
            keel::Array<keel::f32> gpuField(32u * 64u, 0.5f);
            keel::Array<keel::u32> gpuLut(256u, 0xFF22D3EEu);

            static constexpr keel::StringRef kTestPassFrag = R"(
#ifdef GL_ES
precision highp float;
#endif
varying vec2 v_uv;
uniform vec2 u_GridDims;
uniform vec3 u_BgColor;
uniform vec4 u_Params[8];
uniform sampler2D u_DataTex;
uniform sampler2D u_LutTex;
void main() {
    vec4 d = texture2D(u_DataTex, v_uv);
    float val = (d.r * 65280.0 + d.g * 255.0) / 65535.0;
    vec4 col = texture2D(u_LutTex, vec2(clamp(val + u_Params[0].x, 0.0, 1.0), 0.5));
    gl_FragColor = vec4(mix(u_BgColor, col.rgb, 0.8), 1.0);
}
)";

            VectorPath poly;
            poly.MoveTo({20.0f, 20.0f});
            poly.LineTo({60.0f, 20.0f});
            poly.CubicTo({70.0f, 35.0f}, {70.0f, 55.0f}, {50.0f, 65.0f});
            poly.LineTo({20.0f, 60.0f});
            poly.Close();

            // Concentric EvenOdd polygon with interior island hole (exercises GPU Stencil-then-Cover)
            VectorPath donut;
            const Vec2 outerRing[4] = {{80.0f, 20.0f}, {140.0f, 20.0f}, {140.0f, 80.0f}, {80.0f, 80.0f}};
            const Vec2 innerHole[4] = {{95.0f, 35.0f}, {125.0f, 35.0f}, {125.0f, 65.0f}, {95.0f, 65.0f}};
            donut.AddClosedRing({outerRing, 4u});
            donut.AddClosedRing({innerHole, 4u});

            ui.SetDirtyRectEnabled(false);
            for (keel::i32 f = 0; f < 2; ++f) {
                ui.BeginFrame({400.0f, 300.0f}, 1.0f);
                if (auto col = guikit::Column(ui, {.gap = 8.0f, .padding = Padding::All(16.0f)})) {
                    guikit::Heading(ui, {.label = "Native ES3 Smoke Test"});
                    (void)guikit::Button(ui, {.text = "Click Me", .color = StateColor::Primary()});
                    guikit::Tag(ui, "ES3 OK", {.variant = UiVariant::Success});
                }
                ui.EndFrame(glCanvas);

                glCanvas.Begin(400, 300, 1.0f);
                CHECK(!glCanvas.HasActiveGpuPasses());

                ICanvas::GpuShaderPassDesc pass{};
                pass.widgetId = 42u;
                pass.shader = {.shaderId = "test.native.pass.v1", .glslEs2 = kTestPassFrag};
                pass.plotRect = Rect(160.0f, 120.0f, 180.0f, 120.0f);
                pass.bgColor = Color(8, 12, 20, 255);
                pass.dataField = {gpuField, 32u, 64u};
                pass.fullGeneration = 1u;
                pass.sliceGeneration = keel::as<keel::u64>(f + 1);
                pass.headRow = keel::as<keel::u32>(f);
                pass.dataNormOffset = 0.0f;
                pass.dataNormScale = 1.0f;
                pass.colormapId = 0u;
                pass.lutAbgr256 = gpuLut;
                pass.params[0] = {0.1f, 0.0f, 0.0f, 0.0f};
                CHECK(glCanvas.DrawGpuShaderPass(pass, true));
                CHECK(glCanvas.HasActiveGpuPasses());

                glCanvas.DrawPath(poly, PathPaint{
                    .fillColor = Color(56, 189, 248, 200),
                    .strokeColor = Color(255, 255, 255, 255),
                    .strokeWidthPx = 1.5f,
                    .renderFill = true,
                    .renderStroke = true,
                    .dashed = true,
                    .dashOnPx = 6.0f,
                    .dashOffPx = 4.0f
                });
                glCanvas.DrawPath(donut, PathPaint{
                    .fillColor = Color(34, 197, 94, 220),
                    .strokeColor = Color(255, 255, 255, 255),
                    .strokeWidthPx = 1.5f,
                    .fillRule = FillRule::EvenOdd,
                    .renderFill = true,
                    .renderStroke = true
                });
                // Analytical GPU SDF arcs (270 deg rounded knob arc, flat-cap arc, full 360 deg ring) + Gaussian shadow
                glCanvas.DrawShadow(Rect(210.0f, 16.0f, 170.0f, 68.0f), 10.0f, 16.0f, {0.0f, 5.0f}, Color(0, 0, 0, 110));
                glCanvas.DrawArc({240.0f, 50.0f}, 24.0f, -1.25f * keel::kPiF, 0.25f * keel::kPiF, Color(59, 130, 246, 255), 4.0f, true);
                glCanvas.DrawArc({300.0f, 50.0f}, 22.0f, -0.75f * keel::kPiF, 0.0f, Color(245, 158, 11, 220), 3.0f, false);
                glCanvas.DrawArc({355.0f, 50.0f}, 18.0f, 0.0f, keel::kTauF, Color(148, 163, 184, 200), 2.0f, false);
                glCanvas.DrawTextStyled("Vertical -90", {20.0f, 200.0f}, 14.0f, Color(255, 255, 255), FontStyle::Bold, -90.0f);

                // Verify production text rendering settings (LCD RGB/BGR, Grayscale, contrast, stemDarkening, gamma)
                TextRenderSettings trs{};
                trs.smoothing = FontSmoothing::SubpixelRGB;
                trs.hinting = FontHinting::Light;
                trs.gammaCorrectBlending = true;
                trs.contrast = 1.35f;
                trs.stemDarkening = 0.25f;
                glCanvas.SetTextRenderSettings(trs);
                glCanvas.DrawTextStyled("Subpixel RGB + Contrast + Gamma", {20.0f, 220.0f}, 13.0f, Color(248, 250, 252), FontStyle::Regular, 0.0f);

                trs.smoothing = FontSmoothing::SubpixelBGR;
                trs.contrast = 0.85f;
                trs.stemDarkening = 0.40f;
                glCanvas.SetTextRenderSettings(trs);
                glCanvas.DrawTextStyled("Subpixel BGR + Stem Darkening", {20.0f, 240.0f}, 13.0f, Color(15, 23, 42), FontStyle::Monospace, 0.0f);

                trs.smoothing = FontSmoothing::Grayscale;
                trs.gammaCorrectBlending = false;
                trs.contrast = 1.0f;
                trs.stemDarkening = 0.15f;
                glCanvas.SetTextRenderSettings(trs);
                glCanvas.DrawTextStyled("Grayscale Linear", {20.0f, 260.0f}, 13.0f, Color(220, 230, 240), FontStyle::Italic, 0.0f);

                CHECK(glCanvas.DrawImage("assets/guikit-icon-inter850-1024.png", Rect(20.0f, 100.0f, 48.0f, 48.0f), 8.0f, 255));
                CHECK(glCanvas.DrawPixels({samplePixels, 16u, 16u}, Rect(100.0f, 100.0f, 48.0f, 48.0f), 6.0f, 255));
                glCanvas.End();
                nativeWin->SwapBuffers();
            }
            glCanvas.TrimCaches();
            nativeWin->Destroy();
        } else {
            keel::println("  -> Native display unreachable in current environment; skipping live GL test.");
        }
    }

    if (g_failures == 0) {
        keel::println("[PASS] All GuiKit Native tests passed successfully!");
        return 0;
    }
    keel::eprintln("[FAIL] Total failures: {}", g_failures);
    return 1;
}
