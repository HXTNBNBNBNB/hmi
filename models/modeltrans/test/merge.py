# merge_internal_meshes.py
import trimesh
import numpy as np
import sys
import os

def combine_meshes_in_glb(input_path, output_path):
    print(f"Loading {input_path}...")
    scene = trimesh.load(input_path)

    if not isinstance(scene, trimesh.Scene):
        # 如果加载的是单个 mesh，直接保存
        print("Input is a single mesh. Saving as-is.")
        scene.export(output_path)
        return

    # 收集所有 mesh 几何体及其世界变换
    meshes = []
    for node_name in scene.graph.nodes_geometry:
        transform, geom_name = scene.graph[node_name]
        geom = scene.geometry[geom_name]
        if isinstance(geom, trimesh.Trimesh):
            # 应用世界变换（将局部坐标转为世界坐标）
            mesh_transformed = geom.copy()
            mesh_transformed.apply_transform(transform)
            meshes.append(mesh_transformed)
        else:
            print(f"Skipping non-mesh geometry: {geom_name}")

    if not meshes:
        raise ValueError("No valid meshes found in the scene!")

    print(f"Found {len(meshes)} meshes. Combining...")

    # 方法 1：简单拼接（保留各自材质 → 多材质 mesh）
    combined = trimesh.util.concatenate(meshes)

    # 创建新场景并导出
    new_scene = trimesh.Scene(combined)
    print(f"Saving combined mesh to {output_path}...")
    new_scene.export(output_path)
    print("✅ Done! All meshes merged into a single mesh.")

if __name__ == "__main__":
    if len(sys.argv) != 3:
        print("Usage: python merge_internal_meshes.py <input.glb> <output.glb>")
        sys.exit(1)

    input_glb, output_glb = sys.argv[1], sys.argv[2]
    if not os.path.exists(input_glb):
        print("Error: Input file not found.")
        sys.exit(1)

    combine_meshes_in_glb(input_glb, output_glb)
