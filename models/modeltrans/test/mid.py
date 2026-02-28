# reset_glb_to_origin.py
import trimesh
import numpy as np
import sys
import os

def reset_glb_to_clean_origin(input_path, output_path):
    print(f"Loading model: {input_path}")
    scene = trimesh.load(input_path)

    # 如果是单个 mesh，包装成 Scene
    if not isinstance(scene, trimesh.Scene):
        scene = trimesh.Scene(scene)

    # 计算整个场景的几何中心（基于所有 mesh 的顶点）
    all_vertices = []
    for node_name in scene.graph.nodes_geometry:
        transform, geom_name = scene.graph[node_name]
        geom = scene.geometry[geom_name]
        if isinstance(geom, trimesh.Trimesh):
            # 获取世界坐标下的顶点
            verts_world = trimesh.transform_points(geom.vertices, transform)
            all_vertices.append(verts_world)
    
    if not all_vertices:
        raise ValueError("No valid meshes found in the scene!")

    all_vertices = np.vstack(all_vertices)
    centroid = all_vertices.mean(axis=0)
    print(f"Original centroid: {centroid}")

    # 创建新场景
    new_scene = trimesh.Scene()

    # 遍历每个几何体，移除原始变换中的平移/旋转/缩放，
    # 只保留“居中后”的几何 + 单位变换
    for node_name in scene.graph.nodes_geometry:
        _, geom_name = scene.graph[node_name]
        geom = scene.geometry[geom_name]

        if not isinstance(geom, trimesh.Trimesh):
            continue

        # 创建副本并重置其自身变换（我们只关心顶点）
        clean_geom = geom.copy()

        # 将该 mesh 的顶点先转到世界空间（应用原 transform）
        # 然后减去整体 centroid，使其居中
        # 注意：这里我们不保留原局部 transform，而是“烘焙”进顶点
        orig_transform = scene.graph[node_name][0]
        world_verts = trimesh.transform_points(clean_geom.vertices, orig_transform)
        centered_verts = world_verts - centroid
        clean_geom.vertices = centered_verts

        # 添加到新场景，使用单位变换（平移0、旋转0、缩放1）
        new_scene.add_geometry(
            geometry=clean_geom,
            node_name=node_name,
            transform=np.eye(4)  # identity matrix
        )

    print(f"Saving cleaned model to: {output_path}")
    new_scene.export(output_path)
    print("✅ Done! Model is now at origin with scale=1 and rotation=0.")

if __name__ == "__main__":
    if len(sys.argv) != 3:
        print("Usage: python reset_glb_to_origin.py <input.glb> <output.glb>")
        print("This script will:")
        print("  - Move the model's geometric center to (0,0,0)")
        print("  - Reset rotation to zero")
        print("  - Reset scale to 1")
        sys.exit(1)

    input_file = sys.argv[1]
    output_file = sys.argv[2]

    if not os.path.exists(input_file):
        print(f"Error: Input file '{input_file}' not found.")
        sys.exit(1)

    reset_glb_to_clean_origin(input_file, output_file)
