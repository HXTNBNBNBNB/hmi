#pragma once

#include <GLES2/gl2.h>
#include <string>
#include <unordered_map>
#include <vector>

struct CharacterInfo {
    float ax;  // advance x
    float ay;  // advance y
    float bw;  // bitmap width
    float bh;  // bitmap height
    float bl;  // bitmap left
    float bt;  // bitmap top
    float tx;  // texture x offset
    float ty;  // texture y offset
};

class TextRenderer {
public:
    TextRenderer();
    ~TextRenderer();

    // fontPath: 主字体; fallbackFontPath: 备用字体; warningFontPath: 警告用字体（可选，为空则与主字体一致）
    bool init(const std::string& fontPath, int fontSize = 32, const std::string& fallbackFontPath = "",
              const std::string& warningFontPath = "");
    void cleanup();

    void setScreenSize(int width, int height);

    // 渲染文本，x/y为屏幕像素坐标，居中模式下x为中心x坐标
    // useOutline: 描边；useShadow: 先绘一层偏移阴影；useWarningFont: 使用警告字体（若已加载）
    void renderText(const std::string& text, float x, float y, float scale = 1.0f,
                    float r = 1.0f, float g = 1.0f, float b = 1.0f, bool centered = false,
                    bool useOutline = true, bool useShadow = true, bool useWarningFont = false);

    // 获取文本宽度（像素），useWarningFont 为 true 时用警告字体度量（若有）
    float getTextWidth(const std::string& text, float scale = 1.0f, bool useWarningFont = false);

    // 绘制半透明色块（用于警告区域框等）
    void renderRect(float x, float y, float w, float h, float r, float g, float b, float a);

private:
    GLuint program_ = 0;
    GLuint vbo_ = 0;
    GLuint fontTexture_ = 0;
    GLuint rectProgram_ = 0;
    GLuint rectVbo_ = 0;
    GLint rectUProjection_ = -1;
    GLint rectUColor_ = -1;
    GLint rectAPos_ = -1;

    GLint aPos_ = -1;
    GLint aTexCoord_ = -1;
    GLint uProjection_ = -1;
    GLint uTextColor_ = -1;
    GLint uTexture_ = -1;
    GLint uOffset_ = -1;
    GLint uTexelSize_ = -1;
    GLint uOutlineColor_ = -1;
    GLint uOutlineEnabled_ = -1;

    int screenWidth_ = 1280;
    int screenHeight_ = 720;
    int fontSize_ = 32;
    int atlasWidth_ = 0;
    int atlasHeight_ = 0;

    std::unordered_map<uint32_t, CharacterInfo> characters_;
    GLuint warningTexture_ = 0;
    int warningAtlasWidth_ = 0;
    int warningAtlasHeight_ = 0;
    std::unordered_map<uint32_t, CharacterInfo> warningCharacters_;

    bool loadFont(const std::string& fontPath, const std::string& fallbackFontPath = "");
    bool loadWarningFont(const std::string& fontPath);
    GLuint compileShader(GLenum type, const char* source);
    GLuint createProgram(const char* vertSrc, const char* fragSrc);

    // UTF-8 解码
    static uint32_t decodeUTF8(const char*& ptr, const char* end);
};
