#include "../../include/model/ImportedModel.hpp"
#include <algorithm>
#include <iostream>

// 阴影着色器（精准投影）
static const char* shadow_vert_shader = R"(
attribute vec3 aPosition;
uniform mat4 uShadowMVP;
void main() {
    gl_Position = uShadowMVP * vec4(aPosition, 1.0);
}
)";

static const char* shadow_frag_shader = R"(
precision mediump float;
uniform float uShadowAlpha;
void main() {
    gl_FragColor = vec4(0.38, 0.38, 0.42, uShadowAlpha);
}
)";

// 静态成员初始化
GLuint ImportedModel::shader_program_ = 0;
GLuint ImportedModel::shadow_program_ = 0;
GLint ImportedModel::shadow_mvp_loc_ = -1;
GLint ImportedModel::shadow_alpha_loc_ = -1;
bool ImportedModel::shadow_shader_initialized_ = false;
GLint ImportedModel::model_loc_ = -1;
GLint ImportedModel::view_loc_ = -1;
GLint ImportedModel::projection_loc_ = -1;
GLint ImportedModel::baseColorFactor_loc_ = -1;
GLint ImportedModel::metallicFactor_loc_ = -1;
GLint ImportedModel::roughnessFactor_loc_ = -1;
GLint ImportedModel::emissiveFactor_loc_ = -1;
GLint ImportedModel::lightDir_loc_ = -1;
GLint ImportedModel::viewPos_loc_ = -1;
bool ImportedModel::shader_initialized_ = false;

// PBR 顶点着色器
static const char* pbr_vert_shader = R"(
attribute vec3 aPosition;
attribute vec3 aNormal;
attribute vec2 aTexCoord;
attribute vec4 aColor;

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProjection;

varying vec3 vWorldPos;
varying vec3 vNormal;
varying vec2 vTexCoord;
varying vec4 vColor;

void main() {
    vec4 worldPos = uModel * vec4(aPosition, 1.0);
    vWorldPos = worldPos.xyz;
    vNormal = normalize(mat3(uModel) * aNormal);
    vTexCoord = aTexCoord;
    vColor = aColor;
    gl_Position = uProjection * uView * worldPos;
}
)";

// PBR 片段着色器（简化版，适用于 OpenGL ES 2.0，无纹理）
static const char* pbr_frag_shader = R"(
precision mediump float;

varying vec3 vWorldPos;
varying vec3 vNormal;
varying vec2 vTexCoord;
varying vec4 vColor;

uniform vec4 uBaseColorFactor;
uniform float uMetallicFactor;
uniform float uRoughnessFactor;
uniform vec3 uEmissiveFactor;
uniform vec3 uLightDir;
uniform vec3 uViewPos;

void main() {
    // 基础颜色
    vec4 baseColor = uBaseColorFactor * vColor;
    vec3 albedo = baseColor.rgb;

    // 法线和光照方向
    vec3 N = normalize(vNormal);
    vec3 L = normalize(uLightDir);

    // 简单漫反射光照 (Lambert)，仅保留主光源（已屏蔽 Z 轴方向第二光源）
    float NdotL = max(dot(N, L), 0.0);
#if 0
    vec3 L2 = normalize(vec3(-0.5, 0.8, -0.3));
    float NdotL2 = max(dot(N, L2), 0.0);
#endif
    float diffuse = NdotL;  // 原: NdotL * 0.6 + NdotL2 * 0.4

    // 环境光 + 漫反射
    vec3 ambient = vec3(0.35) * albedo;
    vec3 color = ambient + albedo * diffuse * 0.65;

    // 自发光
    color += uEmissiveFactor;

    // Gamma 校正
    color = pow(max(color, vec3(0.0)), vec3(0.4545));

    gl_FragColor = vec4(color, baseColor.a);
}
)";

void ImportedModel::initShader() {
    if (shader_initialized_) return;

    // 编译顶点着色器
    GLuint vert = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vert, 1, &pbr_vert_shader, nullptr);
    glCompileShader(vert);

    GLint success;
    glGetShaderiv(vert, GL_COMPILE_STATUS, &success);
    if (!success) {
        char log[512];
        glGetShaderInfoLog(vert, 512, nullptr, log);
        std::cerr << "PBR vertex shader error: " << log << std::endl;
    }

    // 编译片段着色器
    GLuint frag = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(frag, 1, &pbr_frag_shader, nullptr);
    glCompileShader(frag);

    glGetShaderiv(frag, GL_COMPILE_STATUS, &success);
    if (!success) {
        char log[512];
        glGetShaderInfoLog(frag, 512, nullptr, log);
        std::cerr << "PBR fragment shader error: " << log << std::endl;
    }

    // 链接程序
    shader_program_ = glCreateProgram();
    glAttachShader(shader_program_, vert);
    glAttachShader(shader_program_, frag);
    glLinkProgram(shader_program_);

    glGetProgramiv(shader_program_, GL_LINK_STATUS, &success);
    if (!success) {
        char log[512];
        glGetProgramInfoLog(shader_program_, 512, nullptr, log);
        std::cerr << "PBR shader link error: " << log << std::endl;
    }

    glDeleteShader(vert);
    glDeleteShader(frag);

    // 获取 uniform 位置
    model_loc_ = glGetUniformLocation(shader_program_, "uModel");
    view_loc_ = glGetUniformLocation(shader_program_, "uView");
    projection_loc_ = glGetUniformLocation(shader_program_, "uProjection");
    baseColorFactor_loc_ = glGetUniformLocation(shader_program_, "uBaseColorFactor");
    metallicFactor_loc_ = glGetUniformLocation(shader_program_, "uMetallicFactor");
    roughnessFactor_loc_ = glGetUniformLocation(shader_program_, "uRoughnessFactor");
    emissiveFactor_loc_ = glGetUniformLocation(shader_program_, "uEmissiveFactor");
    lightDir_loc_ = glGetUniformLocation(shader_program_, "uLightDir");
    viewPos_loc_ = glGetUniformLocation(shader_program_, "uViewPos");

    shader_initialized_ = true;
    std::cout << "PBR shader initialized successfully" << std::endl;
}

void ImportedModel::initShadowShader() {
    if (shadow_shader_initialized_) return;

    GLuint vert = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vert, 1, &shadow_vert_shader, nullptr);
    glCompileShader(vert);

    GLint success;
    glGetShaderiv(vert, GL_COMPILE_STATUS, &success);
    if (!success) {
        char log[512];
        glGetShaderInfoLog(vert, 512, nullptr, log);
        std::cerr << "Shadow vertex shader error: " << log << std::endl;
        glDeleteShader(vert);
        return;
    }

    GLuint frag = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(frag, 1, &shadow_frag_shader, nullptr);
    glCompileShader(frag);
    glGetShaderiv(frag, GL_COMPILE_STATUS, &success);
    if (!success) {
        char log[512];
        glGetShaderInfoLog(frag, 512, nullptr, log);
        std::cerr << "Shadow fragment shader error: " << log << std::endl;
        glDeleteShader(vert);
        glDeleteShader(frag);
        return;
    }

    shadow_program_ = glCreateProgram();
    glAttachShader(shadow_program_, vert);
    glAttachShader(shadow_program_, frag);
    glLinkProgram(shadow_program_);
    glGetProgramiv(shadow_program_, GL_LINK_STATUS, &success);
    glDeleteShader(vert);
    glDeleteShader(frag);

    if (!success) {
        char log[512];
        glGetProgramInfoLog(shadow_program_, 512, nullptr, log);
        std::cerr << "Shadow shader link error: " << log << std::endl;
        return;
    }

    shadow_mvp_loc_ = glGetUniformLocation(shadow_program_, "uShadowMVP");
    shadow_alpha_loc_ = glGetUniformLocation(shadow_program_, "uShadowAlpha");
    shadow_shader_initialized_ = true;
}

ImportedModel::ImportedModel()
    : source_file_("")
    , model_format_("")
{
}

ImportedModel::~ImportedModel() {
    cleanupSubmeshes();
}

void ImportedModel::cleanupSubmeshes() {
    // 只有非克隆实例才拥有 VBO/EBO 的所有权
    if (!is_clone_) {
        for (auto& sm : submeshes_) {
            if (sm.vbo) glDeleteBuffers(1, &sm.vbo);
            if (sm.ebo) glDeleteBuffers(1, &sm.ebo);
        }
    }
    submeshes_.clear();
}

bool ImportedModel::load(const std::string& filepath) {
    source_file_ = filepath;
    model_format_ = "imported";
    std::cout << "ImportedModel loaded from: " << filepath << std::endl;
    return true;
}

void ImportedModel::render(const glm::mat4& view, const glm::mat4& projection) {
    if (!visible_) return;

    // 确保着色器已初始化
    initShader();

    // 优先使用子网格渲染
    if (!submeshes_.empty()) {
        glUseProgram(shader_program_);

        glm::mat4 model = getModelMatrix();
        glUniformMatrix4fv(model_loc_, 1, GL_FALSE, glm::value_ptr(model));
        glUniformMatrix4fv(view_loc_, 1, GL_FALSE, glm::value_ptr(view));
        glUniformMatrix4fv(projection_loc_, 1, GL_FALSE, glm::value_ptr(projection));

        // 从 view 矩阵提取相机位置
        glm::mat4 invView = glm::inverse(view);
        glm::vec3 viewPos(invView[3]);
        glUniform3fv(viewPos_loc_, 1, glm::value_ptr(viewPos));

        // 设置光照方向（从右上方）
        glm::vec3 lightDir = glm::normalize(glm::vec3(0.5f, 1.0f, 0.3f));
        glUniform3fv(lightDir_loc_, 1, glm::value_ptr(lightDir));

        GLint pos_attrib = glGetAttribLocation(shader_program_, "aPosition");
        GLint normal_attrib = glGetAttribLocation(shader_program_, "aNormal");
        GLint texcoord_attrib = glGetAttribLocation(shader_program_, "aTexCoord");
        GLint color_attrib = glGetAttribLocation(shader_program_, "aColor");

        for (const auto& sm : submeshes_) {
            // 设置材质参数
            glUniform4fv(baseColorFactor_loc_, 1, glm::value_ptr(sm.material.baseColorFactor));
            glUniform1f(metallicFactor_loc_, sm.material.metallicFactor);
            glUniform1f(roughnessFactor_loc_, sm.material.roughnessFactor);
            glUniform3fv(emissiveFactor_loc_, 1, glm::value_ptr(sm.material.emissiveFactor));

            // 绑定 VBO
            glBindBuffer(GL_ARRAY_BUFFER, sm.vbo);

            glEnableVertexAttribArray(pos_attrib);
            glVertexAttribPointer(pos_attrib, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)0);

            glEnableVertexAttribArray(normal_attrib);
            glVertexAttribPointer(normal_attrib, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                                 (void*)offsetof(Vertex, normal));

            glEnableVertexAttribArray(texcoord_attrib);
            glVertexAttribPointer(texcoord_attrib, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                                 (void*)offsetof(Vertex, texCoords));

            glEnableVertexAttribArray(color_attrib);
            glVertexAttribPointer(color_attrib, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                                 (void*)offsetof(Vertex, color));

            // 绘制
            if (sm.indexCount > 0) {
                glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, sm.ebo);
                glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(sm.indexCount), GL_UNSIGNED_INT, 0);
            } else {
                glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(sm.vertexCount));
            }

            glDisableVertexAttribArray(pos_attrib);
            glDisableVertexAttribArray(normal_attrib);
            glDisableVertexAttribArray(texcoord_attrib);
            glDisableVertexAttribArray(color_attrib);
        }

        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
        glUseProgram(0);
        return;
    }

    // 回退到旧的单一 VBO 渲染
    if (vertices_.empty()) return;

    glUseProgram(shader_program_);

    glm::mat4 model = getModelMatrix();
    glUniformMatrix4fv(model_loc_, 1, GL_FALSE, glm::value_ptr(model));
    glUniformMatrix4fv(view_loc_, 1, GL_FALSE, glm::value_ptr(view));
    glUniformMatrix4fv(projection_loc_, 1, GL_FALSE, glm::value_ptr(projection));

    glm::mat4 invView = glm::inverse(view);
    glm::vec3 viewPos(invView[3]);
    glUniform3fv(viewPos_loc_, 1, glm::value_ptr(viewPos));

    glm::vec3 lightDir = glm::normalize(glm::vec3(0.5f, 1.0f, 0.3f));
    glUniform3fv(lightDir_loc_, 1, glm::value_ptr(lightDir));

    // 默认材质
    glUniform4f(baseColorFactor_loc_, 1.0f, 1.0f, 1.0f, 1.0f);
    glUniform1f(metallicFactor_loc_, 0.0f);
    glUniform1f(roughnessFactor_loc_, 0.5f);
    glUniform3f(emissiveFactor_loc_, 0.0f, 0.0f, 0.0f);

    glBindBuffer(GL_ARRAY_BUFFER, vbo_);

    GLint pos_attrib = glGetAttribLocation(shader_program_, "aPosition");
    GLint normal_attrib = glGetAttribLocation(shader_program_, "aNormal");
    GLint texcoord_attrib = glGetAttribLocation(shader_program_, "aTexCoord");
    GLint color_attrib = glGetAttribLocation(shader_program_, "aColor");

    glEnableVertexAttribArray(pos_attrib);
    glVertexAttribPointer(pos_attrib, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)0);

    glEnableVertexAttribArray(normal_attrib);
    glVertexAttribPointer(normal_attrib, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                         (void*)offsetof(Vertex, normal));

    glEnableVertexAttribArray(texcoord_attrib);
    glVertexAttribPointer(texcoord_attrib, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                         (void*)offsetof(Vertex, texCoords));

    glEnableVertexAttribArray(color_attrib);
    glVertexAttribPointer(color_attrib, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                         (void*)offsetof(Vertex, color));

    if (!indices_.empty()) {
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
        glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(indices_.size()), GL_UNSIGNED_INT, 0);
    } else {
        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertices_.size()));
    }

    glDisableVertexAttribArray(pos_attrib);
    glDisableVertexAttribArray(normal_attrib);
    glDisableVertexAttribArray(texcoord_attrib);
    glDisableVertexAttribArray(color_attrib);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    glUseProgram(0);
}

void ImportedModel::renderShadow(const glm::mat4& view, const glm::mat4& projection,
                                const glm::mat4& shadowMatrix) {
    if (!visible_) return;

    initShadowShader();
    if (!shadow_program_ || shadow_mvp_loc_ < 0) return;

    glm::mat4 model = getModelMatrix();
    glm::mat4 shadowMVP = projection * view * shadowMatrix * model;

    glUseProgram(shadow_program_);
    if (shadow_alpha_loc_ >= 0)
        glUniform1f(shadow_alpha_loc_, 0.15f);
    glUniformMatrix4fv(shadow_mvp_loc_, 1, GL_FALSE, glm::value_ptr(shadowMVP));

    GLint pos_attrib = glGetAttribLocation(shadow_program_, "aPosition");

    if (!submeshes_.empty()) {
        for (const auto& sm : submeshes_) {
            glBindBuffer(GL_ARRAY_BUFFER, sm.vbo);
            glEnableVertexAttribArray(pos_attrib);
            glVertexAttribPointer(pos_attrib, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)0);
            if (sm.indexCount > 0) {
                glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, sm.ebo);
                glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(sm.indexCount), GL_UNSIGNED_INT, 0);
            } else {
                glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(sm.vertexCount));
            }
            glDisableVertexAttribArray(pos_attrib);
        }
    } else if (!vertices_.empty()) {
        glBindBuffer(GL_ARRAY_BUFFER, vbo_);
        glEnableVertexAttribArray(pos_attrib);
        glVertexAttribPointer(pos_attrib, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)0);
        if (!indices_.empty()) {
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
            glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(indices_.size()), GL_UNSIGNED_INT, 0);
        } else {
            glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertices_.size()));
        }
        glDisableVertexAttribArray(pos_attrib);
    }

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    glUseProgram(0);
}

void ImportedModel::update(float deltaTime) {
}

void ImportedModel::setSourceFile(const std::string& filepath) {
    source_file_ = filepath;
}

void ImportedModel::setModelFormat(const std::string& format) {
    model_format_ = format;
}

void ImportedModel::setupMesh() {
    if (!vertices_.empty()) {
        glm::vec3 min_pos(FLT_MAX), max_pos(-FLT_MAX);
        for (const auto& vertex : vertices_) {
            min_pos = glm::min(min_pos, vertex.position);
            max_pos = glm::max(max_pos, vertex.position);
        }
        std::cout << "ImportedModel bounds - Min: (" << min_pos.x << "," << min_pos.y << "," << min_pos.z << ")"
                  << " Max: (" << max_pos.x << "," << max_pos.y << "," << max_pos.z << ")" << std::endl;
    }
    BaseModel::setupMesh();
}

void ImportedModel::setMeshData(const std::vector<Vertex>& vertices, const std::vector<unsigned int>& indices) {
    vertices_ = vertices;
    indices_ = indices;
    setupMesh();
}

void ImportedModel::setSubMeshes(const std::vector<SubMesh>& submeshes, const std::vector<PBRMaterial>& materials) {
    cleanupSubmeshes();

    for (const auto& sm : submeshes) {
        GPUSubMesh gpu_sm;

        // 创建 VBO
        glGenBuffers(1, &gpu_sm.vbo);
        glBindBuffer(GL_ARRAY_BUFFER, gpu_sm.vbo);
        glBufferData(GL_ARRAY_BUFFER, sm.vertices.size() * sizeof(Vertex),
                     sm.vertices.data(), GL_STATIC_DRAW);

        // 创建 EBO
        if (!sm.indices.empty()) {
            glGenBuffers(1, &gpu_sm.ebo);
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, gpu_sm.ebo);
            glBufferData(GL_ELEMENT_ARRAY_BUFFER, sm.indices.size() * sizeof(unsigned int),
                        sm.indices.data(), GL_STATIC_DRAW);
        }

        gpu_sm.vertexCount = sm.vertices.size();
        gpu_sm.indexCount = sm.indices.size();

        // 获取材质
        if (sm.materialIndex >= 0 && sm.materialIndex < static_cast<int>(materials.size())) {
            gpu_sm.material = materials[sm.materialIndex];
        } else {
            gpu_sm.material.baseColorFactor = glm::vec4(0.8f, 0.8f, 0.8f, 1.0f);
            gpu_sm.material.metallicFactor = 0.0f;
            gpu_sm.material.roughnessFactor = 0.5f;
        }

        submeshes_.push_back(gpu_sm);
    }

    glm::vec3 minB(1e30f), maxB(-1e30f);
    for (const auto& sm : submeshes) {
        for (const auto& v : sm.vertices) {
            minB = glm::min(minB, v.position);
            maxB = glm::max(maxB, v.position);
        }
    }
    if (minB.x <= maxB.x) {
        min_bound_ = minB;
        max_bound_ = maxB;
    }

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

    std::cout << "Set " << submeshes_.size() << " submeshes with materials" << std::endl;
}

void ImportedModel::setMaterial(const Material& material) {
}

std::shared_ptr<BaseModel> ImportedModel::clone() const {
    auto cloned = std::make_shared<ImportedModel>();

    // 标记为克隆实例，不拥有 VBO/EBO 所有权
    cloned->is_clone_ = true;

    cloned->setPosition(position_);
    cloned->setRotation(rotation_);
    cloned->setScale(scale_);
    cloned->setVisible(visible_);
    cloned->setOpacity(opacity_);

    // 克隆子网格（共享 VBO/EBO 引用）
    for (const auto& sm : submeshes_) {
        GPUSubMesh new_sm;
        new_sm.material = sm.material;
        new_sm.vertexCount = sm.vertexCount;
        new_sm.indexCount = sm.indexCount;
        new_sm.vbo = sm.vbo;
        new_sm.ebo = sm.ebo;

        cloned->submeshes_.push_back(new_sm);
    }

    // 旧数据兼容
    if (!vertices_.empty() || !indices_.empty()) {
        cloned->setMeshData(vertices_, indices_);
    }

    cloned->source_file_ = source_file_;
    cloned->model_format_ = model_format_;
    cloned->textures_.clear();
    cloned->min_bound_ = min_bound_;
    cloned->max_bound_ = max_bound_;
    if (vertices_.empty() && submeshes_.empty())
        cloned->calculateBounds();

    return cloned;
}
