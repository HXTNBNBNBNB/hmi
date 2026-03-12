#include "ui/TextRenderer.hpp"
#include <cstdio>
#include <cstring>
#include <fstream>
#include <vector>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#define STB_TRUETYPE_IMPLEMENTATION
#include "third_party/stb_truetype.h"

static const char* textVertSrc = R"(
attribute vec2 aPos;
attribute vec2 aTexCoord;
uniform mat4 uProjection;
uniform vec2 uOffset;
varying vec2 vTexCoord;
void main() {
    gl_Position = uProjection * vec4(aPos + uOffset, 0.0, 1.0);
    vTexCoord = aTexCoord;
}
)";

static const char* textFragSrc = R"(
precision mediump float;
varying vec2 vTexCoord;
uniform sampler2D uTexture;
uniform vec3 uTextColor;
uniform vec2 uTexelSize;
uniform vec3 uOutlineColor;
uniform float uOutlineEnabled;
void main() {
    float alpha = texture2D(uTexture, vTexCoord).r;
    float outline = 0.0;
    if (uOutlineEnabled > 0.5) {
        float a1 = texture2D(uTexture, vTexCoord + vec2( uTexelSize.x, 0.0)).r;
        float a2 = texture2D(uTexture, vTexCoord + vec2(-uTexelSize.x, 0.0)).r;
        float a3 = texture2D(uTexture, vTexCoord + vec2(0.0,  uTexelSize.y)).r;
        float a4 = texture2D(uTexture, vTexCoord + vec2(0.0, -uTexelSize.y)).r;
        float a5 = texture2D(uTexture, vTexCoord + vec2( uTexelSize.x,  uTexelSize.y)).r;
        float a6 = texture2D(uTexture, vTexCoord + vec2(-uTexelSize.x,  uTexelSize.y)).r;
        float a7 = texture2D(uTexture, vTexCoord + vec2( uTexelSize.x, -uTexelSize.y)).r;
        float a8 = texture2D(uTexture, vTexCoord + vec2(-uTexelSize.x, -uTexelSize.y)).r;
        outline = max(max(max(a1,a2), max(a3,a4)), max(max(a5,a6), max(a7,a8)));
    }
    vec3 color = uTextColor;
    float outAlpha = alpha;
    if (uOutlineEnabled > 0.5 && alpha > 0.02 && alpha < 0.45 && outline > 0.08) {
        color = uOutlineColor;
        outAlpha = outline * 0.48;
    } else {
        outAlpha = alpha;
    }
    gl_FragColor = vec4(color, outAlpha);
}
)";

static const char* rectVertSrc = R"(
attribute vec2 aPos;
uniform mat4 uProjection;
void main() {
    gl_Position = uProjection * vec4(aPos, 0.0, 1.0);
}
)";

static const char* rectFragSrc = R"(
precision mediump float;
uniform vec4 uColor;
void main() {
    gl_FragColor = uColor;
}
)";

// 常用中文字符集（警告信息中可能出现的）
static const char* commonChineseChars =
    "的一是不了在人有我他这个们中来上大为和国地到以说时要就出会可也你对生能而子那得于着下自之年过发后作里用道行所然家种事"
    "成方多经么去法学如都同现当没动面起看定天分还进好小部其些主样理心她本前开但因只从想实日军者意无力它与长把机十民第公此"
    "已工使情明性知全三又关点正业外将两高间由问很最重并物手应战向头文体政美相见被利什二等产或新己制身果加西斯月话合回特代"
    "内信表化老给世位次度门任常先海通教儿原东声提立及比员解水名真论处走义各入几口认条平系气题活尔更别打女变四神总何电数安"
    "少报才结反受目太量再感建务做接必场件计管期市直德资命山金指克许统区保至队形社便空决治展马科司五基眼书非则听白却界达光"
    "放强即像难且权思王象完设式色路记南品住告类求据程北边死张该交规万取拉格望觉术领共确传师观清今切院让识候带导争运笑飞风"
    "步改收根干造言联持组每济车亲极林服快办议往元英士证近失转夫令准布始怎呢存未远叫台单影具罗字爱击流备兵连调深商算质团集"
    "百需价花党华城石级整府离况亚请技际约示复病息究线似官火断精满支视消越器容照须九增研写称企八功吗包片史委乎查轻易早曾除"
    "农找装广显吧阿李标谈吃图念六引历首医局突专费号尽另周较注语仅考落青随选列武红响虽推势参希古众构房半节土投某案黑维革划"
    "敌致陈律足态护七兴派孩验责营星够章音跟志底站严巴例防族供效续施留讲型料终答紧黄绝奇察母京段依批群项故按河米围江织害斗"
    "双境客纪采举杀攻父母苏木毫客纪采举杀攻父母苏木毫"
    // 警告相关
    "警告危险注意前后左右方障碍物米距离速限超减慢停止";

TextRenderer::TextRenderer() {}

TextRenderer::~TextRenderer() {
    cleanup();
}

void TextRenderer::cleanup() {
    if (warningTexture_) {
        glDeleteTextures(1, &warningTexture_);
        warningTexture_ = 0;
    }
    warningCharacters_.clear();
    if (rectVbo_) {
        glDeleteBuffers(1, &rectVbo_);
        rectVbo_ = 0;
    }
    if (rectProgram_) {
        glDeleteProgram(rectProgram_);
        rectProgram_ = 0;
    }
    if (fontTexture_) {
        glDeleteTextures(1, &fontTexture_);
        fontTexture_ = 0;
    }
    if (vbo_) {
        glDeleteBuffers(1, &vbo_);
        vbo_ = 0;
    }
    if (program_) {
        glDeleteProgram(program_);
        program_ = 0;
    }
    characters_.clear();
}

GLuint TextRenderer::compileShader(GLenum type, const char* source) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);

    GLint success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char log[512];
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        fprintf(stderr, "TextRenderer shader compile error: %s\n", log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

GLuint TextRenderer::createProgram(const char* vertSrc, const char* fragSrc) {
    GLuint vert = compileShader(GL_VERTEX_SHADER, vertSrc);
    GLuint frag = compileShader(GL_FRAGMENT_SHADER, fragSrc);
    if (!vert || !frag) return 0;

    GLuint program = glCreateProgram();
    glAttachShader(program, vert);
    glAttachShader(program, frag);
    glLinkProgram(program);

    GLint success;
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (!success) {
        char log[512];
        glGetProgramInfoLog(program, sizeof(log), nullptr, log);
        fprintf(stderr, "TextRenderer program link error: %s\n", log);
        glDeleteProgram(program);
        program = 0;
    }

    glDeleteShader(vert);
    glDeleteShader(frag);
    return program;
}

// UTF-8 解码
uint32_t TextRenderer::decodeUTF8(const char*& ptr, const char* end) {
    if (ptr >= end) return 0;

    unsigned char c = static_cast<unsigned char>(*ptr);
    uint32_t codepoint = 0;

    if ((c & 0x80) == 0) {
        // ASCII
        codepoint = c;
        ptr++;
    } else if ((c & 0xE0) == 0xC0) {
        // 2-byte sequence
        if (ptr + 1 >= end) { ptr++; return 0; }
        codepoint = (c & 0x1F) << 6;
        codepoint |= (static_cast<unsigned char>(ptr[1]) & 0x3F);
        ptr += 2;
    } else if ((c & 0xF0) == 0xE0) {
        // 3-byte sequence (most Chinese characters)
        if (ptr + 2 >= end) { ptr++; return 0; }
        codepoint = (c & 0x0F) << 12;
        codepoint |= (static_cast<unsigned char>(ptr[1]) & 0x3F) << 6;
        codepoint |= (static_cast<unsigned char>(ptr[2]) & 0x3F);
        ptr += 3;
    } else if ((c & 0xF8) == 0xF0) {
        // 4-byte sequence
        if (ptr + 3 >= end) { ptr++; return 0; }
        codepoint = (c & 0x07) << 18;
        codepoint |= (static_cast<unsigned char>(ptr[1]) & 0x3F) << 12;
        codepoint |= (static_cast<unsigned char>(ptr[2]) & 0x3F) << 6;
        codepoint |= (static_cast<unsigned char>(ptr[3]) & 0x3F);
        ptr += 4;
    } else {
        ptr++;
        return 0;
    }

    return codepoint;
}

bool TextRenderer::init(const std::string& fontPath, int fontSize, const std::string& fallbackFontPath,
                        const std::string& warningFontPath) {
    fontSize_ = fontSize;

    program_ = createProgram(textVertSrc, textFragSrc);
    if (!program_) return false;

    aPos_ = glGetAttribLocation(program_, "aPos");
    aTexCoord_ = glGetAttribLocation(program_, "aTexCoord");
    uProjection_ = glGetUniformLocation(program_, "uProjection");
    uTextColor_ = glGetUniformLocation(program_, "uTextColor");
    uTexture_ = glGetUniformLocation(program_, "uTexture");
    uOffset_ = glGetUniformLocation(program_, "uOffset");
    uTexelSize_ = glGetUniformLocation(program_, "uTexelSize");
    uOutlineColor_ = glGetUniformLocation(program_, "uOutlineColor");
    uOutlineEnabled_ = glGetUniformLocation(program_, "uOutlineEnabled");

    rectProgram_ = createProgram(rectVertSrc, rectFragSrc);
    if (rectProgram_) {
        rectAPos_ = glGetAttribLocation(rectProgram_, "aPos");
        rectUProjection_ = glGetUniformLocation(rectProgram_, "uProjection");
        rectUColor_ = glGetUniformLocation(rectProgram_, "uColor");
        glGenBuffers(1, &rectVbo_);
    }

    glGenBuffers(1, &vbo_);

    if (!loadFont(fontPath, fallbackFontPath)) return false;
    if (!warningFontPath.empty()) {
        loadWarningFont(warningFontPath);
    }
    return true;
}

bool TextRenderer::loadFont(const std::string& fontPath, const std::string& fallbackFontPath) {
    // --- 主字体 ---
    std::ifstream file(fontPath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        fprintf(stderr, "Failed to open font file: %s\n", fontPath.c_str());
        return false;
    }
    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);
    std::vector<unsigned char> fontBuffer(size);
    if (!file.read(reinterpret_cast<char*>(fontBuffer.data()), size)) {
        fprintf(stderr, "Failed to read font file: %s\n", fontPath.c_str());
        return false;
    }
    file.close();

    stbtt_fontinfo font;
    int fontOffset = stbtt_GetFontOffsetForIndex(fontBuffer.data(), 0);
    if (fontOffset < 0) fontOffset = 0;
    if (!stbtt_InitFont(&font, fontBuffer.data(), fontOffset)) {
        fprintf(stderr, "Failed to init font\n");
        return false;
    }
    float scale = stbtt_ScaleForPixelHeight(&font, static_cast<float>(fontSize_));

    // --- 备用字体（可选）---
    std::vector<unsigned char> fallbackBuffer;
    stbtt_fontinfo fallbackFont;
    float fallbackScale = 0.0f;
    bool hasFallback = false;
    if (!fallbackFontPath.empty()) {
        std::ifstream ff(fallbackFontPath, std::ios::binary | std::ios::ate);
        if (ff.is_open()) {
            std::streamsize fsize = ff.tellg();
            ff.seekg(0, std::ios::beg);
            fallbackBuffer.resize(fsize);
            if (ff.read(reinterpret_cast<char*>(fallbackBuffer.data()), fsize)) {
                int foffset = stbtt_GetFontOffsetForIndex(fallbackBuffer.data(), 0);
                if (foffset < 0) foffset = 0;
                if (stbtt_InitFont(&fallbackFont, fallbackBuffer.data(), foffset)) {
                    fallbackScale = stbtt_ScaleForPixelHeight(&fallbackFont, static_cast<float>(fontSize_));
                    hasFallback = true;
                    printf("TextRenderer: Fallback font loaded: %s\n", fallbackFontPath.c_str());
                }
            }
        }
    }

    // --- 收集所有要加载的字符 (ASCII + 常用中文) ---
    std::vector<uint32_t> charsToLoad;
    for (int c = 32; c < 127; ++c) charsToLoad.push_back(c);
    const char* ptr = commonChineseChars;
    const char* end = ptr + strlen(commonChineseChars);
    while (ptr < end) {
        uint32_t cp = decodeUTF8(ptr, end);
        if (cp > 127) charsToLoad.push_back(cp);
    }

    // --- 构建字体 Atlas ---
    atlasWidth_ = 2048;
    atlasHeight_ = 2048;
    std::vector<unsigned char> atlasData(atlasWidth_ * atlasHeight_, 0);
    int x = 0, y = 0, rowHeight = 0, loadedCount = 0;

    for (uint32_t codepoint : charsToLoad) {
        int w = 0, h = 0, xoff = 0, yoff = 0;
        unsigned char* bitmap = nullptr;
        stbtt_fontinfo* usedFont = &font;
        float usedScale = scale;

        // 尝试主字体
        if (stbtt_FindGlyphIndex(&font, codepoint) != 0) {
            bitmap = stbtt_GetCodepointBitmap(&font, 0, scale, codepoint, &w, &h, &xoff, &yoff);
        }
        // 主字体无此字形时尝试备用字体
        if (!bitmap && hasFallback && stbtt_FindGlyphIndex(&fallbackFont, codepoint) != 0) {
            bitmap = stbtt_GetCodepointBitmap(&fallbackFont, 0, fallbackScale, codepoint, &w, &h, &xoff, &yoff);
            usedFont = &fallbackFont;
            usedScale = fallbackScale;
        }

        if (!bitmap) continue; // 两个字体都没有此字形，跳过

        if (w == 0 || h == 0) {
            // 空格等不可见字符
            int advance, lsb;
            stbtt_GetCodepointHMetrics(usedFont, codepoint, &advance, &lsb);
            CharacterInfo ci;
            ci.ax = advance * usedScale;
            ci.ay = 0; ci.bw = 0; ci.bh = 0; ci.bl = 0; ci.bt = 0; ci.tx = 0; ci.ty = 0;
            characters_[codepoint] = ci;
            stbtt_FreeBitmap(bitmap, nullptr);
            loadedCount++;
            continue;
        }

        if (x + w >= atlasWidth_) { x = 0; y += rowHeight + 1; rowHeight = 0; }
        if (y + h >= atlasHeight_) {
            fprintf(stderr, "Font atlas full at %d characters\n", loadedCount);
            stbtt_FreeBitmap(bitmap, nullptr);
            break;
        }

        for (int row = 0; row < h; ++row)
            memcpy(&atlasData[(y + row) * atlasWidth_ + x], &bitmap[row * w], w);

        int advance, lsb;
        stbtt_GetCodepointHMetrics(usedFont, codepoint, &advance, &lsb);
        CharacterInfo ci;
        ci.ax = advance * usedScale;
        ci.ay = 0;
        ci.bw = static_cast<float>(w);
        ci.bh = static_cast<float>(h);
        ci.bl = static_cast<float>(xoff);
        ci.bt = static_cast<float>(-yoff);
        ci.tx = static_cast<float>(x) / atlasWidth_;
        ci.ty = static_cast<float>(y) / atlasHeight_;
        characters_[codepoint] = ci;

        x += w + 1;
        if (h > rowHeight) rowHeight = h;
        stbtt_FreeBitmap(bitmap, nullptr);
        loadedCount++;
    }

    // 创建 OpenGL 纹理
    glGenTextures(1, &fontTexture_);
    glBindTexture(GL_TEXTURE_2D, fontTexture_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, atlasWidth_, atlasHeight_, 0,
                 GL_LUMINANCE, GL_UNSIGNED_BYTE, atlasData.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glBindTexture(GL_TEXTURE_2D, 0);

    printf("TextRenderer: Font loaded, %d characters, atlas %dx%d\n", loadedCount, atlasWidth_, atlasHeight_);
    return true;
}

bool TextRenderer::loadWarningFont(const std::string& fontPath) {
    std::ifstream file(fontPath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        fprintf(stderr, "TextRenderer: Failed to open warning font: %s\n", fontPath.c_str());
        return false;
    }
    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);
    std::vector<unsigned char> fontBuffer(size);
    if (!file.read(reinterpret_cast<char*>(fontBuffer.data()), size)) {
        fprintf(stderr, "TextRenderer: Failed to read warning font: %s\n", fontPath.c_str());
        return false;
    }
    file.close();

    stbtt_fontinfo font;
    int fontOffset = stbtt_GetFontOffsetForIndex(fontBuffer.data(), 0);
    if (fontOffset < 0) fontOffset = 0;
    if (!stbtt_InitFont(&font, fontBuffer.data(), fontOffset)) {
        fprintf(stderr, "TextRenderer: Failed to init warning font\n");
        return false;
    }
    float scale = stbtt_ScaleForPixelHeight(&font, static_cast<float>(fontSize_));

    std::vector<uint32_t> charsToLoad;
    for (int c = 32; c < 127; ++c) charsToLoad.push_back(c);
    const char* ptr = commonChineseChars;
    const char* end = ptr + strlen(commonChineseChars);
    while (ptr < end) {
        uint32_t cp = decodeUTF8(ptr, end);
        if (cp > 127) charsToLoad.push_back(cp);
    }

    warningAtlasWidth_ = 2048;
    warningAtlasHeight_ = 2048;
    std::vector<unsigned char> atlasData(warningAtlasWidth_ * warningAtlasHeight_, 0);
    int x = 0, y = 0, rowHeight = 0, loadedCount = 0;

    for (uint32_t codepoint : charsToLoad) {
        int w = 0, h = 0, xoff = 0, yoff = 0;
        unsigned char* bitmap = stbtt_GetCodepointBitmap(&font, 0, scale, codepoint, &w, &h, &xoff, &yoff);
        if (!bitmap) continue;

        if (w == 0 || h == 0) {
            int advance, lsb;
            stbtt_GetCodepointHMetrics(&font, codepoint, &advance, &lsb);
            CharacterInfo ci;
            ci.ax = advance * scale;
            ci.ay = 0; ci.bw = 0; ci.bh = 0; ci.bl = 0; ci.bt = 0; ci.tx = 0; ci.ty = 0;
            warningCharacters_[codepoint] = ci;
            stbtt_FreeBitmap(bitmap, nullptr);
            loadedCount++;
            continue;
        }

        if (x + w >= warningAtlasWidth_) { x = 0; y += rowHeight + 1; rowHeight = 0; }
        if (y + h >= warningAtlasHeight_) {
            stbtt_FreeBitmap(bitmap, nullptr);
            break;
        }

        for (int row = 0; row < h; ++row)
            memcpy(&atlasData[(y + row) * warningAtlasWidth_ + x], &bitmap[row * w], w);

        int advance, lsb;
        stbtt_GetCodepointHMetrics(&font, codepoint, &advance, &lsb);
        CharacterInfo ci;
        ci.ax = advance * scale;
        ci.ay = 0;
        ci.bw = static_cast<float>(w);
        ci.bh = static_cast<float>(h);
        ci.bl = static_cast<float>(xoff);
        ci.bt = static_cast<float>(-yoff);
        ci.tx = static_cast<float>(x) / warningAtlasWidth_;
        ci.ty = static_cast<float>(y) / warningAtlasHeight_;
        warningCharacters_[codepoint] = ci;

        x += w + 1;
        if (h > rowHeight) rowHeight = h;
        stbtt_FreeBitmap(bitmap, nullptr);
        loadedCount++;
    }

    glGenTextures(1, &warningTexture_);
    glBindTexture(GL_TEXTURE_2D, warningTexture_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, warningAtlasWidth_, warningAtlasHeight_, 0,
                 GL_LUMINANCE, GL_UNSIGNED_BYTE, atlasData.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glBindTexture(GL_TEXTURE_2D, 0);

    printf("TextRenderer: Warning font loaded, %d characters\n", loadedCount);
    return true;
}

void TextRenderer::setScreenSize(int width, int height) {
    screenWidth_ = width;
    screenHeight_ = height;
}

float TextRenderer::getTextWidth(const std::string& text, float scale, bool useWarningFont) {
    const auto& chars = useWarningFont && !warningCharacters_.empty() ? warningCharacters_ : characters_;
    float width = 0;
    const char* ptr = text.c_str();
    const char* end = ptr + text.size();

    while (ptr < end) {
        uint32_t cp = decodeUTF8(ptr, end);
        auto it = chars.find(cp);
        if (it != chars.end()) {
            width += it->second.ax * scale;
        }
    }
    return width;
}

void TextRenderer::renderText(const std::string& text, float x, float y, float scale,
                               float r, float g, float b, bool centered,
                               bool useOutline, bool useShadow, bool useWarningFont) {
    bool useWarnFont = useWarningFont && warningTexture_ != 0 && !warningCharacters_.empty();
    GLuint tex = useWarnFont ? warningTexture_ : fontTexture_;
    const auto& chars = useWarnFont ? warningCharacters_ : characters_;
    int texW = useWarnFont ? warningAtlasWidth_ : atlasWidth_;
    int texH = useWarnFont ? warningAtlasHeight_ : atlasHeight_;

    if (!program_ || !tex || text.empty()) return;

    glUseProgram(program_);

    glm::mat4 projection = glm::ortho(0.0f, static_cast<float>(screenWidth_),
                                       static_cast<float>(screenHeight_), 0.0f);
    glUniformMatrix4fv(uProjection_, 1, GL_FALSE, glm::value_ptr(projection));

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex);
    glUniform1i(uTexture_, 0);

    if (uTexelSize_ >= 0) {
        glUniform2f(uTexelSize_, 1.0f / texW, 1.0f / texH);
    }
    if (uOutlineColor_ >= 0) {
        glUniform3f(uOutlineColor_, 0.12f, 0.12f, 0.14f);
    }
    if (uOutlineEnabled_ >= 0) {
        glUniform1f(uOutlineEnabled_, useOutline ? 1.0f : 0.0f);
    }

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);

    if (centered) {
        float textWidth = getTextWidth(text, scale, useWarnFont);
        x = x - textWidth / 2.0f;
    }

    std::vector<float> vertices;
    vertices.reserve(text.size() * 6 * 4);

    float cursorX = x;
    const char* ptr = text.c_str();
    const char* end = ptr + text.size();

    while (ptr < end) {
        uint32_t cp = decodeUTF8(ptr, end);
        auto it = chars.find(cp);
        if (it == chars.end()) continue;

        const CharacterInfo& ch = it->second;

        if (ch.bw > 0 && ch.bh > 0) {
            float xpos = cursorX + ch.bl * scale;
            float ypos = y - ch.bt * scale + fontSize_ * scale;

            float w = ch.bw * scale;
            float h = ch.bh * scale;

            float tx = ch.tx;
            float ty = ch.ty;
            float tw = ch.bw / static_cast<float>(texW);
            float th = ch.bh / static_cast<float>(texH);

            vertices.push_back(xpos);     vertices.push_back(ypos);      vertices.push_back(tx);      vertices.push_back(ty);
            vertices.push_back(xpos + w); vertices.push_back(ypos);      vertices.push_back(tx + tw); vertices.push_back(ty);
            vertices.push_back(xpos);     vertices.push_back(ypos + h);  vertices.push_back(tx);      vertices.push_back(ty + th);

            vertices.push_back(xpos + w); vertices.push_back(ypos);      vertices.push_back(tx + tw); vertices.push_back(ty);
            vertices.push_back(xpos + w); vertices.push_back(ypos + h);  vertices.push_back(tx + tw); vertices.push_back(ty + th);
            vertices.push_back(xpos);     vertices.push_back(ypos + h);  vertices.push_back(tx);      vertices.push_back(ty + th);
        }

        cursorX += ch.ax * scale;
    }

    if (vertices.empty()) {
        glEnable(GL_DEPTH_TEST);
        glDisable(GL_BLEND);
        return;
    }

    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float), vertices.data(), GL_DYNAMIC_DRAW);

    glEnableVertexAttribArray(aPos_);
    glVertexAttribPointer(aPos_, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(aTexCoord_);
    glVertexAttribPointer(aTexCoord_, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));

    if (uOffset_ >= 0) {
        glUniform2f(uOffset_, 0.0f, 0.0f);
    }

    if (useShadow) {
        const float shadowOffset = 2.0f;
        const float shadowR = 0.12f, shadowG = 0.12f, shadowB = 0.14f;
        if (uOffset_ >= 0) glUniform2f(uOffset_, shadowOffset, shadowOffset);
        glUniform3f(uTextColor_, shadowR, shadowG, shadowB);
        if (uOutlineEnabled_ >= 0) glUniform1f(uOutlineEnabled_, 0.0f);
        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertices.size() / 4));
    }

    if (uOffset_ >= 0) glUniform2f(uOffset_, 0.0f, 0.0f);
    glUniform3f(uTextColor_, r, g, b);
    if (uOutlineEnabled_ >= 0) glUniform1f(uOutlineEnabled_, useOutline ? 1.0f : 0.0f);
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertices.size() / 4));

    glDisableVertexAttribArray(aPos_);
    glDisableVertexAttribArray(aTexCoord_);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);

    glEnable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
}

void TextRenderer::renderRect(float x, float y, float w, float h, float r, float g, float b, float a) {
    if (!rectProgram_ || rectVbo_ == 0) return;

    glUseProgram(rectProgram_);

    glm::mat4 projection = glm::ortho(0.0f, static_cast<float>(screenWidth_),
                                       static_cast<float>(screenHeight_), 0.0f);
    glUniformMatrix4fv(rectUProjection_, 1, GL_FALSE, glm::value_ptr(projection));
    glUniform4f(rectUColor_, r, g, b, a);

    float verts[] = {
        x,     y,
        x + w, y,
        x,     y + h,
        x + w, y,
        x + w, y + h,
        x,     y + h,
    };

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);

    glBindBuffer(GL_ARRAY_BUFFER, rectVbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(rectAPos_);
    glVertexAttribPointer(rectAPos_, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glDisableVertexAttribArray(rectAPos_);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    glEnable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
}
