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
class GlEs3Canvas final : public ICanvas {
public:
    explicit GlEs3Canvas(GlProcAddressFn procLoader, bool loadDefaultFonts = true);
    ~GlEs3Canvas() override;

    GlEs3Canvas(const GlEs3Canvas&) = delete;
    GlEs3Canvas& operator=(const GlEs3Canvas&) = delete;

    /// Returns true if OpenGL entry points, shaders, and GPU buffers initialized successfully.
    [[nodiscard]] bool IsInitialized() const noexcept;

    /// Returns true if running against a desktop OpenGL 3.3+ Core context instead of OpenGL ES 3.0.
    [[nodiscard]] bool IsDesktopGl() const noexcept;

    [[nodiscard]] keel::StringRef GetGlVersionString() const noexcept;
    [[nodiscard]] keel::StringRef GetGlRendererString() const noexcept;

    [[nodiscard]] bool UsesCpuFramebuffer() const noexcept override { return false; }

    bool LoadFontFile(keel::StringRef path, FontStyle style = FontStyle::Regular) override;
    bool LoadFontMemory(
        keel::StringRef name,
        keel::ArrayRef<keel::u8> data,
        FontStyle style = FontStyle::Regular,
        keel::StringRef mimeType = "ttf"
    ) override;

    void Begin(keel::u32 width, keel::u32 height, float scale = 1.0f) override;
    void End() override;

    [[nodiscard]] float GetScale() const noexcept override;
    void SetScale(float scale) override;

    void PushTransform(const Transform2D& transform) override;
    void PopTransform() override;

    [[nodiscard]] keel::StringRef GetBackendName() const noexcept override;

    void Clear(Color color, keel::Optional<Rect> rect = keel::kNullOpt) override;
    void PushClip(Rect clipRect) override;
    void PopClip() override;

    void DrawRect(Rect rect, Color fill) override;
    void DrawRoundRect(
        Rect rect,
        float radius,
        Color fill,
        Color stroke = Color(0, 0, 0, 0),
        float strokeWidth = 0.0f
    ) override;

    Vec2 MeasureText(keel::StringRef text, float fontSize) override;
    Vec2 MeasureTextStyled(keel::StringRef text, float fontSize, FontStyle style) override;
    [[nodiscard]] float GetLineHeight(float fontSize) override;

    void DrawText(keel::StringRef text, Vec2 pos, float fontSize, Color color) override;
    void DrawTextStyled(
        keel::StringRef text,
        Vec2 pos,
        float fontSize,
        Color color,
        FontStyle style,
        float angleDeg = 0.0f
    ) override;
    void DrawTextBox(
        keel::StringRef text,
        Rect bounds,
        float fontSize,
        Color color,
        float alignX = 0.0f,
        float alignY = 0.5f
    ) override;
    void DrawTextBoxStyled(
        keel::StringRef text,
        Rect bounds,
        float fontSize,
        Color color,
        FontStyle style,
        float alignX = 0.0f,
        float alignY = 0.5f
    ) override;

    void DrawLine(Vec2 p1, Vec2 p2, Color color, float strokeWidth = 1.0f) override;
    void DrawTriangle(
        Vec2 p1,
        Vec2 p2,
        Vec2 p3,
        Color fill,
        Color stroke = Color(0, 0, 0, 0),
        float strokeWidth = 0.0f
    ) override;
    void DrawPath(const VectorPath& path, const PathPaint& paint) override;

    bool DrawPixels(
        PixelRef pixels,
        Rect destRect,
        float cornerRadius = 0.0f,
        keel::u8 opacity = 255
    ) override;
    void DrawTexture(
        keel::u64 textureId,
        Rect destRect,
        keel::Optional<Rect> srcRect = keel::kNullOpt,
        Color tint = Color(255, 255, 255, 255)
    ) override;

    void TrimCaches() noexcept override;

    [[nodiscard]] keel::StringRef GetTextEngineName() const noexcept override;
    [[nodiscard]] bool SupportsFontHinting() const noexcept override;
    void SetTextRenderSettings(const TextRenderSettings& settings) noexcept override;
    [[nodiscard]] const TextRenderSettings& GetTextRenderSettings() const noexcept override;

private:
    struct Impl;
    keel::UniquePtr<Impl> m_impl;
};

} // namespace guikit::native
