#include "../../include/model/ImportedModel.hpp"
#include <iostream>

ImportedModel::ImportedModel()
    : source_file_("")
    , model_format_("")
{
}

ImportedModel::~ImportedModel() {
}

bool ImportedModel::load(const std::string& filepath) {
    source_file_ = filepath;
    model_format_ = "imported";

    // 这里应该实现具体的模型加载逻辑
    // 现在只是设置标记
    std::cout << "ImportedModel loaded from: " << filepath << std::endl;
    return true;
}

void ImportedModel::render(const glm::mat4& view, const glm::mat4& projection) {

    if (!visible_ || vertices_.empty()) {
        return;
    }

    // 简单的着色器程序（如果还没有创建）
    static GLuint shader_program = 0;
    static GLint model_location = -1;
    static GLint view_location = -1;
    static GLint projection_location = -1;

    // 创建简单的着色器（首次调用时）
    if (shader_program == 0) {
        const char* vert_shader = R"(
            attribute vec3 aPosition;
            attribute vec3 aNormal;
            attribute vec4 aColor;
            uniform mat4 uModel;
            uniform mat4 uView;
            uniform mat4 uProjection;
            varying vec3 vNormal;
            varying vec3 vPosition;
            varying vec4 vColor;
            void main() {
                vec4 worldPos = uModel * vec4(aPosition, 1.0);
                vPosition = worldPos.xyz;
                vNormal = normalize(mat3(uModel) * aNormal);
                vColor = aColor;
                gl_Position = uProjection * uView * worldPos;
            }
        )";

        const char* frag_shader = R"(
            precision mediump float;
            varying vec3 vNormal;
            varying vec3 vPosition;
            varying vec4 vColor;
            void main() {
                // 均匀光照：先测试颜色显示，避免光照计算影响
                float lighting = 1.0;

                vec3 baseColor = vColor.rgb;
                vec3 litColor = baseColor * lighting;

                // 测试：暂时移除gamma校正，查看颜色是否正确
                // 线性空间 -> sRGB gamma 校正
                // vec3 gamma = vec3(1.0 / 2.2);
                // gl_FragColor = vec4(pow(litColor, gamma), 1.0);

                // 直接输出颜色，不应用gamma校正
                gl_FragColor = vec4(litColor, 1.0);
            }
        )";

        GLuint vert = glCreateShader(GL_VERTEX_SHADER);
        glShaderSource(vert, 1, &vert_shader, nullptr);
        glCompileShader(vert);

        GLint success;
        glGetShaderiv(vert, GL_COMPILE_STATUS, &success);
        if (!success) {
            char info_log[512];
            glGetShaderInfoLog(vert, 512, nullptr, info_log);
            std::cerr << "Vertex shader error: " << info_log << std::endl;
        }

        GLuint frag = glCreateShader(GL_FRAGMENT_SHADER);
        glShaderSource(frag, 1, &frag_shader, nullptr);
        glCompileShader(frag);

        glGetShaderiv(frag, GL_COMPILE_STATUS, &success);
        if (!success) {
            char info_log[512];
            glGetShaderInfoLog(frag, 512, nullptr, info_log);
            std::cerr << "Fragment shader error: " << info_log << std::endl;
        }

        shader_program = glCreateProgram();
        glAttachShader(shader_program, vert);
        glAttachShader(shader_program, frag);
        glLinkProgram(shader_program);

        glGetProgramiv(shader_program, GL_LINK_STATUS, &success);
        if (!success) {
            char info_log[512];
            glGetProgramInfoLog(shader_program, 512, nullptr, info_log);
            std::cerr << "Shader link error: " << info_log << std::endl;
        }

        glDeleteShader(vert);
        glDeleteShader(frag);

        model_location = glGetUniformLocation(shader_program, "uModel");
        view_location = glGetUniformLocation(shader_program, "uView");
        projection_location = glGetUniformLocation(shader_program, "uProjection");
    }

    // 使用着色器程序
    glUseProgram(shader_program);

    // 设置变换矩阵
    glm::mat4 model = getModelMatrix();
    glUniformMatrix4fv(model_location, 1, GL_FALSE, glm::value_ptr(model));
    glUniformMatrix4fv(view_location, 1, GL_FALSE, glm::value_ptr(view));
    glUniformMatrix4fv(projection_location, 1, GL_FALSE, glm::value_ptr(projection));

    // 绑定VBO
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);

    // 设置顶点属性
    GLint pos_attrib = glGetAttribLocation(shader_program, "aPosition");
    glEnableVertexAttribArray(pos_attrib);
    glVertexAttribPointer(pos_attrib, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)0);

    GLint normal_attrib = glGetAttribLocation(shader_program, "aNormal");
    glEnableVertexAttribArray(normal_attrib);
    glVertexAttribPointer(normal_attrib, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                         (void*)offsetof(Vertex, normal));

    GLint color_attrib = glGetAttribLocation(shader_program, "aColor");
    glEnableVertexAttribArray(color_attrib);
    glVertexAttribPointer(color_attrib, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                         (void*)offsetof(Vertex, color));

    // 绘制
    if (!indices_.empty()) {
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
        glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(indices_.size()), GL_UNSIGNED_INT, 0);
    } else {
        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertices_.size()));
    }

    // 清理
    glDisableVertexAttribArray(pos_attrib);
    glDisableVertexAttribArray(normal_attrib);
    glDisableVertexAttribArray(color_attrib);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    if (!indices_.empty()) {
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    }
    glUseProgram(0);

    // std::cout << "Rendering imported model: " << source_file_ << std::endl;
}

void ImportedModel::update(float deltaTime) {
    // 这里应该实现具体的更新逻辑
    // 现在只是占位符
}

void ImportedModel::setSourceFile(const std::string& filepath) {
    source_file_ = filepath;
}

void ImportedModel::setModelFormat(const std::string& format) {
    model_format_ = format;
}

void ImportedModel::setupMesh() {
    // 计算模型边界用于调试
    if (!vertices_.empty()) {
        glm::vec3 min_pos(FLT_MAX), max_pos(-FLT_MAX);
        for (const auto& vertex : vertices_) {
            min_pos = glm::min(min_pos, vertex.position);
            max_pos = glm::max(max_pos, vertex.position);
        }
        glm::vec3 size = max_pos - min_pos;
        glm::vec3 center = (min_pos + max_pos) * 0.5f;

        std::cout << "ImportedModel bounds - Min: (" << min_pos.x << "," << min_pos.y << "," << min_pos.z << ")"
                  << " Max: (" << max_pos.x << "," << max_pos.y << "," << max_pos.z << ")" << std::endl;
        std::cout << "Model size: " << size.x << " x " << size.y << " x " << size.z
                  << " Center: (" << center.x << "," << center.y << "," << center.z << ")" << std::endl;

        // 输出世界坐标下的边界（应用变换后）
        glm::vec3 world_min = getPosition() + min_pos * getScale();
        glm::vec3 world_max = getPosition() + max_pos * getScale();
        std::cout << "World bounds - Min: (" << world_min.x << "," << world_min.y << "," << world_min.z << ")"
                  << " Max: (" << world_max.x << "," << world_max.y << "," << world_max.z << ")" << std::endl;
    }

    // 调用基类实现来创建OpenGL缓冲区和计算边界框
    BaseModel::setupMesh();
}

void ImportedModel::setMeshData(const std::vector<Vertex>& vertices, const std::vector<unsigned int>& indices) {
    vertices_ = vertices;
    indices_ = indices;
    setupMesh();
}

void ImportedModel::setMaterial(const Material& material) {
    // 这里应该实现材质设置
    // 现在只是占位符
}

std::shared_ptr<BaseModel> ImportedModel::clone() const {
    auto cloned = std::make_shared<ImportedModel>();

    // 复制基础属性
    cloned->setPosition(position_);
    cloned->setRotation(rotation_);
    cloned->setScale(scale_);
    cloned->setVisible(visible_);
    cloned->setOpacity(opacity_);

    // 复制模型数据 - 使用setMeshData确保正确初始化OpenGL资源
    if (!vertices_.empty() || !indices_.empty()) {
        cloned->setMeshData(vertices_, indices_);
    } else {
    }

    // 复制特有属性
    cloned->source_file_ = source_file_;
    cloned->model_format_ = model_format_;

    // 纹理处理：克隆实例不复制纹理，避免纹理资源冲突
    // 如果渲染需要纹理，应该使用纹理管理器
    cloned->textures_.clear();

    // 计算边界框
    cloned->calculateBounds();

    return cloned;
}