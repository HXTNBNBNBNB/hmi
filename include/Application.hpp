#pragma once

class GLFWwindow;
class Scene;
class FpsCounter;
class ImGuiLayer;

class Application {
public:
    // 初始化窗口、ImGui、场景、音频、UDP 等
    bool init();

    // 主循环，直到窗口关闭，内部负责清理
    void run();

private:
    // GLFW 回调通过 window user pointer 转发到这些成员方法
    void onFramebufferSize(int width, int height);
    void onMouseButton(int button, int action, int mods);
    void onCursorPos(double xpos, double ypos);
    void onScroll(double xoffset, double yoffset);
    void onKey(int key, int scancode, int action, int mods);

    // 资源
    GLFWwindow* window_{nullptr};
    Scene* scene_{nullptr};       // 由 Application::init 创建并持有（当前仍使用静态对象）
    FpsCounter* fpsCounter_{nullptr};
    ImGuiLayer* imguiLayer_{nullptr};  // 由 Application::init 创建并持有

    // 回调静态桥接函数
    static void FramebufferSizeCallback(GLFWwindow* win, int width, int height);
    static void MouseButtonCallback(GLFWwindow* win, int button, int action, int mods);
    static void CursorPosCallback(GLFWwindow* win, double xpos, double ypos);
    static void ScrollCallback(GLFWwindow* win, double xoffset, double yoffset);
    static void KeyCallback(GLFWwindow* win, int key, int scancode, int action, int mods);
};

