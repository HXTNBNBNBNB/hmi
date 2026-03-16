#include <cstdio>
#include <string>

#include <GLES2/gl2.h>
#include <EGL/egl.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

// Dear ImGui 后端在回调桥接里仍然会直接调用
#include "backends/imgui_impl_glfw.h"

#include "Application.hpp"
#include "scene/Scene.hpp"
#include "scene/fps/FpsCounter.hpp"
#include "data/UDPDataManager.hpp"
#include "scene/audio/AudioPlayer.hpp"
#include "scene/gui/ImGuiLayer.hpp"

static const double kFpsPresets[] = {30.0, 60.0, 90.0, 120.0};
static const int kFpsPresetCount = 4;
static int g_fpsPresetIndex = 1;

bool Application::init() {
    if (!glfwInit()) {
        std::fprintf(stderr, "Failed to init GLFW\n");
        return false;
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    glfwWindowHint(GLFW_CONTEXT_CREATION_API, GLFW_EGL_CONTEXT_API);
    glfwWindowHint(GLFW_SAMPLES, 4);
    glfwWindowHint(GLFW_ALPHA_BITS, 0);  // 强制不透明窗口，避免 EGL 合成器透明

    window_ = glfwCreateWindow(1280, 720, "HMI Scene", nullptr, nullptr);
    if (!window_) {
        std::fprintf(stderr, "Failed to create window\n");
        glfwTerminate();
        return false;
    }
    GLFWwindow* window = window_;

    // 将 this 绑定到窗口，供静态回调中取回 Application 实例
    glfwSetWindowUserPointer(window_, this);

    glfwMakeContextCurrent(window);
    glfwSwapInterval(0); // Disable vsync; FpsCounter handles frame limiting

    const char* glVersion = (const char*)glGetString(GL_VERSION);
    std::printf("GL Vendor:   %s\n", (const char*)glGetString(GL_VENDOR));
    std::printf("GL Renderer: %s\n", (const char*)glGetString(GL_RENDERER));
    std::printf("GL Version:  %s\n", glVersion ? glVersion : "(null)");

    if (!glVersion) {
        std::fprintf(stderr, "ERROR: OpenGL ES context not properly initialized.\n");
        glfwDestroyWindow(window);
        glfwTerminate();
        window_ = nullptr;
        return false;
    }

    glfwSetFramebufferSizeCallback(window, FramebufferSizeCallback);
    glfwSetMouseButtonCallback(window, MouseButtonCallback);
    glfwSetCursorPosCallback(window, CursorPosCallback);
    glfwSetScrollCallback(window, ScrollCallback);
    glfwSetKeyCallback(window, KeyCallback);

    // ImGui 初始化由 ImGuiLayer 负责
    static ImGuiLayer imguiLayer;
    imguiLayer_ = &imguiLayer;
    if (!imguiLayer_->init(window_)) {
        std::fprintf(stderr, "Failed to init ImGuiLayer\n");
        glfwDestroyWindow(window_);
        glfwTerminate();
        window_ = nullptr;
        return false;
    }

    // 创建场景并初始化
    static Scene scene;      // 静态存储期，保证生命周期比 Application::run 长
    scene_ = &scene;

    // 初始化音频播放器
    AudioPlayer& audioPlayer = AudioPlayer::getInstance();
    if (!audioPlayer.initialize()) {
        std::fprintf(stderr, "Warning: Failed to initialize audio player\n");
    }

    // 启动UDP数据接收
    UDPDataManager& udpManager = UDPDataManager::getInstance();
    if (!udpManager.start(8765)) {
        std::fprintf(stderr, "Warning: Failed to start UDP receiver\n");
    }

    if (!scene.init()) {
        std::fprintf(stderr, "Failed to init scene\n");
        udpManager.stop();
        audioPlayer.cleanup();
        if (imguiLayer_) {
            imguiLayer_->shutdown();
            imguiLayer_ = nullptr;
        }
        glfwDestroyWindow(window);
        glfwTerminate();
        window_ = nullptr;
        scene_ = nullptr;
        return false;
    }

    int fbW, fbH;
    glfwGetFramebufferSize(window, &fbW, &fbH);
    scene_->resize(fbW, fbH);

    static FpsCounter fps(25.0);
    fpsCounter_ = &fps;

    return true;
}

void Application::run() {
    if (!window_ || !scene_ || !fpsCounter_ || !imguiLayer_) {
        std::fprintf(stderr, "Application::run called before successful init\n");
        return;
    }

    GLFWwindow* window = window_;
    Scene* scene = scene_;
    FpsCounter* fps = fpsCounter_;
    AudioPlayer& audioPlayer = AudioPlayer::getInstance();
    UDPDataManager& udpManager = UDPDataManager::getInstance();

    char titleBuf[128];

    while (!glfwWindowShouldClose(window)) {
        bool updated = fps->beginFrame();

        glfwPollEvents();

        // ImGui 帧开始 & 渲染 UI
        imguiLayer_->beginFrame();
        imguiLayer_->render(*scene, *fps, audioPlayer, window);

        // 渲染 3D 场景
        scene->render(fps->getDeltaTime());

        // 提交 ImGui 渲染
        imguiLayer_->endFrame(window);

        glfwSwapBuffers(window);

        fps->endFrame();

        if (updated) {
            const char* audioStatus = audioPlayer.isPlaying() ?
                (std::string("音频(") + std::to_string(static_cast<int>(audioPlayer.getCurrentPriority())) + ")").c_str() :
                "无音频";

            std::snprintf(titleBuf, sizeof(titleBuf),
                     "HMI Scene | FPS: %.1f | Frame: %.2f ms | Limit: %.0f (F1-F4) | %s",
                     fps->fps(), fps->frameTime(), fps->targetFps(), audioStatus);
            glfwSetWindowTitle(window, titleBuf);
        }
    }

    // 退出与清理
    fpsCounter_ = nullptr;

    udpManager.stop();
    audioPlayer.cleanup();

    if (imguiLayer_) {
        imguiLayer_->shutdown();
        imguiLayer_ = nullptr;
    }

    scene_ = nullptr;
    window_ = nullptr;

    glfwDestroyWindow(window);
    glfwTerminate();
}

// =========================
// GLFW 静态回调桥接
// =========================

void Application::FramebufferSizeCallback(GLFWwindow* win, int width, int height) {
    if (auto* app = static_cast<Application*>(glfwGetWindowUserPointer(win))) {
        app->onFramebufferSize(width, height);
    }
}

void Application::MouseButtonCallback(GLFWwindow* win, int button, int action, int mods) {
    if (auto* app = static_cast<Application*>(glfwGetWindowUserPointer(win))) {
        // 先交给 ImGui
        ImGui_ImplGlfw_MouseButtonCallback(win, button, action, mods);
        app->onMouseButton(button, action, mods);
    }
}

void Application::CursorPosCallback(GLFWwindow* win, double xpos, double ypos) {
    if (auto* app = static_cast<Application*>(glfwGetWindowUserPointer(win))) {
        app->onCursorPos(xpos, ypos);
    }
}

void Application::ScrollCallback(GLFWwindow* win, double xoffset, double yoffset) {
    if (auto* app = static_cast<Application*>(glfwGetWindowUserPointer(win))) {
        ImGui_ImplGlfw_ScrollCallback(win, xoffset, yoffset);
        app->onScroll(xoffset, yoffset);
    }
}

void Application::KeyCallback(GLFWwindow* win, int key, int scancode, int action, int mods) {
    if (auto* app = static_cast<Application*>(glfwGetWindowUserPointer(win))) {
        ImGui_ImplGlfw_KeyCallback(win, key, scancode, action, mods);
        app->onKey(key, scancode, action, mods);
    }
}

// =========================
// 成员级事件处理
// =========================

void Application::onFramebufferSize(int width, int height) {
    if (scene_) {
        scene_->resize(width, height);
    }
}

void Application::onMouseButton(int button, int action, int /*mods*/) {
    if (scene_) {
        scene_->onMouseButton(window_, button, action);
    }
}

void Application::onCursorPos(double xpos, double ypos) {
    if (scene_) {
        scene_->onCursorPos(xpos, ypos);
    }
}

void Application::onScroll(double /*xoffset*/, double yoffset) {
    if (scene_) {
        scene_->onScroll(yoffset);
    }
}

void Application::onKey(int key, int /*scancode*/, int action, int /*mods*/) {
    if (scene_) {
        scene_->onKey(window_, key, action);
    }

    if (action == GLFW_PRESS && fpsCounter_) {
        // FPS 控制
        if (key >= GLFW_KEY_F1 && key <= GLFW_KEY_F4) {
            g_fpsPresetIndex = key - GLFW_KEY_F1;
            fpsCounter_->setTargetFps(kFpsPresets[g_fpsPresetIndex]);
            std::printf("FPS limit: %.0f\n", kFpsPresets[g_fpsPresetIndex]);
        }
    }

    if (action == GLFW_PRESS) {
        // 音频播放控制键仍然保留
        if (key >= GLFW_KEY_1 && key <= GLFW_KEY_3) {
            AudioPlayer& audioPlayer = AudioPlayer::getInstance();
            int audioNum = key - GLFW_KEY_1 + 1;
            std::printf("播放音频 %d\n", audioNum);
            audioPlayer.playAudio(audioNum, true);
        } else if (key == GLFW_KEY_S) {
            AudioPlayer& audioPlayer = AudioPlayer::getInstance();
            std::printf("停止音频播放\n");
            audioPlayer.stop();
        }
    }
}

