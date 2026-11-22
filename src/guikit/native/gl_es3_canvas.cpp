#include "guikit/native/gl_es3_canvas.hpp"

#include "gl_es3_loader.hpp"
#include "guikit/backend/text_compositor.hpp"
#include "guikit/backend/vector_backend.hpp"
#include "guikit/core/assets.hpp"
#include "guikit/core/text_util.hpp"
#include "guikit/core/tokens.hpp"
#if (defined(GUIKIT_HAS_TEXT_ENGINE) && GUIKIT_HAS_TEXT_ENGINE) || (defined(GUIKIT_HAS_FREETYPE) && GUIKIT_HAS_FREETYPE)
#define GUIKIT_HAS_TEXT_RASTERIZER 1
#include "guikit/backend/text_engine.hpp"
#else
#define GUIKIT_HAS_TEXT_RASTERIZER 0
#endif
#include "keel/array.h"
#include "keel/format.h"
#include "keel/fs.h"
#include "keel/hash_map.h"
#include "keel/math.h"
#include "keel/small_array.h"
#include "keel/string.h"
#include "keel/utf8.h"

namespace guikit::native {

namespace {

using namespace detail;

struct QuadInstance {
    keel::f32 rx, ry, rw, rh;        // location 0: physical pixel rect (x, y, w, h)
    keel::f32 u0, v0, u1, v1;        // location 1: UVs (or p1.xy, p2.xy for Line mode)
    keel::f32 fr, fg, fb, fa;        // location 2: fill / tint RGBA [0..1]
    keel::f32 sr, sg, sb, sa;        // location 3: stroke RGBA [0..1]
    keel::f32 radius, strokeW, mode, extra; // location 4: params
};

struct TriVertex {
    keel::f32 x, y;
    keel::f32 r, g, b, a;
};

static constexpr keel::f32 kModeRoundRect   = 0.0f;
static constexpr keel::f32 kModeLine        = 1.0f;
static constexpr keel::f32 kModeTexture     = 2.0f;
static constexpr keel::f32 kModeGlyphR8     = 3.0f;
static constexpr keel::f32 kModeGlyphLcdRgb = 4.0f;
static constexpr keel::f32 kModeGlyphLcdBgr = 5.0f;
static constexpr keel::f32 kModeArc         = 6.0f;
static constexpr keel::f32 kModeShadow      = 7.0f;

static constexpr keel::u32 kAtlasSize    = 2048u;
static constexpr keel::u32 kLcdAtlasSize = 1024u;

static constexpr const char* kQuadVertBody = R"GLSL(
layout(location = 0) in vec4 a_Rect;
layout(location = 1) in vec4 a_Uv;
layout(location = 2) in vec4 a_Fill;
layout(location = 3) in vec4 a_Stroke;
layout(location = 4) in vec4 a_Params;

uniform vec2 u_Viewport;

out vec2 v_LocalPx;
out vec2 v_HalfSize;
out vec4 v_Uv;
out vec4 v_Fill;
out vec4 v_Stroke;
out vec4 v_Params;

void main() {
    vec2 corner;
    if (gl_VertexID == 0)      corner = vec2(0.0, 0.0);
    else if (gl_VertexID == 1) corner = vec2(1.0, 0.0);
    else if (gl_VertexID == 2) corner = vec2(1.0, 1.0);
    else if (gl_VertexID == 3) corner = vec2(0.0, 0.0);
    else if (gl_VertexID == 4) corner = vec2(1.0, 1.0);
    else                       corner = vec2(0.0, 1.0);

    float mode = a_Params.z;
    vec2 posPx;
    if (mode > 2.5 && mode < 5.5) {
        // Modes 3..5 (FreeType Glyphs): a_Rect=(localX,localY,w,h), a_Stroke.xy=pivotPx, a_Params.xy=(cosA,sinA)
        vec2 pLocal = a_Rect.xy + corner * a_Rect.zw;
        vec2 rot = vec2(pLocal.x * a_Params.x - pLocal.y * a_Params.y,
                        pLocal.x * a_Params.y + pLocal.y * a_Params.x);
        posPx = a_Stroke.xy + rot;
    } else {
        float pad = (mode < 1.5 || mode > 5.5) ? 1.5 : 0.0;
        vec2 rectMin = a_Rect.xy - vec2(pad);
        vec2 rectSize = a_Rect.zw + vec2(pad * 2.0);
        posPx = rectMin + corner * rectSize;
    }

    vec2 center = a_Rect.xy + a_Rect.zw * 0.5;
    v_HalfSize = a_Rect.zw * 0.5;
    v_LocalPx = (mode > 0.5 && mode < 1.5) ? posPx : (posPx - center);
    v_Uv = ((mode > 0.5 && mode < 1.5) || mode > 5.5) ? a_Uv : vec4(mix(a_Uv.xy, a_Uv.zw, corner), 0.0, 0.0);
    v_Fill = a_Fill;
    v_Stroke = a_Stroke;
    v_Params = a_Params;

    vec2 ndc = (posPx / max(u_Viewport, vec2(1.0))) * 2.0 - 1.0;
    ndc.y = -ndc.y;
    gl_Position = vec4(ndc, 0.0, 1.0);
}
)GLSL";

static constexpr const char* kQuadFragBody = R"GLSL(
in vec2 v_LocalPx;
in vec2 v_HalfSize;
in vec4 v_Uv;
in vec4 v_Fill;
in vec4 v_Stroke;
in vec4 v_Params;

uniform sampler2D u_Tex;

out vec4 FragColor;

float sdRoundedBox(vec2 p, vec2 b, float r) {
    float rr = min(r, min(b.x, b.y));
    vec2 q = abs(p) - b + vec2(rr);
    return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - rr;
}

float sdSegment(vec2 p, vec2 a, vec2 b) {
    vec2 pa = p - a;
    vec2 ba = b - a;
    float denom = max(dot(ba, ba), 1e-6);
    float h = clamp(dot(pa, ba) / denom, 0.0, 1.0);
    return length(pa - ba * h);
}

vec4 erfApprox(vec4 x) {
    vec4 s = sign(x);
    vec4 a = abs(x);
    x = 1.0 + (0.278393 + (0.230389 + 0.078108 * (a * a)) * a) * a;
    x *= x;
    return s - s / (x * x);
}

float roundedBoxShadowX(float x, float y, float sigma, float corner, vec2 halfSize) {
    float delta = min(halfSize.y - corner - abs(y), 0.0);
    float curved = halfSize.x - corner + sqrt(max(0.0, corner * corner - delta * delta));
    vec4 integral = 0.5 + 0.5 * erfApprox((x + vec4(-curved, curved, 0.0, 0.0)) * (0.70710678 / sigma));
    return integral.y - integral.x;
}

float roundedBoxShadow(vec2 p, vec2 halfSize, float sigma, float corner) {
    float r = min(corner, min(halfSize.x, halfSize.y));
    if (sigma < 0.5) {
        float d = sdRoundedBox(p, halfSize, r);
        return 1.0 - smoothstep(-0.65, 0.65, d);
    }
    float low = p.y - halfSize.y;
    float high = p.y + halfSize.y;
    float start = clamp(-3.0 * sigma, low, high);
    float end = clamp(3.0 * sigma, low, high);
    float step = (end - start) * 0.25;
    float y = start + step * 0.5;
    float value = 0.0;
    for (int i = 0; i < 4; ++i) {
        value += roundedBoxShadowX(p.x, p.y - y, sigma, r, halfSize)
               * exp(-(y * y) / (2.0 * sigma * sigma)) * (0.39894228 / sigma) * step;
        y += step;
    }
    return clamp(value, 0.0, 1.0);
}

void main() {
    float radius = v_Params.x;
    float strokeW = v_Params.y;
    float mode = v_Params.z;

    if (mode < 0.5) {
        // Mode 0: Solid / Rounded Rectangle + optional inner stroke
        float d = sdRoundedBox(v_LocalPx, v_HalfSize, radius);
        float aa = 0.65;
        float outerCov = 1.0 - smoothstep(-aa, aa, d);
        if (outerCov <= 0.001) discard;

        vec4 outCol = vec4(v_Fill.rgb, v_Fill.a * outerCov);
        if (strokeW > 0.0 && v_Stroke.a > 0.0) {
            float innerD = d + strokeW;
            float fillCov = 1.0 - smoothstep(-aa, aa, innerD);
            float borderCov = clamp(outerCov - fillCov, 0.0, 1.0);

            vec4 fillLayer = vec4(v_Fill.rgb * v_Fill.a, v_Fill.a) * fillCov;
            vec4 borderLayer = vec4(v_Stroke.rgb * v_Stroke.a, v_Stroke.a) * borderCov;
            vec4 combined = borderLayer + fillLayer * (1.0 - borderLayer.a);
            if (combined.a <= 0.001) discard;
            FragColor = vec4(combined.rgb / combined.a, combined.a);
            return;
        }
        if (outCol.a <= 0.001) discard;
        FragColor = outCol;
        return;
    }

    if (mode < 1.5) {
        // Mode 1: Anti-aliased line capsule
        float halfW = max(strokeW * 0.5, 0.5);
        float d = sdSegment(v_LocalPx, v_Uv.xy, v_Uv.zw) - halfW;
        float cov = 1.0 - smoothstep(-0.65, 0.65, d);
        float alpha = v_Fill.a * cov;
        if (alpha <= 0.001) discard;
        FragColor = vec4(v_Fill.rgb, alpha);
        return;
    }

    if (mode < 2.5) {
        // Mode 2: RGBA texture / DrawPixels with optional rounded-rect SDF clip
        vec4 texel = texture(u_Tex, v_Uv.xy);
        float mask = 1.0;
        if (radius > 0.0) {
            float d = sdRoundedBox(v_LocalPx, v_HalfSize, radius);
            mask = 1.0 - smoothstep(-0.65, 0.65, d);
        }
        vec4 col = texel * v_Fill;
        col.a *= mask;
        if (col.a <= 0.001) discard;
        FragColor = col;
        return;
    }

    if (mode > 6.5) {
        // Mode 7: Analytical Gaussian drop shadow for a rounded rectangle (single quad)
        // v_LocalPx = offset from shadow caster center, v_Uv.xy = caster halfSizePx
        // v_Params = (cornerRadiusPx, sigmaPx, 7.0, 0.0)
        float cov = roundedBoxShadow(v_LocalPx, v_Uv.xy, strokeW, radius);
        float alpha = v_Fill.a * cov;
        if (alpha <= 0.001) discard;
        FragColor = vec4(v_Fill.rgb, alpha);
        return;
    }

    if (mode > 5.5) {
        // Mode 6: Analytical circular arc SDF (exact circle + integrated round/flat caps, 256-level AA)
        // v_LocalPx = offset from arc center in physical pixels
        // v_Uv = (cosMid, sinMid, cosHalfAperture, sinHalfAperture)
        // v_Params = (arcRadiusPx, strokeWidthPx, 6.0, capMode: 0=flat, 1=round, 2=fullRing)
        float ra = radius;
        float rb = max(strokeW * 0.5, 0.5);
        float capMode = v_Params.w;
        float d;
        if (capMode > 1.5) {
            d = abs(length(v_LocalPx) - ra) - rb;
        } else {
            vec2 q = vec2(
                v_LocalPx.x * v_Uv.x + v_LocalPx.y * v_Uv.y,
                abs(-v_LocalPx.x * v_Uv.y + v_LocalPx.y * v_Uv.x)
            );
            vec2 sc = v_Uv.zw;
            float tang = q.y * sc.x - q.x * sc.y;
            float ringD = abs(length(q) - ra) - rb;
            if (capMode > 0.5) {
                d = (tang <= 0.0) ? ringD : (length(q - sc * ra) - rb);
            } else if (tang <= 0.0) {
                d = max(ringD, tang);
            } else {
                float rad = dot(q, sc);
                float radDist = max(abs(rad - ra) - rb, 0.0);
                d = length(vec2(radDist, tang));
            }
        }
        float cov = 1.0 - smoothstep(-0.65, 0.65, d);
        float alpha = v_Fill.a * cov;
        if (alpha <= 0.001) discard;
        FragColor = vec4(v_Fill.rgb, alpha);
        return;
    }

    // Modes 3..5: FreeType glyph atlas (3=Grayscale R8, 4=LCD RGB, 5=LCD BGR)
    // v_Stroke.z = invContrast, v_Stroke.w = gammaCorrectFlag, v_Params.w = effStemDarkening
    float invContrast = v_Stroke.z;
    float gammaFlag = v_Stroke.w;
    float effDarken = v_Params.w;

    if (mode < 3.5) {
        float cov = texture(u_Tex, v_Uv.xy).r;
        if (cov <= 0.002) discard;
        if (abs(invContrast - 1.0) > 0.002) {
            cov = pow(cov, invContrast);
        }
        float luma = dot(v_Fill.rgb, vec3(0.2126, 0.7152, 0.0722));
        float polDarken = (gammaFlag > 0.5)
            ? mix(clamp(effDarken * 1.85 + 0.14 * clamp(effDarken / 0.15, 0.0, 1.0), 0.0, 0.50), effDarken * 0.25, luma)
            : effDarken;
        if (polDarken > 0.001) {
            cov = clamp(cov + polDarken * cov * (1.0 - cov) * (1.5 - 0.5 * cov), 0.0, 1.0);
        }
        if (gammaFlag > 0.5) {
            // DirectWrite / Skia calibrated 1.8 text gamma compensation (matches CPU TextCompositor)
            float darkOnLight = 1.0 - pow(max(1.0 - cov, 0.0), 1.0 / 1.8);
            float lightOnDark = pow(cov, 1.0 / 1.8);
            cov = mix(darkOnLight, lightOnDark, luma);
        }
        FragColor = vec4(v_Fill.rgb, v_Fill.a * cov);
        return;
    }

    // Modes 4 & 5: Per-channel LCD subpixel RGB/BGR via hardware GL_CONSTANT_COLOR + GL_ONE_MINUS_SRC_COLOR
    vec3 cov3 = texture(u_Tex, v_Uv.xy).rgb;
    if (mode >= 4.5) {
        cov3 = cov3.bgr;
    }
    if (max(cov3.r, max(cov3.g, cov3.b)) <= 0.002) discard;
    if (abs(invContrast - 1.0) > 0.002) {
        cov3 = pow(max(cov3, vec3(0.0)), vec3(invContrast));
    }
    float luma = dot(v_Fill.rgb, vec3(0.2126, 0.7152, 0.0722));
    float polDarken = (gammaFlag > 0.5)
        ? mix(clamp(effDarken * 1.85 + 0.14 * clamp(effDarken / 0.15, 0.0, 1.0), 0.0, 0.50), effDarken * 0.25, luma)
        : effDarken;
    if (polDarken > 0.001) {
        cov3 = clamp(cov3 + polDarken * cov3 * (vec3(1.0) - cov3) * (vec3(1.5) - 0.5 * cov3), vec3(0.0), vec3(1.0));
    }
    // Saturation-aware & dark-backdrop LCD chroma damping (matches CPU TextCompositor)
    float maxC = max(v_Fill.r, max(v_Fill.g, v_Fill.b));
    float minC = min(v_Fill.r, min(v_Fill.g, v_Fill.b));
    float sat = (maxC > 0.001) ? ((maxC - minC) / maxC) : 0.0;
    float chromaKeep = clamp((1.0 - 0.85 * sat) * mix(1.0, 0.72, luma), 0.10, 1.0);
    float avgCov = (cov3.r + cov3.g + cov3.b) * (1.0 / 3.0);
    cov3 = mix(vec3(avgCov), cov3, chromaKeep);
    if (gammaFlag > 0.5) {
        // DirectWrite / Skia 1.8 text gamma compensation (uniform luma polarity across R/G/B)
        vec3 darkOnLight = vec3(1.0) - pow(max(vec3(1.0) - cov3, vec3(0.0)), vec3(1.0 / 1.8));
        vec3 lightOnDark = pow(max(cov3, vec3(0.0)), vec3(1.0 / 1.8));
        cov3 = mix(darkOnLight, lightOnDark, luma);
    }
    vec3 effCov = cov3 * v_Fill.a;
    FragColor = vec4(effCov, max(effCov.r, max(effCov.g, effCov.b)));
}
)GLSL";

static constexpr const char* kTriVertBody = R"GLSL(
layout(location = 0) in vec2 a_Pos;
layout(location = 1) in vec4 a_Color;

uniform vec2 u_Viewport;

out vec4 v_Color;

void main() {
    vec2 ndc = (a_Pos / max(u_Viewport, vec2(1.0))) * 2.0 - 1.0;
    ndc.y = -ndc.y;
    gl_Position = vec4(ndc, 0.0, 1.0);
    v_Color = a_Color;
}
)GLSL";

static constexpr const char* kTriFragBody = R"GLSL(
in vec4 v_Color;
out vec4 FragColor;

void main() {
    if (v_Color.a <= 0.001) discard;
    FragColor = v_Color;
}
)GLSL";

static constexpr const char* kPassVertBody = R"GLSL(
layout(location = 0) in vec2 a_uv;
out vec2 v_uv;

uniform vec2 u_Viewport;
uniform vec4 u_PlotRect;

void main() {
    v_uv = a_uv;
    vec2 screenPos = u_PlotRect.xy + a_uv * u_PlotRect.zw;
    vec2 ndc = vec2(
        (screenPos.x / max(u_Viewport.x, 1.0)) * 2.0 - 1.0,
        1.0 - (screenPos.y / max(u_Viewport.y, 1.0)) * 2.0
    );
    gl_Position = vec4(ndc, 0.0, 1.0);
}
)GLSL";

[[nodiscard]] inline keel::u32 PackRG16Norm(keel::f32 norm) noexcept {
    const keel::f32 clamped = keel::clamp(norm, 0.0f, 1.0f);
    const keel::u32 q = keel::as<keel::u32>(keel::round_i32(clamped * 65535.0f));
    const keel::u32 hi = (q >> 8) & 0xFFu;
    const keel::u32 lo = q & 0xFFu;
    return hi | (lo << 8) | 0xFF000000u;
}

[[nodiscard]] GLuint CompileGpuPassProgram(
    const GlEs3Api& gl,
    bool isDesktopGl,
    keel::StringRef glslEs2Frag
) {
    const char* vsHeader = isDesktopGl
        ? "#version 330 core\n"
        : "#version 300 es\nprecision highp float;\n";
    const GLchar* vsSources[2] = {vsHeader, kPassVertBody};
    GLuint vs = gl.CreateShader(kGlVertexShader);
    if (vs == 0u) return 0u;
    gl.ShaderSource(vs, 2, vsSources, nullptr);
    gl.CompileShader(vs);
    GLint status = 0;
    gl.GetShaderiv(vs, kGlCompileStatus, &status);
    if (status == 0) {
        gl.DeleteShader(vs);
        return 0u;
    }

    const char* fsPreamble = isDesktopGl
        ? "#version 330 core\n"
          "#define varying in\n"
          "#define texture2D texture\n"
          "#define gl_FragColor guikit_FragColor\n"
          "out vec4 guikit_FragColor;\n"
        : "#version 300 es\n"
          "precision highp float;\n"
          "#define varying in\n"
          "#define texture2D texture\n"
          "#define gl_FragColor guikit_FragColor\n"
          "out vec4 guikit_FragColor;\n";
    const GLchar* fsSources[2] = {fsPreamble, glslEs2Frag.data()};
    const GLint fsLengths[2] = {-1, keel::as<GLint>(glslEs2Frag.size())};
    GLuint fs = gl.CreateShader(kGlFragmentShader);
    if (fs == 0u) {
        gl.DeleteShader(vs);
        return 0u;
    }
    gl.ShaderSource(fs, 2, fsSources, fsLengths);
    gl.CompileShader(fs);
    status = 0;
    gl.GetShaderiv(fs, kGlCompileStatus, &status);
    if (status == 0) {
        char logBuf[512]{};
        GLsizei logLen = 0;
        gl.GetShaderInfoLog(fs, keel::as<GLsizei>(sizeof(logBuf) - 1), &logLen, logBuf);
        keel::eprintln("[GlEs3Canvas] GPU pass frag compile failed: {}", keel::StringRef(logBuf, keel::as<keel::usize>(keel::max(0, logLen))));
        gl.DeleteShader(vs);
        gl.DeleteShader(fs);
        return 0u;
    }

    GLuint prog = gl.CreateProgram();
    gl.AttachShader(prog, vs);
    gl.AttachShader(prog, fs);
    gl.LinkProgram(prog);
    gl.DeleteShader(vs);
    gl.DeleteShader(fs);

    status = 0;
    gl.GetProgramiv(prog, kGlLinkStatus, &status);
    if (status == 0) {
        char logBuf[512]{};
        GLsizei logLen = 0;
        gl.GetProgramInfoLog(prog, keel::as<GLsizei>(sizeof(logBuf) - 1), &logLen, logBuf);
        keel::eprintln("[GlEs3Canvas] GPU pass link failed: {}", keel::StringRef(logBuf, keel::as<keel::usize>(keel::max(0, logLen))));
        gl.DeleteProgram(prog);
        return 0u;
    }
    return prog;
}

[[nodiscard]] GLuint CompileShader(
    const GlEs3Api& gl,
    GLenum type,
    bool isDesktopGl,
    const char* body
) {
    const char* header = isDesktopGl
        ? "#version 330 core\n"
        : "#version 300 es\nprecision highp float;\n";
    const GLchar* sources[2] = {header, body};

    GLuint shader = gl.CreateShader(type);
    if (shader == 0u) return 0u;
    gl.ShaderSource(shader, 2, sources, nullptr);
    gl.CompileShader(shader);

    GLint status = 0;
    gl.GetShaderiv(shader, kGlCompileStatus, &status);
    if (status == 0) {
        char logBuf[512]{};
        GLsizei logLen = 0;
        gl.GetShaderInfoLog(shader, keel::as<GLsizei>(sizeof(logBuf) - 1), &logLen, logBuf);
        keel::eprintln("[GlEs3Canvas] Shader compile failed: {}", keel::StringRef(logBuf, keel::as<keel::usize>(keel::max(0, logLen))));
        gl.DeleteShader(shader);
        return 0u;
    }
    return shader;
}

[[nodiscard]] GLuint LinkProgram(
    const GlEs3Api& gl,
    bool isDesktopGl,
    const char* vertBody,
    const char* fragBody
) {
    GLuint vs = CompileShader(gl, kGlVertexShader, isDesktopGl, vertBody);
    if (vs == 0u) return 0u;
    GLuint fs = CompileShader(gl, kGlFragmentShader, isDesktopGl, fragBody);
    if (fs == 0u) {
        gl.DeleteShader(vs);
        return 0u;
    }

    GLuint prog = gl.CreateProgram();
    gl.AttachShader(prog, vs);
    gl.AttachShader(prog, fs);
    gl.LinkProgram(prog);
    gl.DeleteShader(vs);
    gl.DeleteShader(fs);

    GLint status = 0;
    gl.GetProgramiv(prog, kGlLinkStatus, &status);
    if (status == 0) {
        char logBuf[512]{};
        GLsizei logLen = 0;
        gl.GetProgramInfoLog(prog, keel::as<GLsizei>(sizeof(logBuf) - 1), &logLen, logBuf);
        keel::eprintln("[GlEs3Canvas] Program link failed: {}", keel::StringRef(logBuf, keel::as<keel::usize>(keel::max(0, logLen))));
        gl.DeleteProgram(prog);
        return 0u;
    }
    return prog;
}

[[nodiscard]] keel::usize CountUtf8Codepoints(keel::StringRef text) noexcept {
    keel::usize count = 0;
    for (keel::usize i = 0; i < text.size(); ++i) {
        if ((keel::as<keel::u8>(text[i]) & 0xC0u) != 0x80u) {
            ++count;
        }
    }
    return count;
}

[[nodiscard]] keel::f32 Cross2D(Vec2 a, Vec2 b, Vec2 c) noexcept {
    return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
}

[[nodiscard]] bool PointInTri(Vec2 p, Vec2 a, Vec2 b, Vec2 c) noexcept {
    const keel::f32 c1 = Cross2D(a, b, p);
    const keel::f32 c2 = Cross2D(b, c, p);
    const keel::f32 c3 = Cross2D(c, a, p);
    const bool hasNeg = (c1 < 0.0f) || (c2 < 0.0f) || (c3 < 0.0f);
    const bool hasPos = (c1 > 0.0f) || (c2 > 0.0f) || (c3 > 0.0f);
    return !(hasNeg && hasPos);
}

void FlattenCubicBezier(
    Vec2 p0,
    Vec2 p1,
    Vec2 p2,
    Vec2 p3,
    keel::Array<Vec2>& outPts,
    keel::f32 flatnessPx = 0.6f
) {
    const keel::f32 dx = p3.x - p0.x;
    const keel::f32 dy = p3.y - p0.y;
    const keel::f32 d1 = keel::abs((p1.x - p3.x) * dy - (p1.y - p3.y) * dx);
    const keel::f32 d2 = keel::abs((p2.x - p3.x) * dy - (p2.y - p3.y) * dx);
    const keel::f32 lenSq = dx * dx + dy * dy;
    const keel::f32 err = (d1 + d2);
    const keel::f32 stepsF = keel::clamp(
        keel::ceil(keel::sqrt(keel::max(err / keel::max(flatnessPx * keel::sqrt(keel::max(lenSq, 1.0f)), 0.5f), 1.0f)) * 2.0f),
        2.0f,
        24.0f
    );
    const keel::i32 steps = keel::as<keel::i32>(stepsF);
    for (keel::i32 i = 1; i <= steps; ++i) {
        const keel::f32 t = keel::as<keel::f32>(i) / keel::as<keel::f32>(steps);
        const keel::f32 u = 1.0f - t;
        const keel::f32 b0 = u * u * u;
        const keel::f32 b1 = 3.0f * u * u * t;
        const keel::f32 b2 = 3.0f * u * t * t;
        const keel::f32 b3 = t * t * t;
        outPts.push_back(Vec2{
            b0 * p0.x + b1 * p1.x + b2 * p2.x + b3 * p3.x,
            b0 * p0.y + b1 * p1.y + b2 * p2.y + b3 * p3.y
        });
    }
}

} // namespace

struct GlEs3Canvas::Impl {
    struct TransformState {
        keel::f32 scale{1.0f};
        Vec2 offset{0.0f, 0.0f};
    };

#if GUIKIT_HAS_TEXT_RASTERIZER
    struct AtlasKey {
        keel::u32 codepoint{0};
        keel::u16 sizeQ4{0};
        keel::u8 style{0};
        keel::u8 flags{0}; // bits [0..1] hinting, [2..3] quarter-px phase (0..3)

        bool operator==(const AtlasKey& o) const noexcept {
            return codepoint == o.codepoint && sizeQ4 == o.sizeQ4 &&
                   style == o.style && flags == o.flags;
        }
    };

    struct AtlasKeyHash {
        keel::u64 operator()(const AtlasKey& k) const noexcept {
            keel::u64 h = keel::as<keel::u64>(k.codepoint);
            h ^= (keel::as<keel::u64>(k.sizeQ4) |
                  (keel::as<keel::u64>(k.style) << 16) |
                  (keel::as<keel::u64>(k.flags) << 24)) * 0x9e3779b97f4a7c15ULL;
            h ^= h >> 33;
            return h * 0xff51afd7ed558ccdULL;
        }
    };

    struct AtlasGlyph {
        keel::f32 u0{0.0f}, v0{0.0f}, u1{0.0f}, v1{0.0f};
        keel::u16 width{0};
        keel::u16 height{0};
        keel::i16 bearingX{0};
        keel::i16 bearingY{0};
        keel::f32 advancePx{0.0f};
        bool empty{true};
    };
#endif

    GlEs3Api gl{};
    bool initialized{false};
    bool isDesktopGl{false};
    keel::i32 msaaSamples{0};
    keel::i32 stencilBits{0};
    keel::String glVersion;
    keel::String glRenderer;

    [[nodiscard]] bool HasStencilOps() const noexcept {
        return stencilBits > 0 && gl.ColorMask && gl.StencilFunc && gl.StencilOp && gl.StencilMask;
    }

    GLuint quadProg{0};
    GLint quadUniformViewport{-1};
    GLint quadUniformTex{-1};
    GLuint quadVao{0};
    GLuint quadInstanceVbo{0};
    keel::usize quadVboCapacity{0};

    GLuint triProg{0};
    GLint triUniformViewport{-1};
    GLuint triVao{0};
    GLuint triVbo{0};
    keel::usize triVboCapacity{0};

    struct CachedImage {
        GLuint texId{0};
        keel::u32 width{0};
        keel::u32 height{0};
        keel::u64 lastUsedFrame{0};
        bool valid{false};
        bool probedOnly{false};
    };

    struct CompiledPassProgram {
        GLuint program{0};
        bool failed{false};
        GLint locViewport{-1};
        GLint locPlotRect{-1};
        GLint locGridDims{-1};
        GLint locBgColor{-1};
        GLint locParams{-1};
        GLint locDataTex{-1};
        GLint locLutTex{-1};
    };

    struct WidgetGpuTextures {
        GLuint dataTex{0};
        keel::u32 texW{0};
        keel::u32 texH{0};
        keel::u64 fullGen{0};
        keel::u64 sliceGen{0};
        keel::u64 lastUsedFrame{0};
        GLuint lutTex{0};
        keel::u8 lutId{255};
        bool lutUploaded{false};
    };

    GLuint passQuadVao{0};
    GLuint passQuadVbo{0};
    bool gpuShadersEnabled{true};
    keel::u32 activeGpuPassesThisFrame{0};
    keel::u64 frameCounter{0};
    keel::HashMap<keel::String, CompiledPassProgram> gpuPrograms;
    keel::HashMap<keel::u64, WidgetGpuTextures> gpuWidgetTex;
    keel::Array<keel::u32> gpuRgbaScratch;
    keel::HashMap<keel::String, CachedImage> imageCache;

    GLuint whiteTex{0};
    GLuint atlasTex{0};
    GLuint atlasLcdTex{0};
    GLuint transientPixelsTex{0};
    keel::u32 transientTexW{0};
    keel::u32 transientTexH{0};

    GLuint activeTex{0};
    bool activeLcdBlend{false};
    keel::u32 activeLcdRgbKey{0u};
    keel::Array<QuadInstance> quadBatch;
    keel::Array<TriVertex> triBatch;

    keel::u32 fbWidth{0};
    keel::u32 fbHeight{0};
    keel::f32 scale{1.0f};
    keel::f32 xfScale{1.0f};
    Vec2 xfOffset{0.0f, 0.0f};
    keel::SmallArray<TransformState, 8> transformStack;
    keel::SmallArray<Rect, 16> clipStack;
    TextRenderSettings textSettings{};

#if GUIKIT_HAS_TEXT_RASTERIZER
    TextEngine ft;
    keel::HashMap<AtlasKey, AtlasGlyph, AtlasKeyHash> atlasMap;
    keel::u32 shelfX{1};
    keel::u32 shelfY{1};
    keel::u32 shelfH{0};

    keel::HashMap<AtlasKey, AtlasGlyph, AtlasKeyHash> atlasLcdMap;
    keel::u32 lcdShelfX{1};
    keel::u32 lcdShelfY{1};
    keel::u32 lcdShelfH{0};
    keel::Array<keel::u8> lcdUploadScratch;
#endif

    [[nodiscard]] keel::f32 EffScale() const noexcept { return scale * xfScale; }

    [[nodiscard]] Vec2 PxPt(Vec2 p) const noexcept {
        return Vec2{
            (p.x * xfScale + xfOffset.x) * scale,
            (p.y * xfScale + xfOffset.y) * scale
        };
    }

    [[nodiscard]] Rect PxRect(Rect r) const noexcept {
        const keel::f32 eff = EffScale();
        return Rect(
            (r.x * xfScale + xfOffset.x) * scale,
            (r.y * xfScale + xfOffset.y) * scale,
            r.w * eff,
            r.h * eff
        );
    }

    bool Init(GlProcAddressFn procLoader, bool loadDefaultFonts) {
#if GUIKIT_HAS_TEXT_RASTERIZER
        if (loadDefaultFonts && ft.IsAvailable()) {
            (void)ft.LoadDefaultEmbeddedFonts();
        }
#else
        (void)loadDefaultFonts;
#endif
        if (!gl.Load(procLoader)) {
            return false;
        }

        if (const GLubyte* ver = gl.GetString(kGlVersion)) {
            glVersion = keel::String::from(reinterpret_cast<const char*>(ver));
            isDesktopGl = (glVersion.as_ref().find("OpenGL ES") == keel::StringRef::npos);
        }
        if (const GLubyte* ren = gl.GetString(kGlRenderer)) {
            glRenderer = keel::String::from(reinterpret_cast<const char*>(ren));
        }

        quadProg = LinkProgram(gl, isDesktopGl, kQuadVertBody, kQuadFragBody);
        triProg = LinkProgram(gl, isDesktopGl, kTriVertBody, kTriFragBody);
        if (quadProg == 0u || triProg == 0u) {
            Shutdown();
            return false;
        }

        quadUniformViewport = gl.GetUniformLocation(quadProg, "u_Viewport");
        quadUniformTex = gl.GetUniformLocation(quadProg, "u_Tex");
        triUniformViewport = gl.GetUniformLocation(triProg, "u_Viewport");

        // Setup Quad Instanced VAO + VBO
        gl.GenVertexArrays(1, &quadVao);
        gl.GenBuffers(1, &quadInstanceVbo);
        gl.BindVertexArray(quadVao);
        gl.BindBuffer(kGlArrayBuffer, quadInstanceVbo);

        const GLsizei stride = keel::as<GLsizei>(sizeof(QuadInstance));
        for (GLuint loc = 0; loc < 5; ++loc) {
            gl.EnableVertexAttribArray(loc);
            const auto offset = keel::as<keel::usize>(loc) * 4u * sizeof(keel::f32);
            gl.VertexAttribPointer(loc, 4, kGlFloat, kGlFalse, stride, reinterpret_cast<const void*>(offset));
            gl.VertexAttribDivisor(loc, 1);
        }

        // Setup Triangle VAO + VBO
        gl.GenVertexArrays(1, &triVao);
        gl.GenBuffers(1, &triVbo);
        gl.BindVertexArray(triVao);
        gl.BindBuffer(kGlArrayBuffer, triVbo);
        gl.EnableVertexAttribArray(0);
        gl.VertexAttribPointer(0, 2, kGlFloat, kGlFalse, keel::as<GLsizei>(sizeof(TriVertex)), reinterpret_cast<const void*>(0));
        gl.EnableVertexAttribArray(1);
        gl.VertexAttribPointer(1, 4, kGlFloat, kGlFalse, keel::as<GLsizei>(sizeof(TriVertex)), reinterpret_cast<const void*>(2u * sizeof(keel::f32)));

        // Setup GPU Shader Pass unit-quad VAO + VBO
        static constexpr keel::f32 kUnitQuadUvs[12] = {
            0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f,
            0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f
        };
        gl.GenVertexArrays(1, &passQuadVao);
        gl.GenBuffers(1, &passQuadVbo);
        gl.BindVertexArray(passQuadVao);
        gl.BindBuffer(kGlArrayBuffer, passQuadVbo);
        gl.BufferData(kGlArrayBuffer, keel::as<GLsizeiptr>(sizeof(kUnitQuadUvs)), kUnitQuadUvs, kGlStaticDraw);
        gl.EnableVertexAttribArray(0);
        gl.VertexAttribPointer(0, 2, kGlFloat, kGlFalse, keel::as<GLsizei>(2u * sizeof(keel::f32)), reinterpret_cast<const void*>(0));
        gl.BindVertexArray(0);

        // 1x1 white fallback texture
        gl.GenTextures(1, &whiteTex);
        gl.BindTexture(kGlTexture2D, whiteTex);
        gl.TexParameteri(kGlTexture2D, kGlTextureMinFilter, keel::as<GLint>(kGlNearest));
        gl.TexParameteri(kGlTexture2D, kGlTextureMagFilter, keel::as<GLint>(kGlNearest));
        gl.TexParameteri(kGlTexture2D, kGlTextureWrapS, keel::as<GLint>(kGlClampToEdge));
        gl.TexParameteri(kGlTexture2D, kGlTextureWrapT, keel::as<GLint>(kGlClampToEdge));
        const keel::u32 whitePx = 0xFFFFFFFFu;
        gl.TexImage2D(kGlTexture2D, 0, keel::as<GLint>(kGlRgba8), 1, 1, 0, kGlRgba, kGlUnsignedByte, &whitePx);

        // 1024x1024 R8 glyph atlas texture (GL_NEAREST for bit-exact 1:1 pixel-snapped UI glyphs)
        gl.GenTextures(1, &atlasTex);
        gl.BindTexture(kGlTexture2D, atlasTex);
        gl.TexParameteri(kGlTexture2D, kGlTextureMinFilter, keel::as<GLint>(kGlNearest));
        gl.TexParameteri(kGlTexture2D, kGlTextureMagFilter, keel::as<GLint>(kGlNearest));
        gl.TexParameteri(kGlTexture2D, kGlTextureWrapS, keel::as<GLint>(kGlClampToEdge));
        gl.TexParameteri(kGlTexture2D, kGlTextureWrapT, keel::as<GLint>(kGlClampToEdge));
        gl.PixelStorei(kGlUnpackAlignment, 1);
        keel::Array<keel::u8> zeroAtlas(keel::as<keel::usize>(kAtlasSize) * kAtlasSize, 0u);
        gl.TexImage2D(
            kGlTexture2D,
            0,
            keel::as<GLint>(kGlR8),
            keel::as<GLsizei>(kAtlasSize),
            keel::as<GLsizei>(kAtlasSize),
            0,
            kGlRed,
            kGlUnsignedByte,
            zeroAtlas.data()
        );

        gl.GenTextures(1, &transientPixelsTex);
        gl.BindTexture(kGlTexture2D, transientPixelsTex);
        gl.TexParameteri(kGlTexture2D, kGlTextureMinFilter, keel::as<GLint>(kGlLinear));
        gl.TexParameteri(kGlTexture2D, kGlTextureMagFilter, keel::as<GLint>(kGlLinear));
        gl.TexParameteri(kGlTexture2D, kGlTextureWrapS, keel::as<GLint>(kGlClampToEdge));
        gl.TexParameteri(kGlTexture2D, kGlTextureWrapT, keel::as<GLint>(kGlClampToEdge));

        gl.BindTexture(kGlTexture2D, 0);
        activeTex = whiteTex;

        // Query active MSAA sample count & default framebuffer stencil bit depth
        if (gl.GetIntegerv) {
            while (gl.GetError() != 0u) {}
            GLint samples = 0;
            gl.GetIntegerv(kGlSamples, &samples);
            if (gl.GetError() == 0u && samples > 0) {
                msaaSamples = keel::as<keel::i32>(samples);
            }
        }
#if !defined(__EMSCRIPTEN__)
        if (gl.GetFramebufferAttachmentParameteriv) {
            while (gl.GetError() != 0u) {}
            GLint bits = 0;
            gl.GetFramebufferAttachmentParameteriv(
                kGlFramebuffer, kGlStencil, kGlFramebufferAttachmentStencilSize, &bits
            );
            if (gl.GetError() == 0u && bits > 0) {
                stencilBits = keel::as<keel::i32>(bits);
            }
        }
#endif
        if (stencilBits == 0 && gl.GetIntegerv) {
            while (gl.GetError() != 0u) {}
            GLint bits = 0;
            gl.GetIntegerv(kGlStencilBits, &bits);
            if (gl.GetError() == 0u && bits > 0) {
                stencilBits = keel::as<keel::i32>(bits);
            }
        }
        if (stencilBits > 0 && gl.ClearStencil) {
            gl.ClearStencil(0);
        }

        initialized = true;
        return true;
    }

    void ReleaseGpuPassCaches() noexcept {
        if (gl.DeleteProgram) {
            for (auto [k, p] : gpuPrograms) {
                (void)k;
                if (p.program != 0u) {
                    gl.DeleteProgram(p.program);
                    p.program = 0u;
                }
            }
        }
        gpuPrograms.clear();

        if (gl.DeleteTextures) {
            for (auto [k, wt] : gpuWidgetTex) {
                (void)k;
                if (wt.dataTex != 0u) {
                    gl.DeleteTextures(1, &wt.dataTex);
                    wt.dataTex = 0u;
                }
                if (wt.lutTex != 0u) {
                    gl.DeleteTextures(1, &wt.lutTex);
                    wt.lutTex = 0u;
                }
            }
        }
        gpuWidgetTex.clear();
        gpuRgbaScratch.clear();
    }

    void ReleaseImageCache() noexcept {
        if (gl.DeleteTextures) {
            for (auto [k, img] : imageCache) {
                (void)k;
                if (img.texId != 0u) {
                    gl.DeleteTextures(1, &img.texId);
                    img.texId = 0u;
                }
            }
        }
        imageCache.clear();
    }

    void Shutdown() noexcept {
        if (!initialized && quadProg == 0u && triProg == 0u) {
            return;
        }
        ReleaseGpuPassCaches();
        ReleaseImageCache();
        if (transientPixelsTex != 0u && gl.DeleteTextures) gl.DeleteTextures(1, &transientPixelsTex);
        if (atlasLcdTex != 0u && gl.DeleteTextures) gl.DeleteTextures(1, &atlasLcdTex);
        if (atlasTex != 0u && gl.DeleteTextures) gl.DeleteTextures(1, &atlasTex);
        if (whiteTex != 0u && gl.DeleteTextures) gl.DeleteTextures(1, &whiteTex);
        if (passQuadVbo != 0u && gl.DeleteBuffers) gl.DeleteBuffers(1, &passQuadVbo);
        if (passQuadVao != 0u && gl.DeleteVertexArrays) gl.DeleteVertexArrays(1, &passQuadVao);
        if (quadInstanceVbo != 0u && gl.DeleteBuffers) gl.DeleteBuffers(1, &quadInstanceVbo);
        if (quadVao != 0u && gl.DeleteVertexArrays) gl.DeleteVertexArrays(1, &quadVao);
        if (triVbo != 0u && gl.DeleteBuffers) gl.DeleteBuffers(1, &triVbo);
        if (triVao != 0u && gl.DeleteVertexArrays) gl.DeleteVertexArrays(1, &triVao);
        if (quadProg != 0u && gl.DeleteProgram) gl.DeleteProgram(quadProg);
        if (triProg != 0u && gl.DeleteProgram) gl.DeleteProgram(triProg);
        transientPixelsTex = atlasLcdTex = atlasTex = whiteTex = 0u;
        passQuadVbo = passQuadVao = quadInstanceVbo = quadVao = triVbo = triVao = 0u;
        quadProg = triProg = 0u;
        initialized = false;
    }

    CachedImage* ResolveImage(keel::StringRef source, bool requireTexture) {
        if (source.empty()) return nullptr;
        if (CachedImage* existing = imageCache.get(source)) {
            if (!existing->valid || !requireTexture || existing->texId != 0u) {
                return existing;
            }
        }

        keel::Array<keel::u8> ownedBytes;
        keel::ArrayRef<keel::u8> bytes{};
        if (const auto asset = Assets::Find(source); asset && !asset->Empty()) {
            bytes = asset->Bytes();
        } else if (auto fileBytes = keel::fs_read_file(source)) {
            ownedBytes = keel::move(*fileBytes);
            bytes = ownedBytes.as_ref();
        } else {
            CachedImage invalid{};
            invalid.valid = false;
            imageCache.put(keel::String::from(source), invalid);
            return imageCache.get(source);
        }

        if (!requireTexture) {
            keel::u32 w = 0u;
            keel::u32 h = 0u;
            if (InspectImageFromMemory(bytes, w, h)) {
                CachedImage probed{};
                probed.width = w;
                probed.height = h;
                probed.valid = true;
                probed.probedOnly = true;
                imageCache.put(keel::String::from(source), probed);
                return imageCache.get(source);
            }
        }

        keel::u32 w = 0u;
        keel::u32 h = 0u;
        keel::Array<keel::u32> rgba;
        const bool decoded = DecodeImageFromMemory(bytes, w, h, rgba);
        if (!decoded || w == 0u || h == 0u || rgba.size() < keel::as<keel::usize>(w) * h) {
            CachedImage invalid{};
            invalid.valid = false;
            imageCache.put(keel::String::from(source), invalid);
            return imageCache.get(source);
        }

        CachedImage entry{};
        entry.width = w;
        entry.height = h;
        entry.valid = true;
        entry.probedOnly = !requireTexture;

        if (requireTexture && initialized) {
            GLuint tex = 0u;
            gl.GenTextures(1, &tex);
            gl.BindTexture(kGlTexture2D, tex);
            gl.TexParameteri(kGlTexture2D, kGlTextureMinFilter, keel::as<GLint>(kGlLinear));
            gl.TexParameteri(kGlTexture2D, kGlTextureMagFilter, keel::as<GLint>(kGlLinear));
            gl.TexParameteri(kGlTexture2D, kGlTextureWrapS, keel::as<GLint>(kGlClampToEdge));
            gl.TexParameteri(kGlTexture2D, kGlTextureWrapT, keel::as<GLint>(kGlClampToEdge));
            gl.PixelStorei(kGlUnpackAlignment, 4);
            gl.TexImage2D(
                kGlTexture2D,
                0,
                keel::as<GLint>(kGlRgba8),
                keel::as<GLsizei>(w),
                keel::as<GLsizei>(h),
                0,
                kGlRgba,
                kGlUnsignedByte,
                rgba.data()
            );
            gl.BindTexture(kGlTexture2D, 0);
            entry.texId = tex;
            entry.probedOnly = false;
        }

        imageCache.put(keel::String::from(source), entry);
        return imageCache.get(source);
    }

    CompiledPassProgram* ResolvePassProgram(keel::StringRef shaderId, keel::StringRef glslEs2) {
        if (CompiledPassProgram* existing = gpuPrograms.get(shaderId)) {
            return existing;
        }
        CompiledPassProgram compiled{};
        compiled.program = CompileGpuPassProgram(gl, isDesktopGl, glslEs2);
        if (compiled.program == 0u) {
            compiled.failed = true;
        } else {
            compiled.locViewport = gl.GetUniformLocation(compiled.program, "u_Viewport");
            compiled.locPlotRect = gl.GetUniformLocation(compiled.program, "u_PlotRect");
            compiled.locGridDims = gl.GetUniformLocation(compiled.program, "u_GridDims");
            compiled.locBgColor = gl.GetUniformLocation(compiled.program, "u_BgColor");
            compiled.locParams = gl.GetUniformLocation(compiled.program, "u_Params[0]");
            if (compiled.locParams < 0) {
                compiled.locParams = gl.GetUniformLocation(compiled.program, "u_Params");
            }
            compiled.locDataTex = gl.GetUniformLocation(compiled.program, "u_DataTex");
            compiled.locLutTex = gl.GetUniformLocation(compiled.program, "u_LutTex");
        }
        gpuPrograms.put(keel::String::from(shaderId), compiled);
        return gpuPrograms.get(shaderId);
    }

    void FlushQuads() {
        if (!initialized || quadBatch.empty()) return;

        if (activeLcdBlend) {
            const keel::f32 cr = keel::as<keel::f32>(activeLcdRgbKey & 0xFFu) / 255.0f;
            const keel::f32 cg = keel::as<keel::f32>((activeLcdRgbKey >> 8) & 0xFFu) / 255.0f;
            const keel::f32 cb = keel::as<keel::f32>((activeLcdRgbKey >> 16) & 0xFFu) / 255.0f;
            gl.BlendColor(cr, cg, cb, 1.0f);
            gl.BlendFuncSeparate(kGlConstantColor, kGlOneMinusSrcColor, kGlOne, kGlOneMinusSrcAlpha);
        }

        gl.UseProgram(quadProg);
        gl.Uniform2f(quadUniformViewport, keel::as<keel::f32>(fbWidth), keel::as<keel::f32>(fbHeight));
        gl.Uniform1i(quadUniformTex, 0);
        gl.ActiveTexture(kGlTexture0);
        gl.BindTexture(kGlTexture2D, activeTex ? activeTex : whiteTex);

        gl.BindVertexArray(quadVao);
        gl.BindBuffer(kGlArrayBuffer, quadInstanceVbo);
        const keel::usize byteSize = quadBatch.size() * sizeof(QuadInstance);
        if (byteSize > quadVboCapacity) {
            quadVboCapacity = keel::max(byteSize, quadVboCapacity * 2u + 4096u);
            gl.BufferData(kGlArrayBuffer, keel::as<GLsizeiptr>(quadVboCapacity), nullptr, kGlStreamDraw);
        }
        gl.BufferSubData(kGlArrayBuffer, 0, keel::as<GLsizeiptr>(byteSize), quadBatch.data());
        gl.DrawArraysInstanced(kGlTriangles, 0, 6, keel::as<GLsizei>(quadBatch.size()));
        quadBatch.clear();

        if (activeLcdBlend) {
            gl.BlendFuncSeparate(kGlSrcAlpha, kGlOneMinusSrcAlpha, kGlOne, kGlOneMinusSrcAlpha);
        }
    }

    void FlushTris() {
        if (!initialized || triBatch.empty()) return;

        gl.UseProgram(triProg);
        gl.Uniform2f(triUniformViewport, keel::as<keel::f32>(fbWidth), keel::as<keel::f32>(fbHeight));

        gl.BindVertexArray(triVao);
        gl.BindBuffer(kGlArrayBuffer, triVbo);
        const keel::usize byteSize = triBatch.size() * sizeof(TriVertex);
        if (byteSize > triVboCapacity) {
            triVboCapacity = keel::max(byteSize, triVboCapacity * 2u + 4096u);
            gl.BufferData(kGlArrayBuffer, keel::as<GLsizeiptr>(triVboCapacity), nullptr, kGlStreamDraw);
        }
        gl.BufferSubData(kGlArrayBuffer, 0, keel::as<GLsizeiptr>(byteSize), triBatch.data());
        gl.DrawArrays(kGlTriangles, 0, keel::as<GLsizei>(triBatch.size()));
        triBatch.clear();
    }

    void FlushAll() {
        FlushQuads();
        FlushTris();
    }

    void ApplyClip() {
        if (!initialized) return;
        if (clipStack.empty()) {
            gl.Disable(kGlScissorTest);
            return;
        }
        const Rect& cr = clipStack.back();
        const GLint x0 = keel::max(0, keel::floor_i32(cr.x));
        const GLint y0Top = keel::max(0, keel::floor_i32(cr.y));
        const GLint x1 = keel::min(keel::as<GLint>(fbWidth), keel::ceil_i32(cr.Right()));
        const GLint y1Top = keel::min(keel::as<GLint>(fbHeight), keel::ceil_i32(cr.Bottom()));
        const GLsizei w = keel::max(0, x1 - x0);
        const GLsizei h = keel::max(0, y1Top - y0Top);
        const GLint glY = keel::as<GLint>(fbHeight) - y1Top;
        gl.Enable(kGlScissorTest);
        gl.Scissor(x0, keel::max(0, glY), w, h);
    }

    void PushQuad(
        const QuadInstance& q,
        GLuint tex,
        bool isLcd = false,
        keel::u32 lcdRgbKey = 0u
    ) {
        if (!initialized) return;
        if (!triBatch.empty()) {
            FlushTris();
        }
        const bool texChanged = (tex != 0u && tex != activeTex);
        const bool blendChanged = (isLcd != activeLcdBlend) || (isLcd && lcdRgbKey != activeLcdRgbKey);
        if (texChanged || blendChanged) {
            FlushQuads();
            if (tex != 0u) {
                activeTex = tex;
            }
            activeLcdBlend = isLcd;
            activeLcdRgbKey = lcdRgbKey;
        }
        quadBatch.push_back(q);
    }

    void PushTri(Vec2 a, Vec2 b, Vec2 c, Color col) {
        if (!initialized || col.a == 0u) return;
        if (!quadBatch.empty()) {
            FlushQuads();
        }
        const keel::f32 r = col.r / 255.0f;
        const keel::f32 g = col.g / 255.0f;
        const keel::f32 bl = col.b / 255.0f;
        const keel::f32 al = col.a / 255.0f;
        triBatch.push_back(TriVertex{a.x, a.y, r, g, bl, al});
        triBatch.push_back(TriVertex{b.x, b.y, r, g, bl, al});
        triBatch.push_back(TriVertex{c.x, c.y, r, g, bl, al});
    }

#if GUIKIT_HAS_TEXT_RASTERIZER
    void ResetAtlas() {
        FlushQuads();
        atlasMap.clear();
        shelfX = 1u;
        shelfY = 1u;
        shelfH = 0u;
        atlasLcdMap.clear();
        lcdShelfX = 1u;
        lcdShelfY = 1u;
        lcdShelfH = 0u;
    }

    bool EnsureLcdAtlas() {
        if (atlasLcdTex != 0u) return true;
        if (!initialized) return false;
        gl.GenTextures(1, &atlasLcdTex);
        if (atlasLcdTex == 0u) return false;
        gl.BindTexture(kGlTexture2D, atlasLcdTex);
        gl.TexParameteri(kGlTexture2D, kGlTextureMinFilter, keel::as<GLint>(kGlNearest));
        gl.TexParameteri(kGlTexture2D, kGlTextureMagFilter, keel::as<GLint>(kGlNearest));
        gl.TexParameteri(kGlTexture2D, kGlTextureWrapS, keel::as<GLint>(kGlClampToEdge));
        gl.TexParameteri(kGlTexture2D, kGlTextureWrapT, keel::as<GLint>(kGlClampToEdge));
        gl.PixelStorei(kGlUnpackAlignment, 4);
        keel::Array<keel::u8> zeroLcd(keel::as<keel::usize>(kLcdAtlasSize) * kLcdAtlasSize * 4u, 0u);
        gl.TexImage2D(
            kGlTexture2D,
            0,
            keel::as<GLint>(kGlRgba8),
            keel::as<GLsizei>(kLcdAtlasSize),
            keel::as<GLsizei>(kLcdAtlasSize),
            0,
            kGlRgba,
            kGlUnsignedByte,
            zeroLcd.data()
        );
        gl.BindTexture(kGlTexture2D, 0);
        return true;
    }

    const AtlasGlyph* ResolveAtlasGlyph(keel::u32 cp, keel::f32 scaledPx, FontStyle style, FontHinting effHint, keel::u8 phase) {
        const keel::u16 sizeQ4 = keel::as<keel::u16>(keel::clamp(keel::round_i32(scaledPx * 4.0f), 4, 2048));
        const keel::u8 flags = keel::as<keel::u8>((keel::as<keel::u8>(effHint) & 3u) | ((phase & 3u) << 2));
        const AtlasKey key{
            .codepoint = cp,
            .sizeQ4 = sizeQ4,
            .style = keel::as<keel::u8>(style),
            .flags = flags
        };
        if (const AtlasGlyph* found = atlasMap.get(key)) {
            return found;
        }

        TextEngine::RasterizedGlyph rg{};
        if (!ft.RasterizeCodepoint(cp, scaledPx, style, effHint, /*subpixel=*/false, phase, rg)) {
            return nullptr;
        }

        AtlasGlyph ag{};
        ag.width = rg.width;
        ag.height = rg.height;
        ag.bearingX = rg.bearingX;
        ag.bearingY = rg.bearingY;
        ag.advancePx = rg.advancePx;
        ag.empty = rg.empty || !rg.bitmap || rg.width == 0u || rg.height == 0u;

        if (!ag.empty) {
            if (rg.width + 2u >= kAtlasSize || rg.height + 2u >= kAtlasSize) {
                ag.empty = true;
            } else {
                if (shelfX + rg.width + 1u >= kAtlasSize) {
                    shelfX = 1u;
                    shelfY += shelfH + 1u;
                    shelfH = 0u;
                }
                if (shelfY + rg.height + 1u >= kAtlasSize) {
                    FlushQuads();
                    atlasMap.clear();
                    shelfX = 1u;
                    shelfY = 1u;
                    shelfH = 0u;
                }

                const keel::u32 gx = shelfX;
                const keel::u32 gy = shelfY;
                shelfX += rg.width + 1u;
                shelfH = keel::max(shelfH, keel::as<keel::u32>(rg.height));

                gl.BindTexture(kGlTexture2D, atlasTex);
                gl.PixelStorei(kGlUnpackAlignment, 1);
                gl.TexSubImage2D(
                    kGlTexture2D,
                    0,
                    keel::as<GLint>(gx),
                    keel::as<GLint>(gy),
                    keel::as<GLsizei>(rg.width),
                    keel::as<GLsizei>(rg.height),
                    kGlRed,
                    kGlUnsignedByte,
                    rg.bitmap
                );

                const keel::f32 inv = 1.0f / keel::as<keel::f32>(kAtlasSize);
                ag.u0 = keel::as<keel::f32>(gx) * inv;
                ag.v0 = keel::as<keel::f32>(gy) * inv;
                ag.u1 = keel::as<keel::f32>(gx + rg.width) * inv;
                ag.v1 = keel::as<keel::f32>(gy + rg.height) * inv;
            }
        }

        atlasMap.put(key, ag);
        return atlasMap.get(key);
    }

    const AtlasGlyph* ResolveLcdAtlasGlyph(keel::u32 cp, keel::f32 scaledPx, FontStyle style, FontHinting effHint, keel::u8 phase) {
        if (!EnsureLcdAtlas()) {
            return nullptr;
        }
        const keel::u16 sizeQ4 = keel::as<keel::u16>(keel::clamp(keel::round_i32(scaledPx * 4.0f), 4, 2048));
        const keel::u8 flags = keel::as<keel::u8>((keel::as<keel::u8>(effHint) & 3u) | ((phase & 3u) << 2));
        const AtlasKey key{
            .codepoint = cp,
            .sizeQ4 = sizeQ4,
            .style = keel::as<keel::u8>(style),
            .flags = flags
        };
        if (const AtlasGlyph* found = atlasLcdMap.get(key)) {
            return found;
        }

        TextEngine::RasterizedGlyph rg{};
        if (!ft.RasterizeCodepoint(cp, scaledPx, style, effHint, /*subpixel=*/true, phase, rg)) {
            return nullptr;
        }

        AtlasGlyph ag{};
        ag.width = rg.width;
        ag.height = rg.height;
        ag.bearingX = rg.bearingX;
        ag.bearingY = rg.bearingY;
        ag.advancePx = rg.advancePx;
        ag.empty = rg.empty || !rg.bitmap || rg.width == 0u || rg.height == 0u;

        if (!ag.empty) {
            if (rg.width + 2u >= kLcdAtlasSize || rg.height + 2u >= kLcdAtlasSize) {
                ag.empty = true;
            } else {
                if (lcdShelfX + rg.width + 1u >= kLcdAtlasSize) {
                    lcdShelfX = 1u;
                    lcdShelfY += lcdShelfH + 1u;
                    lcdShelfH = 0u;
                }
                if (lcdShelfY + rg.height + 1u >= kLcdAtlasSize) {
                    FlushQuads();
                    atlasLcdMap.clear();
                    lcdShelfX = 1u;
                    lcdShelfY = 1u;
                    lcdShelfH = 0u;
                }

                const keel::u32 gx = lcdShelfX;
                const keel::u32 gy = lcdShelfY;
                lcdShelfX += rg.width + 1u;
                lcdShelfH = keel::max(lcdShelfH, keel::as<keel::u32>(rg.height));

                const keel::usize numPx = keel::as<keel::usize>(rg.width) * rg.height;
                lcdUploadScratch.resize(numPx * 4u);
                if (rg.isSubpixel) {
                    for (keel::u32 y = 0u; y < rg.height; ++y) {
                        const keel::u8* srcRow = rg.bitmap + keel::as<keel::usize>(y) * rg.strideBytes;
                        keel::u8* dstRow = lcdUploadScratch.data() + keel::as<keel::usize>(y) * rg.width * 4u;
                        for (keel::u32 x = 0u; x < rg.width; ++x) {
                            dstRow[x * 4u + 0u] = srcRow[x * 3u + 0u];
                            dstRow[x * 4u + 1u] = srcRow[x * 3u + 1u];
                            dstRow[x * 4u + 2u] = srcRow[x * 3u + 2u];
                            dstRow[x * 4u + 3u] = 255u;
                        }
                    }
                } else {
                    for (keel::u32 y = 0u; y < rg.height; ++y) {
                        const keel::u8* srcRow = rg.bitmap + keel::as<keel::usize>(y) * rg.strideBytes;
                        keel::u8* dstRow = lcdUploadScratch.data() + keel::as<keel::usize>(y) * rg.width * 4u;
                        for (keel::u32 x = 0u; x < rg.width; ++x) {
                            const keel::u8 c = srcRow[x];
                            dstRow[x * 4u + 0u] = c;
                            dstRow[x * 4u + 1u] = c;
                            dstRow[x * 4u + 2u] = c;
                            dstRow[x * 4u + 3u] = 255u;
                        }
                    }
                }

                gl.BindTexture(kGlTexture2D, atlasLcdTex);
                gl.PixelStorei(kGlUnpackAlignment, 4);
                gl.TexSubImage2D(
                    kGlTexture2D,
                    0,
                    keel::as<GLint>(gx),
                    keel::as<GLint>(gy),
                    keel::as<GLsizei>(rg.width),
                    keel::as<GLsizei>(rg.height),
                    kGlRgba,
                    kGlUnsignedByte,
                    lcdUploadScratch.data()
                );

                const keel::f32 inv = 1.0f / keel::as<keel::f32>(kLcdAtlasSize);
                ag.u0 = keel::as<keel::f32>(gx) * inv;
                ag.v0 = keel::as<keel::f32>(gy) * inv;
                ag.u1 = keel::as<keel::f32>(gx + rg.width) * inv;
                ag.v1 = keel::as<keel::f32>(gy + rg.height) * inv;
            }
        }

        atlasLcdMap.put(key, ag);
        return atlasLcdMap.get(key);
    }
#endif
};

GlEs3Canvas::GlEs3Canvas(GlProcAddressFn procLoader, bool loadDefaultFonts)
    : m_impl(keel::make_unique<Impl>()) {
    (void)m_impl->Init(procLoader, loadDefaultFonts);
}

GlEs3Canvas::~GlEs3Canvas() {
    if (m_impl) {
        m_impl->Shutdown();
    }
}

bool GlEs3Canvas::IsInitialized() const noexcept {
    return m_impl && m_impl->initialized;
}

bool GlEs3Canvas::IsDesktopGl() const noexcept {
    return m_impl && m_impl->isDesktopGl;
}

keel::StringRef GlEs3Canvas::GetGlVersionString() const noexcept {
    return m_impl ? m_impl->glVersion.as_ref() : keel::StringRef{};
}

keel::StringRef GlEs3Canvas::GetGlRendererString() const noexcept {
    return m_impl ? m_impl->glRenderer.as_ref() : keel::StringRef{};
}

keel::i32 GlEs3Canvas::GetMsaaSamples() const noexcept {
    return m_impl ? m_impl->msaaSamples : 0;
}

keel::i32 GlEs3Canvas::GetStencilBits() const noexcept {
    return m_impl ? m_impl->stencilBits : 0;
}

bool GlEs3Canvas::LoadFontFile(keel::StringRef path, FontStyle style) {
#if GUIKIT_HAS_TEXT_RASTERIZER
    if (const auto asset = Assets::Find(path); asset && !asset->Empty()) {
        m_impl->ResetAtlas();
        return m_impl->ft.LoadFontMemory(asset->Bytes(), style, /*copyData=*/false);
    }
    if (auto bytes = keel::fs_read_file(path)) {
        m_impl->ResetAtlas();
        return m_impl->ft.LoadFontMemory(*bytes, style, /*copyData=*/true);
    }
#else
    (void)path;
    (void)style;
#endif
    return false;
}

bool GlEs3Canvas::LoadFontMemory(
    keel::StringRef /*name*/,
    keel::ArrayRef<keel::u8> data,
    FontStyle style,
    keel::StringRef /*mimeType*/
) {
#if GUIKIT_HAS_TEXT_RASTERIZER
    if (data.empty()) return false;
    m_impl->ResetAtlas();
    return m_impl->ft.LoadFontMemory(data, style, /*copyData=*/true);
#else
    (void)data;
    (void)style;
    return false;
#endif
}

void GlEs3Canvas::Begin(keel::u32 width, keel::u32 height, keel::f32 scale) {
    m_impl->fbWidth = width;
    m_impl->fbHeight = height;
    m_impl->scale = (scale > 0.0f) ? scale : 1.0f;
    m_impl->xfScale = 1.0f;
    m_impl->xfOffset = {0.0f, 0.0f};
    m_impl->transformStack.clear();
    m_impl->clipStack.clear();
    m_impl->quadBatch.clear();
    m_impl->triBatch.clear();
    m_impl->activeLcdBlend = false;
    m_impl->activeLcdRgbKey = 0u;
    m_impl->activeGpuPassesThisFrame = 0u;
    ++m_impl->frameCounter;

    if (!m_impl->initialized) return;

    auto& gl = m_impl->gl;
    gl.Viewport(0, 0, keel::as<GLsizei>(width), keel::as<GLsizei>(height));
    gl.Disable(kGlDepthTest);
    gl.Disable(kGlStencilTest);
    gl.Disable(kGlCullFace);
    gl.Disable(kGlScissorTest);
    if (m_impl->isDesktopGl && m_impl->msaaSamples > 1) {
        gl.Enable(kGlMultisample);
    }
    gl.Enable(kGlBlend);
    gl.BlendEquation(kGlFuncAdd);
    gl.BlendFuncSeparate(kGlSrcAlpha, kGlOneMinusSrcAlpha, kGlOne, kGlOneMinusSrcAlpha);
}

void GlEs3Canvas::End() {
    if (!m_impl->initialized) return;
    m_impl->FlushAll();
    m_impl->gl.Disable(kGlScissorTest);
}

keel::f32 GlEs3Canvas::GetScale() const noexcept {
    return m_impl->EffScale();
}

void GlEs3Canvas::SetScale(keel::f32 scale) {
    m_impl->scale = (scale > 0.0f) ? scale : 1.0f;
}

void GlEs3Canvas::PushTransform(const Transform2D& transform) {
    m_impl->transformStack.push_back(Impl::TransformState{m_impl->xfScale, m_impl->xfOffset});
    const Transform2D combined = Transform2D{m_impl->xfScale, m_impl->xfOffset}.Then(transform);
    m_impl->xfScale = combined.scale;
    m_impl->xfOffset = combined.offset;
}

void GlEs3Canvas::PopTransform() {
    if (m_impl->transformStack.empty()) return;
    const auto prev = m_impl->transformStack.back();
    m_impl->transformStack.pop_back();
    m_impl->xfScale = prev.scale;
    m_impl->xfOffset = prev.offset;
}

keel::StringRef GlEs3Canvas::GetBackendName() const noexcept {
#if defined(__EMSCRIPTEN__)
    return keel::StringRef("WebGL 2.0 (Native GPU)");
#else
    return (m_impl && m_impl->isDesktopGl)
        ? keel::StringRef("OpenGL 3.3 Core (Native GPU)")
        : keel::StringRef("OpenGL ES 3.0 (Native GPU)");
#endif
}

void GlEs3Canvas::Clear(Color color, keel::Optional<Rect> rect) {
    if (!m_impl->initialized) return;
    m_impl->FlushAll();
    auto& gl = m_impl->gl;

    if (!rect.has_value()) {
        gl.Disable(kGlScissorTest);
        gl.ClearColor(color.r / 255.0f, color.g / 255.0f, color.b / 255.0f, color.a / 255.0f);
        GLbitfield mask = kGlColorBufferBit;
        if (m_impl->stencilBits > 0) {
            if (gl.StencilMask) gl.StencilMask(0xFFu);
            if (gl.ClearStencil) gl.ClearStencil(0);
            mask |= kGlStencilBufferBit;
        }
        gl.Clear(mask);
        m_impl->ApplyClip();
    } else {
        const Rect pr = m_impl->PxRect(*rect);
        const GLint x0 = keel::max(0, keel::floor_i32(pr.x));
        const GLint y0Top = keel::max(0, keel::floor_i32(pr.y));
        const GLint x1 = keel::min(keel::as<GLint>(m_impl->fbWidth), keel::ceil_i32(pr.Right()));
        const GLint y1Top = keel::min(keel::as<GLint>(m_impl->fbHeight), keel::ceil_i32(pr.Bottom()));
        const GLsizei w = keel::max(0, x1 - x0);
        const GLsizei h = keel::max(0, y1Top - y0Top);
        const GLint glY = keel::as<GLint>(m_impl->fbHeight) - y1Top;
        gl.Enable(kGlScissorTest);
        gl.Scissor(x0, keel::max(0, glY), w, h);
        gl.ClearColor(color.r / 255.0f, color.g / 255.0f, color.b / 255.0f, color.a / 255.0f);
        gl.Clear(kGlColorBufferBit);
        m_impl->ApplyClip();
    }
}

void GlEs3Canvas::PushClip(Rect clipRect) {
    m_impl->FlushAll();
    const Rect mapped = m_impl->PxRect(clipRect);
    if (!m_impl->clipStack.empty()) {
        m_impl->clipStack.push_back(m_impl->clipStack.back().Intersect(mapped));
    } else {
        m_impl->clipStack.push_back(mapped);
    }
    m_impl->ApplyClip();
}

void GlEs3Canvas::PopClip() {
    if (m_impl->clipStack.empty()) return;
    m_impl->FlushAll();
    m_impl->clipStack.pop_back();
    m_impl->ApplyClip();
}

void GlEs3Canvas::DrawRect(Rect rect, Color fill) {
    if (fill.a == 0u || rect.w <= 0.0f || rect.h <= 0.0f) return;
    const Rect pr = m_impl->PxRect(rect);
    m_impl->PushQuad(QuadInstance{
        pr.x, pr.y, pr.w, pr.h,
        0.0f, 0.0f, 1.0f, 1.0f,
        fill.r / 255.0f, fill.g / 255.0f, fill.b / 255.0f, fill.a / 255.0f,
        0.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, kModeRoundRect, 0.0f
    }, 0u);
}

void GlEs3Canvas::DrawRoundRect(
    Rect rect,
    keel::f32 radius,
    Color fill,
    Color stroke,
    keel::f32 strokeWidth
) {
    if (rect.w <= 0.0f || rect.h <= 0.0f) return;
    const bool hasFill = (fill.a > 0u);
    const bool hasStroke = (stroke.a > 0u && strokeWidth > 0.0f);
    if (!hasFill && !hasStroke) return;

    const keel::f32 eff = m_impl->EffScale();
    const Rect pr = m_impl->PxRect(rect);
    const keel::f32 rPx = keel::max(0.0f, radius * eff);
    const keel::f32 swPx = hasStroke ? keel::max(1.0f, strokeWidth * eff) : 0.0f;

    m_impl->PushQuad(QuadInstance{
        pr.x, pr.y, pr.w, pr.h,
        0.0f, 0.0f, 1.0f, 1.0f,
        fill.r / 255.0f, fill.g / 255.0f, fill.b / 255.0f, fill.a / 255.0f,
        stroke.r / 255.0f, stroke.g / 255.0f, stroke.b / 255.0f, stroke.a / 255.0f,
        rPx, swPx, kModeRoundRect, 0.0f
    }, 0u);
}

void GlEs3Canvas::DrawShadow(
    Rect rect,
    keel::f32 cornerRadius,
    keel::f32 blurRadius,
    Vec2 offset,
    Color color,
    keel::f32 spread
) {
    if (!m_impl->initialized || color.a == 0u || rect.w <= 0.0f || rect.h <= 0.0f) return;
    const Rect shifted(
        rect.x + offset.x - spread,
        rect.y + offset.y - spread,
        rect.w + spread * 2.0f,
        rect.h + spread * 2.0f
    );
    if (shifted.w <= 0.0f || shifted.h <= 0.0f) return;

    const keel::f32 eff = m_impl->EffScale();
    const Rect pr = m_impl->PxRect(shifted);
    const keel::f32 sigma = keel::max(0.0f, blurRadius * eff * 0.5f);
    const keel::f32 rPx = keel::clamp((cornerRadius + spread) * eff, 0.0f, keel::min(pr.w, pr.h) * 0.5f);
    const keel::f32 pad = keel::ceil(3.0f * sigma) + 1.0f;

    m_impl->PushQuad(QuadInstance{
        pr.x - pad, pr.y - pad, pr.w + pad * 2.0f, pr.h + pad * 2.0f,
        pr.w * 0.5f, pr.h * 0.5f, 0.0f, 0.0f,
        color.r / 255.0f, color.g / 255.0f, color.b / 255.0f, color.a / 255.0f,
        0.0f, 0.0f, 0.0f, 0.0f,
        rPx, sigma, kModeShadow, 0.0f
    }, 0u);
}

Vec2 GlEs3Canvas::MeasureText(keel::StringRef text, keel::f32 fontSize) {
    return MeasureTextStyled(text, fontSize, FontStyle::Regular);
}

Vec2 GlEs3Canvas::MeasureTextStyled(keel::StringRef text, keel::f32 fontSize, FontStyle style) {
    if (fontSize <= 0.0f) return {0.0f, 0.0f};
#if GUIKIT_HAS_TEXT_RASTERIZER
    if (m_impl->ft.HasFontForStyle(style)) {
        const keel::f32 eff = m_impl->EffScale();
        const FontHinting effHint = TextCompositor::ResolveEffectiveHinting(m_impl->textSettings.hinting, eff);
        const Vec2 px = m_impl->ft.MeasureText(text, fontSize * eff, style, effHint);
        return {px.x / eff, px.y / eff};
    }
#endif
    const keel::f32 ratio = (style == FontStyle::Monospace) ? 0.60f : kDefaultCharAdvanceRatio;
    const keel::f32 w = keel::as<keel::f32>(CountUtf8Codepoints(text)) * (fontSize * ratio);
    return {w, GetLineHeight(fontSize)};
}

keel::f32 GlEs3Canvas::GetLineHeight(keel::f32 fontSize) {
    if (fontSize <= 0.0f) return 0.0f;
#if GUIKIT_HAS_TEXT_RASTERIZER
    if (m_impl->ft.HasFontForStyle(FontStyle::Regular)) {
        const keel::f32 eff = m_impl->EffScale();
        const FontHinting effHint = TextCompositor::ResolveEffectiveHinting(m_impl->textSettings.hinting, eff);
        TextEngine::FontMetrics fm{};
        if (m_impl->ft.GetFontMetrics(fontSize * eff, FontStyle::Regular, effHint, fm)) {
            return fm.lineHeightPx / eff;
        }
    }
#endif
    return fontSize * kDefaultLineHeightMultiplier;
}

void GlEs3Canvas::DrawText(keel::StringRef text, Vec2 pos, keel::f32 fontSize, Color color) {
    DrawTextStyled(text, pos, fontSize, color, FontStyle::Regular, 0.0f);
}

void GlEs3Canvas::DrawTextStyled(
    keel::StringRef text,
    Vec2 pos,
    keel::f32 fontSize,
    Color color,
    FontStyle style,
    keel::f32 angleDeg
) {
    if (!m_impl->initialized || text.empty() || fontSize <= 0.0f || color.a == 0u) return;

#if GUIKIT_HAS_TEXT_RASTERIZER
    if (!m_impl->ft.HasFontForStyle(style)) return;

    const keel::f32 eff = m_impl->EffScale();
    const keel::f32 scaledPx = fontSize * eff;
    const FontHinting effHint = TextCompositor::ResolveEffectiveHinting(m_impl->textSettings.hinting, eff);
    TextEngine::FontMetrics fm{};
    if (!m_impl->ft.GetFontMetrics(scaledPx, style, effHint, fm)) {
        return;
    }

    const Vec2 originPx = m_impl->PxPt(pos);
    const bool hasRot = (keel::abs(angleDeg) > 0.01f);
    const keel::f32 rad = hasRot ? keel::deg_to_rad(angleDeg) : 0.0f;
    const keel::f32 cosA = hasRot ? keel::cos(rad) : 1.0f;
    const keel::f32 sinA = hasRot ? keel::sin(rad) : 0.0f;

    const FontSmoothing effSmooth = hasRot
        ? FontSmoothing::Grayscale
        : TextCompositor::ResolveEffectiveSmoothing(m_impl->textSettings.smoothing, eff);
    const bool useLcd = (!hasRot && (effSmooth == FontSmoothing::SubpixelRGB || effSmooth == FontSmoothing::SubpixelBGR));
    const bool bgrOrder = (effSmooth == FontSmoothing::SubpixelBGR);
    const bool useSubPhase = (!hasRot && effHint != FontHinting::Normal);

    const keel::f32 clampedContrast = keel::clamp(m_impl->textSettings.contrast, 0.5f, 2.0f);
    const keel::f32 invContrast = 1.0f / clampedContrast;
    const keel::f32 sizeFactor = keel::clamp((28.0f - scaledPx) / 16.0f, 0.0f, 1.0f);
    const keel::f32 effDarken = keel::clamp(m_impl->textSettings.stemDarkening, 0.0f, 0.5f) * sizeFactor;
    const keel::f32 gammaFlag = m_impl->textSettings.gammaCorrectBlending ? 1.0f : 0.0f;

    const keel::f32 glyphMode = useLcd
        ? (bgrOrder ? kModeGlyphLcdBgr : kModeGlyphLcdRgb)
        : kModeGlyphR8;
    const keel::u32 lcdRgbKey = keel::as<keel::u32>(color.r) |
                                (keel::as<keel::u32>(color.g) << 8) |
                                (keel::as<keel::u32>(color.b) << 16);

    keel::f32 penX = hasRot ? 0.0f : originPx.x;
    const keel::f32 baselineY = hasRot ? fm.ascentPx : keel::round(originPx.y + fm.ascentPx);
    const keel::f32 pivotX = hasRot ? originPx.x : 0.0f;
    const keel::f32 pivotY = hasRot ? originPx.y : 0.0f;

    keel::u32 prevCp = 0u;
    keel::usize offset = 0u;
    while (offset < text.size()) {
        const keel::u32 cp = keel::utf8_decode_next(text, offset);
        if (cp == '\n' || cp == '\r') continue;
        if (cp == '\t') {
            if (const auto* sp = m_impl->ResolveAtlasGlyph(keel::as<keel::u32>(' '), scaledPx, style, effHint, 0u)) {
                penX += (sp->advancePx > 0.0f) ? (sp->advancePx * 4.0f) : (scaledPx * 2.4f);
            } else {
                penX += scaledPx * 2.4f;
            }
            prevCp = 0u;
            continue;
        }
        if (prevCp != 0u) {
            const keel::f32 kPx = m_impl->ft.GetKerningPx(prevCp, cp, scaledPx, style, effHint);
            penX += useSubPhase ? kPx : keel::round(kPx);
        }

        keel::f32 cellX = penX;
        keel::u8 phase = 0u;
        if (!hasRot) {
            if (useSubPhase) {
                if (useLcd) {
                    const keel::i32 qx = keel::round_i32(penX * 3.0f);
                    keel::i32 rem = qx % 3;
                    if (rem < 0) rem += 3;
                    cellX = keel::as<keel::f32>((qx - rem) / 3);
                    phase = keel::as<keel::u8>(rem);
                } else {
                    const keel::i32 qx = keel::round_i32(penX * 4.0f);
                    cellX = keel::as<keel::f32>(qx >> 2);
                    phase = keel::as<keel::u8>(qx & 3);
                }
            } else {
                cellX = keel::round(penX);
            }
        }

        const Impl::AtlasGlyph* ag = useLcd
            ? m_impl->ResolveLcdAtlasGlyph(cp, scaledPx, style, effHint, phase)
            : m_impl->ResolveAtlasGlyph(cp, scaledPx, style, effHint, phase);
        if (!ag) {
            prevCp = cp;
            continue;
        }

        if (!ag->empty) {
            const keel::f32 gx = cellX + keel::as<keel::f32>(ag->bearingX);
            const keel::f32 gy = baselineY - keel::as<keel::f32>(ag->bearingY);
            const GLuint tex = useLcd ? m_impl->atlasLcdTex : m_impl->atlasTex;
            m_impl->PushQuad(QuadInstance{
                gx, gy, keel::as<keel::f32>(ag->width), keel::as<keel::f32>(ag->height),
                ag->u0, ag->v0, ag->u1, ag->v1,
                color.r / 255.0f, color.g / 255.0f, color.b / 255.0f, color.a / 255.0f,
                pivotX, pivotY, invContrast, gammaFlag,
                cosA, sinA, glyphMode, effDarken
            }, tex, useLcd, lcdRgbKey);
        }
        penX += ag->advancePx;
        prevCp = cp;
    }
#else
    (void)pos;
    (void)style;
    (void)angleDeg;
#endif
}

void GlEs3Canvas::DrawTextBox(
    keel::StringRef text,
    Rect bounds,
    keel::f32 fontSize,
    Color color,
    keel::f32 alignX,
    keel::f32 alignY
) {
    DrawTextBoxStyled(text, bounds, fontSize, color, FontStyle::Regular, alignX, alignY);
}

void GlEs3Canvas::DrawTextBoxStyled(
    keel::StringRef text,
    Rect bounds,
    keel::f32 fontSize,
    Color color,
    FontStyle style,
    keel::f32 alignX,
    keel::f32 alignY
) {
    if (!m_impl->initialized || text.empty() || fontSize <= 0.0f || color.a == 0u ||
        bounds.w <= 0.0f || bounds.h <= 0.0f) {
        return;
    }

    keel::FixedString<256> truncated;
    keel::StringRef drawStr = text;
    if (MeasureTextStyled(text, fontSize, style).x > bounds.w + kTextSubpixelTolerance) {
        truncated = TextUtil::TruncateWithEllipsis(text, bounds.w, fontSize, this);
        drawStr = truncated;
    }

    const Vec2 dims = MeasureTextStyled(drawStr, fontSize, style);
    const keel::f32 x = bounds.x + keel::max(0.0f, (bounds.w - dims.x) * alignX);
    keel::f32 y = bounds.y + (bounds.h - dims.y) * alignY;

#if GUIKIT_HAS_TEXT_RASTERIZER
    if (m_impl->ft.HasFontForStyle(style)) {
        const keel::f32 eff = m_impl->EffScale();
        const FontHinting effHint = TextCompositor::ResolveEffectiveHinting(m_impl->textSettings.hinting, eff);
        TextEngine::FontMetrics fm{};
        if (m_impl->ft.GetFontMetrics(fontSize * eff, style, effHint, fm)) {
            const keel::f32 asc = fm.ascentPx / eff;
            const keel::f32 cap = fm.capHeightPx / eff;
            if (alignY > 0.4f && alignY < 0.6f) {
                const keel::f32 capTop = bounds.y + (bounds.h - cap) * 0.5f;
                y = capTop - (asc - cap);
            }
        }
    }
#endif

    const keel::f32 vPad = keel::max(4.0f, fontSize * 0.35f);
    PushClip(Rect(bounds.x - 2.0f, bounds.y - vPad, bounds.w + 4.0f, bounds.h + vPad * 2.0f));
    DrawTextStyled(drawStr, {x, y}, fontSize, color, style, 0.0f);
    PopClip();
}

void GlEs3Canvas::DrawLine(Vec2 p1, Vec2 p2, Color color, keel::f32 strokeWidth) {
    if (!m_impl->initialized || color.a == 0u || strokeWidth <= 0.0f) return;
    const keel::f32 eff = m_impl->EffScale();
    const Vec2 a = m_impl->PxPt(p1);
    const Vec2 b = m_impl->PxPt(p2);
    const keel::f32 swPx = keel::max(1.0f, strokeWidth * eff);
    const keel::f32 halfW = swPx * 0.5f + 1.0f;

    const keel::f32 minX = keel::min(a.x, b.x) - halfW;
    const keel::f32 minY = keel::min(a.y, b.y) - halfW;
    const keel::f32 maxX = keel::max(a.x, b.x) + halfW;
    const keel::f32 maxY = keel::max(a.y, b.y) + halfW;

    m_impl->PushQuad(QuadInstance{
        minX, minY, maxX - minX, maxY - minY,
        a.x, a.y, b.x, b.y,
        color.r / 255.0f, color.g / 255.0f, color.b / 255.0f, color.a / 255.0f,
        0.0f, 0.0f, 0.0f, 0.0f,
        0.0f, swPx, kModeLine, 0.0f
    }, 0u);
}

void GlEs3Canvas::DrawArc(
    Vec2 center,
    keel::f32 radius,
    keel::f32 startAngleRad,
    keel::f32 endAngleRad,
    Color color,
    keel::f32 strokeWidth,
    bool roundCaps
) {
    if (!m_impl->initialized || color.a == 0u || radius <= 0.25f || strokeWidth <= 0.0f) return;
    keel::f32 a0 = startAngleRad;
    keel::f32 a1 = endAngleRad;
    if (a1 < a0) {
        keel::swap(a0, a1);
    }
    const keel::f32 span = a1 - a0;
    if (span < 1e-4f) return;

    const keel::f32 eff = m_impl->EffScale();
    const Vec2 cPx = m_impl->PxPt(center);
    const keel::f32 ra = radius * eff;
    const keel::f32 swPx = keel::max(1.0f, strokeWidth * eff);
    const keel::f32 outerR = ra + swPx * 0.5f + 1.0f;

    const bool fullRing = (span >= keel::kTauF - 1e-4f);
    const keel::f32 midAngle = 0.5f * (a0 + a1);
    const keel::f32 halfAp = fullRing ? keel::kPiF : (0.5f * span);
    const keel::f32 capMode = fullRing ? 2.0f : (roundCaps ? 1.0f : 0.0f);

    m_impl->PushQuad(QuadInstance{
        cPx.x - outerR, cPx.y - outerR, outerR * 2.0f, outerR * 2.0f,
        keel::cos(midAngle), keel::sin(midAngle), keel::cos(halfAp), keel::sin(halfAp),
        color.r / 255.0f, color.g / 255.0f, color.b / 255.0f, color.a / 255.0f,
        0.0f, 0.0f, 0.0f, 0.0f,
        ra, swPx, kModeArc, capMode
    }, 0u);
}

void GlEs3Canvas::DrawTriangle(
    Vec2 p1,
    Vec2 p2,
    Vec2 p3,
    Color fill,
    Color stroke,
    keel::f32 strokeWidth
) {
    if (!m_impl->initialized) return;
    if (fill.a > 0u) {
        m_impl->PushTri(m_impl->PxPt(p1), m_impl->PxPt(p2), m_impl->PxPt(p3), fill);
    }
    if (stroke.a > 0u && strokeWidth > 0.0f) {
        DrawLine(p1, p2, stroke, strokeWidth);
        DrawLine(p2, p3, stroke, strokeWidth);
        DrawLine(p3, p1, stroke, strokeWidth);
    }
}

void GlEs3Canvas::DrawPath(const VectorPath& path, const PathPaint& paint) {
    if (!m_impl->initialized || path.Empty()) return;

    const bool doFill = paint.renderFill && paint.fillColor.a > 0u;
    const bool doStroke = paint.renderStroke && paint.strokeColor.a > 0u && paint.strokeWidthPx > 0.0f;
    if (!doFill && !doStroke) return;

    struct Subpath {
        keel::Array<Vec2> pts;
        bool closed{false};
    };

    keel::Array<Subpath> subpaths;
    Subpath cur{};

    auto commitSubpath = [&]() {
        if (cur.pts.size() >= 2u) {
            subpaths.push_back(keel::move(cur));
        }
        cur = Subpath{};
    };

    keel::usize ptIdx = 0u;
    const keel::usize numPts = path.points.size();
    for (PathVerb verb : path.verbs) {
        switch (verb) {
            case PathVerb::MoveTo:
                commitSubpath();
                if (ptIdx < numPts) {
                    cur.pts.push_back(path.points[ptIdx++]);
                }
                break;
            case PathVerb::LineTo:
                if (ptIdx < numPts) {
                    cur.pts.push_back(path.points[ptIdx++]);
                }
                break;
            case PathVerb::CubicTo:
                if (ptIdx + 2u < numPts && !cur.pts.empty()) {
                    const Vec2 p0 = cur.pts.back();
                    const Vec2 c1 = path.points[ptIdx];
                    const Vec2 c2 = path.points[ptIdx + 1u];
                    const Vec2 p3 = path.points[ptIdx + 2u];
                    ptIdx += 3u;
                    FlattenCubicBezier(p0, c1, c2, p3, cur.pts);
                } else {
                    ptIdx = keel::min(ptIdx + 3u, numPts);
                }
                break;
            case PathVerb::Close:
                cur.closed = true;
                commitSubpath();
                break;
        }
    }
    commitSubpath();
    if (subpaths.empty()) return;

    // 1. Fill pass (all subpaths before strokes)
    if (doFill) {
        keel::Array<keel::Array<Vec2>> fillPolys;
        fillPolys.reserve(subpaths.size());
        keel::f32 minX = 1e30f;
        keel::f32 minY = 1e30f;
        keel::f32 maxX = -1e30f;
        keel::f32 maxY = -1e30f;

        for (const Subpath& sp : subpaths) {
            if (sp.pts.size() < 3u) continue;
            keel::Array<Vec2> poly;
            poly.reserve(sp.pts.size());
            for (const Vec2& p : sp.pts) {
                const Vec2 px = m_impl->PxPt(p);
                if (poly.empty() ||
                    keel::abs(poly.back().x - px.x) > 1e-3f ||
                    keel::abs(poly.back().y - px.y) > 1e-3f) {
                    poly.push_back(px);
                }
            }
            if (poly.size() >= 3u &&
                keel::abs(poly.front().x - poly.back().x) <= 1e-3f &&
                keel::abs(poly.front().y - poly.back().y) <= 1e-3f) {
                poly.pop_back();
            }
            if (poly.size() >= 3u) {
                for (const Vec2& v : poly) {
                    minX = keel::min(minX, v.x);
                    minY = keel::min(minY, v.y);
                    maxX = keel::max(maxX, v.x);
                    maxY = keel::max(maxY, v.y);
                }
                fillPolys.push_back(keel::move(poly));
            }
        }

        if (!fillPolys.empty()) {
            const bool useStencil = m_impl->HasStencilOps() &&
                (paint.fillRule == FillRule::EvenOdd || fillPolys.size() > 1u);

            if (useStencil) {
                // GPU Stencil-then-Cover: handles EvenOdd holes, nested rings, self-intersections, and MSAA
                m_impl->FlushAll();
                auto& gl = m_impl->gl;
                gl.Enable(kGlStencilTest);
                gl.ColorMask(kGlFalse, kGlFalse, kGlFalse, kGlFalse);
                gl.StencilMask(0xFFu);
                gl.StencilFunc(kGlAlways, 0, 0xFFu);
                if (paint.fillRule == FillRule::EvenOdd || !gl.StencilOpSeparate) {
                    gl.StencilOp(kGlKeep, kGlKeep, kGlInvert);
                } else {
                    gl.StencilOpSeparate(kGlFront, kGlKeep, kGlKeep, kGlIncrWrap);
                    gl.StencilOpSeparate(kGlBack, kGlKeep, kGlKeep, kGlDecrWrap);
                }

                const Color maskCol(255u, 255u, 255u, 255u);
                for (const auto& poly : fillPolys) {
                    const Vec2& anchor = poly[0];
                    for (keel::usize i = 1u; i + 1u < poly.size(); ++i) {
                        m_impl->PushTri(anchor, poly[i], poly[i + 1u], maskCol);
                    }
                }
                m_impl->FlushTris();

                // Cover bounding box & simultaneously reset touched stencil pixels back to 0
                gl.ColorMask(kGlTrue, kGlTrue, kGlTrue, kGlTrue);
                gl.StencilFunc(kGlNotEqual, 0, 0xFFu);
                gl.StencilOp(kGlZero, kGlZero, kGlZero);

                const Vec2 tl{minX - 1.0f, minY - 1.0f};
                const Vec2 tr{maxX + 1.0f, minY - 1.0f};
                const Vec2 br{maxX + 1.0f, maxY + 1.0f};
                const Vec2 bl{minX - 1.0f, maxY + 1.0f};
                m_impl->PushTri(tl, tr, br, paint.fillColor);
                m_impl->PushTri(tl, br, bl, paint.fillColor);
                m_impl->FlushTris();
                gl.Disable(kGlStencilTest);
            } else {
                // Fast batched ear-clipping for single-contour NonZero polygons (zero stencil flush)
                for (const auto& poly : fillPolys) {
                    keel::f64 area2 = 0.0;
                    for (keel::usize i = 0; i < poly.size(); ++i) {
                        const Vec2& p0 = poly[i];
                        const Vec2& p1 = poly[(i + 1u) % poly.size()];
                        area2 += keel::as<keel::f64>(p0.x) * p1.y - keel::as<keel::f64>(p1.x) * p0.y;
                    }
                    const keel::f32 winding = (area2 >= 0.0) ? 1.0f : -1.0f;

                    keel::Array<keel::usize> idx;
                    idx.reserve(poly.size());
                    for (keel::usize i = 0; i < poly.size(); ++i) idx.push_back(i);

                    keel::usize guard = poly.size() * poly.size();
                    while (idx.size() > 3u && guard-- > 0u) {
                        bool clipped = false;
                        const keel::usize n = idx.size();
                        for (keel::usize i = 0; i < n; ++i) {
                            const Vec2& a = poly[idx[(i + n - 1u) % n]];
                            const Vec2& b = poly[idx[i]];
                            const Vec2& c = poly[idx[(i + 1u) % n]];
                            if (Cross2D(a, b, c) * winding <= 1e-5f) continue;

                            bool containsAny = false;
                            for (keel::usize j = 0; j < n; ++j) {
                                if (j == i || j == (i + n - 1u) % n || j == (i + 1u) % n) continue;
                                if (PointInTri(poly[idx[j]], a, b, c)) {
                                    containsAny = true;
                                    break;
                                }
                            }
                            if (!containsAny) {
                                m_impl->PushTri(a, b, c, paint.fillColor);
                                for (keel::usize k = i + 1u; k < n; ++k) {
                                    idx[k - 1u] = idx[k];
                                }
                                idx.pop_back();
                                clipped = true;
                                break;
                            }
                        }
                        if (!clipped) break;
                    }
                    if (idx.size() == 3u) {
                        m_impl->PushTri(poly[idx[0]], poly[idx[1]], poly[idx[2]], paint.fillColor);
                    }
                }
            }
        }
    }

    // 2. Stroke pass (anti-aliased solid or dashed line capsules on top of fill)
    if (doStroke) {
        for (const Subpath& sp : subpaths) {
            const keel::usize segCount = sp.closed ? sp.pts.size() : (sp.pts.size() - 1u);
            if (paint.dashed && paint.dashOnPx > 0.0f && paint.dashOffPx > 0.0f) {
                const keel::f32 dashOn = paint.dashOnPx;
                const keel::f32 period = dashOn + paint.dashOffPx;
                keel::f32 phase = 0.0f;
                for (keel::usize i = 0; i < segCount; ++i) {
                    const Vec2 a = sp.pts[i];
                    const Vec2 b = sp.pts[(i + 1u) % sp.pts.size()];
                    const keel::f32 dx = b.x - a.x;
                    const keel::f32 dy = b.y - a.y;
                    const keel::f32 segLen = keel::hypot(dx, dy);
                    if (segLen <= 1e-4f) continue;
                    const keel::f32 invLen = 1.0f / segLen;
                    const keel::f32 ux = dx * invLen;
                    const keel::f32 uy = dy * invLen;
                    keel::f32 dist = 0.0f;
                    while (dist < segLen - 1e-4f) {
                        const keel::f32 remain = segLen - dist;
                        if (phase < dashOn) {
                            const keel::f32 step = keel::min(remain, dashOn - phase);
                            DrawLine(
                                Vec2{a.x + ux * dist, a.y + uy * dist},
                                Vec2{a.x + ux * (dist + step), a.y + uy * (dist + step)},
                                paint.strokeColor,
                                paint.strokeWidthPx
                            );
                            dist += step;
                            phase += step;
                        } else {
                            const keel::f32 step = keel::min(remain, period - phase);
                            dist += step;
                            phase += step;
                        }
                        if (phase >= period - 1e-4f) {
                            phase = 0.0f;
                        }
                    }
                }
            } else {
                for (keel::usize i = 0; i < segCount; ++i) {
                    DrawLine(sp.pts[i], sp.pts[(i + 1u) % sp.pts.size()], paint.strokeColor, paint.strokeWidthPx);
                }
            }
        }
    }
}

Vec2 GlEs3Canvas::GetImageSize(keel::StringRef source) {
    if (!m_impl) return {0.0f, 0.0f};
    const auto* img = m_impl->ResolveImage(source, /*requireTexture=*/false);
    if (!img || !img->valid) return {0.0f, 0.0f};
    return {keel::as<keel::f32>(img->width), keel::as<keel::f32>(img->height)};
}

bool GlEs3Canvas::DrawImage(
    keel::StringRef source,
    Rect destRect,
    keel::f32 cornerRadius,
    keel::u8 opacity
) {
    if (!m_impl || !m_impl->initialized || source.empty() || opacity == 0u ||
        destRect.w <= 0.0f || destRect.h <= 0.0f) {
        return false;
    }

    auto* img = m_impl->ResolveImage(source, /*requireTexture=*/true);
    if (!img || !img->valid || img->texId == 0u) {
        return false;
    }
    img->lastUsedFrame = m_impl->frameCounter;

    const keel::f32 eff = m_impl->EffScale();
    const Rect pr = m_impl->PxRect(destRect);
    m_impl->PushQuad(QuadInstance{
        pr.x, pr.y, pr.w, pr.h,
        0.0f, 0.0f, 1.0f, 1.0f,
        1.0f, 1.0f, 1.0f, opacity / 255.0f,
        0.0f, 0.0f, 0.0f, 0.0f,
        keel::max(0.0f, cornerRadius * eff), 0.0f, kModeTexture, 0.0f
    }, img->texId);
    return true;
}

bool GlEs3Canvas::DrawPixels(
    PixelRef pixels,
    Rect destRect,
    keel::f32 cornerRadius,
    keel::u8 opacity
) {
    if (!m_impl->initialized || pixels.Empty() || opacity == 0u ||
        destRect.w <= 0.0f || destRect.h <= 0.0f) {
        return false;
    }

    // Flush pending quads before modifying the shared transient texture
    m_impl->FlushAll();

    auto& gl = m_impl->gl;
    const keel::u32 w = pixels.Width();
    const keel::u32 h = pixels.Height();

    keel::Array<keel::u32> packed;
    const keel::u32* uploadPtr = pixels.Data();
    if (pixels.RowStride() != w) {
        packed.resize(keel::as<keel::usize>(w) * h);
        for (keel::u32 y = 0; y < h; ++y) {
            const auto row = pixels.Row(y);
            keel::mem_copy(packed.data() + keel::as<keel::usize>(y) * w, row.data(), keel::as<keel::usize>(w) * sizeof(keel::u32));
        }
        uploadPtr = packed.data();
    }

    gl.BindTexture(kGlTexture2D, m_impl->transientPixelsTex);
    gl.PixelStorei(kGlUnpackAlignment, 4);
    if (w != m_impl->transientTexW || h != m_impl->transientTexH) {
        m_impl->transientTexW = w;
        m_impl->transientTexH = h;
        gl.TexImage2D(
            kGlTexture2D,
            0,
            keel::as<GLint>(kGlRgba8),
            keel::as<GLsizei>(w),
            keel::as<GLsizei>(h),
            0,
            kGlRgba,
            kGlUnsignedByte,
            uploadPtr
        );
    } else {
        gl.TexSubImage2D(
            kGlTexture2D,
            0,
            0,
            0,
            keel::as<GLsizei>(w),
            keel::as<GLsizei>(h),
            kGlRgba,
            kGlUnsignedByte,
            uploadPtr
        );
    }

    const keel::f32 eff = m_impl->EffScale();
    const Rect pr = m_impl->PxRect(destRect);
    m_impl->PushQuad(QuadInstance{
        pr.x, pr.y, pr.w, pr.h,
        0.0f, 0.0f, 1.0f, 1.0f,
        1.0f, 1.0f, 1.0f, opacity / 255.0f,
        0.0f, 0.0f, 0.0f, 0.0f,
        keel::max(0.0f, cornerRadius * eff), 0.0f, kModeTexture, 0.0f
    }, m_impl->transientPixelsTex);
    m_impl->FlushQuads();
    return true;
}

void GlEs3Canvas::DrawTexture(
    keel::u64 textureId,
    Rect destRect,
    keel::Optional<Rect> /*srcRect*/,
    Color tint
) {
    if (!m_impl->initialized || textureId == 0u || tint.a == 0u ||
        destRect.w <= 0.0f || destRect.h <= 0.0f) {
        return;
    }
    const Rect pr = m_impl->PxRect(destRect);
    m_impl->PushQuad(QuadInstance{
        pr.x, pr.y, pr.w, pr.h,
        0.0f, 0.0f, 1.0f, 1.0f,
        tint.r / 255.0f, tint.g / 255.0f, tint.b / 255.0f, tint.a / 255.0f,
        0.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, kModeTexture, 0.0f
    }, keel::as<GLuint>(textureId));
}

void GlEs3Canvas::SetGpuShadersEnabled(bool enabled) noexcept {
    if (m_impl) {
        m_impl->gpuShadersEnabled = enabled;
    }
}

bool GlEs3Canvas::SupportsGpuShaders() const noexcept {
    return m_impl && m_impl->initialized && m_impl->gpuShadersEnabled;
}

bool GlEs3Canvas::HasActiveGpuPasses() const noexcept {
    return m_impl && m_impl->activeGpuPassesThisFrame > 0u;
}

bool GlEs3Canvas::DrawGpuShaderPass(const GpuShaderPassDesc& pass, bool /*punchHole*/) {
    if (!m_impl || !m_impl->initialized || !m_impl->gpuShadersEnabled) {
        return false;
    }
    if (pass.shader.shaderId.empty() || pass.shader.glslEs2.empty() ||
        pass.dataField.empty() || pass.plotRect.w <= 0.0f || pass.plotRect.h <= 0.0f) {
        return false;
    }

    const keel::u32 texH = keel::as<keel::u32>(pass.dataField.extent(0));
    const keel::u32 texW = keel::as<keel::u32>(pass.dataField.extent(1));
    if (texW == 0u || texH == 0u) {
        return false;
    }

    const Rect plotPx = m_impl->PxRect(pass.plotRect);
    const Rect clipPx = m_impl->clipStack.empty()
        ? plotPx
        : plotPx.Intersect(m_impl->clipStack.back());
    if (clipPx.w <= 0.5f || clipPx.h <= 0.5f) {
        return true; // Clipped completely out of view
    }

    const auto* prog = m_impl->ResolvePassProgram(pass.shader.shaderId, pass.shader.glslEs2);
    if (!prog || prog->failed || prog->program == 0u) {
        return false;
    }

    // Flush pending UI geometry so this GPU shader renders inline in exact Z-order
    m_impl->FlushAll();

    auto& gl = m_impl->gl;
    auto* wt = m_impl->gpuWidgetTex.get(pass.widgetId);
    if (!wt) {
        Impl::WidgetGpuTextures fresh{};
        m_impl->gpuWidgetTex.put(pass.widgetId, fresh);
        wt = m_impl->gpuWidgetTex.get(pass.widgetId);
    }
    if (!wt) {
        return false;
    }
    wt->lastUsedFrame = m_impl->frameCounter;

    const keel::f32 invNormScale = 1.0f / keel::max(1e-6f, pass.dataNormScale);
    const bool needFullUpload = (wt->dataTex == 0u || wt->texW != texW || wt->texH != texH ||
                                 wt->fullGen != pass.fullGeneration);
    if (needFullUpload) {
        if (wt->dataTex == 0u) {
            gl.GenTextures(1, &wt->dataTex);
        }
        gl.ActiveTexture(kGlTexture0);
        gl.BindTexture(kGlTexture2D, wt->dataTex);
        gl.TexParameteri(kGlTexture2D, kGlTextureMinFilter, keel::as<GLint>(kGlNearest));
        gl.TexParameteri(kGlTexture2D, kGlTextureMagFilter, keel::as<GLint>(kGlNearest));
        gl.TexParameteri(kGlTexture2D, kGlTextureWrapS, keel::as<GLint>(kGlClampToEdge));
        gl.TexParameteri(kGlTexture2D, kGlTextureWrapT, keel::as<GLint>(kGlClampToEdge));
        gl.PixelStorei(kGlUnpackAlignment, 4);

        const keel::usize count = keel::as<keel::usize>(texW) * texH;
        m_impl->gpuRgbaScratch.resize(count);
        const keel::f32* src = pass.dataField.data();
        for (keel::usize i = 0u; i < count; ++i) {
            m_impl->gpuRgbaScratch[i] = PackRG16Norm((src[i] - pass.dataNormOffset) * invNormScale);
        }

        if (wt->texW != texW || wt->texH != texH) {
            wt->texW = texW;
            wt->texH = texH;
            gl.TexImage2D(
                kGlTexture2D,
                0,
                keel::as<GLint>(kGlRgba8),
                keel::as<GLsizei>(texW),
                keel::as<GLsizei>(texH),
                0,
                kGlRgba,
                kGlUnsignedByte,
                m_impl->gpuRgbaScratch.data()
            );
        } else {
            gl.TexSubImage2D(
                kGlTexture2D,
                0,
                0,
                0,
                keel::as<GLsizei>(texW),
                keel::as<GLsizei>(texH),
                kGlRgba,
                kGlUnsignedByte,
                m_impl->gpuRgbaScratch.data()
            );
        }
        wt->fullGen = pass.fullGeneration;
        wt->sliceGen = pass.sliceGeneration;
    } else if (pass.sliceGeneration != 0u && wt->sliceGen != pass.sliceGeneration && pass.headRow < texH) {
        // O(1) ring-buffer row update (Waterfall / live spectrogram)
        m_impl->gpuRgbaScratch.resize(texW);
        const keel::f32* rowSrc = pass.dataField.data() + keel::as<keel::usize>(pass.headRow) * texW;
        for (keel::u32 x = 0u; x < texW; ++x) {
            m_impl->gpuRgbaScratch[x] = PackRG16Norm((rowSrc[x] - pass.dataNormOffset) * invNormScale);
        }
        gl.ActiveTexture(kGlTexture0);
        gl.BindTexture(kGlTexture2D, wt->dataTex);
        gl.PixelStorei(kGlUnpackAlignment, 4);
        gl.TexSubImage2D(
            kGlTexture2D,
            0,
            0,
            keel::as<GLint>(pass.headRow),
            keel::as<GLsizei>(texW),
            1,
            kGlRgba,
            kGlUnsignedByte,
            m_impl->gpuRgbaScratch.data()
        );
        wt->sliceGen = pass.sliceGeneration;
    }

    if (pass.lutAbgr256.size() >= 256u) {
        if (!wt->lutUploaded || wt->lutId != pass.colormapId || wt->lutTex == 0u) {
            if (wt->lutTex == 0u) {
                gl.GenTextures(1, &wt->lutTex);
            }
            gl.ActiveTexture(kGlTexture1);
            gl.BindTexture(kGlTexture2D, wt->lutTex);
            gl.TexParameteri(kGlTexture2D, kGlTextureMinFilter, keel::as<GLint>(kGlLinear));
            gl.TexParameteri(kGlTexture2D, kGlTextureMagFilter, keel::as<GLint>(kGlLinear));
            gl.TexParameteri(kGlTexture2D, kGlTextureWrapS, keel::as<GLint>(kGlClampToEdge));
            gl.TexParameteri(kGlTexture2D, kGlTextureWrapT, keel::as<GLint>(kGlClampToEdge));
            gl.PixelStorei(kGlUnpackAlignment, 4);
            gl.TexImage2D(
                kGlTexture2D,
                0,
                keel::as<GLint>(kGlRgba8),
                256,
                1,
                0,
                kGlRgba,
                kGlUnsignedByte,
                pass.lutAbgr256.data()
            );
            wt->lutId = pass.colormapId;
            wt->lutUploaded = true;
        }
    }

    // Apply clip scissor for this GPU pass
    const GLint scX0 = keel::max(0, keel::floor_i32(clipPx.x));
    const GLint scY0Top = keel::max(0, keel::floor_i32(clipPx.y));
    const GLint scX1 = keel::min(keel::as<GLint>(m_impl->fbWidth), keel::ceil_i32(clipPx.Right()));
    const GLint scY1Top = keel::min(keel::as<GLint>(m_impl->fbHeight), keel::ceil_i32(clipPx.Bottom()));
    const GLsizei scW = keel::max(0, scX1 - scX0);
    const GLsizei scH = keel::max(0, scY1Top - scY0Top);
    const GLint scGlY = keel::as<GLint>(m_impl->fbHeight) - scY1Top;

    gl.Enable(kGlScissorTest);
    gl.Scissor(scX0, keel::max(0, scGlY), scW, scH);

    gl.UseProgram(prog->program);
    if (prog->locViewport >= 0) {
        gl.Uniform2f(prog->locViewport, keel::as<keel::f32>(m_impl->fbWidth), keel::as<keel::f32>(m_impl->fbHeight));
    }
    if (prog->locPlotRect >= 0) {
        gl.Uniform4f(prog->locPlotRect, plotPx.x, plotPx.y, plotPx.w, plotPx.h);
    }
    if (prog->locGridDims >= 0) {
        gl.Uniform2f(prog->locGridDims, keel::as<keel::f32>(texW), keel::as<keel::f32>(texH));
    }
    if (prog->locBgColor >= 0) {
        gl.Uniform3f(
            prog->locBgColor,
            pass.bgColor.r / 255.0f,
            pass.bgColor.g / 255.0f,
            pass.bgColor.b / 255.0f
        );
    }
    if (prog->locParams >= 0) {
        gl.Uniform4fv(prog->locParams, 8, &pass.params[0].x);
    }

    gl.ActiveTexture(kGlTexture0);
    gl.BindTexture(kGlTexture2D, wt->dataTex);
    if (prog->locDataTex >= 0) {
        gl.Uniform1i(prog->locDataTex, 0);
    }

    gl.ActiveTexture(kGlTexture1);
    gl.BindTexture(kGlTexture2D, wt->lutTex ? wt->lutTex : m_impl->whiteTex);
    if (prog->locLutTex >= 0) {
        gl.Uniform1i(prog->locLutTex, 1);
    }

    gl.BindVertexArray(m_impl->passQuadVao);
    gl.DrawArrays(kGlTriangles, 0, 6);

    // Restore canvas GL state
    gl.BindVertexArray(0);
    gl.ActiveTexture(kGlTexture1);
    gl.BindTexture(kGlTexture2D, 0);
    gl.ActiveTexture(kGlTexture0);
    gl.BindTexture(kGlTexture2D, 0);
    m_impl->ApplyClip();

    ++m_impl->activeGpuPassesThisFrame;
    return true;
}

void GlEs3Canvas::TrimCaches() noexcept {
    if (!m_impl) return;
#if GUIKIT_HAS_TEXT_RASTERIZER
    m_impl->ft.TrimCaches();
    m_impl->lcdUploadScratch = {};
#endif
    m_impl->gpuRgbaScratch = {};
    if (m_impl->quadBatch.empty() && m_impl->quadBatch.capacity() > 4096u) {
        m_impl->quadBatch = {};
    }
    if (m_impl->triBatch.empty() && m_impl->triBatch.capacity() > 8192u) {
        m_impl->triBatch = {};
    }

    // Evict GPU shader textures for widgets that are no longer active on screen
    if (m_impl->initialized && !m_impl->gpuWidgetTex.empty()) {
        keel::SmallArray<keel::u64, 16> staleWidgets;
        for (auto [wid, wt] : m_impl->gpuWidgetTex) {
            if (wt.lastUsedFrame + 2u < m_impl->frameCounter) {
                if (wt.dataTex != 0u && m_impl->gl.DeleteTextures) {
                    m_impl->gl.DeleteTextures(1, &wt.dataTex);
                    wt.dataTex = 0u;
                }
                if (wt.lutTex != 0u && m_impl->gl.DeleteTextures) {
                    m_impl->gl.DeleteTextures(1, &wt.lutTex);
                    wt.lutTex = 0u;
                }
                if (staleWidgets.size() < staleWidgets.capacity()) {
                    staleWidgets.push_back(wid);
                }
            }
        }
        for (keel::u64 wid : staleWidgets) {
            m_impl->gpuWidgetTex.remove(wid);
        }
    }

    // Evict stale image textures when image cache grows beyond lightweight working set
    if (m_impl->initialized && m_impl->imageCache.size() > 16u) {
        keel::Array<keel::String> staleImages;
        for (auto [key, img] : m_impl->imageCache) {
            if (img.lastUsedFrame + 8u < m_impl->frameCounter) {
                if (img.texId != 0u && m_impl->gl.DeleteTextures) {
                    m_impl->gl.DeleteTextures(1, &img.texId);
                    img.texId = 0u;
                }
                staleImages.push_back(key.clone());
            }
        }
        for (const auto& key : staleImages) {
            m_impl->imageCache.remove(key);
        }
    }
}

keel::StringRef GlEs3Canvas::GetTextEngineName() const noexcept {
#if defined(GUIKIT_HAS_FREETYPE) && GUIKIT_HAS_FREETYPE
    return "FreeType (GPU Atlas + LCD)";
#elif GUIKIT_HAS_TEXT_RASTERIZER
    return "keel::font (GPU Atlas + LCD)";
#else
    return "None";
#endif
}

bool GlEs3Canvas::SupportsFontHinting() const noexcept {
#if GUIKIT_HAS_TEXT_RASTERIZER
    return true;
#else
    return false;
#endif
}

void GlEs3Canvas::SetTextRenderSettings(const TextRenderSettings& settings) noexcept {
    if (!m_impl) return;
    const bool hintingChanged = (m_impl->textSettings.hinting != settings.hinting);
    m_impl->textSettings = settings;
#if GUIKIT_HAS_TEXT_RASTERIZER
    if (hintingChanged && m_impl->initialized) {
        m_impl->ResetAtlas();
    }
#else
    (void)hintingChanged;
#endif
}

const TextRenderSettings& GlEs3Canvas::GetTextRenderSettings() const noexcept {
    return m_impl->textSettings;
}

} // namespace guikit::native
