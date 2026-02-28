import trimesh
import numpy as np
import sys
import os

def center_model_at_origin(input_path, output_path):
    """
    将 glTF 模型的几何包围盒中心移动到 (0, 0, 0)
    即：min + max / 2 == [0, 0, 0]
    """
    print(f"Loading model: {input_path}")
    scene = trimesh.load(input_path)

    # Handle both single mesh and scene
    if isinstance(scene, trimesh.Scene):
        # We'll apply transform to each geometry individually
        for name, geom in scene.geometry.items():
            if isinstance(geom, trimesh.Trimesh):
                bounds = geom.bounds
                if bounds is None:
                    print(f"Warning: Geometry '{name}' has no bounds. Skipping.")
                    continue
                center = (bounds[0] + bounds[1]) / 2.0
                geom.apply_translation(-center)
            else:
                print(f"Skipping non-mesh geometry: {type(geom)}")
    else:
        # Single mesh
        bounds = scene.bounds
        if bounds is not None:
            center = (bounds[0] + bounds[1]) / 2.0
            scene.apply_translation(-center)
        else:
            raise ValueError("Mesh has no valid bounds!")

    print(f"Saving centered model to: {output_path}")
    scene.export(output_path)
    print("✅ Model centered at (0, 0, 0) successfully!")


if __name__ == "__main__":
    if len(sys.argv) != 3:
        print("Usage: python center_gltf_at_origin.py <input.gltf> <output.gltf>")
        print("Example: python center_gltf_at_origin.py truck.gltf truck_centered.gltf")
        sys.exit(1)

    input_file = sys.argv[1]
    output_file = sys.argv[2]

    if not os.path.exists(input_file):
        print(f"Error: Input file {input_file} not found!")
        sys.exit(1)

    center_model_at_origin(input_file, output_file)
