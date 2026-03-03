#pragma once

#include <GLES2/gl2.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <vector>

// 新增包含
#include "model/ModelManager.hpp"
#include "controller/ModelController.hpp"
#include "controller/UDPDataManager.hpp"
#include "ui/TextRenderer.hpp"

class Scene {
public:
    Scene();
    ~Scene();

    bool init();
    void resize(int width, int height);
    void render();

    // 模型控制接口(使用ModelController)
    void setModelTransform(const std::string& objectId, const ObjectState& state);

    // UDP数据更新接口
    void updateFromUdpData(const std::vector<ProcessedUdpObstacle>& obstacles);

    // 模型加载接口(使用ModelManager)
    void loadNecessaryModels();

    // 模型属性初始化接口(支持一个模型 多个实例)
    void initModelsAttribute();

    void onMouseButton(GLFWwindow* window, int button, int action);
    void onCursorPos(double xpos, double ypos);
    void onScroll(double yoffset);
    void onKey(GLFWwindow* window, int key, int action);

    int screenWidth() const { return screenWidth_; }
    int screenHeight() const { return screenHeight_; }

private:
    GLuint compileShader(GLenum type, const char* source);
    GLuint createProgram(const char* vertSrc, const char* fragSrc);
    glm::vec3 getCameraPosition() const;

    int screenWidth_ = 1280;
    int screenHeight_ = 720;

    // Camera
    float cameraYaw_ = 0.0f;
    float cameraPitch_ = 25.0f;
    float cameraDistance_ = 25.0f;
    glm::vec3 cameraTarget_{0.0f, 0.0f, 0.0f};

    // Mouse
    bool leftPressed_ = false;
    double lastLX_ = 0.0, lastLY_ = 0.0;
    bool rightPressed_ = false;
    double lastRX_ = 0.0, lastRY_ = 0.0;

    // Ground plane
    GLuint groundProgram_ = 0;
    GLuint planeVBO_ = 0;
    GLint aPos_ground_ = -1;
    GLint uModel_ = -1;
    GLint uView_ = -1;
    GLint uProjection_ = -1;

    // Axis indicator
    GLuint axisProgram_ = 0;
    GLuint axisVBO_ = 0;
    GLint aPos_axis_ = -1;
    GLint aColor_axis_ = -1;
    GLint uVP_axis_ = -1;

    // Lane lines (车道线)
    GLuint laneVBO_ = 0;

    // 文本渲染器
    TextRenderer textRenderer_;

    // 车号显示（硬编码，方便后续修改）
    std::string vehicleId_ = "NBZS-JLPT-AT700";

    // 模型控制成员
    ModelController* model_controller_;
};
