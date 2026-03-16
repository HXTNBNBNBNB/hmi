#include "ImGuiLayer.hpp"

#include <GLES2/gl2.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_opengl3.h"

#include "scene/fps/FpsCounter.hpp"
#include "scene/Scene.hpp"
#include "scene/audio/AudioPlayer.hpp"

bool ImGuiLayer::init(GLFWwindow* window) {
    if (initialized_) return true;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    ImGui::StyleColorsDark();

    // 加载支持中文的字体：与 Scene 的 TextRenderer 使用同一套候选路径
    io.Fonts->Clear();
    const char* fontCandidates[] = {
        "resources/fonts/NotoSansCJK-Regular.ttc",
        "../resources/fonts/NotoSansCJK-Regular.ttc",
        "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.otf",
        "/usr/share/fonts/truetype/noto/NotoSansCJK-Regular.ttc",
        "resources/fonts/DroidSansFallback.ttf",
        "../resources/fonts/DroidSansFallback.ttf",
        "resources/fonts/uming.ttc",
        "../resources/fonts/uming.ttc",
        nullptr
    };

    const ImWchar* glyphRanges = io.Fonts->GetGlyphRangesChineseFull();
    ImFont* font = nullptr;
    for (int i = 0; fontCandidates[i] != nullptr; ++i) {
        font = io.Fonts->AddFontFromFileTTF(
            fontCandidates[i],
            18.0f,
            nullptr,
            glyphRanges
        );
        if (font) break;
    }
    if (!font) {
        io.Fonts->AddFontDefault();
    }

    ImGui_ImplGlfw_InitForOpenGL(window, false);
    ImGui_ImplOpenGL3_Init("#version 100");  // GLES2 使用 #version 100

    initialized_ = true;
    return true;
}

void ImGuiLayer::beginFrame() {
    if (!initialized_) return;
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
}

void ImGuiLayer::render(Scene& /*scene*/, FpsCounter& fps, AudioPlayer& audioPlayer, GLFWwindow* /*window*/) {
    if (!initialized_) return;

    // 后续你可以在这里集中扩展所有 ImGui 面板和按钮
    ImGui::Begin("控制面板");
    ImGui::Text("FPS: %.1f", fps.fps());
    ImGui::Separator();

    if (ImGui::Button("播放音频1")) {
        audioPlayer.playAudio(1, true);
    }
    ImGui::SameLine();
    if (ImGui::Button("停止音频")) {
        audioPlayer.stop();
    }

    ImGui::End();
}

void ImGuiLayer::endFrame(GLFWwindow* window) {
    if (!initialized_) return;

    ImGui::Render();
    int display_w, display_h;
    glfwGetFramebufferSize(window, &display_w, &display_h);
    glViewport(0, 0, display_w, display_h);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

void ImGuiLayer::shutdown() {
    if (!initialized_) return;
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    initialized_ = false;
}

