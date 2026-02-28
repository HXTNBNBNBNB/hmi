// 启用Draco支持（必须在包含头文件之前定义）
#define DRACO_MESH_COMPRESSION_SUPPORTED

#include "loader/GLTFLoader.hpp"
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>
#include <cmath>

GLTFLoader::GLTFLoader()
    : is_loaded_(false)
{
}

GLTFLoader::~GLTFLoader() {
}

bool GLTFLoader::loadFromFile(const std::string& filepath) {
    clearError();

    try {
        // 根据扩展名选择加载方式
        bool ok = false;
        size_t dot = filepath.rfind('.');
        std::string ext = (dot != std::string::npos) ? filepath.substr(dot) : "";
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

        if (ext == ".glb") {
            ok = gltf_model_.LoadBinaryFromFile(filepath);
        } else {
            ok = gltf_model_.LoadASCIIFromFile(filepath);
        }

        if (!ok) {
            error_message_ = "Failed to load GLTF/GLB file: " + filepath;
            return false;
        }

        if (!validateModel()) {
            return false;
        }

        is_loaded_ = true;
        return true;

    } catch (const std::exception& e) {
        error_message_ = "Exception during GLTF loading: " + std::string(e.what());
        return false;
    }
}

bool GLTFLoader::loadFromString(const std::string& gltf_string) {
    clearError();

    try {
        if (!gltf_model_.LoadASCIIFromString(gltf_string)) {
            error_message_ = "Failed to parse GLTF string";
            return false;
        }

        if (!validateModel()) {
            return false;
        }

        is_loaded_ = true;
        return true;

    } catch (const std::exception& e) {
        error_message_ = "Exception during GLTF parsing: " + std::string(e.what());
        return false;
    }
}

size_t GLTFLoader::getMeshCount() const {
    return is_loaded_ ? gltf_model_.meshes.size() : 0;
}

size_t GLTFLoader::getNodeCount() const {
    return is_loaded_ ? gltf_model_.nodes.size() : 0;
}

size_t GLTFLoader::getSceneCount() const {
    return is_loaded_ ? gltf_model_.scenes.size() : 0;
}

bool GLTFLoader::getMeshData(int mesh_index, std::vector<Vertex>& vertices, std::vector<unsigned int>& indices) {
    if (!is_loaded_ || mesh_index < 0 || mesh_index >= static_cast<int>(gltf_model_.meshes.size())) {
        error_message_ = "Invalid mesh index: " + std::to_string(mesh_index);
        return false;
    }

    const auto& mesh = gltf_model_.meshes[mesh_index];

    if (mesh.primitives.empty()) {
        error_message_ = "Mesh has no primitives";
        return false;
    }

    std::vector<glm::vec3> all_positions;
    std::vector<glm::vec3> all_normals;
    std::vector<glm::vec2> all_texcoords;
    std::vector<glm::vec4> all_colors;
    std::vector<unsigned int> all_indices;

    size_t vertex_offset = 0;

    // RK3568 内存预算：限制总顶点数防止 OOM
    // 4.4M 顶点 * 32 bytes/vertex = ~140MB，超出 RK3568 可用内存
    // 500K 顶点 * 32 bytes = ~16MB，安全范围
    const size_t VERTEX_BUDGET = 500000;
    size_t budget_remaining = VERTEX_BUDGET;

    // 先按 accessor 声明的顶点数对 primitives 排序，优先加载小图元
    // （小图元通常是车身细节部件，大图元通常是高密度面片可以降级）
    std::vector<size_t> prim_order;
    prim_order.reserve(mesh.primitives.size());
    for (size_t i = 0; i < mesh.primitives.size(); ++i) {
        prim_order.push_back(i);
    }
    std::sort(prim_order.begin(), prim_order.end(), [&](size_t a, size_t b) {
        auto get_count = [&](size_t idx) -> size_t {
            auto pos_it = mesh.primitives[idx].attributes.find("POSITION");
            if (pos_it == mesh.primitives[idx].attributes.end()) return 0;
            return gltf_model_.accessors[pos_it->second].count;
        };
        return get_count(a) < get_count(b);
    });

    for (size_t order_idx = 0; order_idx < prim_order.size(); ++order_idx) {
        size_t prim_idx = prim_order[order_idx];
        const auto& primitive = mesh.primitives[prim_idx];

        std::vector<glm::vec3> positions;
        std::vector<glm::vec3> normals;
        std::vector<glm::vec4> prim_colors;
        std::vector<unsigned int> prim_indices;

#ifdef DRACO_MESH_COMPRESSION_SUPPORTED
        if (primitive.has_draco_extension && primitive.draco_buffer_view >= 0) {
            if (!decodeDracoPrimitive(primitive.draco_buffer_view, positions, normals, prim_colors, prim_indices)) {
                std::cerr << "Failed to decode Draco primitive " << prim_idx << std::endl;
                continue;
            }
        } else
#endif
        {
            auto pos_it = primitive.attributes.find("POSITION");
            if (pos_it == primitive.attributes.end()) {
                continue;
            }

            const auto& pos_accessor = gltf_model_.accessors[pos_it->second];

            if (!readPositions(pos_accessor, positions)) {
                continue;
            }

            auto normal_it = primitive.attributes.find("NORMAL");
            if (normal_it != primitive.attributes.end()) {
                readNormals(gltf_model_.accessors[normal_it->second], normals);
            }

            auto color_it = primitive.attributes.find("COLOR_0");
            if (color_it != primitive.attributes.end()) {
                readColors(gltf_model_.accessors[color_it->second], prim_colors);
            }

            if (primitive.indices >= 0) {
                readIndices(gltf_model_.accessors[primitive.indices], prim_indices);
            }
        }

        if (normals.empty() || normals.size() != positions.size()) {
            normals.assign(positions.size(), glm::vec3(0.0f, 1.0f, 0.0f));
        }

        if (prim_colors.empty() || prim_colors.size() != positions.size()) {
            // 如果没有顶点颜色，使用材质颜色或默认颜色
        }

        // 检查预算：如果这个 primitive 会超出预算，跳过
        if (positions.size() > budget_remaining) {
            continue;
        }
        budget_remaining -= positions.size();

        std::vector<glm::vec2> texcoords;
        auto uv_it = primitive.attributes.find("TEXCOORD_0");
        if (uv_it != primitive.attributes.end() && !primitive.has_draco_extension) {
            readTexCoords(gltf_model_.accessors[uv_it->second], texcoords);
        }
        if (texcoords.empty() || texcoords.size() != positions.size()) {
            texcoords.assign(positions.size(), glm::vec2(0.0f, 0.0f));
        }

        // 查找材质颜色
        glm::vec4 prim_color(0.8f, 0.8f, 0.8f, 1.0f); // 默认灰色
        if (primitive.material >= 0 &&
            primitive.material < static_cast<int>(gltf_model_.materials.size())) {
            const auto& mat = gltf_model_.materials[primitive.material];
            const auto& bcf = mat.pbrMetallicRoughness.baseColorFactor;
            prim_color = glm::vec4(
                static_cast<float>(bcf[0]),
                static_cast<float>(bcf[1]),
                static_cast<float>(bcf[2]),
                static_cast<float>(bcf[3])
            );
            std::cout << "  Prim[" << prim_idx << "] mat=" << primitive.material
                      << " \"" << mat.name << "\" color=("
                      << bcf[0] << "," << bcf[1] << "," << bcf[2] << "," << bcf[3]
                      << ") verts=" << positions.size() << std::endl;
        }
        all_positions.insert(all_positions.end(), positions.begin(), positions.end());
        all_normals.insert(all_normals.end(), normals.begin(), normals.end());
        all_texcoords.insert(all_texcoords.end(), texcoords.begin(), texcoords.end());

        // 添加颜色数据：如果读取了顶点颜色，使用它们；否则使用材质颜色
        if (!prim_colors.empty() && prim_colors.size() == positions.size()) {
            all_colors.insert(all_colors.end(), prim_colors.begin(), prim_colors.end());
        } else {
            std::vector<glm::vec4> colors(positions.size(), prim_color);
            all_colors.insert(all_colors.end(), colors.begin(), colors.end());
        }

        if (!prim_indices.empty()) {
            for (unsigned int idx : prim_indices) {
                all_indices.push_back(idx + static_cast<unsigned int>(vertex_offset));
            }
        } else {
            for (unsigned int i = 0; i < static_cast<unsigned int>(positions.size()); ++i) {
                all_indices.push_back(static_cast<unsigned int>(vertex_offset + i));
            }
        }

        vertex_offset += positions.size();
    }

    vertices.clear();
    vertices.reserve(all_positions.size());

    for (size_t i = 0; i < all_positions.size(); ++i) {
        Vertex vertex;
        vertex.position = all_positions[i];
        vertex.normal = (i < all_normals.size()) ? all_normals[i] : glm::vec3(0.0f, 1.0f, 0.0f);
        vertex.texCoords = (i < all_texcoords.size()) ? all_texcoords[i] : glm::vec2(0.0f, 0.0f);
        vertex.color = (i < all_colors.size()) ? all_colors[i] : glm::vec4(0.8f, 0.8f, 0.8f, 1.0f);
        vertices.push_back(vertex);
    }

    indices = std::move(all_indices);
    return !vertices.empty();
}

bool GLTFLoader::getMeshName(int mesh_index, std::string& name) {
    if (!is_loaded_ || mesh_index < 0 || mesh_index >= static_cast<int>(gltf_model_.meshes.size())) {
        return false;
    }
    name = gltf_model_.meshes[mesh_index].name;
    return true;
}

bool GLTFLoader::getNodeTransform(int node_index, glm::vec3& translation, glm::vec3& rotation, glm::vec3& scale) {
    if (!is_loaded_ || node_index < 0 || node_index >= static_cast<int>(gltf_model_.nodes.size())) {
        return false;
    }

    const auto& node = gltf_model_.nodes[node_index];

    translation = glm::vec3(0.0f);
    rotation = glm::vec3(0.0f);
    scale = glm::vec3(1.0f);

    if (node.translation.size() >= 3) {
        translation.x = static_cast<float>(node.translation[0]);
        translation.y = static_cast<float>(node.translation[1]);
        translation.z = static_cast<float>(node.translation[2]);
    }

    if (node.rotation.size() >= 4) {
        glm::quat quat(
            static_cast<float>(node.rotation[3]),
            static_cast<float>(node.rotation[0]),
            static_cast<float>(node.rotation[1]),
            static_cast<float>(node.rotation[2])
        );
        rotation = glm::degrees(glm::eulerAngles(quat));
    }

    if (node.scale.size() >= 3) {
        scale.x = static_cast<float>(node.scale[0]);
        scale.y = static_cast<float>(node.scale[1]);
        scale.z = static_cast<float>(node.scale[2]);
    }

    return true;
}

bool GLTFLoader::getNodeMesh(int node_index, int& mesh_index) {
    if (!is_loaded_ || node_index < 0 || node_index >= static_cast<int>(gltf_model_.nodes.size())) {
        return false;
    }
    mesh_index = gltf_model_.nodes[node_index].mesh;
    return mesh_index >= 0;
}

std::string GLTFLoader::getNodeName(int node_index) const {
    if (!is_loaded_ || node_index < 0 || node_index >= static_cast<int>(gltf_model_.nodes.size())) {
        return "";
    }
    return gltf_model_.nodes[node_index].name;
}

int GLTFLoader::getDefaultScene() const {
    return is_loaded_ ? gltf_model_.defaultScene : -1;
}

bool GLTFLoader::getSceneNodes(int scene_index, std::vector<int>& node_indices) {
    if (!is_loaded_ || scene_index < 0 || scene_index >= static_cast<int>(gltf_model_.scenes.size())) {
        return false;
    }
    node_indices = gltf_model_.scenes[scene_index].nodes;
    return true;
}

bool GLTFLoader::validateModel() {
    if (gltf_model_.buffers.empty()) {
        error_message_ = "No buffers found in model";
        return false;
    }
    if (gltf_model_.bufferViews.empty()) {
        error_message_ = "No buffer views found in model";
        return false;
    }
    if (gltf_model_.accessors.empty()) {
        error_message_ = "No accessors found in model";
        return false;
    }
    return true;
}

bool GLTFLoader::readPositions(const tinygltf::Accessor& accessor, std::vector<glm::vec3>& positions) {
    if (accessor.bufferView < 0 || accessor.bufferView >= static_cast<int>(gltf_model_.bufferViews.size())) {
        return false;
    }

    const auto& buffer_view = gltf_model_.bufferViews[accessor.bufferView];

    if (buffer_view.buffer < 0 || buffer_view.buffer >= static_cast<int>(gltf_model_.buffers.size())) {
        return false;
    }

    const auto& buffer = gltf_model_.buffers[buffer_view.buffer];

    size_t offset = buffer_view.byteOffset + accessor.byteOffset;
    size_t stride = buffer_view.byteStride > 0 ? buffer_view.byteStride : sizeof(float) * 3;
    size_t count = accessor.count;

    size_t required_size = offset + count * stride;
    if (required_size > buffer.data.size()) {
        std::cerr << "Buffer overflow in readPositions: required " << required_size
                  << ", available " << buffer.data.size() << std::endl;
        return false;
    }

    positions.resize(count);
    const unsigned char* data = buffer.data.data() + offset;
    for (size_t i = 0; i < count; ++i) {
        const float* vertex = reinterpret_cast<const float*>(data + i * stride);
        positions[i] = glm::vec3(vertex[0], vertex[1], vertex[2]);
    }

    return true;
}

bool GLTFLoader::readNormals(const tinygltf::Accessor& accessor, std::vector<glm::vec3>& normals) {
    if (accessor.bufferView < 0 || accessor.bufferView >= static_cast<int>(gltf_model_.bufferViews.size())) {
        return false;
    }

    const auto& buffer_view = gltf_model_.bufferViews[accessor.bufferView];
    const auto& buffer = gltf_model_.buffers[buffer_view.buffer];

    size_t offset = buffer_view.byteOffset + accessor.byteOffset;
    size_t stride = buffer_view.byteStride > 0 ? buffer_view.byteStride : sizeof(float) * 3;
    size_t count = accessor.count;

    if (offset + count * stride > buffer.data.size()) {
        return false;
    }

    normals.resize(count);
    const unsigned char* data = buffer.data.data() + offset;
    for (size_t i = 0; i < count; ++i) {
        const float* normal = reinterpret_cast<const float*>(data + i * stride);
        normals[i] = glm::vec3(normal[0], normal[1], normal[2]);
    }

    return true;
}

bool GLTFLoader::readTexCoords(const tinygltf::Accessor& accessor, std::vector<glm::vec2>& texcoords) {
    if (accessor.bufferView < 0 || accessor.bufferView >= static_cast<int>(gltf_model_.bufferViews.size())) {
        return false;
    }

    const auto& buffer_view = gltf_model_.bufferViews[accessor.bufferView];
    const auto& buffer = gltf_model_.buffers[buffer_view.buffer];

    size_t offset = buffer_view.byteOffset + accessor.byteOffset;
    size_t stride = buffer_view.byteStride > 0 ? buffer_view.byteStride : sizeof(float) * 2;
    size_t count = accessor.count;

    if (offset + count * stride > buffer.data.size()) {
        return false;
    }

    texcoords.resize(count);
    const unsigned char* data = buffer.data.data() + offset;
    for (size_t i = 0; i < count; ++i) {
        const float* uv = reinterpret_cast<const float*>(data + i * stride);
        texcoords[i] = glm::vec2(uv[0], uv[1]);
    }
    return true;
}

bool GLTFLoader::readColors(const tinygltf::Accessor& accessor, std::vector<glm::vec4>& colors) {
    if (accessor.bufferView < 0 || accessor.bufferView >= static_cast<int>(gltf_model_.bufferViews.size())) {
        return false;
    }

    const auto& buffer_view = gltf_model_.bufferViews[accessor.bufferView];
    const auto& buffer = gltf_model_.buffers[buffer_view.buffer];

    size_t offset = buffer_view.byteOffset + accessor.byteOffset;
    size_t stride = buffer_view.byteStride > 0 ? buffer_view.byteStride : sizeof(float) * 4;
    size_t count = accessor.count;

    if (offset + count * stride > buffer.data.size()) {
        return false;
    }

    colors.resize(count);
    const unsigned char* data = buffer.data.data() + offset;

    // 根据组件类型处理颜色
    if (accessor.componentType == 5121) { // UNSIGNED_BYTE
        for (size_t i = 0; i < count; ++i) {
            const unsigned char* color_bytes = reinterpret_cast<const unsigned char*>(data + i * stride);
            colors[i] = glm::vec4(
                color_bytes[0] / 255.0f,
                color_bytes[1] / 255.0f,
                color_bytes[2] / 255.0f,
                accessor.type == "VEC4" ? (color_bytes[3] / 255.0f) : 1.0f
            );
        }
    } else if (accessor.componentType == 5126) { // FLOAT
        for (size_t i = 0; i < count; ++i) {
            const float* color_floats = reinterpret_cast<const float*>(data + i * stride);
            colors[i] = glm::vec4(
                color_floats[0],
                color_floats[1],
                color_floats[2],
                accessor.type == "VEC4" ? color_floats[3] : 1.0f
            );
        }
    } else {
        std::cerr << "Unsupported color componentType: " << accessor.componentType << std::endl;
        return false;
    }

    return true;
}

bool GLTFLoader::readIndices(const tinygltf::Accessor& accessor, std::vector<unsigned int>& indices) {
    if (accessor.bufferView < 0 || accessor.bufferView >= static_cast<int>(gltf_model_.bufferViews.size())) {
        return false;
    }

    const auto& buffer_view = gltf_model_.bufferViews[accessor.bufferView];
    const auto& buffer = gltf_model_.buffers[buffer_view.buffer];

    size_t offset = buffer_view.byteOffset + accessor.byteOffset;
    size_t count = accessor.count;

    indices.resize(count);
    const unsigned char* data = buffer.data.data() + offset;

    switch (accessor.componentType) {
        case 5121: { // UNSIGNED_BYTE
            for (size_t i = 0; i < count; ++i) {
                indices[i] = static_cast<unsigned int>(data[i]);
            }
            break;
        }
        case 5123: { // UNSIGNED_SHORT
            const unsigned short* short_data = reinterpret_cast<const unsigned short*>(data);
            for (size_t i = 0; i < count; ++i) {
                indices[i] = static_cast<unsigned int>(short_data[i]);
            }
            break;
        }
        case 5125: { // UNSIGNED_INT
            const unsigned int* int_data = reinterpret_cast<const unsigned int*>(data);
            for (size_t i = 0; i < count; ++i) {
                indices[i] = int_data[i];
            }
            break;
        }
        default:
            std::cerr << "Unsupported index componentType: " << accessor.componentType << std::endl;
            return false;
    }

    return true;
}

#ifdef DRACO_MESH_COMPRESSION_SUPPORTED
bool GLTFLoader::decodeDracoPrimitive(int draco_buffer_view,
                                      std::vector<glm::vec3>& positions,
                                      std::vector<glm::vec3>& normals,
                                      std::vector<glm::vec4>& colors,
                                      std::vector<unsigned int>& indices) {
    if (draco_buffer_view < 0 || draco_buffer_view >= static_cast<int>(gltf_model_.bufferViews.size())) {
        return false;
    }

    const auto& buffer_view = gltf_model_.bufferViews[draco_buffer_view];
    if (buffer_view.buffer < 0 || buffer_view.buffer >= static_cast<int>(gltf_model_.buffers.size())) {
        return false;
    }

    const auto& buffer = gltf_model_.buffers[buffer_view.buffer];
    size_t offset = buffer_view.byteOffset;
    size_t size = buffer_view.byteLength;

    if (offset + size > buffer.data.size()) {
        std::cerr << "Draco buffer overflow: offset=" << offset << " size=" << size
                  << " total=" << buffer.data.size() << std::endl;
        return false;
    }

    draco::Decoder decoder;
    draco::DecoderBuffer decoder_buffer;
    decoder_buffer.Init(reinterpret_cast<const char*>(buffer.data.data() + offset), size);

    auto type_statusor = draco::Decoder::GetEncodedGeometryType(&decoder_buffer);
    if (!type_statusor.ok() || type_statusor.value() != draco::TRIANGULAR_MESH) {
        return false;
    }

    auto mesh_statusor = decoder.DecodeMeshFromBuffer(&decoder_buffer);
    if (!mesh_statusor.ok()) {
        std::cerr << "Draco decode failed: " << mesh_statusor.status().error_msg_string() << std::endl;
        return false;
    }

    const std::unique_ptr<draco::Mesh>& mesh = mesh_statusor.value();
    if (!mesh) return false;

    const draco::PointAttribute* pos_attr = mesh->GetNamedAttribute(draco::GeometryAttribute::POSITION);
    if (!pos_attr) return false;

    size_t num_points = mesh->num_points();
    positions.resize(num_points);
    for (draco::PointIndex i(0); i < static_cast<uint32_t>(num_points); ++i) {
        float v[3] = {0.f, 0.f, 0.f};
        const uint8_t* src = pos_attr->GetAddress(pos_attr->mapped_index(i));
        if (src) std::memcpy(v, src, sizeof(float) * 3);
        positions[i.value()] = glm::vec3(v[0], v[1], v[2]);
    }

    const draco::PointAttribute* norm_attr = mesh->GetNamedAttribute(draco::GeometryAttribute::NORMAL);
    if (norm_attr) {
        normals.resize(num_points);
        for (draco::PointIndex i(0); i < static_cast<uint32_t>(num_points); ++i) {
            float v[3] = {0.f, 1.f, 0.f};
            const uint8_t* src = norm_attr->GetAddress(norm_attr->mapped_index(i));
            if (src) std::memcpy(v, src, sizeof(float) * 3);
            normals[i.value()] = glm::vec3(v[0], v[1], v[2]);
        }
    }

    const draco::PointAttribute* color_attr = mesh->GetNamedAttribute(draco::GeometryAttribute::COLOR);
    if (color_attr) {
        colors.resize(num_points);
        for (draco::PointIndex i(0); i < static_cast<uint32_t>(num_points); ++i) {
            float v[4] = {1.f, 1.f, 1.f, 1.f}; // 默认白色
            const uint8_t* src = color_attr->GetAddress(color_attr->mapped_index(i));
            if (src) {
                if (color_attr->num_components() == 3) {
                    std::memcpy(v, src, sizeof(float) * 3);
                    v[3] = 1.0f; // alpha设为1
                } else if (color_attr->num_components() == 4) {
                    std::memcpy(v, src, sizeof(float) * 4);
                }
            }
            colors[i.value()] = glm::vec4(v[0], v[1], v[2], v[3]);
        }
    }

    size_t num_faces = mesh->num_faces();
    indices.reserve(num_faces * 3);
    for (draco::FaceIndex f(0); f < static_cast<uint32_t>(num_faces); ++f) {
        const draco::Mesh::Face& face = mesh->face(f);
        indices.push_back(face[0].value());
        indices.push_back(face[1].value());
        indices.push_back(face[2].value());
    }

    return true;
}
#endif
