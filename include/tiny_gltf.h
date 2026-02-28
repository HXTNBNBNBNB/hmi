/*
 * TinyGLTF - 使用jsoncpp的GLTF加载器
 * 完整的GLTF 2.0解析实现
 */

#ifndef TINY_GLTF_H_
#define TINY_GLTF_H_

#include <json/json.h>
#include <cstring>
#include <map>
#include <string>
#include <vector>
#include <algorithm>
#include <iostream>
#include <fstream>
#include <sstream>
#include <cmath>
#include <cstdint>

namespace tinygltf {

// Base64 解码辅助
namespace detail {
inline std::vector<uint8_t> base64_decode(const std::string& encoded) {
    static const std::string chars =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    static int lookup[256] = {0};
    static bool init = false;
    if (!init) {
        std::memset(lookup, -1, sizeof(lookup));
        for (int i = 0; i < 64; i++) lookup[(unsigned char)chars[i]] = i;
        init = true;
    }

    std::vector<uint8_t> result;
    result.reserve(encoded.size() * 3 / 4);
    int val = 0, valb = -8;
    for (unsigned char c : encoded) {
        if (lookup[c] == -1) break;
        val = (val << 6) + lookup[c];
        valb += 6;
        if (valb >= 0) {
            result.push_back(static_cast<uint8_t>((val >> valb) & 0xFF));
            valb -= 8;
        }
    }
    return result;
}
} // namespace detail

// 基础数据结构
struct Vec3 {
    double x, y, z;
    Vec3() : x(0), y(0), z(0) {}
    Vec3(double x_, double y_, double z_) : x(x_), y(y_), z(z_) {}
};

struct Vec4 {
    double x, y, z, w;
    Vec4() : x(0), y(0), z(0), w(0) {}
    Vec4(double x_, double y_, double z_, double w_) : x(x_), y(y_), z(z_), w(w_) {}
};

struct Vec2 {
    double x, y;
    Vec2() : x(0), y(0) {}
    Vec2(double x_, double y_) : x(x_), y(y_) {}
};

// Buffer数据
struct Buffer {
    std::vector<uint8_t> data;
    std::string uri;
    size_t byteLength;
};

// BufferView
struct BufferView {
    int buffer;
    size_t byteOffset;
    size_t byteLength;
    int target;  // 34962 = ARRAY_BUFFER, 34963 = ELEMENT_ARRAY_BUFFER
    size_t byteStride;
};

// Accessor
struct Accessor {
    int bufferView;
    size_t byteOffset;
    int componentType;  // 5120=BYTE, 5121=UNSIGNED_BYTE, 5122=SHORT, 5123=UNSIGNED_SHORT, 5125=UNSIGNED_INT, 5126=FLOAT
    size_t count;
    std::string type;   // "SCALAR", "VEC2", "VEC3", "VEC4", "MAT2", "MAT3", "MAT4"
    bool normalized;
    std::vector<double> min;
    std::vector<double> max;
};

// Primitive图元
struct Primitive {
    std::map<std::string, int> attributes;  // POSITION, NORMAL, TEXCOORD_0, etc.
    int indices;      // -1 if no indices
    int material;     // -1 if no material
    int mode;         // 0=POINTS, 1=LINES, 2=LINE_LOOP, 3=LINE_STRIP, 4=TRIANGLES, 5=TRIANGLE_STRIP, 6=TRIANGLE_FAN

    // KHR_draco_mesh_compression 扩展
    bool has_draco_extension;
    int draco_buffer_view;                       // Draco 压缩数据所在的 bufferView
    std::map<std::string, int> draco_attributes; // Draco 属性映射 (e.g. "POSITION" -> 0)
};

// Mesh网格
struct Mesh {
    std::string name;
    std::vector<Primitive> primitives;
    std::vector<double> weights;  // 形状键权重
};

// Node节点
struct Node {
    std::string name;
    int mesh;         // -1 if no mesh
    int skin;         // -1 if no skin
    int camera;       // -1 if no camera
    std::vector<double> translation;  // [x, y, z]
    std::vector<double> rotation;     // [x, y, z, w]
    std::vector<double> scale;        // [x, y, z]
    std::vector<double> matrix;       // 4x4 matrix (16 elements)
    std::vector<int> children;
    std::vector<double> weights;      // 形状键权重
};

// Scene场景
struct Scene {
    std::string name;
    std::vector<int> nodes;
};

// PBR 金属-粗糙度材质参数
struct PbrMetallicRoughness {
    double baseColorFactor[4];  // RGBA, default [1,1,1,1]
    double metallicFactor;       // default 1.0
    double roughnessFactor;      // default 1.0
    PbrMetallicRoughness() : metallicFactor(1.0), roughnessFactor(1.0) {
        baseColorFactor[0] = 1.0; baseColorFactor[1] = 1.0;
        baseColorFactor[2] = 1.0; baseColorFactor[3] = 1.0;
    }
};

// 材质
struct Material {
    std::string name;
    PbrMetallicRoughness pbrMetallicRoughness;
    bool doubleSided;
    Material() : doubleSided(false) {}
};

// Asset信息
struct Asset {
    std::string version;
    std::string generator;
    std::string copyright;
};

// 主模型类
struct Model {
    std::vector<Buffer> buffers;
    std::vector<BufferView> bufferViews;
    std::vector<Accessor> accessors;
    std::vector<Mesh> meshes;
    std::vector<Material> materials;
    std::vector<Node> nodes;
    std::vector<Scene> scenes;
    Asset asset;
    int defaultScene;
    std::string base_dir_;  // 基目录，用于解析外部 .bin 文件路径

    bool LoadASCIIFromString(const std::string& str);
    bool LoadASCIIFromFile(const std::string& filename);
    bool LoadBinaryFromFile(const std::string& filename);

private:
    bool ParseJson(const Json::Value& root);

    // 辅助解析函数
    bool ParseBuffers(const Json::Value& buffers_array);
    bool ParseBufferViews(const Json::Value& bufferViews_array);
    bool ParseAccessors(const Json::Value& accessors_array);
    bool ParseMeshes(const Json::Value& meshes_array);
    bool ParseMaterials(const Json::Value& materials_array);
    bool ParseNodes(const Json::Value& nodes_array);
    bool ParseScenes(const Json::Value& scenes_array);
    bool ParseAsset(const Json::Value& asset_obj);

    // 数据转换辅助
    std::vector<double> JsonToArrayDouble(const Json::Value& json_array);
    std::vector<int> JsonToArrayInt(const Json::Value& json_array);
    std::map<std::string, int> JsonObjectToMap(const Json::Value& json_obj);
};

// 实现部分
inline bool Model::LoadASCIIFromFile(const std::string& filename) {
    // 提取基目录用于解析外部 .bin 文件路径
    size_t last_sep = filename.find_last_of("/\\");
    if (last_sep != std::string::npos) {
        base_dir_ = filename.substr(0, last_sep + 1);
    } else {
        base_dir_ = "./";
    }

    std::ifstream file(filename);
    if (!file.is_open()) {
        std::cerr << "Failed to open file: " << filename << std::endl;
        return false;
    }

    Json::Value root;
    Json::CharReaderBuilder builder;
    JSONCPP_STRING errs;

    if (!Json::parseFromStream(builder, file, &root, &errs)) {
        std::cerr << "JSON parsing failed: " << errs << std::endl;
        return false;
    }

    return ParseJson(root);
}

inline bool Model::LoadASCIIFromString(const std::string& str) {
    Json::Value root;
    Json::CharReaderBuilder builder;
    JSONCPP_STRING errs;
    std::istringstream stream(str);

    if (!Json::parseFromStream(builder, stream, &root, &errs)) {
        std::cerr << "JSON parsing failed: " << errs << std::endl;
        return false;
    }

    return ParseJson(root);
}

// GLB 二进制格式加载
// GLB 文件结构: 12字节头 + chunk0(JSON) + chunk1(BIN)
inline bool Model::LoadBinaryFromFile(const std::string& filename) {
    size_t last_sep = filename.find_last_of("/\\");
    if (last_sep != std::string::npos) {
        base_dir_ = filename.substr(0, last_sep + 1);
    } else {
        base_dir_ = "./";
    }

    std::ifstream file(filename, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "Failed to open GLB file: " << filename << std::endl;
        return false;
    }

    // 读取整个文件
    file.seekg(0, std::ios::end);
    size_t file_size = static_cast<size_t>(file.tellg());
    file.seekg(0, std::ios::beg);

    if (file_size < 12) {
        std::cerr << "GLB file too small: " << file_size << std::endl;
        return false;
    }

    std::vector<uint8_t> data(file_size);
    file.read(reinterpret_cast<char*>(data.data()), file_size);

    // 解析 GLB 头 (12 bytes)
    uint32_t magic, version, length;
    std::memcpy(&magic, data.data(), 4);
    std::memcpy(&version, data.data() + 4, 4);
    std::memcpy(&length, data.data() + 8, 4);

    if (magic != 0x46546C67) { // "glTF" in little-endian
        std::cerr << "Invalid GLB magic: 0x" << std::hex << magic << std::dec << std::endl;
        return false;
    }

    // 解析 chunks
    size_t offset = 12;
    std::string json_string;
    std::vector<uint8_t> bin_data;

    while (offset + 8 <= file_size) {
        uint32_t chunk_length, chunk_type;
        std::memcpy(&chunk_length, data.data() + offset, 4);
        std::memcpy(&chunk_type, data.data() + offset + 4, 4);
        offset += 8;

        if (offset + chunk_length > file_size) {
            std::cerr << "GLB chunk extends beyond file" << std::endl;
            break;
        }

        if (chunk_type == 0x4E4F534A) { // "JSON" in little-endian
            json_string.assign(reinterpret_cast<const char*>(data.data() + offset), chunk_length);
        } else if (chunk_type == 0x004E4942) { // "BIN\0" in little-endian
            bin_data.assign(data.data() + offset, data.data() + offset + chunk_length);
        }

        offset += chunk_length;
    }

    if (json_string.empty()) {
        std::cerr << "No JSON chunk found in GLB" << std::endl;
        return false;
    }

    // 解析 JSON
    Json::Value root;
    Json::CharReaderBuilder builder;
    JSONCPP_STRING errs;
    std::istringstream stream(json_string);

    if (!Json::parseFromStream(builder, stream, &root, &errs)) {
        std::cerr << "GLB JSON parsing failed: " << errs << std::endl;
        return false;
    }

    // 先解析 JSON 结构
    if (!ParseJson(root)) {
        return false;
    }

    // GLB 的 buffer[0] 数据来自 BIN chunk（覆盖 ParseBuffers 中可能为空的 data）
    if (!bin_data.empty() && !buffers.empty()) {
        buffers[0].data = std::move(bin_data);
    }

    return true;
}

// 完整的JSON解析实现
inline bool Model::ParseJson(const Json::Value& root) {
    // 解析asset信息
    if (root.isMember("asset")) {
        if (!ParseAsset(root["asset"])) {
            std::cerr << "Failed to parse asset" << std::endl;
            return false;
        }
    }

    // 解析buffers
    if (root.isMember("buffers")) {
        if (!ParseBuffers(root["buffers"])) {
            std::cerr << "Failed to parse buffers" << std::endl;
            return false;
        }
    }

    // 解析bufferViews
    if (root.isMember("bufferViews")) {
        if (!ParseBufferViews(root["bufferViews"])) {
            std::cerr << "Failed to parse bufferViews" << std::endl;
            return false;
        }
    }

    // 解析accessors
    if (root.isMember("accessors")) {
        if (!ParseAccessors(root["accessors"])) {
            std::cerr << "Failed to parse accessors" << std::endl;
            return false;
        }
    }

    // 解析materials (在meshes之前，确保材质数据可用)
    if (root.isMember("materials")) {
        if (!ParseMaterials(root["materials"])) {
            std::cerr << "Failed to parse materials" << std::endl;
            return false;
        }
    }

    // 解析meshes
    if (root.isMember("meshes")) {
        if (!ParseMeshes(root["meshes"])) {
            std::cerr << "Failed to parse meshes" << std::endl;
            return false;
        }
    }

    // 解析nodes
    if (root.isMember("nodes")) {
        if (!ParseNodes(root["nodes"])) {
            std::cerr << "Failed to parse nodes" << std::endl;
            return false;
        }
    }

    // 解析scenes
    if (root.isMember("scenes")) {
        if (!ParseScenes(root["scenes"])) {
            std::cerr << "Failed to parse scenes" << std::endl;
            return false;
        }
    }

    // 获取默认场景
    defaultScene = -1;
    if (root.isMember("scene")) {
        defaultScene = root["scene"].asInt();
    }

    std::cout << "GLTF parsing completed successfully:" << std::endl;
    std::cout << "  Buffers: " << buffers.size() << std::endl;
    std::cout << "  BufferViews: " << bufferViews.size() << std::endl;
    std::cout << "  Accessors: " << accessors.size() << std::endl;
    std::cout << "  Meshes: " << meshes.size() << std::endl;
    std::cout << "  Nodes: " << nodes.size() << std::endl;
    std::cout << "  Scenes: " << scenes.size() << std::endl;

    return true;
}

} // namespace tinygltf

// 辅助函数实现
namespace tinygltf {

// 数据转换辅助函数
inline std::vector<double> Model::JsonToArrayDouble(const Json::Value& json_array) {
    std::vector<double> result;
    if (json_array.isArray()) {
        for (const auto& val : json_array) {
            result.push_back(val.asDouble());
        }
    }
    return result;
}

inline std::vector<int> Model::JsonToArrayInt(const Json::Value& json_array) {
    std::vector<int> result;
    if (json_array.isArray()) {
        for (const auto& val : json_array) {
            result.push_back(val.asInt());
        }
    }
    return result;
}

inline std::map<std::string, int> Model::JsonObjectToMap(const Json::Value& json_obj) {
    std::map<std::string, int> result;
    if (json_obj.isObject()) {
        for (const auto& key : json_obj.getMemberNames()) {
            result[key] = json_obj[key].asInt();
        }
    }
    return result;
}

// 具体解析函数实现
inline bool Model::ParseAsset(const Json::Value& asset_obj) {
    if (!asset_obj.isObject()) return false;

    if (asset_obj.isMember("version")) {
        asset.version = asset_obj["version"].asString();
    }
    if (asset_obj.isMember("generator")) {
        asset.generator = asset_obj["generator"].asString();
    }
    if (asset_obj.isMember("copyright")) {
        asset.copyright = asset_obj["copyright"].asString();
    }

    return true;
}

inline bool Model::ParseBuffers(const Json::Value& buffers_array) {
    if (!buffers_array.isArray()) return false;

    buffers.clear();
    buffers.reserve(buffers_array.size());

    for (const auto& buffer_json : buffers_array) {
        Buffer buffer;
        if (buffer_json.isMember("byteLength")) {
            buffer.byteLength = buffer_json["byteLength"].asUInt64();
        }
        if (buffer_json.isMember("uri")) {
            buffer.uri = buffer_json["uri"].asString();

            // 解析 data URI (base64 内嵌) 或外部 .bin 文件
            const std::string& uri = buffer.uri;
            const std::string prefix = "data:application/octet-stream;base64,";
            const std::string prefix2 = "data:application/gltf-buffer;base64,";

            if (uri.compare(0, prefix.size(), prefix) == 0) {
                // base64 内嵌数据
                std::string encoded = uri.substr(prefix.size());
                buffer.data = detail::base64_decode(encoded);
                std::cout << "Decoded base64 buffer: " << buffer.data.size()
                          << " bytes (expected " << buffer.byteLength << ")" << std::endl;
            } else if (uri.compare(0, prefix2.size(), prefix2) == 0) {
                // 另一种常见的 base64 MIME 类型
                std::string encoded = uri.substr(prefix2.size());
                buffer.data = detail::base64_decode(encoded);
                std::cout << "Decoded base64 buffer: " << buffer.data.size()
                          << " bytes (expected " << buffer.byteLength << ")" << std::endl;
            } else if (uri.find("://") == std::string::npos) {
                // 外部 .bin 文件（相对路径）——在 LoadASCIIFromFile 中需要拼接基目录
                // 此处由 base_dir_ 辅助（见下方改动）
                std::string bin_path = base_dir_ + uri;
                std::ifstream bin_file(bin_path, std::ios::binary);
                if (bin_file.is_open()) {
                    bin_file.seekg(0, std::ios::end);
                    size_t file_size = static_cast<size_t>(bin_file.tellg());
                    bin_file.seekg(0, std::ios::beg);
                    buffer.data.resize(file_size);
                    bin_file.read(reinterpret_cast<char*>(buffer.data.data()), file_size);
                    std::cout << "Loaded external buffer: " << bin_path
                              << " (" << file_size << " bytes)" << std::endl;
                } else {
                    std::cerr << "Failed to open external buffer: " << bin_path << std::endl;
                    return false;
                }
            }
        }

        if (buffer.data.size() < buffer.byteLength) {
            std::cerr << "Warning: buffer data size (" << buffer.data.size()
                      << ") < declared byteLength (" << buffer.byteLength << ")" << std::endl;
        }

        buffers.push_back(buffer);
    }

    return true;
}

inline bool Model::ParseBufferViews(const Json::Value& bufferViews_array) {
    if (!bufferViews_array.isArray()) return false;

    bufferViews.clear();
    bufferViews.reserve(bufferViews_array.size());

    for (const auto& bv_json : bufferViews_array) {
        BufferView bv;
        bv.buffer = bv_json["buffer"].asInt();
        bv.byteOffset = bv_json.get("byteOffset", 0).asUInt64();
        bv.byteLength = bv_json["byteLength"].asUInt64();
        bv.target = bv_json.get("target", 0).asInt();
        bv.byteStride = bv_json.get("byteStride", 0).asUInt64();
        bufferViews.push_back(bv);
    }

    return true;
}

inline bool Model::ParseAccessors(const Json::Value& accessors_array) {
    if (!accessors_array.isArray()) return false;

    accessors.clear();
    accessors.reserve(accessors_array.size());

    for (const auto& acc_json : accessors_array) {
        Accessor acc;
        acc.bufferView = acc_json.get("bufferView", -1).asInt();
        acc.byteOffset = acc_json.get("byteOffset", 0).asUInt64();
        acc.componentType = acc_json["componentType"].asInt();
        acc.count = acc_json["count"].asUInt64();
        acc.type = acc_json["type"].asString();
        acc.normalized = acc_json.get("normalized", false).asBool();

        if (acc_json.isMember("min")) {
            acc.min = JsonToArrayDouble(acc_json["min"]);
        }
        if (acc_json.isMember("max")) {
            acc.max = JsonToArrayDouble(acc_json["max"]);
        }

        accessors.push_back(acc);
    }

    return true;
}

inline bool Model::ParseMeshes(const Json::Value& meshes_array) {
    if (!meshes_array.isArray()) return false;

    meshes.clear();
    meshes.reserve(meshes_array.size());

    for (const auto& mesh_json : meshes_array) {
        Mesh mesh;
        if (mesh_json.isMember("name")) {
            mesh.name = mesh_json["name"].asString();
        }

        if (mesh_json.isMember("weights")) {
            mesh.weights = JsonToArrayDouble(mesh_json["weights"]);
        }

        // 解析primitives
        if (mesh_json.isMember("primitives") && mesh_json["primitives"].isArray()) {
            for (const auto& prim_json : mesh_json["primitives"]) {
                Primitive prim;
                if (prim_json.isMember("attributes")) {
                    prim.attributes = JsonObjectToMap(prim_json["attributes"]);
                }
                prim.indices = prim_json.get("indices", -1).asInt();
                prim.material = prim_json.get("material", -1).asInt();
                prim.mode = prim_json.get("mode", 4).asInt(); // 默认TRIANGLES

                // 解析 KHR_draco_mesh_compression 扩展
                prim.has_draco_extension = false;
                prim.draco_buffer_view = -1;
                if (prim_json.isMember("extensions") && prim_json["extensions"].isObject()) {
                    const Json::Value& extensions = prim_json["extensions"];
                    if (extensions.isMember("KHR_draco_mesh_compression")) {
                        const Json::Value& draco = extensions["KHR_draco_mesh_compression"];
                        prim.has_draco_extension = true;
                        prim.draco_buffer_view = draco.get("bufferView", -1).asInt();
                        if (draco.isMember("attributes") && draco["attributes"].isObject()) {
                            prim.draco_attributes = JsonObjectToMap(draco["attributes"]);
                        }
                        std::cout << "  Found Draco extension: bufferView=" << prim.draco_buffer_view
                                  << ", attributes=" << prim.draco_attributes.size() << std::endl;
                    }
                }

                mesh.primitives.push_back(prim);
            }
        }

        meshes.push_back(mesh);
    }

    return true;
}

inline bool Model::ParseMaterials(const Json::Value& materials_array) {
    if (!materials_array.isArray()) return false;

    materials.clear();
    materials.reserve(materials_array.size());

    for (const auto& mat_json : materials_array) {
        Material mat;

        if (mat_json.isMember("name")) {
            mat.name = mat_json["name"].asString();
        }
        mat.doubleSided = mat_json.get("doubleSided", false).asBool();

        if (mat_json.isMember("pbrMetallicRoughness")) {
            const Json::Value& pbr = mat_json["pbrMetallicRoughness"];

            if (pbr.isMember("baseColorFactor") && pbr["baseColorFactor"].isArray()) {
                const Json::Value& bcf = pbr["baseColorFactor"];
                for (int i = 0; i < 4 && i < static_cast<int>(bcf.size()); ++i) {
                    mat.pbrMetallicRoughness.baseColorFactor[i] = bcf[i].asDouble();
                }
            }
            mat.pbrMetallicRoughness.metallicFactor = pbr.get("metallicFactor", 1.0).asDouble();
            mat.pbrMetallicRoughness.roughnessFactor = pbr.get("roughnessFactor", 1.0).asDouble();
        }

        materials.push_back(mat);
    }

    std::cout << "  Materials: " << materials.size() << std::endl;
    return true;
}

inline bool Model::ParseNodes(const Json::Value& nodes_array) {
    if (!nodes_array.isArray()) return false;

    nodes.clear();
    nodes.reserve(nodes_array.size());

    for (const auto& node_json : nodes_array) {
        Node node;
        if (node_json.isMember("name")) {
            node.name = node_json["name"].asString();
        }

        node.mesh = node_json.get("mesh", -1).asInt();
        node.skin = node_json.get("skin", -1).asInt();
        node.camera = node_json.get("camera", -1).asInt();

        if (node_json.isMember("translation")) {
            node.translation = JsonToArrayDouble(node_json["translation"]);
        }
        if (node_json.isMember("rotation")) {
            node.rotation = JsonToArrayDouble(node_json["rotation"]);
        }
        if (node_json.isMember("scale")) {
            node.scale = JsonToArrayDouble(node_json["scale"]);
        }
        if (node_json.isMember("matrix")) {
            node.matrix = JsonToArrayDouble(node_json["matrix"]);
        }
        if (node_json.isMember("children")) {
            node.children = JsonToArrayInt(node_json["children"]);
        }
        if (node_json.isMember("weights")) {
            node.weights = JsonToArrayDouble(node_json["weights"]);
        }

        nodes.push_back(node);
    }

    return true;
}

inline bool Model::ParseScenes(const Json::Value& scenes_array) {
    if (!scenes_array.isArray()) return false;

    scenes.clear();
    scenes.reserve(scenes_array.size());

    for (const auto& scene_json : scenes_array) {
        Scene scene;
        if (scene_json.isMember("name")) {
            scene.name = scene_json["name"].asString();
        }

        if (scene_json.isMember("nodes")) {
            scene.nodes = JsonToArrayInt(scene_json["nodes"]);
        }

        scenes.push_back(scene);
    }

    return true;
}

} // namespace tinygltf

#endif // TINY_GLTF_H_