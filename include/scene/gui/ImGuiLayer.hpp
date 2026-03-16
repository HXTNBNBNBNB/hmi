#pragma once

#include <string>

struct GLFWwindow;
class FpsCounter;
class Scene;
class AudioPlayer;

// 负责 Dear ImGui 的初始化、每帧开始/结束和渲染控制面板
class ImGuiLayer {
public:
    ImGuiLayer() = default;

    // 初始化 ImGui 上下文和后端（需要已创建好的 OpenGL/GLFW 窗口）
    bool init(GLFWwindow* window);

    // 每帧开始前调用（在 pollEvents 之后、场景渲染之前）
    void beginFrame();

    // 渲染控制面板等 UI，并输出到当前 framebuffer
    void render(Scene& scene, FpsCounter& fps, AudioPlayer& audioPlayer, GLFWwindow* window);

    // 结束 ImGui 并提交绘制
    void endFrame(GLFWwindow* window);

    // 程序退出时清理 ImGui 资源
    void shutdown();

private:
    bool initialized_ = false;
};

