#pragma once

#include <GLES2/gl2.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <string>
#include <vector>
#include <memory>

// 顶点结构定义
struct Vertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 texCoords;
    glm::vec4 color;      // RGBA 材质颜色
};

// 纹理结构
struct Texture {
    GLuint id;
    std::string type;  // "texture_diffuse", "texture_specular", etc.
    std::string path;
};

// 材质结构
struct Material {
    glm::vec3 ambient;
    glm::vec3 diffuse;
    glm::vec3 specular;
    float shininess;
};

class BaseModel {
protected:
    // OpenGL资源 (OpenGL ES 2.0不支持VAO)
    GLuint vbo_;
    GLuint ebo_;

    // 模型数据
    std::vector<Vertex> vertices_;
    std::vector<unsigned int> indices_;
    std::vector<Texture> textures_;

    // 变换属性
    glm::vec3 position_;
    glm::vec3 rotation_;    // 欧拉角 (degrees)
    glm::vec3 scale_;

    // 渲染状态
    bool visible_;
    float opacity_;

    // 边界框
    glm::vec3 min_bound_;
    glm::vec3 max_bound_;

public:
    BaseModel();
    virtual ~BaseModel();

    // 核心接口 - 子类必须实现
    virtual bool load(const std::string& filepath) = 0;
    virtual void render(const glm::mat4& view, const glm::mat4& projection) = 0;
    virtual void update(float deltaTime) = 0;

    // 克隆接口 - 用于创建实例
    virtual std::shared_ptr<BaseModel> clone() const = 0;

    // 变换控制接口
    void setPosition(const glm::vec3& position);
    void setRotation(const glm::vec3& rotation);
    void setScale(const glm::vec3& scale);

    void translate(const glm::vec3& delta);
    void rotate(const glm::vec3& delta);

    // 获取变换矩阵
    glm::mat4 getModelMatrix() const;

    // 属性访问
    const glm::vec3& getPosition() const { return position_; }
    const glm::vec3& getRotation() const { return rotation_; }
    const glm::vec3& getScale() const { return scale_; }

    // 渲染控制
    void setVisible(bool visible) { visible_ = visible; }
    bool isVisible() const { return visible_; }

    void setOpacity(float opacity) { opacity_ = glm::clamp(opacity, 0.0f, 1.0f); }
    float getOpacity() const { return opacity_; }

    // 边界查询
    glm::vec3 getSize() const { return max_bound_ - min_bound_; }
    glm::vec3 getCenter() const { return (min_bound_ + max_bound_) * 0.5f; }

    // 统计信息访问
    size_t getVertexCount() const { return vertices_.size(); }
    size_t getIndexCount() const { return indices_.size(); }
    size_t getTextureCount() const { return textures_.size(); }

protected:
    // 内部辅助方法
    void setupMesh();
    void calculateBounds();
    virtual void bindTextures();
    virtual void unbindTextures();
};