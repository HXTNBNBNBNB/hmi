#pragma once

#include <unordered_map>
#include <string>
#include <memory>
#include <vector>
#include "BaseModel.hpp"
#include "importer/ModelImporter.hpp"

class ModelManager {
private:
    static ModelManager* instance_;

    // 模型存储
    std::unordered_map<std::string, std::shared_ptr<BaseModel>> loaded_models_;

    // 模型实例（同一模型可以有多个实例）
    std::unordered_map<std::string, std::shared_ptr<BaseModel>> model_instances_;

    // 模型到实例的映射关系
    std::unordered_map<std::string, std::vector<std::string>> model_to_instances_;

    // 实例到模型的映射关系（快速反向查找）
    std::unordered_map<std::string, std::string> instance_to_model_;

    // 资源路径配置
    std::string model_search_path_;

    // 统计信息
    size_t total_vertices_;
    size_t total_indices_;
    size_t total_textures_;

public:
    static ModelManager& getInstance();

    // 初始化和配置
    void setModelSearchPath(const std::string& path);
    const std::string& getModelSearchPath() const { return model_search_path_; }

    // 模型加载接口
    template<typename ModelType>
    std::shared_ptr<ModelType> loadModel(const std::string& modelId, const std::string& filepath);
    std::shared_ptr<BaseModel> loadModel(const std::string& modelId, const std::string& filepath);

    // 外部注册已加载的模型
    void registerModel(const std::string& modelId, std::shared_ptr<BaseModel> model);

    // 实例化接口
    std::shared_ptr<BaseModel> createInstance(const std::string& instanceId, const std::string& modelId);

    // 获取模型接口
    std::shared_ptr<BaseModel> getModel(const std::string& id);
    template<typename ModelType>
    std::shared_ptr<ModelType> getModelAs(const std::string& id);

    // 管理接口
    void unloadModel(const std::string& id);
    void unloadAllModels();
    bool hasModel(const std::string& id) const;

    // 实例管理接口
    void destroyInstance(const std::string& instanceId);
    void destroyAllInstances();
    void destroyInstancesOfModel(const std::string& modelId);

    // 实例查询接口
    size_t getInstanceCountForModel(const std::string& modelId) const;
    std::vector<std::string> getInstanceIdsForModel(const std::string& modelId) const;

    // 批量操作
    std::vector<std::string> getAllModelIds() const;
    size_t getModelCount() const;

    // 渲染接口
    void renderAll(const glm::mat4& view, const glm::mat4& projection);
    void renderModel(const std::string& id, const glm::mat4& view, const glm::mat4& projection);

    // 更新接口
    void updateAll(float deltaTime);
    void updateModel(const std::string& id, float deltaTime);

    // 统计信息
    size_t getTotalVertices() const { return total_vertices_; }
    size_t getTotalIndices() const { return total_indices_; }
    size_t getTotalTextures() const { return total_textures_; }

private:
    ModelManager();
    ~ModelManager();

    // 内部辅助方法
    std::string resolvePath(const std::string& filepath);
    void updateStatistics();
    void removeFromStatistics(const std::shared_ptr<BaseModel>& model);
};

// 模板函数实现
template<typename ModelType>
std::shared_ptr<ModelType> ModelManager::loadModel(const std::string& modelId, const std::string& filepath) {
    auto model = loadModel(modelId, filepath); // 调用非模板版本
    return std::dynamic_pointer_cast<ModelType>(model);
}

template<typename ModelType>
std::shared_ptr<ModelType> ModelManager::getModelAs(const std::string& id) {
    auto model = getModel(id);
    return std::dynamic_pointer_cast<ModelType>(model);
}