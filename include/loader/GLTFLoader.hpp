#pragma once

#include "../model/BaseModel.hpp"
#include "tiny_gltf.h"
#include <vector>
#include <string>

#ifdef DRACO_MESH_COMPRESSION_SUPPORTED
#include <draco/compression/decode.h>
#include <draco/core/decoder_buffer.h>
#endif

class GLTFLoader {
private:
    tinygltf::Model gltf_model_;
    bool is_loaded_;
    std::string error_message_;

public:
    GLTFLoader();
    ~GLTFLoader();

    // 核心加载接口
    bool loadFromFile(const std::string& filepath);
    bool loadFromString(const std::string& gltf_string);

    // 数据访问接口
    size_t getMeshCount() const;
    size_t getNodeCount() const;
    size_t getSceneCount() const;

    // 网格数据提取
    bool getMeshData(int mesh_index, std::vector<Vertex>& vertices, std::vector<unsigned int>& indices);
    bool getMeshName(int mesh_index, std::string& name);

    // 节点变换获取
    bool getNodeTransform(int node_index, glm::vec3& translation, glm::vec3& rotation, glm::vec3& scale);
    bool getNodeMesh(int node_index, int& mesh_index);
    std::string getNodeName(int node_index) const;

    // 场景信息
    int getDefaultScene() const;
    bool getSceneNodes(int scene_index, std::vector<int>& node_indices);

    // 错误处理
    bool hasError() const { return !error_message_.empty(); }
    const std::string& getErrorMessage() const { return error_message_; }
    void clearError() { error_message_.clear(); }

private:
    bool validateModel();

    // 数据读取
    bool readPositions(const tinygltf::Accessor& accessor, std::vector<glm::vec3>& positions);
    bool readNormals(const tinygltf::Accessor& accessor, std::vector<glm::vec3>& normals);
    bool readTexCoords(const tinygltf::Accessor& accessor, std::vector<glm::vec2>& texcoords);
    bool readColors(const tinygltf::Accessor& accessor, std::vector<glm::vec4>& colors);
    bool readIndices(const tinygltf::Accessor& accessor, std::vector<unsigned int>& indices);

#ifdef DRACO_MESH_COMPRESSION_SUPPORTED
    bool decodeDracoPrimitive(int draco_buffer_view, std::vector<glm::vec3>& positions,
                              std::vector<glm::vec3>& normals, std::vector<glm::vec4>& colors,
                              std::vector<unsigned int>& indices);
#endif
};
