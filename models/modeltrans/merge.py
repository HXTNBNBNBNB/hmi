from pygltflib import GLTF2

def reparent_and_transform(
    input_glb: str,
    output_glb: str,
    parent_node_name: str,      # 父节点名称，如 "car"
    child_node_name: str,       # 子节点名称，如 "wheel"
    local_transform: dict       # 相对于父节点的局部变换
):
    gltf = GLTF2().load(input_glb)

    # 查找节点索引
    parent_idx = None
    child_idx = None
    for i, node in enumerate(gltf.nodes):
        if node.name == parent_node_name:
            parent_idx = i
        if node.name == child_node_name:
            child_idx = i

    if parent_idx is None or child_idx is None:
        raise ValueError(f"未找到节点: parent={parent_node_name}, child={child_node_name}")

    # 从原场景中移除 child（避免重复）
    for scene in gltf.scenes:
        if child_idx in scene.nodes:
            scene.nodes.remove(child_idx)

    # 设置 child 的局部变换
    child_node = gltf.nodes[child_idx]
    child_node.translation = local_transform.get("translation", [0, 0, 0])
    child_node.rotation = local_transform.get("rotation", [0, 0, 0, 1])
    child_node.scale = local_transform.get("scale", [1, 1, 1])

    # 将 child 添加为 parent 的子节点
    parent_node = gltf.nodes[parent_idx]
    if parent_node.children is None:
        parent_node.children = []
    parent_node.children.append(child_idx)

    # 保存
    gltf.save(output_glb)
    print(f"✅ 已将 '{child_node_name}' 设为 '{parent_node_name}' 的子节点，并应用局部变换")
    print(f"输出: {output_glb}")


# ===== 使用示例 =====
if __name__ == "__main__":
    reparent_and_transform(
        input_glb="combined.glb",
        output_glb="reparented.glb",
        parent_node_name="CarBody",     # 替换为你的车体节点名
        child_node_name="Wheel_FrontLeft",  # 替换为你的轮胎节点名
        local_transform={
            "translation": [1.2, 0.3, 0.5],   # 相对于车体的位置
            "rotation": [0.0, 0.0, 0.0, 1.0], # 无旋转
            "scale": [1.0, 1.0, 1.0]
        }
    )
