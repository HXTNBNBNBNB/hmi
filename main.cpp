#include <cstdio>

#include <GLES2/gl2.h>
#include <EGL/egl.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include "Scene.hpp"
#include "FpsCounter.hpp"
#include "controller/UDPDataManager.hpp"

static Scene* g_scene = nullptr;
static FpsCounter* g_fps = nullptr;

static const double kFpsPresets[] = {30.0, 60.0, 90.0, 120.0};
static const int kFpsPresetCount = 4;
static int g_fpsPresetIndex = 1; // default 60

static void framebufferSizeCallback(GLFWwindow*, int width, int height) {
    if (g_scene) g_scene->resize(width, height);
}

static void mouseButtonCallback(GLFWwindow* window, int button, int action, int) {
    if (g_scene) g_scene->onMouseButton(window, button, action);
}

static void cursorPosCallback(GLFWwindow*, double xpos, double ypos) {
    if (g_scene) g_scene->onCursorPos(xpos, ypos);
}

static void scrollCallback(GLFWwindow*, double, double yoffset) {
    if (g_scene) g_scene->onScroll(yoffset);
}

static void keyCallback(GLFWwindow* window, int key, int, int action, int) {
    if (g_scene) g_scene->onKey(window, key, action);

    if (action == GLFW_PRESS && g_fps) {
        // F1=30  F2=60  F3=90  F4=120
        if (key >= GLFW_KEY_F1 && key <= GLFW_KEY_F4) {
            g_fpsPresetIndex = key - GLFW_KEY_F1;
            g_fps->setTargetFps(kFpsPresets[g_fpsPresetIndex]);
            printf("FPS limit: %.0f\n", kFpsPresets[g_fpsPresetIndex]);
        }
    }
}

int main() {
    if (!glfwInit()) {
        fprintf(stderr, "Failed to init GLFW\n");
        return 1;
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    glfwWindowHint(GLFW_CONTEXT_CREATION_API, GLFW_EGL_CONTEXT_API);
    glfwWindowHint(GLFW_SAMPLES, 4);
    glfwWindowHint(GLFW_ALPHA_BITS, 0);  // 强制不透明窗口，避免 EGL 合成器透明

    GLFWwindow* window = glfwCreateWindow(1280, 720, "HMI Scene", nullptr, nullptr);
    if (!window) {
        fprintf(stderr, "Failed to create window\n");
        glfwTerminate();
        return 1;
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(0); // Disable vsync; FpsCounter handles frame limiting

    const char* glVersion = (const char*)glGetString(GL_VERSION);
    printf("GL Vendor:   %s\n", (const char*)glGetString(GL_VENDOR));
    printf("GL Renderer: %s\n", (const char*)glGetString(GL_RENDERER));
    printf("GL Version:  %s\n", glVersion ? glVersion : "(null)");

    if (!glVersion) {
        fprintf(stderr, "ERROR: OpenGL ES context not properly initialized.\n");
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }

    glfwSetFramebufferSizeCallback(window, framebufferSizeCallback);
    glfwSetMouseButtonCallback(window, mouseButtonCallback);
    glfwSetCursorPosCallback(window, cursorPosCallback);
    glfwSetScrollCallback(window, scrollCallback);
    glfwSetKeyCallback(window, keyCallback);

    Scene scene;
    g_scene = &scene;

    // 启动UDP数据接收
    UDPDataManager& udpManager = UDPDataManager::getInstance();
    if (!udpManager.start(8765)) {
        fprintf(stderr, "Warning: Failed to start UDP receiver\n");
    }

    if (!scene.init()) {
        fprintf(stderr, "Failed to init scene\n");
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }

    int fbW, fbH;
    glfwGetFramebufferSize(window, &fbW, &fbH);
    scene.resize(fbW, fbH);

    FpsCounter fps(25.0);
    g_fps = &fps;
    char titleBuf[128];

    while (!glfwWindowShouldClose(window)) {
        bool updated = fps.beginFrame();

        glfwPollEvents();
        scene.render();
        glfwSwapBuffers(window);

        fps.endFrame();

        if (updated) {
            snprintf(titleBuf, sizeof(titleBuf),
                     "HMI Scene | FPS: %.1f | Frame: %.2f ms | Limit: %.0f (F1-F4)",
                     fps.fps(), fps.frameTime(), fps.targetFps());
            glfwSetWindowTitle(window, titleBuf);
        }
    }

    g_fps = nullptr;

    // 停止UDP数据接收
    udpManager.stop();

    g_scene = nullptr;
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
