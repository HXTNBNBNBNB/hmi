#include "Scene.hpp"
#include "controller/UDPDataManager.hpp"
#include "audioplayder/AudioPlayer.hpp"

#include <cstdio>
#include <cmath>
#include <iostream>
#include <unordered_map>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>


// ============================================================
// Voice alarm lists
// ============================================================
// 与 Python 发送方对齐：direction/type 均为英文字符串
// Python calc_voice_alarm 输出: direction: left|right|front|left_front|right_front
//                          type: truck|pedestrian|other
std::unordered_map<std::string, int> voice_map = {
  // 方位（1-5）
  {"left",        1},   // 左侧
  {"right",       2},   // 右侧
  {"front",       3},   // 前方  ✗注意: 需要 3.ogg 存在，当前资源目录缺少该文件将跳过
  {"left_front",  4},   // 左前
  {"right_front", 5},   // 右前
  // 障碍物类型（8-10）
  {"truck",       8},   // 卡车
  {"pedestrian",  9},   // 行人
  {"other",       10},  // 障碍物
  // 特殊整句（11-12）- 由高优先级逐走直接使用，不在拼接序列中出现
  {"stop_obstacle", 11}, // 停车注意障碍物
  {"danger_stop",   12}, // 危险立即停车
};

// ============================================================
// Shader sources
// ============================================================

static const char* groundVertSrc = R"(
attribute vec3 aPos;
uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProjection;
varying vec3 vWorldPos;
void main() {
    vec4 worldPos = uModel * vec4(aPos, 1.0);
    vWorldPos = worldPos.xyz;
    gl_Position = uProjection * uView * worldPos;
}
)";

static const char* groundFragSrc = R"(
precision mediump float;
varying vec3 vWorldPos;

void main() {
    // 地面法线（固定向上）
    vec3 normal = vec3(0.0, 1.0, 0.0);

    // 光照方向：偏顶部斜射，保证水平地面获得足够照度
    vec3 lightDir = normalize(vec3(-0.5, 2.0, 0.3));

    float diff = max(dot(normal, lightDir), 0.0);
    float ambient = 0.65; // 高环境光，保持地面整体白亮
    float lighting = ambient + diff * 0.35;
    lighting = min(lighting, 1.0);

    // 基础颜色（蓝白色地面）
    vec3 baseColor = vec3(0.88, 0.93, 1.0);
    vec3 color = baseColor * lighting;

    // 远处淡出效果
    float dist = length(vWorldPos.xz);
    float fade = 1.0 - smoothstep(20.0, 30.0, dist);
    color = mix(vec3(0.75, 0.82, 0.92), color, fade);

    // 测试：暂时移除gamma校正
    // 线性空间 -> sRGB gamma 校正
    // vec3 gamma = vec3(1.0 / 2.2);
    // gl_FragColor = vec4(pow(color, gamma), 1.0);

    // 直接输出颜色，不应用gamma校正
    gl_FragColor = vec4(color, 1.0);
}
)";

static const char* axisVertSrc = R"(
attribute vec3 aPos;
attribute vec3 aColor;
uniform mat4 uVP;
varying vec3 vColor;
void main() {
    vColor = aColor;
    gl_Position = uVP * vec4(aPos, 1.0);
}
)";

static const char* axisFragSrc = R"(
precision mediump float;
varying vec3 vColor;
void main() {
    // 测试：暂时移除gamma校正
    // 线性空间 -> sRGB gamma 校正
    // vec3 gamma = vec3(1.0 / 2.2);
    // gl_FragColor = vec4(pow(vColor, gamma), 1.0);

    // 直接输出颜色，不应用gamma校正
    gl_FragColor = vec4(vColor, 1.0);
}
)";

// ============================================================
// Scene implementation
// ============================================================

Scene::Scene()
    : model_controller_(nullptr)
{
    loadNecessaryModels();
    initModelsAttribute();
}

Scene::~Scene() {
    // 清理模型控制器（ModelManager和ModelImporter是单例，不需要手动删除）
    if (model_controller_) delete model_controller_;

    if (planeVBO_) glDeleteBuffers(1, &planeVBO_);
    if (axisVBO_) glDeleteBuffers(1, &axisVBO_);
    if (laneVBO_) glDeleteBuffers(1, &laneVBO_);
    if (groundProgram_) glDeleteProgram(groundProgram_);
    if (axisProgram_) glDeleteProgram(axisProgram_);
}

GLuint Scene::compileShader(GLenum type, const char* source) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);

    GLint success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char log[512];
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        fprintf(stderr, "Shader compile error: %s\n", log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

GLuint Scene::createProgram(const char* vertSrc, const char* fragSrc) {
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
        fprintf(stderr, "Program link error: %s\n", log);
        glDeleteProgram(program);
        program = 0;
    }

    glDeleteShader(vert);
    glDeleteShader(frag);
    return program;
}

glm::vec3 Scene::getCameraPosition() const {
    float pitchRad = glm::radians(cameraPitch_);
    float yawRad = glm::radians(cameraYaw_);
    glm::vec3 offset;
    offset.x = cameraDistance_ * cosf(pitchRad) * cosf(yawRad);
    offset.y = cameraDistance_ * sinf(pitchRad);
    offset.z = cameraDistance_ * cosf(pitchRad) * sinf(yawRad);
    return cameraTarget_ + offset;
}

bool Scene::init() {
    // 初始化模型管理系统（使用单例模式）
    model_controller_ = new ModelController();

    // 设置控制器回调
    model_controller_->setStateChangeCallback([this](const std::string& objectId, const ObjectState& state) {
        // 当模型状态改变时的回调处理
        auto& modelManager = ModelManager::getInstance();
        auto model = modelManager.getModel(objectId);
        if (model) {
            model->setPosition(state.position);
            model->setRotation(state.rotation);
            model->setScale(state.scale);
            model->setVisible(state.visible);
        }
    });

    // 初始化原有的着色器和几何体
    groundProgram_ = createProgram(groundVertSrc, groundFragSrc);
    if (!groundProgram_) return false;

    aPos_ground_ = glGetAttribLocation(groundProgram_, "aPos");
    uModel_ = glGetUniformLocation(groundProgram_, "uModel");
    uView_ = glGetUniformLocation(groundProgram_, "uView");
    uProjection_ = glGetUniformLocation(groundProgram_, "uProjection");

    axisProgram_ = createProgram(axisVertSrc, axisFragSrc);
    if (!axisProgram_) return false;

    aPos_axis_ = glGetAttribLocation(axisProgram_, "aPos");
    aColor_axis_ = glGetAttribLocation(axisProgram_, "aColor");
    uVP_axis_ = glGetUniformLocation(axisProgram_, "uVP");

    // 地面几何体
    float planeSize = 200.0f;
    float planeVertices[] = {
        -planeSize, 0.0f, -planeSize,
         planeSize, 0.0f, -planeSize,
         planeSize, 0.0f,  planeSize,
        -planeSize, 0.0f, -planeSize,
         planeSize, 0.0f,  planeSize,
        -planeSize, 0.0f,  planeSize,
    };

    glGenBuffers(1, &planeVBO_);
    glBindBuffer(GL_ARRAY_BUFFER, planeVBO_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(planeVertices), planeVertices, GL_STATIC_DRAW);

#if 0
    // 坐标轴几何体
    float axisLen = 1.5f;
    float axisVertices[] = {
        0,0,0, 1,0,0,   axisLen,0,0, 1,0,0,
        0,0,0, 0,1,0,   0,axisLen,0, 0,1,0,
        0,0,0, 0,0,1,   0,0,axisLen, 0,0,1,
    };

    glGenBuffers(1, &axisVBO_);
    glBindBuffer(GL_ARRAY_BUFFER, axisVBO_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(axisVertices), axisVertices, GL_STATIC_DRAW);
#endif
    // 车道线几何体 (4条线，5个车道，每条线z不同)
    // 格式: x, y, z, r, g, b (与坐标轴相同)
    float laneXMin = -60.0f;
    float laneXMax = 60.0f;
    float laneY = 0.01f;  // 略高于地面避免z-fighting
    float laneColor[3] = {0.55f, 0.55f, 0.55f};  // 灰白色
    float laneZ[4] = {-6.3f, -2.0f, 2.0f, 6.3f};  // 4条分隔线

    // 4条线，每条线2个顶点，每顶点6个float (pos + color)
    float laneVertices[4 * 2 * 6];
    for (int i = 0; i < 4; ++i) {
        int base = i * 12;  // 每条线12个float
        // 起点
        laneVertices[base + 0] = laneXMin;
        laneVertices[base + 1] = laneY;
        laneVertices[base + 2] = laneZ[i];
        laneVertices[base + 3] = laneColor[0];
        laneVertices[base + 4] = laneColor[1];
        laneVertices[base + 5] = laneColor[2];
        // 终点
        laneVertices[base + 6] = laneXMax;
        laneVertices[base + 7] = laneY;
        laneVertices[base + 8] = laneZ[i];
        laneVertices[base + 9] = laneColor[0];
        laneVertices[base + 10] = laneColor[1];
        laneVertices[base + 11] = laneColor[2];
    }

    glGenBuffers(1, &laneVBO_);
    glBindBuffer(GL_ARRAY_BUFFER, laneVBO_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(laneVertices), laneVertices, GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    glEnable(GL_DEPTH_TEST);
    glClearColor(0.08f, 0.08f, 0.10f, 1.0f);

    // 初始化文本渲染器：优先使用笔画粗细均匀的字体
    struct FontPair { const char* primary; const char* fallback; };
    FontPair fontPairs[] = {
        {"resources/fonts/NotoSansCJK-Regular.ttc", nullptr},
        {"../resources/fonts/NotoSansCJK-Regular.ttc", nullptr},
        {"/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.otf", nullptr},
        {"/usr/share/fonts/truetype/noto/NotoSansCJK-Regular.ttc", nullptr},
        {"resources/fonts/DroidSansFallback.ttf",   "resources/fonts/uming.ttc"},
        {"../resources/fonts/DroidSansFallback.ttf", "../resources/fonts/uming.ttc"},
        {"resources/fonts/uming.ttc",               nullptr},
        {"../resources/fonts/uming.ttc",            nullptr},
        {nullptr, nullptr}
    };
    bool fontLoaded = false;
    for (int i = 0; fontPairs[i].primary != nullptr; ++i) {
        std::string fb = fontPairs[i].fallback ? fontPairs[i].fallback : "";
        std::string warnFont = fb.empty() ? "" : fb;  // 警告用另一套字体（如 uming）区分
        if (textRenderer_.init(fontPairs[i].primary, 40, fb, warnFont)) {
            fontLoaded = true;
            break;
        }
    }
    if (!fontLoaded) {
        fprintf(stderr, "Warning: Failed to init text renderer, no font found\n");
    }
    textRenderer_.setScreenSize(screenWidth_, screenHeight_);

    return true;
}

void Scene::resize(int width, int height) {
    screenWidth_ = width;
    screenHeight_ = height;
    glViewport(0, 0, width, height);
    textRenderer_.setScreenSize(width, height);
}

void Scene::render(double deltaTime) {
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    float aspect = static_cast<float>(screenWidth_) / static_cast<float>(screenHeight_);
    glm::mat4 projection = glm::perspective(glm::radians(45.0f), aspect, 0.1f, 200.0f);
    glm::vec3 camPos = getCameraPosition();
    glm::mat4 view = glm::lookAt(camPos, cameraTarget_, glm::vec3(0, 1, 0));
    glm::mat4 model(1.0f);

    // 渲染地面
    glUseProgram(groundProgram_);
    glUniformMatrix4fv(uModel_, 1, GL_FALSE, glm::value_ptr(model));
    glUniformMatrix4fv(uView_, 1, GL_FALSE, glm::value_ptr(view));
    glUniformMatrix4fv(uProjection_, 1, GL_FALSE, glm::value_ptr(projection));

    glBindBuffer(GL_ARRAY_BUFFER, planeVBO_);
    glEnableVertexAttribArray(aPos_ground_);
    glVertexAttribPointer(aPos_ground_, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glDisableVertexAttribArray(aPos_ground_);

    // 渲染坐标轴
    glUseProgram(axisProgram_);
    glm::mat4 vp = projection * view;
    glUniformMatrix4fv(uVP_axis_, 1, GL_FALSE, glm::value_ptr(vp));

#if 0  // 坐标轴已屏蔽
    glBindBuffer(GL_ARRAY_BUFFER, axisVBO_);
    glEnableVertexAttribArray(aPos_axis_);
    glVertexAttribPointer(aPos_axis_, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(aColor_axis_);
    glVertexAttribPointer(aColor_axis_, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));

    glLineWidth(2.0f);
    glDrawArrays(GL_LINES, 0, 6);

    glDisableVertexAttribArray(aPos_axis_);
    glDisableVertexAttribArray(aColor_axis_);
#endif

    // 渲染车道线 (复用 axisProgram_)
    glBindBuffer(GL_ARRAY_BUFFER, laneVBO_);
    glEnableVertexAttribArray(aPos_axis_);
    glVertexAttribPointer(aPos_axis_, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(aColor_axis_);
    glVertexAttribPointer(aColor_axis_, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));

    glLineWidth(5.0f);
    glDrawArrays(GL_LINES, 0, 8);  // 4条线，每条2个顶点

    glDisableVertexAttribArray(aPos_axis_);
    glDisableVertexAttribArray(aColor_axis_);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    // 平面投影阴影：光照在 truck 正上方，高度 1，Z=0，方向竖直向下
    glm::vec3 lightDir = glm::normalize(glm::vec3(0.0f, 1.0f, 0.0f));
    const float shadowBias = 0.012f;  // 抬高阴影平面，避免与地面 z-fighting 导致线条闪烁
    glm::mat4 shadowMatrix(
        glm::vec4(1, 0, 0, 0),
        glm::vec4(-lightDir.x / lightDir.y, 0, -lightDir.z / lightDir.y, 0),
        glm::vec4(0, 0, 1, 0),
        glm::vec4(lightDir.x * shadowBias / lightDir.y, shadowBias,
                  lightDir.z * shadowBias / lightDir.y, 1));

    GLboolean wasCull = glIsEnabled(GL_CULL_FACE);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);  // 阴影不写深度，只做颜色混合，避免阴影内线条闪烁
    ModelManager::getInstance().renderShadows(view, projection, shadowMatrix);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    if (wasCull) glEnable(GL_CULL_FACE);

    // 消费UDP数据更新
    UDPDataManager::getInstance().consumeUpdates(*this);

    // 更新并渲染模型
    if (model_controller_) {
        model_controller_->update(static_cast<float>(deltaTime));
    }

    // 渲染ModelManager管理的模型
    ModelManager::getInstance().renderAll(view, projection);

    // 渲染2D文本 (车号和警告信息)
    // 车号显示在屏幕中央上方
    float centerX = screenWidth_ / 2.0f;
    float topY = 15.0f;  // 距顶部15像素
    textRenderer_.renderText(vehicleId_, centerX, topY, 1.75f, 0.35f, 0.55f, 1.0f, true, false, true);

    // 警告信息显示在车号下方（与车号纵向间距加大）
    auto warnings = UDPDataManager::getInstance().getWarnings();
    float warningY = topY + 78.0f;
    for (size_t i = 0; i < warnings.size() && i < 3; ++i) {
        textRenderer_.renderText(warnings[i], centerX, warningY, 1.0f, 1.0f, 0.95f, 0.25f, true, false, true, true);
        warningY += 42.0f;
    }

    // 控制音频播放
    // 音频编号映射（basePath/*.ogg）：
    //   0        注意（拼接头）
    //   1-5  方位: left/right/front/left_front/right_front
    //   6-7  距离: 10米内（6）、5米内（7）
    //   8-10 类型: truck/pedestrian/other
    //   12   危险立即停车（priority==3 时的独立接口）
    // 普通拼接： {0, 方位, 距离, 类型}  e.g. {0,1,7,8} = 注意+左侧+5米内+卡车
    // 高优先级： {12}  危险立即停车（独立整句）
    // 每次报警事件只触发一次（tryConsumeVoiceAlarm 消费后自动清除）
    VoiceAlarm voiceAlarm;
    if (UDPDataManager::getInstance().tryConsumeVoiceAlarm(voiceAlarm)) {
      if (voiceAlarm.type.size() && voiceAlarm.priority && voiceAlarm.distance > 0.0f && voiceAlarm.direction.size()) {
        AudioPlayer& audioPlayer = AudioPlayer::getInstance();
        auto prio = static_cast<AudioPlayer::Priority>(voiceAlarm.priority);

        if (prio == AudioPlayer::Priority::HIGH) {
          // 高优先级（<1.5m）：播放独立整句“危险立即停车”
          audioPlayer.playAudioSequence({12}, prio, true);
        } else {
          // 普通拼接: 注意(0) + 方位 + 距离 + 类型
          // 使用 find() 避免向全局 map 插入默认值
          auto findOrSkip = [&](const std::string& key) -> int {
            auto it = voice_map.find(key);
            return (it != voice_map.end()) ? it->second : -1;  // -1 表示该位置跳过
          };
          int distNum = (voiceAlarm.distance <= 5.0f) ? 7 : 6;
          std::vector<int> seq = {
            0,                                    // 注意
            findOrSkip(voiceAlarm.direction),     // 方位
            distNum,                              // 距离阈値
            findOrSkip(voiceAlarm.type)           // 障碍物类型
          };

          std::cout << "----------------------sequence: ";
          for(auto it : seq) {
            std::cout << it << " ";
          }
          std::cout << std::endl;

          audioPlayer.playAudioSequence(seq, prio, true);
        }
      }
    }
}

void Scene::setModelTransform(const std::string& objectId, const ObjectState& state) {
    if (model_controller_) {
        model_controller_->setObjectState(objectId, state);
    }
}

void Scene::loadNecessaryModels() {
  std::cout << "=== Loading every necessary models in scene ===" << std::endl;
  auto& modelManager = ModelManager::getInstance();

  std::string truckModelPath = "/opt/models/truck.glb";
  if(modelManager.loadModel("truck", truckModelPath)) {
      std::cout << "Successfully loaded truck model from: " << truckModelPath << std::endl;
  } else {
      std::cerr << "Failed to load truck model from: " << truckModelPath << std::endl;
  }

  std::string personModelPath = "/opt/models/person.glb";
  if(modelManager.loadModel("person", personModelPath)) {
    std::cout << "Successfully loaded person model from: " << personModelPath << std::endl;
  } else {
    std::cerr << "Failed to load person model from: " << personModelPath << std::endl;
  }


  std::string otherTruckModelPath = "/opt/models/otruck.glb"; // 不带箱车
  if(modelManager.loadModel("otruck", otherTruckModelPath)) {
    std::cout << "Successfully loaded other truck model from: " << otherTruckModelPath << std::endl;
  } else {
    std::cerr << "Failed to load other truck model from: " << otherTruckModelPath << std::endl;
  }
#if 0 // 暂时不显示带箱车的模型 感知的判断是带不带挂 不是带不带箱
  std::string otherWTruckModelPath = "/opt/models/wtruck.glb"; // 带箱车
  if(modelManager.loadModel("wtruck", otherWTruckModelPath)) {
    std::cout << "Successfully loaded other wtruck model from: " << otherWTruckModelPath << std::endl;
  } else {
    std::cerr << "Failed to load other wtruck model from: " << otherWTruckModelPath << std::endl;
  }
#endif
  std::string cubeModelPath = "/opt/models/cube.glb"; // 立方体
  if(modelManager.loadModel("cube", cubeModelPath)) {
    std::cout << "Successfully loaded cube model from: " << cubeModelPath << std::endl;
  } else {
    std::cerr << "Failed to load cube model from: " << cubeModelPath << std::endl;
  }

  // 显示所有已加载模型
  auto model_ids = modelManager.getAllModelIds();
  std::cout << "Currently loaded models (" << model_ids.size() << "):" << std::endl;
  for (const auto& id : model_ids) {
    std::cout << "  - " << id << std::endl;
  }

  std::cout << std::endl;
}

void Scene::initModelsAttribute() {
  auto& modelManager = ModelManager::getInstance();

  if (modelManager.hasModel("truck")) {
    std::cout << "Truck model found" << std::endl;
    auto truckModel = modelManager.getModel("truck");
    // 设置卡车初始状态
    ObjectState truck_state;
    truck_state.position = glm::vec3(8.5f, -0.01f, 0.0f);
    truck_state.rotation = glm::vec3(0.0f, 0.0f, 0.0f);
    truck_state.scale = glm::vec3(1.0f, 1.0f, 1.0f);
    truck_state.visible = true;
    truck_state.opacity = 1.0f;
    // 直接设置模型属性以确保立即生效
    truckModel->setPosition(truck_state.position);
    truckModel->setRotation(truck_state.rotation);
    truckModel->setScale(truck_state.scale);
    truckModel->setVisible(truck_state.visible);

    // 通知ModelController以便状态管理和动画
    setModelTransform("truck", truck_state);
    std::cout << "Truck placed at position ("
              << truck_state.position.x << ", "
              << truck_state.position.y << ", "
              << truck_state.position.z << ")" << std::endl;
    std::cout << "=== Default truck models setup completed ===" << std::endl;
  } else {
    std::cerr << "Failed to find truck model ..." << std::endl;
  }

  // 后续模型设置 仅作静态展示用，不通过ModelController管理（因为后续会通过UDP动态控制它们）
#if 0
  if(modelManager.hasModel("otruck")) {
    std::cout << "Other truck model found" << std::endl;
    auto otruckModel = modelManager.getModel("otruck");
    // 设置卡车初始状态
    ObjectState otruck_state;
    otruck_state.position = glm::vec3(0.0f, 0.0f, -3.0f);
    otruck_state.rotation = glm::vec3(0.0f, 0.0f, 0.0f);
    otruck_state.scale = glm::vec3(1.0f, 1.0f, 1.0f);
    otruck_state.visible = true; // 默认实例 不可见
    otruck_state.opacity = 1.0f;

    // 直接设置模型属性以确保立即生效
    otruckModel->setPosition(otruck_state.position);
    otruckModel->setRotation(otruck_state.rotation);
    otruckModel->setScale(otruck_state.scale);
    otruckModel->setVisible(otruck_state.visible);

    // 通知ModelController以便状态管理和动画
    setModelTransform("otruck", otruck_state);
    std::cout << "Other Truck placed at position ("
              << otruck_state.position.x << ", "
              << otruck_state.position.y << ", "
              << otruck_state.position.z << ")" << std::endl;
  } else {
    std::cerr << "Failed to find other truck model ..." << std::endl;
  }

  if(modelManager.hasModel("wtruck")) {
    std::cout << "Other truck model found" << std::endl;
    auto otruckModel = modelManager.getModel("wtruck");
    // 设置卡车初始状态
    ObjectState otruck_state;
    otruck_state.position = glm::vec3(0.0f, 0.0f, 3.0f);
    otruck_state.rotation = glm::vec3(0.0f, 0.0f, 0.0f);
    otruck_state.scale = glm::vec3(1.0f, 1.0f, 1.0f);
    otruck_state.visible = true; // 默认实例 不可见
    otruck_state.opacity = 1.0f;

    // 直接设置模型属性以确保立即生效
    otruckModel->setPosition(otruck_state.position);
    otruckModel->setRotation(otruck_state.rotation);
    otruckModel->setScale(otruck_state.scale);
    otruckModel->setVisible(otruck_state.visible);

    // 通知ModelController以便状态管理和动画
    setModelTransform("wtruck", otruck_state);
    std::cout << "Other WTruck placed at position ("
              << otruck_state.position.x << ", "
              << otruck_state.position.y << ", "
              << otruck_state.position.z << ")" << std::endl;
  } else {
    std::cerr << "Failed to find other wtruck model ..." << std::endl;
  }

  if(modelManager.hasModel("person")) {
    std::cout << "Person model found, creating multiple instances..." << std::endl;
    auto person = modelManager.getModel("person");

    ObjectState person_state;
    person_state.position = glm::vec3(8.0f, 0.0f, -4.0f);
    person_state.rotation = glm::vec3(0.0f, 0.0f, 0.0f);
    person_state.scale = glm::vec3(1.0f, 1.0f, 1.0f);
    person_state.visible = true; // 默认实例 不可见
    person_state.opacity = 1.0f;

    person->setPosition(person_state.position);
    person->setRotation(person_state.rotation);
    person->setScale(person_state.scale);
    person->setVisible(person_state.visible);
    person->setOpacity(person_state.opacity);

    setModelTransform("person", person_state);
    std::cout << "Person instance placed at position ("
              << person_state.position.x << ", "
              << person_state.position.y << ", "
              << person_state.position.z << ")" << std::endl;
  } else {
    std::cerr << "Failed to find person model ..." << std::endl;
  }

  if(modelManager.hasModel("cube")) {
    std::cout << "Cube model found" << std::endl;
    auto cubeModel = modelManager.getModel("cube");
    // 设置立方体初始状态
    ObjectState cube_state;
    cube_state.position = glm::vec3(0.0f, 0.0f, 0.0f);
    cube_state.rotation = glm::vec3(0.0f, 0.0f, 0.0f);
    cube_state.scale = glm::vec3(1.0f, 1.0f, 1.0f);
    cube_state.visible = true; // 默认实例 不可见
    cube_state.opacity = 1.0f;

    // 直接设置模型属性以确保立即生效
    cubeModel->setPosition(cube_state.position);
    cubeModel->setRotation(cube_state.rotation);
    cubeModel->setScale(cube_state.scale);
    cubeModel->setVisible(cube_state.visible);

    // 通知ModelController以便状态管理和动画
    setModelTransform("cube", cube_state);
    std::cout << "Cube placed at position ("
              << cube_state.position.x << ", "
              << cube_state.position.y << ", "
              << cube_state.position.z << ")" << std::endl;
  } else {
    std::cerr << "Failed to find cube model ..." << std::endl;
  }

#endif
}

void Scene::onMouseButton(GLFWwindow* window, int button, int action) {
    if (button == GLFW_MOUSE_BUTTON_LEFT) {
        leftPressed_ = (action == GLFW_PRESS);
        if (leftPressed_) glfwGetCursorPos(window, &lastLX_, &lastLY_);
    }
    if (button == GLFW_MOUSE_BUTTON_RIGHT) {
        rightPressed_ = (action == GLFW_PRESS);
        if (rightPressed_) glfwGetCursorPos(window, &lastRX_, &lastRY_);
    }
}

void Scene::onCursorPos(double xpos, double ypos) {
    if (leftPressed_) {
        float dx = static_cast<float>(xpos - lastLX_);
        float dy = static_cast<float>(ypos - lastLY_);
        cameraYaw_ += dx * 0.3f;
        cameraPitch_ += dy * 0.3f;
        cameraPitch_ = glm::clamp(cameraPitch_, 5.0f, 89.0f);
        lastLX_ = xpos;
        lastLY_ = ypos;
    }
    if (rightPressed_) {
        float dx = static_cast<float>(xpos - lastRX_);
        float dy = static_cast<float>(ypos - lastRY_);

        glm::vec3 camPos = getCameraPosition();
        glm::vec3 forward = glm::normalize(cameraTarget_ - camPos);
        glm::vec3 right = glm::normalize(glm::cross(forward, glm::vec3(0, 1, 0)));
        glm::vec3 up = glm::normalize(glm::cross(right, forward));

        float panSpeed = cameraDistance_ * 0.002f;
        cameraTarget_ -= right * dx * panSpeed;
        cameraTarget_ += up * dy * panSpeed;

        lastRX_ = xpos;
        lastRY_ = ypos;
    }
}

void Scene::onScroll(double yoffset) {
    cameraDistance_ -= static_cast<float>(yoffset) * 0.8f;
    cameraDistance_ = glm::clamp(cameraDistance_, 2.0f, 50.0f);
}

void Scene::onKey(GLFWwindow* window, int key, int action) {
    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) {
        glfwSetWindowShouldClose(window, GLFW_TRUE);
    }
    if (key == GLFW_KEY_R && action == GLFW_PRESS) {
        cameraYaw_ = -0.0f;
        cameraPitch_ = 25.0f;
        cameraDistance_ = 25.0f;
        cameraTarget_ = glm::vec3(0.0f);
    }
}


void Scene::updateFromUdpData(const std::vector<ProcessedUdpObstacle>& obstacles) {
  if(obstacles.empty()) return;

  // std::cout << "[UDP] Processing " << obstacles.size() << " obstacles" << std::endl;

  auto& modelManager = ModelManager::getInstance();
  std::unordered_set<std::string> processedInstanceIds;

  // 处理每个障碍物
  for (const auto& obs : obstacles) {
    // 确定模型类型和实例ID
    std::string modelId;
    if (obs.type == "truck") {
      modelId = "otruck"; // 默认使用不带挂车的模型
#if 0
      if(obs.has_trailer) {
        modelId = "wtruck"; // 带挂车的卡车
      } else {
        modelId = "otruck"; // 不带挂车的卡车
      }
#endif
    } else if (obs.type == "pedestrian") {
      modelId = "person";
    } else {
      modelId = "cube"; // 其他类型使用立方体显示
    }

    // 使用障碍物ID作为实例ID（确保唯一性）
    std::string instanceId = obs.type + obs.id + "_" + std::to_string(obs.has_trailer);
    processedInstanceIds.insert(instanceId);

    // 检查实例是否已存在
    bool isNewInstance = false;
    auto instance = modelManager.getModel(instanceId);
    if (!instance) {
      instance = modelManager.createInstance(instanceId, modelId);
      if (!instance) {
        std::cerr << "[UDP] Failed to create instance: " << instanceId << " for model: " << modelId << std::endl;
        continue;
      }
      isNewInstance = true;
    }

    // 设置目标状态
    ObjectState state;
    state.scale = glm::vec3(1.0f, 1.0f, 1.0f);
    state.visible = true;
    state.opacity = 1.0f;

    if (obs.type == "truck") {
      state.position = glm::vec3(-obs.position.x + 8.0f, 0.0f, obs.position.y);
      float target_yaw = obs.rotationY;
      float model_offset = 180.0f;
      state.rotation = glm::vec3(0.0f, target_yaw - model_offset, 0.0f);
    } else if (obs.type == "pedestrian") {
      state.position = glm::vec3(-obs.position.x + 8.0f, 0.0f, obs.position.y);
      state.rotation = glm::vec3(0.0f, 0.0f, 0.0f);
    } else if (obs.type == "other") {
      state.position = glm::vec3(-obs.position.x + 8.0f, 0.0f, obs.position.y);
      state.scale = glm::vec3(obs.size.x, 2.0f, obs.size.y);
      state.rotation = glm::vec3(0.0f, -90.0f, 0.0f);
    }

    if (isNewInstance) {
      // 新实例：直接设置到位
      instance->setPosition(state.position);
      instance->setRotation(state.rotation);
      instance->setScale(state.scale);
      instance->setVisible(state.visible);
      instance->setOpacity(state.opacity);
      setModelTransform(instanceId, state);
    } else {
      // 已有实例：用插值动画过渡，移动更流畅
      const float kAnimDuration = 0.12f;
      if (model_controller_) {
        model_controller_->animateTo(instanceId, state, kAnimDuration);
      } else {
        instance->setPosition(state.position);
        instance->setRotation(state.rotation);
        instance->setScale(state.scale);
        setModelTransform(instanceId, state);
      }
    }
  }

  // 清理不再存在的实例
  const std::vector<std::string> modelIds = {"otruck", "person", "wtruck", "cube"};
  for (const auto& modelId : modelIds) {
    auto instanceIds = modelManager.getInstanceIdsForModel(modelId);
    for (const auto& instanceId : instanceIds) {
      // 如果实例不在本次处理的集合中，则销毁它
      if (processedInstanceIds.find(instanceId) == processedInstanceIds.end()) {
        //std::cout << "[UDP] Destroying instance: " << instanceId << " (no longer in UDP data)" << std::endl;
        modelManager.destroyInstance(instanceId);
      }
    }
  }
  //std::cout << "[UDP] Update completed. Active instances: " << processedInstanceIds.size() << std::endl;
}



