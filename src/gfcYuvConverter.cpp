#include "gfcYuvConverter.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <map>
#include <string>

#include "gfcMovieReader.h"  // the gfcYuv* enums only; no libav here

namespace {

// Code-value maths shared by the shader (as uniforms) and the CPU path.
// Codes c = texel x codeScale; B = bit depth, M = 2^B - 1.
//   video range: y = (cY - 16*2^(B-8)) / (219*2^(B-8)), cb = (cCb - 128*2^(B-8)) / (224*2^(B-8))
//   full range:  y = cY / M,                          cb = (cCb - 2^(B-1)) / M
//   R = y + 2(1-Kr) cr;  G = y - 2(1-Kb)Kb/Kg cb - 2(1-Kr)Kr/Kg cr;  B = y + 2(1-Kb) cb
// Unspecified matrix => Rec.709, unspecified range => video.
struct YuvMaths {
    float codeScale = 255.0f;
    float yOff = 16.0f, yScale = 219.0f, cOff = 128.0f, cScale = 224.0f;
    float rCr = 0, gCb = 0, gCr = 0, bCb = 0;
    float alphaScale = 255.0f;
    float chromaScaleX = 1, chromaScaleY = 1, chromaOffX = 0, chromaOffY = 0;
};

YuvMaths mathsFor(const gfcGLFrameInfo& in) {
    YuvMaths m;
    const int bits = std::clamp(in.bitDepth, 8, 16);
    const double k = double(1 << (bits - 8));
    const double maxCode = double((1 << bits) - 1);
    m.codeScale = bits > 8 ? 65535.0f : 255.0f;
    if (in.range == int(gfcYuvRange::Full)) {
        m.yOff = 0.0f;
        m.yScale = float(maxCode);
        m.cOff = float(1 << (bits - 1));
        m.cScale = float(maxCode);
    } else {
        m.yOff = float(16.0 * k);
        m.yScale = float(219.0 * k);
        m.cOff = float(128.0 * k);
        m.cScale = float(224.0 * k);
    }
    double kr = 0.2126, kb = 0.0722;  // Rec.709, also the untagged default
    if (in.matrix == int(gfcYuvMatrix::BT601)) { kr = 0.299; kb = 0.114; }
    else if (in.matrix == int(gfcYuvMatrix::BT2020)) { kr = 0.2627; kb = 0.0593; }
    const double kg = 1.0 - kr - kb;
    m.rCr = float(2.0 * (1.0 - kr));
    m.bCb = float(2.0 * (1.0 - kb));
    m.gCb = float(2.0 * (1.0 - kb) * kb / kg);
    m.gCr = float(2.0 * (1.0 - kr) * kr / kg);
    m.alphaScale = float(maxCode);

    // Chroma coordinate for the luma texel centre s: s*r + offset, r = 1/sub.
    // Horizontally co-sited ((s-0.5)r+0.5) unless Center; vertically centred
    // (t*r) unless TopLeft. 4:4:4 reduces to s, t.
    const double rx = 1.0 / std::max(1, in.subX), ry = 1.0 / std::max(1, in.subY);
    m.chromaScaleX = float(rx);
    m.chromaScaleY = float(ry);
    m.chromaOffX = in.siting == int(gfcChromaSiting::Center) ? 0.0f : float(0.5 - 0.5 * rx);
    m.chromaOffY = in.siting == int(gfcChromaSiting::TopLeft) ? float(0.5 - 0.5 * ry) : 0.0f;
    return m;
}

bool planesUsable(const gfcGLFrameInfo& in) {
    if (!in.planarYuv || in.planeCount < 3 || in.planeCount > 4) return false;
    const int bps = in.bitDepth > 8 ? 2 : 1;
    for (int p = 0; p < in.planeCount; ++p) {
        const gfcGLFrameInfo::Plane& pl = in.planes[p];
        if (!pl.data || pl.width <= 0 || pl.height <= 0 || pl.strideBytes < pl.width * bps ||
            pl.strideBytes % bps != 0)
            return false;
    }
    return true;
}

void resolveSrcRect(const gfcGLFrameInfo& in, int& x, int& y, int& w, int& h) {
    const int pw = in.planes[0].width, ph = in.planes[0].height;
    if (w <= 0 || h <= 0) { x = 0; y = 0; w = pw; h = ph; }
    x = std::clamp(x, 0, pw - 1);
    y = std::clamp(y, 0, ph - 1);
    w = std::clamp(w, 1, pw - x);
    h = std::clamp(h, 1, ph - y);
}

// ---------------------------------------------------------------- GPU path

const char* kVertexSrc = "void main(){ gl_Position = gl_Vertex; }\n";

const char* kFragmentSrc =
    "#extension GL_ARB_texture_rectangle : require\n"
    "uniform sampler2DRect planeY;\n"
    "uniform sampler2DRect planeCb;\n"
    "uniform sampler2DRect planeCr;\n"
    "#ifdef HAS_ALPHA\n"
    "uniform sampler2DRect planeA;\n"
    "#endif\n"
    "uniform vec4 srcRect;\n"       // x, y, w, h in luma texels
    "uniform vec2 dstSize;\n"
    "uniform vec4 chromaMap;\n"     // scale x, scale y, offset x, offset y
    "uniform float codeScale;\n"
    "uniform vec4 levels;\n"        // yOff, yScale, cOff, cScale
    "uniform vec4 coeffs;\n"        // rCr, gCb, gCr, bCb
    "uniform float alphaScale;\n"
    "void main(){\n"
    "  vec2 st = srcRect.xy + gl_FragCoord.xy * (srcRect.zw / dstSize);\n"
    "  vec2 cst = st * chromaMap.xy + chromaMap.zw;\n"
    "  float y = (texture2DRect(planeY, st).r * codeScale - levels.x) / levels.y;\n"
    "  float cb = (texture2DRect(planeCb, cst).r * codeScale - levels.z) / levels.w;\n"
    "  float cr = (texture2DRect(planeCr, cst).r * codeScale - levels.z) / levels.w;\n"
    "  vec4 o;\n"
    "  o.r = y + coeffs.x * cr;\n"
    "  o.g = y - coeffs.y * cb - coeffs.z * cr;\n"
    "  o.b = y + coeffs.w * cb;\n"
    "#ifdef HAS_ALPHA\n"
    "  o.a = texture2DRect(planeA, st).r * codeScale / alphaScale;\n"
    "#else\n"
    "  o.a = 1.0;\n"
    "#endif\n"
    "  gl_FragColor = o;\n"  // no clamp: 8-bit storage clamps, 16F keeps headroom
    "}\n";

struct Program {
    GLhandleARB program = 0;
    GLint locSrcRect = -1, locDstSize = -1, locChromaMap = -1, locCodeScale = -1;
    GLint locLevels = -1, locCoeffs = -1, locAlphaScale = -1;
    bool ok = false;
};

struct PlaneTex {
    GLuint id = 0;
    int w = 0, h = 0;
    GLint internalFormat = 0;
};

std::map<int, Program> gPrograms;  // key: bitDepth, planeCount, hasAlpha
PlaneTex gPlaneTex[4];
GLuint gFbo = 0;

std::string infoLog(GLhandleARB obj) {
    GLint len = 0;
    glGetObjectParameterivARB(obj, GL_OBJECT_INFO_LOG_LENGTH_ARB, &len);
    if (len <= 1) return std::string();
    std::string log(size_t(len), '\0');
    GLsizei written = 0;
    glGetInfoLogARB(obj, len, &written, &log[0]);
    log.resize(size_t(std::max(0, int(written))));
    return log;
}

Program& programFor(const gfcGLFrameInfo& in) {
    const bool alpha = in.hasAlpha && in.planeCount == 4;
    const int key = (in.bitDepth << 8) | (in.planeCount << 1) | (alpha ? 1 : 0);
    auto it = gPrograms.find(key);
    if (it != gPrograms.end()) return it->second;
    Program& p = gPrograms[key];

    const std::string frag = std::string(alpha ? "#define HAS_ALPHA 1\n" : "") + kFragmentSrc;
    const char* fragSrc = frag.c_str();
    GLhandleARB vs = glCreateShaderObjectARB(GL_VERTEX_SHADER_ARB);
    glShaderSourceARB(vs, 1, &kVertexSrc, nullptr);
    glCompileShaderARB(vs);
    GLhandleARB fs = glCreateShaderObjectARB(GL_FRAGMENT_SHADER_ARB);
    glShaderSourceARB(fs, 1, &fragSrc, nullptr);
    glCompileShaderARB(fs);
    GLint vsOk = 0, fsOk = 0, linked = 0;
    glGetObjectParameterivARB(vs, GL_OBJECT_COMPILE_STATUS_ARB, &vsOk);
    glGetObjectParameterivARB(fs, GL_OBJECT_COMPILE_STATUS_ARB, &fsOk);
    if (!vsOk || !fsOk) {
        std::printf("gfcYuvConverter: shader compile failed:\n%s%s\n", infoLog(vs).c_str(), infoLog(fs).c_str());
        glDeleteObjectARB(vs);
        glDeleteObjectARB(fs);
        return p;
    }
    p.program = glCreateProgramObjectARB();
    glAttachObjectARB(p.program, vs);
    glAttachObjectARB(p.program, fs);
    glLinkProgramARB(p.program);
    glDeleteObjectARB(vs);  // flagged; freed with the program
    glDeleteObjectARB(fs);
    glGetObjectParameterivARB(p.program, GL_OBJECT_LINK_STATUS_ARB, &linked);
    if (!linked) {
        std::printf("gfcYuvConverter: program link failed:\n%s\n", infoLog(p.program).c_str());
        glDeleteObjectARB(p.program);
        p.program = 0;
        return p;
    }
    GLhandleARB prev = glGetHandleARB(GL_PROGRAM_OBJECT_ARB);
    glUseProgramObjectARB(p.program);
    glUniform1iARB(glGetUniformLocationARB(p.program, "planeY"), 0);
    glUniform1iARB(glGetUniformLocationARB(p.program, "planeCb"), 1);
    glUniform1iARB(glGetUniformLocationARB(p.program, "planeCr"), 2);
    if (alpha) glUniform1iARB(glGetUniformLocationARB(p.program, "planeA"), 3);
    glUseProgramObjectARB(prev);
    p.locSrcRect = glGetUniformLocationARB(p.program, "srcRect");
    p.locDstSize = glGetUniformLocationARB(p.program, "dstSize");
    p.locChromaMap = glGetUniformLocationARB(p.program, "chromaMap");
    p.locCodeScale = glGetUniformLocationARB(p.program, "codeScale");
    p.locLevels = glGetUniformLocationARB(p.program, "levels");
    p.locCoeffs = glGetUniformLocationARB(p.program, "coeffs");
    p.locAlphaScale = glGetUniformLocationARB(p.program, "alphaScale");
    p.ok = true;
    return p;
}

// Uploads one plane into its cached rectangle texture on the active unit.
// Single-channel GL_R8/GL_R16 (ARB_texture_rg) where available: native on the
// Metal GL layer and measurably faster to upload than GL_LUMINANCE16 (4K
// ProRes 4444: ~10.7 vs ~14.3 ms GPU time per frame); luminance otherwise.
// Both keep the full 16 bits.
void uploadPlane(int index, const gfcGLFrameInfo::Plane& pl, int bitDepth, GLint filter) {
    PlaneTex& t = gPlaneTex[index];
    const bool sixteen = bitDepth > 8;
    const bool rg = GLAD_GL_ARB_texture_rg || GLAD_GL_VERSION_3_0;
    const GLint internal = rg ? (sixteen ? GL_R16 : GL_R8) : (sixteen ? GL_LUMINANCE16 : GL_LUMINANCE8);
    const GLenum format = rg ? GL_RED : GL_LUMINANCE;
    const GLenum type = sixteen ? GL_UNSIGNED_SHORT : GL_UNSIGNED_BYTE;
    if (!t.id) glGenTextures(1, &t.id);
    glBindTexture(GL_TEXTURE_RECTANGLE_ARB, t.id);
    glTexParameteri(GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MAG_FILTER, filter);
    glTexParameteri(GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MIN_FILTER, filter);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, pl.strideBytes / (sixteen ? 2 : 1));
    if (t.w == pl.width && t.h == pl.height && t.internalFormat == internal) {
        glTexSubImage2D(GL_TEXTURE_RECTANGLE_ARB, 0, 0, 0, pl.width, pl.height, format, type, pl.data);
    } else {
        glTexImage2D(GL_TEXTURE_RECTANGLE_ARB, 0, internal, pl.width, pl.height, 0, format, type, pl.data);
        t.w = pl.width;
        t.h = pl.height;
        t.internalFormat = internal;
    }
}

// ---------------------------------------------------------------- CPU path

inline float code(const gfcGLFrameInfo::Plane& pl, bool sixteen, int x, int y) {
    x = std::clamp(x, 0, pl.width - 1);
    y = std::clamp(y, 0, pl.height - 1);
    const uint8_t* row = static_cast<const uint8_t*>(pl.data) + size_t(y) * size_t(pl.strideBytes);
    return sixteen ? float(reinterpret_cast<const uint16_t*>(row)[x]) : float(row[x]);
}

// Bilinear at a rectangle-texture coordinate (texel centres at i + 0.5),
// clamped to the edge like GL_CLAMP_TO_EDGE + GL_LINEAR.
inline float bilinear(const gfcGLFrameInfo::Plane& pl, bool sixteen, float s, float t) {
    const float fs = s - 0.5f, ft = t - 0.5f;
    const int x0 = int(std::floor(fs)), y0 = int(std::floor(ft));
    const float ax = fs - float(x0), ay = ft - float(y0);
    const float top = code(pl, sixteen, x0, y0) * (1 - ax) + code(pl, sixteen, x0 + 1, y0) * ax;
    const float bot = code(pl, sixteen, x0, y0 + 1) * (1 - ax) + code(pl, sixteen, x0 + 1, y0 + 1) * ax;
    return top * (1 - ay) + bot * ay;
}

inline unsigned char to8(float v) {
    return (unsigned char)std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f);
}

}  // namespace

GLuint gfcYuvConvertToTexture(const gfcGLFrameInfo& info, int srcX, int srcY, int srcW, int srcH,
                              int dstW, int dstH, GLuint internalFormat) {
    if (!planesUsable(info) || dstW <= 0 || dstH <= 0) return 0;
    resolveSrcRect(info, srcX, srcY, srcW, srcH);
    while (glGetError() != GL_NO_ERROR) {}  // clear stale errors

    Program& prog = programFor(info);
    if (!prog.ok) return 0;
    const YuvMaths m = mathsFor(info);
    const bool alpha = info.hasAlpha && info.planeCount == 4;

    // The viewport renders into QOpenGLWidget's own framebuffer (not 0), so
    // the binding is saved and restored rather than assumed (developer_notes §24).
    GLint prevFbo = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING_EXT, &prevFbo);
    GLhandleARB prevProgram = glGetHandleARB(GL_PROGRAM_OBJECT_ARB);
    glPushAttrib(GL_ALL_ATTRIB_BITS);
    glPushClientAttrib(GL_CLIENT_PIXEL_STORE_BIT);

    // Planes: luma (and alpha) nearest at 1:1, bilinear when scaling; chroma
    // always bilinear (the upsampling filter).
    const GLint lumaFilter = (srcW == dstW && srcH == dstH) ? GL_NEAREST : GL_LINEAR;
    for (int p = 0; p < info.planeCount && p < (alpha ? 4 : 3); ++p) {
        glActiveTexture(GL_TEXTURE0 + p);
        uploadPlane(p, info.planes[p], info.bitDepth, (p == 1 || p == 2) ? GL_LINEAR : lumaFilter);
    }

    GLuint out = 0;
    glActiveTexture(GL_TEXTURE0 + 4);
    glGenTextures(1, &out);
    glBindTexture(GL_TEXTURE_RECTANGLE_ARB, out);
    glTexParameteri(GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_S, GL_CLAMP);
    glTexParameteri(GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_T, GL_CLAMP);
    glTexParameteri(GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_RECTANGLE_ARB, 0, GLint(internalFormat), dstW, dstH, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glBindTexture(GL_TEXTURE_RECTANGLE_ARB, 0);

    if (!gFbo) glGenFramebuffersEXT(1, &gFbo);
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, gFbo);
    glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT, GL_TEXTURE_RECTANGLE_ARB, out, 0);
    const GLenum status = glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT);
    bool ok = status == GL_FRAMEBUFFER_COMPLETE_EXT;
    if (ok) {
        glDisable(GL_BLEND);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_SCISSOR_TEST);
        glDisable(GL_ALPHA_TEST);
        glDisable(GL_STENCIL_TEST);
        glDisable(GL_CULL_FACE);
        glDisable(GL_DITHER);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glViewport(0, 0, dstW, dstH);

        glUseProgramObjectARB(prog.program);
        glUniform4fARB(prog.locSrcRect, float(srcX), float(srcY), float(srcW), float(srcH));
        glUniform2fARB(prog.locDstSize, float(dstW), float(dstH));
        glUniform4fARB(prog.locChromaMap, m.chromaScaleX, m.chromaScaleY, m.chromaOffX, m.chromaOffY);
        glUniform1fARB(prog.locCodeScale, m.codeScale);
        glUniform4fARB(prog.locLevels, m.yOff, m.yScale, m.cOff, m.cScale);
        glUniform4fARB(prog.locCoeffs, m.rCr, m.gCb, m.gCr, m.bCb);
        if (prog.locAlphaScale >= 0) glUniform1fARB(prog.locAlphaScale, m.alphaScale);

        // Clip-space quad (the vertex shader ignores the matrices).
        glBegin(GL_QUADS);
        glVertex2f(-1.0f, -1.0f);
        glVertex2f(1.0f, -1.0f);
        glVertex2f(1.0f, 1.0f);
        glVertex2f(-1.0f, 1.0f);
        glEnd();
        glUseProgramObjectARB(prevProgram);
    } else {
        std::printf("gfcYuvConverter: framebuffer incomplete (0x%x) for %dx%d\n", status, dstW, dstH);
    }
    glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT, GL_TEXTURE_RECTANGLE_ARB, 0, 0);
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, GLuint(prevFbo));
    glPopClientAttrib();
    glPopAttrib();

    const GLenum err = glGetError();
    if (err != GL_NO_ERROR) {
        std::printf("gfcYuvConverter: GL error 0x%x during conversion\n", err);
        ok = false;
    }
    if (!ok) {
        glDeleteTextures(1, &out);
        return 0;
    }
    return out;
}

bool gfcYuvConvertToRGBA8(const gfcGLFrameInfo& info, int dstW, int dstH, std::vector<unsigned char>& rgba) {
    if (!planesUsable(info) || dstW <= 0 || dstH <= 0) return false;
    int sx0 = info.srcX, sy0 = info.srcY, sw = info.srcW, sh = info.srcH;
    resolveSrcRect(info, sx0, sy0, sw, sh);
    const YuvMaths m = mathsFor(info);
    const bool sixteen = info.bitDepth > 8;
    const bool alpha = info.hasAlpha && info.planeCount == 4;
    rgba.assign(size_t(dstW) * size_t(dstH) * 4, 0);
    for (int y = 0; y < dstH; ++y) {
        const int sy = sy0 + int((long long)y * sh / dstH);
        const float t = float(sy) + 0.5f;
        for (int x = 0; x < dstW; ++x) {
            const int sx = sx0 + int((long long)x * sw / dstW);
            const float s = float(sx) + 0.5f;
            const float cs = s * m.chromaScaleX + m.chromaOffX;
            const float ct = t * m.chromaScaleY + m.chromaOffY;
            const float yv = (code(info.planes[0], sixteen, sx, sy) - m.yOff) / m.yScale;
            const float cb = (bilinear(info.planes[1], sixteen, cs, ct) - m.cOff) / m.cScale;
            const float cr = (bilinear(info.planes[2], sixteen, cs, ct) - m.cOff) / m.cScale;
            unsigned char* o = &rgba[(size_t(y) * dstW + x) * 4];
            o[0] = to8(yv + m.rCr * cr);
            o[1] = to8(yv - m.gCb * cb - m.gCr * cr);
            o[2] = to8(yv + m.bCb * cb);
            o[3] = alpha ? to8(code(info.planes[3], sixteen, sx, sy) / m.alphaScale) : 255;
        }
    }
    return true;
}

void gfcYuvConverterRelease() {
    for (auto& kv : gPrograms)
        if (kv.second.program) glDeleteObjectARB(kv.second.program);
    gPrograms.clear();
    for (PlaneTex& t : gPlaneTex) {
        if (t.id) glDeleteTextures(1, &t.id);
        t = PlaneTex();
    }
    if (gFbo) glDeleteFramebuffersEXT(1, &gFbo);
    gFbo = 0;
}
