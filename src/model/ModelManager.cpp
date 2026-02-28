#include "model/ModelManager.hpp"
#include <iostream>
#include <algorithm>

ModelManager* ModelManager::instance_ = nullptr;

ModelManager& ModelManager::getInstance() {
    if (!instance_) {
        instance_ = new ModelManager();
    }
    return *instance_;
}

ModelManager::ModelManager()
    : model_search_path_("./models/")
    , total_vertices_(0)
    , total_indices_(0)
    , total_textures_(0)
{
}

ModelManager::~ModelManager() {
    unloadAllModels();
    if (instance_) {
        delete instance_;
        instance_ = nullptr;
    }
}

void ModelManager::setModelSearchPath(const std::string& path) {
    model_search_path_ = path;
    if (!model_search_path_.empty() && model_search_path_.back() != '/') {
        model_search_path_ += '/';
    }
}

void ModelManager::registerModel(const std::string& modelId, std::shared_ptr<BaseModel> model) {
    if (model) {
        loaded_models_[modelId] = model;
        updateStatistics();
    }
}

std::shared_ptr<BaseModel> ModelManager::createInstance(
    const std::string& instanceId, const std::string& modelId) {

    // 检查实例ID是否已存在
    if (model_instances_.find(instanceId) != model_instances_.end()) {
        std::cerr << "Instance '" << instanceId << "' already exists" << std::endl;
        return nullptr;
    }

    auto it = loaded_models_.find(modelId);
    if (it == loaded_models_.end()) {
        std::cerr << "Model '" << modelId << "' not found" << std::endl;
        return nullptr;
    }

    // 克隆模型以创建真正的独立实例
    auto cloned = it->second->clone();
    if (!cloned) {
        std::cerr << "Failed to clone model '" << modelId << "'" << std::endl;
        return nullptr;
    }

    model_instances_[instanceId] = cloned;

    // 维护模型到实例的映射关系
    model_to_instances_[modelId].push_back(instanceId);

    // 维护实例到模型的映射关系
    instance_to_model_[instanceId] = modelId;

    std::cout << "ModelManager: Created instance '" << instanceId << "' from model '" << modelId << "'" << std::endl;

    return cloned;
}

std::shared_ptr<BaseModel> ModelManager::getModel(const std::string& id) {
    auto instance_it = model_instances_.find(id);
    if (instance_it != model_instances_.end()) {
        return instance_it->second;  // 直接返回shared_ptr
    }

    auto model_it = loaded_models_.find(id);
    if (model_it != loaded_models_.end()) {
        return model_it->second;
    }

    return nullptr;
}

void ModelManager::unloadModel(const std::string& id) {
    auto it = loaded_models_.find(id);
    if (it != loaded_models_.end()) {
        removeFromStatistics(it->second);
        loaded_models_.erase(it);
        std::cout << "ModelManager: Unloaded model '" << id << "'" << std::endl;
    } else {
        std::cerr << "ModelManager: Model '" << id << "' not found" << std::endl;
    }

    // 清理模型到实例的映射关系
    auto model_instances_it = model_to_instances_.find(id);
    if (model_instances_it != model_to_instances_.end()) {
        // 从实例到模型的映射中删除这些实例
        for (const auto& instanceId : model_instances_it->second) {
            instance_to_model_.erase(instanceId);
        }
        // 删除模型到实例的映射
        model_to_instances_.erase(model_instances_it);
        std::cout << "ModelManager: Cleared mapping for model '" << id << "' (instances become orphaned)" << std::endl;
    }

    // 注意：这里只删除与模型ID相同的实例（默认实例）
    // 用户通过createInstance创建的不同ID实例不会自动删除
    // 这可能导致悬空实例引用已卸载的模型
    // 使用destroyInstance或destroyAllInstances手动管理实例生命周期
    auto instance_it = model_instances_.find(id);
    if (instance_it != model_instances_.end()) {
        model_instances_.erase(instance_it);
        std::cout << "ModelManager: Also removed default instance with same ID '" << id << "'" << std::endl;
    }
}

void ModelManager::unloadAllModels() {
    loaded_models_.clear();
    model_instances_.clear();
    model_to_instances_.clear();
    instance_to_model_.clear();
    total_vertices_ = 0;
    total_indices_ = 0;
    total_textures_ = 0;
}

bool ModelManager::hasModel(const std::string& id) const {
    return loaded_models_.find(id) != loaded_models_.end() ||
           model_instances_.find(id) != model_instances_.end();
}

std::vector<std::string> ModelManager::getAllModelIds() const {
    std::vector<std::string> ids;
    ids.reserve(loaded_models_.size());
    for (const auto& pair : loaded_models_) {
        ids.push_back(pair.first);
    }
    return ids;
}

size_t ModelManager::getModelCount() const {
    return loaded_models_.size();
}

void ModelManager::renderAll(const glm::mat4& view, const glm::mat4& projection) {
    for (auto& pair : model_instances_) {
        const std::string& instanceId = pair.first;
        auto model = pair.second;  // shared_ptr，无需lock()

        if (model) {
            if (model->isVisible()) {
                model->render(view, projection);
            }
        }
    }
}

void ModelManager::renderModel(const std::string& id, const glm::mat4& view, const glm::mat4& projection) {
    auto model = getModel(id);
    if (model && model->isVisible()) {
        model->render(view, projection);
    }
}

void ModelManager::updateAll(float deltaTime) {
    for (auto& pair : loaded_models_) {
        pair.second->update(deltaTime);
    }
    for (auto& pair : model_instances_) {
        auto model = pair.second;  // shared_ptr
        if (model) {
            model->update(deltaTime);
        }
    }
}

void ModelManager::updateModel(const std::string& id, float deltaTime) {
    auto model = getModel(id);
    if (model) {
        model->update(deltaTime);
    }
}

std::string ModelManager::resolvePath(const std::string& filepath) {
    if (filepath.empty()) return filepath;
    if (filepath[0] == '/') return filepath;
    return model_search_path_ + filepath;
}

void ModelManager::updateStatistics() {
    total_vertices_ = 0;
    total_indices_ = 0;
    total_textures_ = 0;
    for (const auto& pair : loaded_models_) {
        total_vertices_ += pair.second->getVertexCount();
        total_indices_ += pair.second->getIndexCount();
        total_textures_ += pair.second->getTextureCount();
    }
}

void ModelManager::removeFromStatistics(const std::shared_ptr<BaseModel>& model) {
    if (model) {
        total_vertices_ -= model->getVertexCount();
        total_indices_ -= model->getIndexCount();
        total_textures_ -= model->getTextureCount();
    }
}

std::shared_ptr<BaseModel> ModelManager::loadModel(const std::string& modelId, const std::string& filepath) {
    // 检查是否已加载
    if (hasModel(modelId)) {
        std::cout << "Model '" << modelId << "' is already loaded" << std::endl;
        return getModel(modelId);
    }

    std::cout << "ModelManager: Loading model '" << modelId << "' from " << filepath << std::endl;

    // 使用ModelImporter导入模型
    auto& importer = ModelImporter::getInstance();
    auto model = importer.importModel(modelId, filepath);

    if (model) {
        // 注册到ModelManager
        registerModel(modelId, model);

        // 自动创建同名实例以便渲染和访问
        if (model_instances_.find(modelId) == model_instances_.end()) {
            model_instances_[modelId] = model;
            // 将默认实例添加到映射关系
            model_to_instances_[modelId].push_back(modelId);
            instance_to_model_[modelId] = modelId;
            std::cout << "ModelManager: Created default instance '" << modelId << "'" << std::endl;
        }

        std::cout << "ModelManager: Model '" << modelId << "' loaded successfully" << std::endl << std::endl;
        return model;
    } else {
        std::cerr << "ModelManager: Failed to load model '" << modelId << "' from " << filepath << std::endl << std::endl;
        return nullptr;
    }
}

void ModelManager::destroyInstance(const std::string& instanceId) {
    auto it = model_instances_.find(instanceId);
    if (it != model_instances_.end()) {
        // 从模型到实例的映射中删除
        auto model_it = instance_to_model_.find(instanceId);
        if (model_it != instance_to_model_.end()) {
            const std::string& modelId = model_it->second;
            auto& instances = model_to_instances_[modelId];
            instances.erase(std::remove(instances.begin(), instances.end(), instanceId), instances.end());

            // 如果该模型的实例列表为空，可以清理空向量（可选）
            if (instances.empty()) {
                model_to_instances_.erase(modelId);
            }

            // 从实例到模型的映射中删除
            instance_to_model_.erase(model_it);
        }

        model_instances_.erase(it);
        std::cout << "ModelManager: Destroyed instance '" << instanceId << "'" << std::endl;
    } else {
        std::cerr << "ModelManager: Instance '" << instanceId << "' not found" << std::endl;
    }
}

void ModelManager::destroyAllInstances() {
    size_t count = model_instances_.size();
    model_instances_.clear();
    model_to_instances_.clear();
    instance_to_model_.clear();
    std::cout << "ModelManager: Destroyed all " << count << " instances" << std::endl;
}

void ModelManager::destroyInstancesOfModel(const std::string& modelId) {
    auto it = model_to_instances_.find(modelId);
    if (it == model_to_instances_.end()) {
        std::cout << "ModelManager: No instances found for model '" << modelId << "'" << std::endl;
        return;
    }

    // 复制实例ID列表，因为destroyInstance会修改原始列表
    std::vector<std::string> instanceIds = it->second;
    size_t count = instanceIds.size();

    for (const auto& instanceId : instanceIds) {
        destroyInstance(instanceId);
    }

    std::cout << "ModelManager: Destroyed " << count << " instances of model '" << modelId << "'" << std::endl;
}

size_t ModelManager::getInstanceCountForModel(const std::string& modelId) const {
    auto it = model_to_instances_.find(modelId);
    if (it != model_to_instances_.end()) {
        return it->second.size();
    }
    return 0;
}

std::vector<std::string> ModelManager::getInstanceIdsForModel(const std::string& modelId) const {
    auto it = model_to_instances_.find(modelId);
    if (it != model_to_instances_.end()) {
        return it->second; // 返回副本
    }
    return std::vector<std::string>();
}
