#include "model/BaseModel.hpp"
#include <iostream>
#include <algorithm>

BaseModel::BaseModel()
    : vbo_(0), ebo_(0)
    , position_(0.0f, 0.0f, 0.0f)
    , rotation_(0.0f, 0.0f, 0.0f)
    , scale_(1.0f, 1.0f, 1.0f)
    , visible_(true)
    , opacity_(1.0f)
    , min_bound_(0.0f, 0.0f, 0.0f)
    , max_bound_(0.0f, 0.0f, 0.0f)
{
}

BaseModel::~BaseModel() {
    // OpenGL ES 2.0不支持VAO，只清理VBO和纹理
    if (vbo_) glDeleteBuffers(1, &vbo_);
    if (ebo_) glDeleteBuffers(1, &ebo_);

    // 清理纹理
    for (auto& texture : textures_) {
        glDeleteTextures(1, &texture.id);
    }
}

void BaseModel::setPosition(const glm::vec3& position) {
    position_ = position;
}

void BaseModel::setRotation(const glm::vec3& rotation) {
    rotation_ = rotation;
}

void BaseModel::setScale(const glm::vec3& scale) {
    scale_ = scale;
}

void BaseModel::translate(const glm::vec3& delta) {
    position_ += delta;
}

void BaseModel::rotate(const glm::vec3& delta) {
    rotation_ += delta;
}

glm::mat4 BaseModel::getModelMatrix() const {
    glm::mat4 model = glm::mat4(1.0f);

    // 应用变换（顺序很重要）
    model = glm::translate(model, position_);
    model = glm::rotate(model, glm::radians(rotation_.x), glm::vec3(1.0f, 0.0f, 0.0f));
    model = glm::rotate(model, glm::radians(rotation_.y), glm::vec3(0.0f, 1.0f, 0.0f));
    model = glm::rotate(model, glm::radians(rotation_.z), glm::vec3(0.0f, 0.0f, 1.0f));
    model = glm::scale(model, scale_);

    return model;
}

void BaseModel::setupMesh() {
    if (vertices_.empty()) return;

    // OpenGL ES 2.0不支持VAO，直接使用VBO
    glGenBuffers(1, &vbo_);
    if (!indices_.empty()) {
        glGenBuffers(1, &ebo_);
    }

    // 加载顶点数据
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER,
                 vertices_.size() * sizeof(Vertex),
                 vertices_.data(),
                 GL_STATIC_DRAW);

    // 加载索引数据（如果有）
    if (!indices_.empty()) {
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                     indices_.size() * sizeof(unsigned int),
                     indices_.data(),
                     GL_STATIC_DRAW);
    }

    // 注意：每次渲染时都需要重新设置顶点属性指针
    calculateBounds();
}

void BaseModel::calculateBounds() {
    if (vertices_.empty()) return;

    min_bound_ = vertices_[0].position;
    max_bound_ = vertices_[0].position;

    for (const auto& vertex : vertices_) {
        min_bound_ = glm::min(min_bound_, vertex.position);
        max_bound_ = glm::max(max_bound_, vertex.position);
    }
}

void BaseModel::bindTextures() {
    for (size_t i = 0; i < textures_.size(); i++) {
        glActiveTexture(GL_TEXTURE0 + i);
        glBindTexture(GL_TEXTURE_2D, textures_[i].id);
    }
}

void BaseModel::unbindTextures() {
    for (size_t i = 0; i < textures_.size(); i++) {
        glActiveTexture(GL_TEXTURE0 + i);
        glBindTexture(GL_TEXTURE_2D, 0);
    }
    glActiveTexture(GL_TEXTURE0);
}