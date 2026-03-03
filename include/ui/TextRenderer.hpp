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

    bool init(const std::string& fontPath, int fontSize = 32);
    void cleanup();

    void setScreenSize(int width, int height);

    // 渲染文本，x/y为屏幕像素坐标，居中模式下x为中心x坐标
    void renderText(const std::string& text, float x, float y, float scale = 1.0f,
                    float r = 1.0f, float g = 1.0f, float b = 1.0f, bool centered = false);

    // 获取文本宽度（像素）
    float getTextWidth(const std::string& text, float scale = 1.0f);

private:
    GLuint program_ = 0;
    GLuint vbo_ = 0;
    GLuint fontTexture_ = 0;

    GLint aPos_ = -1;
    GLint aTexCoord_ = -1;
    GLint uProjection_ = -1;
    GLint uTextColor_ = -1;
    GLint uTexture_ = -1;

    int screenWidth_ = 1280;
    int screenHeight_ = 720;
    int fontSize_ = 32;
    int atlasWidth_ = 0;
    int atlasHeight_ = 0;

    // 使用 uint32_t 作为键支持 Unicode
    std::unordered_map<uint32_t, CharacterInfo> characters_;

    bool loadFont(const std::string& fontPath);
    GLuint compileShader(GLenum type, const char* source);
    GLuint createProgram(const char* vertSrc, const char* fragSrc);

    // UTF-8 解码
    static uint32_t decodeUTF8(const char*& ptr, const char* end);
};
