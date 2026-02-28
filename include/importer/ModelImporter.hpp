#pragma once

#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include "../model/ImportedModel.hpp"

class ModelImporter {
private:
    static ModelImporter* instance_;

    // 导入配置
    std::string model_search_path_;
    std::vector<std::string> supported_formats_;

    // 已导入的模型缓存
    std::unordered_map<std::string, std::shared_ptr<BaseModel>> imported_models_;

    // 统计信息
    size_t total_imported_;
    size_t successful_imports_;
    size_t failed_imports_;

public:
    static ModelImporter& getInstance();

    // 初始化和配置
    void initialize();
    void setModelSearchPath(const std::string& path);
    void addSupportedFormat(const std::string& format);

    // 核心导入接口
    std::shared_ptr<BaseModel> importModel(const std::string& model_id, const std::string& filepath);
    std::shared_ptr<BaseModel> importModelFromMemory(const std::string& model_id, const std::string& data);

    // 批量导入
    std::unordered_map<std::string, std::shared_ptr<BaseModel>>
    importMultipleModels(const std::vector<std::pair<std::string, std::string>>& model_list);

    // 模型管理
    std::shared_ptr<BaseModel> getModel(const std::string& model_id);
    void removeModel(const std::string& model_id);
    void removeAllModels();

    // 查询接口
    bool hasModel(const std::string& model_id) const;
    std::vector<std::string> getAllModelIds() const;
    size_t getModelCount() const;

    // 统计信息
    size_t getTotalImported() const { return total_imported_; }
    size_t getSuccessfulImports() const { return successful_imports_; }
    size_t getFailedImports() const { return failed_imports_; }
    float getSuccessRate() const;

    // 实用工具
    std::vector<std::string> scanForModels(const std::string& directory) const;
    bool isValidModelFile(const std::string& filepath) const;
    std::string getFileExtension(const std::string& filepath) const;

private:
    ModelImporter();
    ~ModelImporter();

    // 内部导入方法
    std::shared_ptr<BaseModel> importGLTFModel(const std::string& model_id, const std::string& filepath);
    std::shared_ptr<BaseModel> importOBJModel(const std::string& model_id, const std::string& filepath);

    // 辅助方法
    std::string resolveFilePath(const std::string& filepath);
};