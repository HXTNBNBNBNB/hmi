#pragma once

#include <mutex>
#include <thread>
#include <atomic>
#include <unordered_map>
#include <unordered_set>
#include <string>
#include <json/json.h>
#include <glm/glm.hpp>
#include "ModelController.hpp"

// 网络相关头文件
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

class Scene; // 前向声明

// UDP解析后的障碍物数据
struct ProcessedUdpObstacle {
    std::string id;       // 障碍物ID
    std::string type;     // "truck" 、 "pedestrian" 、 "other"
    glm::vec3 position;   // 转换前的原始坐标
    float rotationY;      // 旋转角度
    bool has_trailer;     // 是否有车挂 只有type为truck时才有意义
    glm::vec3 size;       // 障碍物长宽高（可选，根据需要添加）
};

struct VoiceAlarm {
    std::string direction; // 方位
    std::string type; // "low"、"medium"、"high"
    float distance; // 距离
    int priority; // 声音报警优先级 1-3
};

class UDPDataManager {
public:
    static UDPDataManager& getInstance();

    // 启动/停止UDP接收线程
    bool start(int port = 8765);
    void stop();

    // 主线程消费更新
    void consumeUpdates(Scene& scene);

    // 获取警告信息（最多3条）
    std::vector<std::string> getWarnings() const;

    // 消费一次声音报警（读取并清除，线程安全；无新报警时返回 false）
    bool tryConsumeVoiceAlarm(VoiceAlarm& out);

    // 获取状态信息
    bool isRunning() const { return running_; }
    size_t getUpdateCount() const { return update_count_; }

private:
    static UDPDataManager* instance_;

    UDPDataManager();
    ~UDPDataManager();

    // 禁止复制
    UDPDataManager(const UDPDataManager&) = delete;
    UDPDataManager& operator=(const UDPDataManager&) = delete;

    // UDP接收线程函数
    void receiverThread(int port);

    // 数据处理
    bool parseJsonData(const std::string& jsonStr);

    // 线程安全数据访问
    bool tryUpdateData(const std::vector<ProcessedUdpObstacle>& obstacles);
    bool tryConsumeData(std::vector<ProcessedUdpObstacle>& outData);

private:
    std::mutex data_mutex_;
    std::vector<ProcessedUdpObstacle> latest_data_;
    std::atomic<bool> data_updated_{false};

    // 警告信息（最多3条）
    mutable std::mutex warnings_mutex_;
    std::vector<std::string> warnings_;

    VoiceAlarm voice_alarm_;          // 声音报警信息
    bool voice_alarm_pending_{false};   // 是否有待消费的报警
    mutable std::mutex voice_alarm_mutex_; // 保护 voice_alarm_ 读写

    std::thread receiver_thread_;
    std::atomic<bool> running_{false};
    std::atomic<size_t> update_count_{0};

    // 旋转平滑滤波器
    std::unordered_map<std::string, float> rotation_history_;  // 存储每个障碍物的平滑后旋转值
    float smoothing_factor_ = 0.15f;  // 平滑因子 (0-1)，越小越平滑

    // 角度平滑辅助函数
    float smoothAngle(const std::string& id, float newAngle);
};