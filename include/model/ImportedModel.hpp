#pragma once

#include "../model/BaseModel.hpp"
#include "../loader/GLTFLoader.hpp"

// GPU 子网格数据
struct GPUSubMesh {
    GLuint vbo = 0;
    GLuint ebo = 0;
    size_t indexCount = 0;
    size_t vertexCount = 0;
    PBRMaterial material;
};

class ImportedModel : public BaseModel {
private:
    std::string source_file_;
    std::string model_format_;

    // 新增：子网格列表（每个有独立材质）
    std::vector<GPUSubMesh> submeshes_;

    // 标记是否为克隆实例（克隆实例不拥有 VBO/EBO，不应删除）
    bool is_clone_ = false;

    // 静态着色器程序（所有实例共享）
    static GLuint shader_program_;
    static GLint model_loc_, view_loc_, projection_loc_;
    static GLint baseColorFactor_loc_, metallicFactor_loc_, roughnessFactor_loc_;
    static GLint emissiveFactor_loc_;
    static GLint lightDir_loc_, viewPos_loc_;
    static bool shader_initialized_;

    static void initShader();

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
    void setSubMeshes(const std::vector<SubMesh>& submeshes, const std::vector<PBRMaterial>& materials);
    void setMaterial(const Material& material);

    // 克隆接口实现
    std::shared_ptr<BaseModel> clone() const override;

private:
    void setupMesh();
    void cleanupSubmeshes();
};