#pragma once

#include "guikit/backend/i_platform_window.hpp"
#include "keel/core.h"
#include "keel/dynlib.h"

namespace guikit::native::detail {

using GLenum = keel::u32;
using GLbitfield = keel::u32;
using GLuint = keel::u32;
using GLint = keel::i32;
using GLsizei = keel::i32;
using GLboolean = keel::u8;
using GLubyte = keel::u8;
using GLfloat = float;
using GLchar = char;
using GLsizeiptr = keel::isize;
using GLintptr = keel::isize;

#if defined(_WIN32)
#define GUIKIT_GL_APIENTRY __stdcall
#else
#define GUIKIT_GL_APIENTRY
#endif

static constexpr GLenum kGlFalse = 0u;
static constexpr GLenum kGlTrue = 1u;

static constexpr GLbitfield kGlColorBufferBit = 0x00004000u;

static constexpr GLenum kGlTriangles = 0x0004u;
static constexpr GLenum kGlUnsignedByte = 0x1401u;
static constexpr GLenum kGlFloat = 0x1406u;

static constexpr GLenum kGlBlend = 0x0BE2u;
static constexpr GLenum kGlScissorTest = 0x0C11u;
static constexpr GLenum kGlDepthTest = 0x0B71u;
static constexpr GLenum kGlCullFace = 0x0B44u;
static constexpr GLenum kGlUnpackAlignment = 0x0CF5u;

static constexpr GLenum kGlOne = 1u;
static constexpr GLenum kGlOneMinusSrcColor = 0x0301u;
static constexpr GLenum kGlSrcAlpha = 0x0302u;
static constexpr GLenum kGlOneMinusSrcAlpha = 0x0303u;
static constexpr GLenum kGlConstantColor = 0x8001u;
static constexpr GLenum kGlFuncAdd = 0x8006u;

static constexpr GLenum kGlTexture2D = 0x0DE1u;
static constexpr GLenum kGlTexture0 = 0x84C0u;
static constexpr GLenum kGlTexture1 = 0x84C1u;
static constexpr GLenum kGlTextureMagFilter = 0x2800u;
static constexpr GLenum kGlTextureMinFilter = 0x2801u;
static constexpr GLenum kGlTextureWrapS = 0x2802u;
static constexpr GLenum kGlTextureWrapT = 0x2803u;
static constexpr GLenum kGlNearest = 0x2600u;
static constexpr GLenum kGlLinear = 0x2601u;
static constexpr GLenum kGlClampToEdge = 0x812Fu;

static constexpr GLenum kGlRed = 0x1903u;
static constexpr GLenum kGlRgba = 0x1908u;
static constexpr GLenum kGlR8 = 0x8229u;
static constexpr GLenum kGlRgba8 = 0x8058u;

static constexpr GLenum kGlArrayBuffer = 0x8892u;
static constexpr GLenum kGlStreamDraw = 0x88E0u;
static constexpr GLenum kGlStaticDraw = 0x88E4u;

static constexpr GLenum kGlFragmentShader = 0x8B30u;
static constexpr GLenum kGlVertexShader = 0x8B31u;
static constexpr GLenum kGlCompileStatus = 0x8B81u;
static constexpr GLenum kGlLinkStatus = 0x8B82u;
static constexpr GLenum kGlInfoLogLength = 0x8B84u;

static constexpr GLenum kGlVendor = 0x1F00u;
static constexpr GLenum kGlRenderer = 0x1F01u;
static constexpr GLenum kGlVersion = 0x1F02u;

struct GlEs3Api {
    keel::DynLib fallbackLib;

    using PfnGetString = const GLubyte*(GUIKIT_GL_APIENTRY*)(GLenum name);
    using PfnGetError = GLenum(GUIKIT_GL_APIENTRY*)();
    using PfnViewport = void(GUIKIT_GL_APIENTRY*)(GLint x, GLint y, GLsizei width, GLsizei height);
    using PfnScissor = void(GUIKIT_GL_APIENTRY*)(GLint x, GLint y, GLsizei width, GLsizei height);
    using PfnEnable = void(GUIKIT_GL_APIENTRY*)(GLenum cap);
    using PfnDisable = void(GUIKIT_GL_APIENTRY*)(GLenum cap);
    using PfnClearColor = void(GUIKIT_GL_APIENTRY*)(GLfloat r, GLfloat g, GLfloat b, GLfloat a);
    using PfnClear = void(GUIKIT_GL_APIENTRY*)(GLbitfield mask);
    using PfnBlendFuncSeparate = void(GUIKIT_GL_APIENTRY*)(GLenum sRGB, GLenum dRGB, GLenum sAlpha, GLenum dAlpha);
    using PfnBlendColor = void(GUIKIT_GL_APIENTRY*)(GLfloat r, GLfloat g, GLfloat b, GLfloat a);
    using PfnBlendEquation = void(GUIKIT_GL_APIENTRY*)(GLenum mode);
    using PfnPixelStorei = void(GUIKIT_GL_APIENTRY*)(GLenum pname, GLint param);

    using PfnCreateShader = GLuint(GUIKIT_GL_APIENTRY*)(GLenum type);
    using PfnShaderSource = void(GUIKIT_GL_APIENTRY*)(GLuint shader, GLsizei count, const GLchar* const* str, const GLint* len);
    using PfnCompileShader = void(GUIKIT_GL_APIENTRY*)(GLuint shader);
    using PfnGetShaderiv = void(GUIKIT_GL_APIENTRY*)(GLuint shader, GLenum pname, GLint* params);
    using PfnGetShaderInfoLog = void(GUIKIT_GL_APIENTRY*)(GLuint shader, GLsizei maxLen, GLsizei* len, GLchar* infoLog);
    using PfnDeleteShader = void(GUIKIT_GL_APIENTRY*)(GLuint shader);

    using PfnCreateProgram = GLuint(GUIKIT_GL_APIENTRY*)();
    using PfnAttachShader = void(GUIKIT_GL_APIENTRY*)(GLuint program, GLuint shader);
    using PfnLinkProgram = void(GUIKIT_GL_APIENTRY*)(GLuint program);
    using PfnGetProgramiv = void(GUIKIT_GL_APIENTRY*)(GLuint program, GLenum pname, GLint* params);
    using PfnGetProgramInfoLog = void(GUIKIT_GL_APIENTRY*)(GLuint program, GLsizei maxLen, GLsizei* len, GLchar* infoLog);
    using PfnUseProgram = void(GUIKIT_GL_APIENTRY*)(GLuint program);
    using PfnDeleteProgram = void(GUIKIT_GL_APIENTRY*)(GLuint program);

    using PfnGetUniformLocation = GLint(GUIKIT_GL_APIENTRY*)(GLuint program, const GLchar* name);
    using PfnUniform1i = void(GUIKIT_GL_APIENTRY*)(GLint loc, GLint v0);
    using PfnUniform1f = void(GUIKIT_GL_APIENTRY*)(GLint loc, GLfloat v0);
    using PfnUniform2f = void(GUIKIT_GL_APIENTRY*)(GLint loc, GLfloat v0, GLfloat v1);
    using PfnUniform3f = void(GUIKIT_GL_APIENTRY*)(GLint loc, GLfloat v0, GLfloat v1, GLfloat v2);
    using PfnUniform4f = void(GUIKIT_GL_APIENTRY*)(GLint loc, GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3);
    using PfnUniform4fv = void(GUIKIT_GL_APIENTRY*)(GLint loc, GLsizei count, const GLfloat* value);

    using PfnGenVertexArrays = void(GUIKIT_GL_APIENTRY*)(GLsizei n, GLuint* arrays);
    using PfnBindVertexArray = void(GUIKIT_GL_APIENTRY*)(GLuint array);
    using PfnDeleteVertexArrays = void(GUIKIT_GL_APIENTRY*)(GLsizei n, const GLuint* arrays);

    using PfnGenBuffers = void(GUIKIT_GL_APIENTRY*)(GLsizei n, GLuint* buffers);
    using PfnBindBuffer = void(GUIKIT_GL_APIENTRY*)(GLenum target, GLuint buffer);
    using PfnBufferData = void(GUIKIT_GL_APIENTRY*)(GLenum target, GLsizeiptr size, const void* data, GLenum usage);
    using PfnBufferSubData = void(GUIKIT_GL_APIENTRY*)(GLenum target, GLintptr offset, GLsizeiptr size, const void* data);
    using PfnDeleteBuffers = void(GUIKIT_GL_APIENTRY*)(GLsizei n, const GLuint* buffers);

    using PfnEnableVertexAttribArray = void(GUIKIT_GL_APIENTRY*)(GLuint index);
    using PfnVertexAttribPointer = void(GUIKIT_GL_APIENTRY*)(GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const void* pointer);
    using PfnVertexAttribDivisor = void(GUIKIT_GL_APIENTRY*)(GLuint index, GLuint divisor);

    using PfnDrawArrays = void(GUIKIT_GL_APIENTRY*)(GLenum mode, GLint first, GLsizei count);
    using PfnDrawArraysInstanced = void(GUIKIT_GL_APIENTRY*)(GLenum mode, GLint first, GLsizei count, GLsizei instancecount);

    using PfnGenTextures = void(GUIKIT_GL_APIENTRY*)(GLsizei n, GLuint* textures);
    using PfnBindTexture = void(GUIKIT_GL_APIENTRY*)(GLenum target, GLuint texture);
    using PfnActiveTexture = void(GUIKIT_GL_APIENTRY*)(GLenum texture);
    using PfnTexParameteri = void(GUIKIT_GL_APIENTRY*)(GLenum target, GLenum pname, GLint param);
    using PfnTexImage2D = void(GUIKIT_GL_APIENTRY*)(GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const void* pixels);
    using PfnTexSubImage2D = void(GUIKIT_GL_APIENTRY*)(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLenum type, const void* pixels);
    using PfnDeleteTextures = void(GUIKIT_GL_APIENTRY*)(GLsizei n, const GLuint* textures);

    PfnGetString GetString{nullptr};
    PfnGetError GetError{nullptr};
    PfnViewport Viewport{nullptr};
    PfnScissor Scissor{nullptr};
    PfnEnable Enable{nullptr};
    PfnDisable Disable{nullptr};
    PfnClearColor ClearColor{nullptr};
    PfnClear Clear{nullptr};
    PfnBlendFuncSeparate BlendFuncSeparate{nullptr};
    PfnBlendColor BlendColor{nullptr};
    PfnBlendEquation BlendEquation{nullptr};
    PfnPixelStorei PixelStorei{nullptr};

    PfnCreateShader CreateShader{nullptr};
    PfnShaderSource ShaderSource{nullptr};
    PfnCompileShader CompileShader{nullptr};
    PfnGetShaderiv GetShaderiv{nullptr};
    PfnGetShaderInfoLog GetShaderInfoLog{nullptr};
    PfnDeleteShader DeleteShader{nullptr};

    PfnCreateProgram CreateProgram{nullptr};
    PfnAttachShader AttachShader{nullptr};
    PfnLinkProgram LinkProgram{nullptr};
    PfnGetProgramiv GetProgramiv{nullptr};
    PfnGetProgramInfoLog GetProgramInfoLog{nullptr};
    PfnUseProgram UseProgram{nullptr};
    PfnDeleteProgram DeleteProgram{nullptr};

    PfnGetUniformLocation GetUniformLocation{nullptr};
    PfnUniform1i Uniform1i{nullptr};
    PfnUniform1f Uniform1f{nullptr};
    PfnUniform2f Uniform2f{nullptr};
    PfnUniform3f Uniform3f{nullptr};
    PfnUniform4f Uniform4f{nullptr};
    PfnUniform4fv Uniform4fv{nullptr};

    PfnGenVertexArrays GenVertexArrays{nullptr};
    PfnBindVertexArray BindVertexArray{nullptr};
    PfnDeleteVertexArrays DeleteVertexArrays{nullptr};

    PfnGenBuffers GenBuffers{nullptr};
    PfnBindBuffer BindBuffer{nullptr};
    PfnBufferData BufferData{nullptr};
    PfnBufferSubData BufferSubData{nullptr};
    PfnDeleteBuffers DeleteBuffers{nullptr};

    PfnEnableVertexAttribArray EnableVertexAttribArray{nullptr};
    PfnVertexAttribPointer VertexAttribPointer{nullptr};
    PfnVertexAttribDivisor VertexAttribDivisor{nullptr};

    PfnDrawArrays DrawArrays{nullptr};
    PfnDrawArraysInstanced DrawArraysInstanced{nullptr};

    PfnGenTextures GenTextures{nullptr};
    PfnBindTexture BindTexture{nullptr};
    PfnActiveTexture ActiveTexture{nullptr};
    PfnTexParameteri TexParameteri{nullptr};
    PfnTexImage2D TexImage2D{nullptr};
    PfnTexSubImage2D TexSubImage2D{nullptr};
    PfnDeleteTextures DeleteTextures{nullptr};

    template <typename Fn>
    bool LoadFn(GlProcAddressFn procLoader, const char* name, Fn& out) noexcept {
        void* raw = procLoader ? procLoader(name) : nullptr;
        if (!raw && fallbackLib.is_open()) {
            raw = fallbackLib.symbol_raw(name);
        }
        if (!raw) {
            out = nullptr;
            return false;
        }
        keel::mem_copy(&out, &raw, sizeof(out));
        return true;
    }

    bool Load(GlProcAddressFn procLoader) noexcept {
        if (!procLoader) {
            return false;
        }
#if defined(_WIN32)
        if (auto lib = keel::DynLib::open("opengl32.dll")) {
            fallbackLib = keel::move(*lib);
        }
#elif defined(__linux__)
        if (auto lib = keel::DynLib::open("libGLESv2.so.2")) {
            fallbackLib = keel::move(*lib);
        } else if (auto glLib = keel::DynLib::open("libGL.so.1")) {
            fallbackLib = keel::move(*glLib);
        }
#endif
        bool ok = true;
        ok = LoadFn(procLoader, "glGetString", GetString) && ok;
        ok = LoadFn(procLoader, "glGetError", GetError) && ok;
        ok = LoadFn(procLoader, "glViewport", Viewport) && ok;
        ok = LoadFn(procLoader, "glScissor", Scissor) && ok;
        ok = LoadFn(procLoader, "glEnable", Enable) && ok;
        ok = LoadFn(procLoader, "glDisable", Disable) && ok;
        ok = LoadFn(procLoader, "glClearColor", ClearColor) && ok;
        ok = LoadFn(procLoader, "glClear", Clear) && ok;
        ok = LoadFn(procLoader, "glBlendFuncSeparate", BlendFuncSeparate) && ok;
        ok = LoadFn(procLoader, "glBlendColor", BlendColor) && ok;
        ok = LoadFn(procLoader, "glBlendEquation", BlendEquation) && ok;
        ok = LoadFn(procLoader, "glPixelStorei", PixelStorei) && ok;

        ok = LoadFn(procLoader, "glCreateShader", CreateShader) && ok;
        ok = LoadFn(procLoader, "glShaderSource", ShaderSource) && ok;
        ok = LoadFn(procLoader, "glCompileShader", CompileShader) && ok;
        ok = LoadFn(procLoader, "glGetShaderiv", GetShaderiv) && ok;
        ok = LoadFn(procLoader, "glGetShaderInfoLog", GetShaderInfoLog) && ok;
        ok = LoadFn(procLoader, "glDeleteShader", DeleteShader) && ok;

        ok = LoadFn(procLoader, "glCreateProgram", CreateProgram) && ok;
        ok = LoadFn(procLoader, "glAttachShader", AttachShader) && ok;
        ok = LoadFn(procLoader, "glLinkProgram", LinkProgram) && ok;
        ok = LoadFn(procLoader, "glGetProgramiv", GetProgramiv) && ok;
        ok = LoadFn(procLoader, "glGetProgramInfoLog", GetProgramInfoLog) && ok;
        ok = LoadFn(procLoader, "glUseProgram", UseProgram) && ok;
        ok = LoadFn(procLoader, "glDeleteProgram", DeleteProgram) && ok;

        ok = LoadFn(procLoader, "glGetUniformLocation", GetUniformLocation) && ok;
        ok = LoadFn(procLoader, "glUniform1i", Uniform1i) && ok;
        ok = LoadFn(procLoader, "glUniform1f", Uniform1f) && ok;
        ok = LoadFn(procLoader, "glUniform2f", Uniform2f) && ok;
        ok = LoadFn(procLoader, "glUniform3f", Uniform3f) && ok;
        ok = LoadFn(procLoader, "glUniform4f", Uniform4f) && ok;
        ok = LoadFn(procLoader, "glUniform4fv", Uniform4fv) && ok;

        ok = LoadFn(procLoader, "glGenVertexArrays", GenVertexArrays) && ok;
        ok = LoadFn(procLoader, "glBindVertexArray", BindVertexArray) && ok;
        ok = LoadFn(procLoader, "glDeleteVertexArrays", DeleteVertexArrays) && ok;

        ok = LoadFn(procLoader, "glGenBuffers", GenBuffers) && ok;
        ok = LoadFn(procLoader, "glBindBuffer", BindBuffer) && ok;
        ok = LoadFn(procLoader, "glBufferData", BufferData) && ok;
        ok = LoadFn(procLoader, "glBufferSubData", BufferSubData) && ok;
        ok = LoadFn(procLoader, "glDeleteBuffers", DeleteBuffers) && ok;

        ok = LoadFn(procLoader, "glEnableVertexAttribArray", EnableVertexAttribArray) && ok;
        ok = LoadFn(procLoader, "glVertexAttribPointer", VertexAttribPointer) && ok;
        ok = LoadFn(procLoader, "glVertexAttribDivisor", VertexAttribDivisor) && ok;

        ok = LoadFn(procLoader, "glDrawArrays", DrawArrays) && ok;
        ok = LoadFn(procLoader, "glDrawArraysInstanced", DrawArraysInstanced) && ok;

        ok = LoadFn(procLoader, "glGenTextures", GenTextures) && ok;
        ok = LoadFn(procLoader, "glBindTexture", BindTexture) && ok;
        ok = LoadFn(procLoader, "glActiveTexture", ActiveTexture) && ok;
        ok = LoadFn(procLoader, "glTexParameteri", TexParameteri) && ok;
        ok = LoadFn(procLoader, "glTexImage2D", TexImage2D) && ok;
        ok = LoadFn(procLoader, "glTexSubImage2D", TexSubImage2D) && ok;
        ok = LoadFn(procLoader, "glDeleteTextures", DeleteTextures) && ok;
        return ok;
    }
};

} // namespace guikit::native::detail
