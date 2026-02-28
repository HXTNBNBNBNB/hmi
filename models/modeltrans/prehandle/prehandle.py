# standardize_to_face_negative_x.py
import trimesh
import numpy as np
import os
import sys
import transforms3d as t3d

def standardize_model_to_face_negative_x(input_path, output_path):
    print(f"Processing: {os.path.basename(input_path)}")
    
    # 加载模型
    scene = trimesh.load(input_path)
    if not isinstance(scene, trimesh.Scene):
        scene = trimesh.Scene(scene)

    # 合并所有 mesh 到世界坐标
    all_vertices = []
    meshes = []
    for node_name in scene.graph.nodes_geometry:
        transform, geom_name = scene.graph[node_name]
        geom = scene.geometry[geom_name]
        if isinstance(geom, trimesh.Trimesh):
            m = geom.copy()
            m.apply_transform(transform)
            meshes.append(m)
            all_vertices.append(m.vertices)
    
    if not meshes:
        raise ValueError("No valid mesh found!")

    # 合并为单个 mesh（保留多材质）
    combined = trimesh.util.concatenate(meshes)
    
    # Step 1: 移动到底部中心（Y=0 为地面）
    bounds = combined.bounds
    min_y = bounds[0][1]
    center_xz = (bounds[0][[0, 2]] + bounds[1][[0, 2]]) / 2.0  # XZ 平面中心
    offset = np.array([center_xz[0], min_y, center_xz[1]])
    combined.vertices -= offset

    # Step 2: 旋转模型，使其朝向 -X
    # 假设原始模型“自然朝向”是其最长水平轴（PCA 主方向）
    # 获取水平顶点（忽略 Y）
    verts_xz = combined.vertices[:, [0, 2]]  # (N, 2)
    
    # 计算主方向（使用 PCA）
    centroid_xz = np.mean(verts_xz, axis=0)
    centered_xz = verts_xz - centroid_xz
    cov = np.cov(centered_xz.T)
    eigvals, eigvecs = np.linalg.eigh(cov)
    main_dir = eigvecs[:, np.argmax(eigvals)]  # 最大方差方向
    
    # 确保主方向指向“前”（我们定义长边为前向）
    # 如果模型对称（如球），则跳过旋转
    if np.linalg.norm(main_dir) < 1e-6:
        print("  Warning: Model is symmetric, keeping original orientation.")
        target_rotation = np.eye(4)
    else:
        # 当前主方向在 XZ 平面
        current_yaw = np.arctan2(main_dir[0], main_dir[1])  # 注意：main_dir = [x, z]
        
        # 我们希望模型朝 -X → 在 XZ 平面中，-X = (-1, 0)
        target_yaw = np.pi  # -X 方向的角度（从 +Z 起算：+Z=0, +X=π/2, -Z=π, -X=3π/2 或 -π/2）
        # 但更简单：直接计算从 current_dir 到 (-1, 0) 的旋转
        
        target_dir = np.array([-1.0, 0.0])  # -X in XZ
        dot = np.dot(main_dir, target_dir)
        det = main_dir[0] * target_dir[1] - main_dir[1] * target_dir[0]
        angle = np.arctan2(det, dot)  # 从 current 到 target 的有符号角
        
        # 构建绕 Y 轴的旋转矩阵
        target_rotation = t3d.axangles.axangle2mat([0, 1, 0], angle)
        target_rotation = np.vstack([np.column_stack([target_rotation, [0,0,0]]), [0,0,0,1]])

    # 应用旋转（绕原点）
    combined.apply_transform(target_rotation)

    # Step 3: 确保底部仍在 Y=0（旋转可能影响 Y，但因为我们绕 Y 轴转，Y 不变）
    # 所以无需再调整 Y

    # 创建新场景并导出
    new_scene = trimesh.Scene(combined)
    new_scene.export(output_path)
    print(f"  ✅ Saved to: {os.path.basename(output_path)}")
    print(f"    New bounds: {new_scene.bounds.ravel()}")
    print(f"    Centroid: {new_scene.centroid}")


def process_directory(input_dir, output_dir):
    os.makedirs(output_dir, exist_ok=True)
    for fname in os.listdir(input_dir):
        if fname.lower().endswith('.glb'):
            input_path = os.path.join(input_dir, fname)
            output_path = os.path.join(output_dir, fname)
            try:
                standardize_model_to_face_negative_x(input_path, output_path)
            except Exception as e:
                print(f"  ❌ Failed: {fname} - {e}")


if __name__ == "__main__":
    if len(sys.argv) != 3:
        print("Usage: python standardize_to_face_negative_x.py <input_folder> <output_folder>")
        print("This script will:")
        print("  - Move model origin to bottom center (Y=0)")
        print("  - Rotate model to face -X direction")
        print("  - Preserve real-world scale (1 unit = 1 meter)")
        print("  - Output clean GLB ready for rendering")
        sys.exit(1)

    input_folder = sys.argv[1]
    output_folder = sys.argv[2]

    if not os.path.isdir(input_folder):
        print(f"Error: Input folder '{input_folder}' not found.")
        sys.exit(1)

    process_directory(input_folder, output_folder)
    print("\n🎉 All models standardized!")
