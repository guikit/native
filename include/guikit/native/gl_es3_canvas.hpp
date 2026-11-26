#pragma once

#include "guikit/backend/i_canvas.hpp"
#include "guikit/backend/i_platform_window.hpp"
#include "keel/smart_ptr.h"

namespace guikit::native {

/// Hardware-accelerated 2D `ICanvas` implementation targeting OpenGL ES 3.0 and OpenGL 3.3 Core.
///
/// Uses an instanced SDF quad batcher (rounded rectangles, borders, anti-aliased lines,
/// textured quads, FreeType glyph atlas) plus a triangulated path/polygon renderer.
/// Zero external GL loader dependencies (`glad`/`glew` not required); all GL entry points
/// are resolved dynamically through `GlProcAddressFn`.
class GlEs3Canvas final : public ICanvas, public IGpuPassRenderer {
public:
    explicit GlEs3Canvas(GlProcAddressFn procLoader, bool loadDefaultFonts = true);
    ~GlEs3Canvas() override;

    GlEs3Canvas(const GlEs3Canvas&) = delete;
    GlEs3Canvas& operator=(const GlEs3Canvas&) = delete;

    [[nodiscard]] IGpuPassRenderer* QueryGpuPassRenderer() noexcept override { return this; }
    [[nodiscard]] const IGpuPassRenderer* QueryGpuPassRenderer() const noexcept override { return this; }

    /// Returns true if OpenGL entry points, shaders, and GPU buffers initialized successfully.
    [[nodiscard]] bool IsInitialized() const noexcept;

    /// Returns true if running against a desktop OpenGL 3.3+ Core context instead of OpenGL ES 3.0.
    [[nodiscard]] bool IsDesktopGl() const noexcept;

    [[nodiscard]] keel::StringRef GetGlVersionString() const noexcept;
    [[nodiscard]] keel::StringRef GetGlRendererString() const noexcept;
    [[nodiscard]] keel::i32 GetMsaaSamples() const noexcept;
    [[nodiscard]] keel::i32 GetStencilBits() const noexcept;

    bool LoadFontFile(keel::StringRef path, FontStyle style = FontStyle::Regular) override;
    bool LoadFontMemory(
        keel::StringRef name,
        keel::ArrayRef<keel::u8> data,
        FontStyle style = FontStyle::Regular,
        keel::StringRef mimeType = "ttf"
    ) override;
    bool AddFallbackFontMemory(
        keel::ArrayRef<keel::u8> data,
        bool copyData = true
    ) override;
    void ClearFallbackFonts() noexcept override;

    void Begin(keel::u32 width, keel::u32 height, keel::f32 scale = 1.0f) override;
    void End() override;

    [[nodiscard]] keel::f32 GetScale() const noexcept override;
    void SetScale(keel::f32 scale) override;

    void PushTransform(const Transform2D& transform) override;
    void PopTransform() override;

    [[nodiscard]] keel::StringRef GetBackendName() const noexcept override;

    void Clear(Color color, keel::Optional<Rect> rect = keel::kNullOpt) override;
    [[nodiscard]] bool SupportsRoundedClip() const noexcept override { return true; }
    void PushClip(Rect clipRect) override;
    void PushRoundedClip(Rect clipRect, keel::f32 cornerRadius) override;
    void PopClip() override;

    void DrawRect(Rect rect, Color fill) override;
    void DrawRoundRect(
        Rect rect,
        keel::f32 radius,
        Color fill,
        Color stroke = Color(0, 0, 0, 0),
        keel::f32 strokeWidth = 0.0f
    ) override;
    void DrawShadow(
        Rect rect,
        keel::f32 cornerRadius,
        keel::f32 blurRadius,
        Vec2 offset = {0.0f, 4.0f},
        Color color = Color(0, 0, 0, 96),
        keel::f32 spread = 0.0f
    ) override;
    [[nodiscard]] bool SupportsBackdropBlur() const noexcept override;
    void DrawGlassRect(Rect rect, keel::f32 radius, const GlassParams& params) override;

    Vec2 MeasureText(keel::StringRef text, keel::f32 fontSize) override;
    Vec2 MeasureTextStyled(keel::StringRef text, keel::f32 fontSize, FontStyle style) override;
    [[nodiscard]] keel::f32 GetLineHeight(keel::f32 fontSize) override;

    void DrawText(keel::StringRef text, Vec2 pos, keel::f32 fontSize, Color color) override;
    void DrawTextStyled(
        keel::StringRef text,
        Vec2 pos,
        keel::f32 fontSize,
        Color color,
        FontStyle style,
        keel::f32 angleDeg = 0.0f
    ) override;
    void DrawTextBox(
        keel::StringRef text,
        Rect bounds,
        keel::f32 fontSize,
        Color color,
        keel::f32 alignX = 0.0f,
        keel::f32 alignY = 0.5f
    ) override;
    void DrawTextBoxStyled(
        keel::StringRef text,
        Rect bounds,
        keel::f32 fontSize,
        Color color,
        FontStyle style,
        keel::f32 alignX = 0.0f,
        keel::f32 alignY = 0.5f
    ) override;

    void DrawLine(Vec2 p1, Vec2 p2, Color color, keel::f32 strokeWidth = 1.0f) override;
    void DrawArc(
        Vec2 center,
        keel::f32 radius,
        keel::f32 startAngleRad,
        keel::f32 endAngleRad,
        Color color,
        keel::f32 strokeWidth = 1.5f,
        bool roundCaps = true
    ) override;
    void DrawTriangle(
        Vec2 p1,
        Vec2 p2,
        Vec2 p3,
        Color fill,
        Color stroke = Color(0, 0, 0, 0),
        keel::f32 strokeWidth = 0.0f
    ) override;
    void DrawPath(const VectorPath& path, const PathPaint& paint) override;

    Vec2 GetImageSize(keel::StringRef source) override;
    bool DrawImage(
        keel::StringRef source,
        Rect destRect,
        keel::f32 cornerRadius = 0.0f,
        keel::u8 opacity = 255
    ) override;

    bool DrawPixels(
        PixelRef pixels,
        Rect destRect,
        keel::f32 cornerRadius = 0.0f,
        keel::u8 opacity = 255
    ) override;
    void DrawTexture(
        keel::u64 textureId,
        Rect destRect,
        keel::Optional<Rect> srcRect = keel::kNullOpt,
        Color tint = Color(255, 255, 255, 255)
    ) override;

    void SetGpuShadersEnabled(bool enabled) noexcept override;
    [[nodiscard]] bool SupportsGpuShaders() const noexcept override;
    [[nodiscard]] bool HasActiveGpuPasses() const noexcept override;
    [[nodiscard]] keel::ArrayRef<GpuShaderPassDesc> GetGpuShaderPasses() const noexcept override { return {}; }
    bool DrawGpuShaderPass(const GpuShaderPassDesc& pass, bool punchHole = true) override;

    void TrimCaches() noexcept override;
    [[nodiscard]] keel::Optional<keel::f32> GetLastGpuFrameTimeMs() const noexcept override;

    [[nodiscard]] keel::StringRef GetTextEngineName() const noexcept override;
    [[nodiscard]] bool SupportsFontHinting() const noexcept override;
    void SetTextRenderSettings(const TextRenderSettings& settings) noexcept override;
    [[nodiscard]] const TextRenderSettings& GetTextRenderSettings() const noexcept override;

private:
    struct Impl;
    keel::UniquePtr<Impl> m_impl;
};

} // namespace guikit::native
