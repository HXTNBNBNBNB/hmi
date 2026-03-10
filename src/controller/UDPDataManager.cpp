#include "controller/UDPDataManager.hpp"
#include "Scene.hpp"
#include "model/ModelManager.hpp"
#include <iostream>
#include <sstream>
#include <cstring>
#include <cerrno>
#include <fcntl.h>
#include <functional>
#include <cmath>

UDPDataManager* UDPDataManager::instance_ = nullptr;

// 角度平滑：处理角度环绕问题并应用指数平滑
float UDPDataManager::smoothAngle(const std::string& id, float newAngle) {
    auto it = rotation_history_.find(id);
    if (it == rotation_history_.end()) {
        // 首次出现，直接使用新值
        rotation_history_[id] = newAngle;
        return newAngle;
    }

    float oldAngle = it->second;

    // 处理角度环绕 (-180 到 180 或 0 到 360)
    float diff = newAngle - oldAngle;

    // 将差值归一化到 [-180, 180]
    while (diff > 180.0f) diff -= 360.0f;
    while (diff < -180.0f) diff += 360.0f;

    // 指数平滑
    float smoothedAngle = oldAngle + diff * smoothing_factor_;

    // 归一化结果
    while (smoothedAngle > 360.0f) smoothedAngle -= 360.0f;
    while (smoothedAngle < 0.0f) smoothedAngle += 360.0f;

    rotation_history_[id] = smoothedAngle;
    return smoothedAngle;
}

UDPDataManager& UDPDataManager::getInstance() {
    if (!instance_) {
        instance_ = new UDPDataManager();
    }
    return *instance_;
}

UDPDataManager::UDPDataManager() {
    // 构造函数留空，所有成员都有默认初始化
}

UDPDataManager::~UDPDataManager() {
    stop();
}

bool UDPDataManager::start(int port) {
    if (running_) {
        std::cout << "UDPDataManager: Already running" << std::endl;
        return true;
    }

    running_ = true;
    receiver_thread_ = std::thread(&UDPDataManager::receiverThread, this, port);

    std::cout << "UDPDataManager: Started UDP receiver on port " << port << std::endl;
    return true;
}

void UDPDataManager::stop() {
    if (!running_) return;

    running_ = false;
    if (receiver_thread_.joinable()) {
        receiver_thread_.join();
    }

    std::cout << "UDPDataManager: Stopped" << std::endl;
}

void UDPDataManager::receiverThread(int port) {
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        std::cerr << "UDPDataManager: Failed to create socket: " << strerror(errno) << std::endl;
        return;
    }

    // 设置socket为非阻塞
    int flags = fcntl(sockfd, F_GETFL, 0);
    fcntl(sockfd, F_SETFL, flags | O_NONBLOCK);

    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(port);

    if (bind(sockfd, (sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
        std::cerr << "UDPDataManager: Failed to bind socket on port " << port
                  << ": " << strerror(errno) << std::endl;
        close(sockfd);
        return;
    }

    std::cout << "UDPDataManager: Listening on port " << port << std::endl;

    char buffer[4096];
    while (running_) {
        sockaddr_in clientAddr{};
        socklen_t clientLen = sizeof(clientAddr);

        // 非阻塞接收，避免长时间阻塞
        int recvLen = recvfrom(sockfd, buffer, sizeof(buffer)-1, 0,
                              (sockaddr*)&clientAddr, &clientLen);

        if (recvLen > 0) {
            buffer[recvLen] = '\0';
            std::string jsonStr(buffer, recvLen);

            // 调试输出
            //std::cout << "UDPDataManager: Received " << recvLen << " bytes" << std::endl;

            // 尝试解析并更新数据
            if (!parseJsonData(jsonStr)) {
                std::cerr << "UDPDataManager: Failed to parse JSON data" << std::endl;
            }
        } else if (recvLen < 0) {
            // 非阻塞socket在无数据时会返回EAGAIN/EWOULDBLOCK
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                std::cerr << "UDPDataManager: recvfrom error: " << strerror(errno) << std::endl;
            }
        }

        // 短暂休眠，避免CPU占用过高
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    close(sockfd);
    std::cout << "UDPDataManager: Receiver thread stopped" << std::endl;
}

bool UDPDataManager::tryUpdateData(const std::vector<ProcessedUdpObstacle>& obstacles) {
    std::unique_lock<std::mutex> lock(data_mutex_, std::try_to_lock);
    if (!lock.owns_lock()) {
        // 锁被占用，丢弃这包数据
        std::cout << "UDPDataManager: Lock busy, dropping UDP data" << std::endl;
        return false;
    }

    latest_data_ = obstacles;
    data_updated_ = true;
    update_count_++;
    return true;
}

bool UDPDataManager::tryConsumeData(std::vector<ProcessedUdpObstacle>& outData) {
    std::unique_lock<std::mutex> lock(data_mutex_, std::try_to_lock);
    if (!lock.owns_lock()) {
        // 锁被占用，跳过本次更新
        return false;
    }

    if (!data_updated_) {
        // 没有新数据
        return false;
    }

    outData = std::move(latest_data_);
    latest_data_.clear();
    data_updated_ = false;
    return true;
}

bool UDPDataManager::parseJsonData(const std::string& jsonStr) {
    Json::Value root;
    Json::CharReaderBuilder builder;
    std::string errors;
    std::istringstream jsonStream(jsonStr);

    if (!Json::parseFromStream(builder, jsonStream, &root, &errors)) {
        std::cerr << "UDPDataManager: JSON parse error: " << errors << std::endl;
        return false;
    }

    // 解析警告信息（最多3条）- 从 alarms 数组提取 text 字段
    if (root.isMember("alarms") && root["alarms"].isArray()) {
        std::lock_guard<std::mutex> lock(warnings_mutex_);
        warnings_.clear();
        const Json::Value& alarmsArray = root["alarms"];
        for (Json::ArrayIndex i = 0; i < alarmsArray.size() && i < 3; ++i) {
            if (alarmsArray[i].isMember("text") && alarmsArray[i]["text"].isString()) {
                warnings_.push_back(alarmsArray[i]["text"].asString());
            }
        }
    }
#if 1
    if (root.isMember("voice_alarm") &&
    root["voice_alarm"].isMember("type") &&
    root["voice_alarm"].isMember("priority") &&
    root["voice_alarm"].isMember("distance") &&
    root["voice_alarm"].isMember("direction")) {
        std::lock_guard<std::mutex> lock(voice_alarm_mutex_);
        voice_alarm_.type = root["voice_alarm"]["type"].asString();
        voice_alarm_.priority = root["voice_alarm"]["priority"].asInt();
        voice_alarm_.distance = root["voice_alarm"]["distance"].asFloat();
        voice_alarm_.direction = root["voice_alarm"]["direction"].asString();
        voice_alarm_pending_ = true;  // 标记有待播报警
    } else {
        std::lock_guard<std::mutex> lock(voice_alarm_mutex_);
        // 只有当前无待播报警时才清除，避免覆盖未消费的报警
        if (!voice_alarm_pending_) {
            voice_alarm_.direction.clear();
            voice_alarm_.distance = 0.0f;
            voice_alarm_.priority = 0;
            voice_alarm_.type.clear();
        }
    }
#endif
    // 检查是否有障碍物数据
    const Json::Value& obstacles = root["obstacles"];
    if (!obstacles.isArray()) {
        std::cerr << "UDPDataManager: No obstacles array in JSON" << std::endl;
        return false;
    }

    //std::cout << std::endl << "UDPDataManager: Parsing " << obstacles.size() << " obstacles" << std::endl;

    std::vector<ProcessedUdpObstacle> processed_obstacles;

    for (const auto& obs : obstacles) {
        // 检查必要字段
        if (!obs.isMember("id") || !obs.isMember("type") || !obs.isMember("rotationY") ||
            !obs.isMember("x") || !obs.isMember("y") || !obs.isMember("z") ||
            !obs.isMember("length") || !obs.isMember("width") || !obs.isMember("height")
          ) {
            std::cerr << "UDPDataManager: Missing required fields in obstacle" << std::endl;
            continue;
        }

        std::string type = obs["type"].asString();
        std::string id = obs["id"].asString();
        bool has_trailer = obs.isMember("has_trailer") ? obs["has_trailer"].asBool() : false;

        // 转换坐标 比例尺0.1，根据实际情况调整
        glm::vec3 convertedPos = glm::vec3(
            obs["x"].asFloat() * 0.4,
            obs["y"].asFloat() * 0.4,
            obs["z"].asFloat() * 0.4
        );

        ProcessedUdpObstacle pobs;
        pobs.type = type;
        pobs.id = id;
        pobs.has_trailer = has_trailer; // 只有truck类型才需要用到车挂信息 区分使用的模型
        pobs.position = glm::vec3(convertedPos.x, convertedPos.y, convertedPos.z);
        pobs.size = glm::vec3(obs["length"].asFloat() * 0.4, obs["width"].asFloat() * 0.4, obs["height"].asFloat() * 0.4); // 根据实际情况调整比例

        // 对旋转角度应用平滑滤波
        float rawRotation = obs.isMember("rotationY") ? obs["rotationY"].asFloat() : 0.0f;
        std::string smoothKey = type + id;  // 使用 type+id 作为唯一标识
        pobs.rotationY = smoothAngle(smoothKey, rawRotation);

        processed_obstacles.push_back(pobs);
        // std::cout << "Processed obstacle: " << pobs.type << " " << pobs.position.x << " " << pobs.position.y << " " << pobs.position.z;
        // std::cout << "  Size: " << pobs.size.x << " " << pobs.size.y << " " << pobs.size.z << std::endl;
        // std::cout << "  RotationY: " << pobs.rotationY << std::endl;
    }

    if (!processed_obstacles.empty()) {
        if (tryUpdateData(processed_obstacles)) {
            // std::cout << "UDPDataManager: Updated " << processed_obstacles.size() << " obstacles" << std::endl << std::endl;
            return true;
        }
    }

    return false;
}

void UDPDataManager::consumeUpdates(Scene& scene) {
    std::vector<ProcessedUdpObstacle> newData;
    if (tryConsumeData(newData)) {
        scene.updateFromUdpData(newData);
        //std::cout << "UDPDataManager: Applied " << newData.size() << " updates" << std::endl;
    }
}

std::vector<std::string> UDPDataManager::getWarnings() const {
    std::lock_guard<std::mutex> lock(warnings_mutex_);
    return warnings_;
}

bool UDPDataManager::tryConsumeVoiceAlarm(VoiceAlarm& out) {
    std::lock_guard<std::mutex> lock(voice_alarm_mutex_);
    if (!voice_alarm_pending_) {
        return false;
    }
    out = voice_alarm_;
    voice_alarm_pending_ = false;  // 消费后清除标志，下次只有新事件才会再次触发
    return true;
}