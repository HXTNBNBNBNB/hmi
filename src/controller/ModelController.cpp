#include "controller/ModelController.hpp"
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>
#include <cmath>
#include <algorithm>
#include <iostream>

ModelController::ModelController() {
}

ModelController::~ModelController() {
}

void ModelController::setObjectState(const std::string& objectId, const ObjectState& state) {
    object_states_[objectId] = state;

    // 触发回调
    if (state_change_callback_) {
        state_change_callback_(objectId, state);
    }
}

ObjectState ModelController::getObjectState(const std::string& objectId) const {
    auto it = object_states_.find(objectId);
    if (it != object_states_.end()) {
        return it->second;
    }
    return ObjectState(); // 返回默认状态
}

void ModelController::batchUpdate(const std::unordered_map<std::string, ObjectState>& states) {
    for (const auto& pair : states) {
        setObjectState(pair.first, pair.second);
    }
}

std::unordered_map<std::string, ObjectState> ModelController::getAllStates() const {
    return object_states_;
}

void ModelController::setAnimation(const std::string& objectId, int animationIndex) {
    animation_states_[objectId].animation_index = animationIndex;
}

void ModelController::setAnimationTime(const std::string& objectId, float time) {
    animation_states_[objectId].animation_time = time;
}

void ModelController::setMorphTargets(const std::string& objectId, const std::vector<float>& weights) {
    animation_states_[objectId].morph_weights = weights;
}

void ModelController::animateTo(const std::string& objectId, const ObjectState& target, float duration) {
    if (duration <= 0.0f) {
        setObjectState(objectId, target);
        return;
    }

    target_states_[objectId] = target;
    interpolation_progress_[objectId] = 0.0f; // 经过时间重置
    animation_durations_[objectId] = duration; // 存储持续时间
}

void ModelController::animateBatch(const std::unordered_map<std::string, ObjectState>& targets, float duration) {
    for (const auto& pair : targets) {
        animateTo(pair.first, pair.second, duration);
    }
}

bool ModelController::hasObject(const std::string& objectId) const {
    return object_states_.find(objectId) != object_states_.end();
}

std::vector<std::string> ModelController::getAllObjectIds() const {
    std::vector<std::string> ids;
    ids.reserve(object_states_.size());

    for (const auto& pair : object_states_) {
        ids.push_back(pair.first);
    }

    return ids;
}

void ModelController::setStateChangeCallback(std::function<void(const std::string&, const ObjectState&)> callback) {
    state_change_callback_ = callback;
}

void ModelController::update(float deltaTime) {
    interpolateStates(deltaTime);
}

void ModelController::interpolateStates(float deltaTime) {
    std::vector<std::string> completed_objects;

    for (auto& pair : interpolation_progress_) {
        const std::string& objectId = pair.first;
        float& progress = pair.second;

        // 更新经过时间
        progress += deltaTime;

        auto target_it = target_states_.find(objectId);
        auto current_it = object_states_.find(objectId);
        auto duration_it = animation_durations_.find(objectId);

        if (target_it != target_states_.end() && current_it != object_states_.end() && duration_it != animation_durations_.end()) {
            ObjectState current_state = current_it->second;
            ObjectState target_state = target_it->second;
            float duration = duration_it->second;

            // 计算插值因子（0到1）
            float t = std::min(progress / duration, 1.0f);
            t = easeInOut(t);

            // 执行插值
            ObjectState interpolated = interpolate(current_state, target_state, t);
            setObjectState(objectId, interpolated);

            // 检查是否完成
            if (progress >= duration) {
                completed_objects.push_back(objectId);
            }
        }
    }

    // 清理已完成的插值
    for (const std::string& objectId : completed_objects) {
        interpolation_progress_.erase(objectId);
        target_states_.erase(objectId);
        animation_durations_.erase(objectId);
    }
}

ObjectState ModelController::interpolate(const ObjectState& from, const ObjectState& to, float t) {
    ObjectState result;

    // 线性插值位置和缩放
    result.position = glm::mix(from.position, to.position, t);
    result.scale = glm::mix(from.scale, to.scale, t);

    // 球面插值旋转（避免万向节死锁）
    glm::quat from_quat = glm::quat(glm::radians(from.rotation));
    glm::quat to_quat = glm::quat(glm::radians(to.rotation));
    glm::quat interp_quat = glm::slerp(from_quat, to_quat, t);
    result.rotation = glm::degrees(glm::eulerAngles(interp_quat));

    // 线性插值其他属性
    result.visible = (t < 0.5f) ? from.visible : to.visible;
    result.opacity = glm::mix(from.opacity, to.opacity, t);

    return result;
}

void ModelController::moveForward(const std::string& objectId, float distance, float duration) {
    if (!hasObject(objectId)) {
        std::cout << "ModelController: 对象不存在: " << objectId << std::endl;
        return;
    }

    ObjectState current = getObjectState(objectId);
    float yawRad = glm::radians(current.rotation.y);

    // 坐标系约定：
    // - 前方 = -X
    // - 上方 = +Y
    // - 左侧 = +Z
    // 所以：forward = (-cos(yaw), 0, sin(yaw))
    glm::vec3 forwardDir = glm::vec3(
        -cos(yawRad),  // X: 负方向为前
         0.0f,         // Y: 无垂直移动
         sin(yawRad)   // Z: 左转时 Z 增加（左）
    );

    ObjectState target = current;
    target.position += forwardDir * distance;

    animateTo(objectId, target, duration);

    std::cout << "ModelController: 移动对象 " << objectId
              << " 距离 " << distance
              << " 方向 (" << forwardDir.x << ", " << forwardDir.y << ", " << forwardDir.z << ")"
              << " 从 (" << current.position.x << ", " << current.position.y << ", " << current.position.z << ")"
              << " 到 (" << target.position.x << ", " << target.position.y << ", " << target.position.z << ")"
              << std::endl;
}

float ModelController::easeInOut(float t) {
    // 缓入缓出函数
    return t < 0.5f ? 2.0f * t * t : 1.0f - pow(-2.0f * t + 2.0f, 2.0f) / 2.0f;
}