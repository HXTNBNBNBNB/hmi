import trimesh
import sys
import os

if len(sys.argv) != 2:
    print("Usage: python get_min_y.py <model.glb>", file=sys.stderr)
    sys.exit(1)

path = sys.argv[1]
if not os.path.exists(path):
    print("File not found", file=sys.stderr)
    sys.exit(1)

scene = trimesh.load(path, force='scene')
bounds = scene.bounds
if bounds is None:
    print("No geometry", file=sys.stderr)
    sys.exit(1)

print(bounds[0][1])  # min_y
