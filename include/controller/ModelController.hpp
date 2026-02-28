#pragma once

#include <unordered_map>
#include <string>
#include <memory>
#include <glm/glm.hpp>
#include <functional>

// 对象状态结构
struct ObjectState {
    glm::vec3 position{0.0f, 0.0f, 0.0f};
    glm::vec3 rotation{0.0f, 0.0f, 0.0f};  // 欧拉角
    glm::vec3 scale{1.0f, 1.0f, 1.0f};
    bool visible{true};
    float opacity{1.0f};

    // 构造函数便于使用
    ObjectState() = default;
    ObjectState(const glm::vec3& pos) : position(pos) {}
    ObjectState(const glm::vec3& pos, const glm::vec3& rot)
        : position(pos), rotation(rot) {}
};

// 动画参数结构
struct AnimationParams {
    int animation_index{-1};
    float animation_time{0.0f};
    std::vector<float> morph_weights;
    bool loop{true};
};

class ModelController {
private:
    // 对象状态映射
    std::unordered_map<std::string, ObjectState> object_states_;

    // 动画状态
    std::unordered_map<std::string, AnimationParams> animation_states_;

    // 变换插值
    std::unordered_map<std::string, ObjectState> target_states_;
    std::unordered_map<std::string, float> interpolation_progress_; // 经过的时间（秒）
    std::unordered_map<std::string, float> animation_durations_;    // 动画持续时间（秒）

    // 回调函数
    std::function<void(const std::string&, const ObjectState&)> state_change_callback_;

public:
    ModelController();
    ~ModelController();

    // 核心控制接口
    void setObjectState(const std::string& objectId, const ObjectState& state);
    ObjectState getObjectState(const std::string& objectId) const;

    // 批量操作
    void batchUpdate(const std::unordered_map<std::string, ObjectState>& states);
    std::unordered_map<std::string, ObjectState> getAllStates() const;

    // 动画控制
    void setAnimation(const std::string& objectId, int animationIndex);
    void setAnimationTime(const std::string& objectId, float time);
    void setMorphTargets(const std::string& objectId, const std::vector<float>& weights);

    // 插值动画
    void animateTo(const std::string& objectId, const ObjectState& target, float duration);
    void animateBatch(const std::unordered_map<std::string, ObjectState>& targets, float duration);

    // 查询接口
    bool hasObject(const std::string& objectId) const;
    std::vector<std::string> getAllObjectIds() const;

    // 移动控制
    void moveForward(const std::string& objectId, float distance, float duration = 1.0f);

    // 回调注册
    void setStateChangeCallback(std::function<void(const std::string&, const ObjectState&)> callback);

    // 更新循环
    void update(float deltaTime);

private:
    // 内部辅助方法
    void interpolateStates(float deltaTime);
    ObjectState interpolate(const ObjectState& from, const ObjectState& to, float t);
    float easeInOut(float t);
};