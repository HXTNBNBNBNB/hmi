import trimesh
import numpy as np
import sys
import os

def normalize_model_to_height_one(input_path, output_path, target_height=1.0):
    """
    Normalize a glTF model:
    - Scale so that Y-axis height = target_height (default 1.0)
    - Move bottom to Y=0, center in XZ
    - Rotate from +Z forward to -X forward (rotate -90° around Y)
    """
    print(f"Loading: {input_path}")
    scene = trimesh.load(input_path)

    # Handle both single mesh and scene
    if isinstance(scene, trimesh.Scene):
        # Extract all geometry and concatenate into one mesh (for bbox calc)
        meshes = []
        for geom in scene.geometry.values():
            if isinstance(geom, trimesh.Trimesh):
                meshes.append(geom)
            elif hasattr(geom, 'vertices') and hasattr(geom, 'faces'):
                # Trimesh might load as other types (e.g., PointCloud), skip those
                meshes.append(trimesh.Trimesh(vertices=geom.vertices, faces=geom.faces))
        if not meshes:
            raise ValueError("No valid mesh geometry found in scene.")
        combined = trimesh.util.concatenate(meshes)
    else:
        combined = scene

    # Step 1: Compute bounding box
    bounds = combined.bounds  # shape (2, 3): [[min_x, min_y, min_z], [max_x, max_y, max_z]]
    if bounds is None:
        raise ValueError("Could not compute bounds. Mesh may be empty.")

    min_pt = bounds[0]
    max_pt = bounds[1]
    size = max_pt - min_pt
    original_height = size[1]  # Y dimension

    if original_height <= 0:
        raise ValueError("Model has zero or negative height!")

    # Step 2: Compute scale factor to make height = target_height
    scale_factor = target_height / original_height
    print(f"Original size: {size[0]:.2f} x {size[1]:.2f} x {size[2]:.2f} → Scaling by {scale_factor:.4f}")

    # Step 3: Apply transformations to the original scene (not just combined mesh)
    # We'll apply to each geometry individually to preserve structure
    if isinstance(scene, trimesh.Scene):
        for name, geom in scene.geometry.items():
            if isinstance(geom, trimesh.Trimesh):
                # a) Scale
                geom.apply_scale(scale_factor)
                # b) Translate to move bottom to Y=0 and center in XZ
                new_bounds = geom.bounds
                offset_x = -(new_bounds[0, 0] + new_bounds[1, 0]) / 2
                offset_y = -new_bounds[0, 1]  # move min_y to 0
                offset_z = -(new_bounds[0, 2] + new_bounds[1, 2]) / 2
                geom.apply_translation([offset_x, offset_y, offset_z])
                # c) Rotate around Y axis by -90 degrees (to face -X)
                rot_matrix = trimesh.transformations.rotation_matrix(
                    -np.pi / 2, [0, 1, 0]  # angle, axis
                )
                geom.apply_transform(rot_matrix)
    else:
        # Single mesh
        scene.apply_scale(scale_factor)
        new_bounds = scene.bounds
        offset_x = -(new_bounds[0, 0] + new_bounds[1, 0]) / 2
        offset_y = -new_bounds[0, 1]
        offset_z = -(new_bounds[0, 2] + new_bounds[1, 2]) / 2
        scene.apply_translation([offset_x, offset_y, offset_z])
        rot_matrix = trimesh.transformations.rotation_matrix(-np.pi / 2, [0, 1, 0])
        scene.apply_transform(rot_matrix)

    # Export
    print(f"Saving normalized model to: {output_path}")
    scene.export(output_path)
    print("✅ Done!")


if __name__ == "__main__":
    if len(sys.argv) != 3:
        print("Usage: python normalize_gltf.py <input.gltf> <output.gltf>")
        print("Example: python normalize_gltf.py truck.gltf truck_normalized.gltf")
        sys.exit(1)

    input_file = sys.argv[1]
    output_file = sys.argv[2]

    if not os.path.exists(input_file):
        print(f"Error: Input file {input_file} not found!")
        sys.exit(1)

    normalize_model_to_height_one(input_file, output_file)
