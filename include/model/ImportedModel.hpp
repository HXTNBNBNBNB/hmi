#pragma once

#include "../model/BaseModel.hpp"

class ImportedModel : public BaseModel {
private:
    std::string source_file_;
    std::string model_format_;

public:
    ImportedModel();
    ~ImportedModel() override;

    // 实现抽象接口
    bool load(const std::string& filepath) override;
    void render(const glm::mat4& view, const glm::mat4& projection) override;
    void update(float deltaTime) override;

    // 特有功能
    void setSourceFile(const std::string& filepath);
    const std::string& getSourceFile() const { return source_file_; }

    void setModelFormat(const std::string& format);
    const std::string& getModelFormat() const { return model_format_; }

    // 数据设置接口
    void setMeshData(const std::vector<Vertex>& vertices, const std::vector<unsigned int>& indices);
    void setMaterial(const Material& material);

    // 克隆接口实现
    std::shared_ptr<BaseModel> clone() const override;

private:
    void setupMesh();
};