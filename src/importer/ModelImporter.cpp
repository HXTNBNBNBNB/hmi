#include "importer/ModelImporter.hpp"
#include "loader/GLTFLoader.hpp"
#include <iostream>
#include <filesystem>
#include <algorithm>

ModelImporter* ModelImporter::instance_ = nullptr;

ModelImporter& ModelImporter::getInstance() {
    if (!instance_) {
        instance_ = new ModelImporter();
    }
    return *instance_;
}

ModelImporter::ModelImporter()
    : model_search_path_("../resources/models/")
    , total_imported_(0)
    , successful_imports_(0)
    , failed_imports_(0)
{
    // 初始化支持的格式
    supported_formats_ = {".gltf", ".glb", ".obj"};
}

ModelImporter::~ModelImporter() {
    removeAllModels();
    if (instance_) {
        delete instance_;
        instance_ = nullptr;
    }
}

void ModelImporter::initialize() {
    std::filesystem::create_directories(model_search_path_);
}

void ModelImporter::setModelSearchPath(const std::string& path) {
    model_search_path_ = path;
    if (!model_search_path_.empty() && model_search_path_.back() != '/') {
        model_search_path_ += '/';
    }

    // 确保目录存在
    std::filesystem::create_directories(model_search_path_);
}

void ModelImporter::addSupportedFormat(const std::string& format) {
    // 转换为小写并确保以点开头
    std::string fmt = format;
    std::transform(fmt.begin(), fmt.end(), fmt.begin(), ::tolower);

    if (fmt[0] != '.') {
        fmt = "." + fmt;
    }

    // 检查是否已存在
    if (std::find(supported_formats_.begin(), supported_formats_.end(), fmt) == supported_formats_.end()) {
        supported_formats_.push_back(fmt);
    }
}

std::shared_ptr<BaseModel> ModelImporter::importModel(const std::string& model_id, const std::string& filepath) {
    total_imported_++;

    // 检查是否已导入
    auto existing = imported_models_.find(model_id);
    if (existing != imported_models_.end()) {
        return existing->second;
    }

    // 解析文件路径
    std::string full_path = resolveFilePath(filepath);

    // 检查文件是否存在
    if (!std::filesystem::exists(full_path)) {
        std::cerr << "Model file not found: " << full_path << std::endl;
        failed_imports_++;
        return nullptr;
    }

    // 根据文件扩展名选择导入器
    std::string extension = getFileExtension(full_path);

    std::shared_ptr<BaseModel> model = nullptr;

    if (extension == ".gltf" || extension == ".glb") {
        model = importGLTFModel(model_id, full_path);
    } else if (extension == ".obj") {
        model = importOBJModel(model_id, full_path);
    } else {
        std::cerr << "Unsupported file format: " << extension << std::endl;
        failed_imports_++;
        return nullptr;
    }

    if (model) {
        imported_models_[model_id] = model;
        successful_imports_++;
    } else {
        failed_imports_++;
        std::cerr << "Failed to import model '" << model_id << "'" << std::endl;
    }

    return model;
}

std::shared_ptr<BaseModel> ModelImporter::importModelFromMemory(const std::string& model_id, const std::string& data) {
    total_imported_++;

    // 这里实现从内存数据导入的逻辑
    // 简化实现：创建一个导入模型
    auto model = std::make_shared<ImportedModel>();
    model->setSourceFile("memory");
    model->setModelFormat("memory");

    // 实际实现需要解析内存中的数据
    // 这里只是示意

    if (model) {
        imported_models_[model_id] = model;
        successful_imports_++;
        std::cout << "Successfully imported model '" << model_id << "' from memory" << std::endl;
    } else {
        failed_imports_++;
    }

    return model;
}

std::unordered_map<std::string, std::shared_ptr<BaseModel>>
ModelImporter::importMultipleModels(const std::vector<std::pair<std::string, std::string>>& model_list) {

    std::unordered_map<std::string, std::shared_ptr<BaseModel>> results;

    for (const auto& [model_id, filepath] : model_list) {
        auto model = importModel(model_id, filepath);
        if (model) {
            results[model_id] = model;
        }
    }

    return results;
}

std::shared_ptr<BaseModel> ModelImporter::getModel(const std::string& model_id) {
    auto it = imported_models_.find(model_id);
    return (it != imported_models_.end()) ? it->second : nullptr;
}

void ModelImporter::removeModel(const std::string& model_id) {
    imported_models_.erase(model_id);
}

void ModelImporter::removeAllModels() {
    imported_models_.clear();
    total_imported_ = 0;
    successful_imports_ = 0;
    failed_imports_ = 0;
}

bool ModelImporter::hasModel(const std::string& model_id) const {
    return imported_models_.find(model_id) != imported_models_.end();
}

std::vector<std::string> ModelImporter::getAllModelIds() const {
    std::vector<std::string> ids;
    ids.reserve(imported_models_.size());

    for (const auto& pair : imported_models_) {
        ids.push_back(pair.first);
    }

    return ids;
}

size_t ModelImporter::getModelCount() const {
    return imported_models_.size();
}

float ModelImporter::getSuccessRate() const {
    if (total_imported_ == 0) return 0.0f;
    return static_cast<float>(successful_imports_) / static_cast<float>(total_imported_);
}

std::vector<std::string> ModelImporter::scanForModels(const std::string& directory) const {
    std::vector<std::string> model_files;

    try {
        for (const auto& entry : std::filesystem::directory_iterator(directory)) {
            if (entry.is_regular_file()) {
                std::string extension = entry.path().extension().string();
                std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);

                if (std::find(supported_formats_.begin(), supported_formats_.end(), extension)
                    != supported_formats_.end()) {
                    model_files.push_back(entry.path().string());
                }
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "Error scanning directory: " << e.what() << std::endl;
    }

    return model_files;
}

bool ModelImporter::isValidModelFile(const std::string& filepath) const {
    std::string extension = getFileExtension(filepath);
    return std::find(supported_formats_.begin(), supported_formats_.end(), extension)
           != supported_formats_.end();
}

std::string ModelImporter::getFileExtension(const std::string& filepath) const {
    std::string extension = std::filesystem::path(filepath).extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);
    return extension;
}

std::shared_ptr<BaseModel> ModelImporter::importGLTFModel(const std::string& model_id, const std::string& filepath) {
    GLTFLoader loader;

    if (!loader.loadFromFile(filepath)) {
        std::cerr << "GLTF loader error: " << loader.getErrorMessage() << std::endl;
        return nullptr;
    }

    auto model = std::make_shared<ImportedModel>();
    model->setSourceFile(filepath);
    model->setModelFormat("gltf");

    // 加载所有材质
    std::vector<PBRMaterial> materials;
    size_t mat_count = loader.getMaterialCount();
    materials.reserve(mat_count);
    for (size_t i = 0; i < mat_count; ++i) {
        PBRMaterial mat;
        if (loader.getMaterial(static_cast<int>(i), mat)) {
            materials.push_back(mat);
            std::cout << "Material[" << i << "] \"" << mat.name << "\" color=("
                      << mat.baseColorFactor.r << "," << mat.baseColorFactor.g << ","
                      << mat.baseColorFactor.b << ") metallic=" << mat.metallicFactor
                      << " roughness=" << mat.roughnessFactor << std::endl;
        }
    }

    // 使用新的子网格接口
    std::vector<SubMesh> all_submeshes;

    for (int m = 0; m < static_cast<int>(loader.getMeshCount()); ++m) {
        std::vector<SubMesh> mesh_submeshes;
        if (loader.getSubMeshes(m, mesh_submeshes)) {
            all_submeshes.insert(all_submeshes.end(),
                                mesh_submeshes.begin(), mesh_submeshes.end());
        }
    }

    if (!all_submeshes.empty()) {
        model->setSubMeshes(all_submeshes, materials);

        // 统计顶点数
        size_t total_verts = 0, total_indices = 0;
        for (const auto& sm : all_submeshes) {
            total_verts += sm.vertices.size();
            total_indices += sm.indices.size();
        }
        std::cout << "Loaded GLTF model with " << all_submeshes.size() << " submeshes, "
                  << total_verts << " vertices, " << total_indices << " indices" << std::endl;
    } else {
        std::cerr << "No submesh data extracted from GLTF file" << std::endl;
    }

    return model;
}

std::shared_ptr<BaseModel> ModelImporter::importOBJModel(const std::string& model_id, const std::string& filepath) {
    auto model = std::make_shared<ImportedModel>();
    model->setSourceFile(filepath);
    model->setModelFormat("obj");
    // OBJ import not implemented
    return model;
}

std::string ModelImporter::resolveFilePath(const std::string& filepath) {
    if (filepath.empty() || filepath[0] == '/') {
        return filepath;
    }
    return model_search_path_ + filepath;
}