# Fit check of the one-piece case against Waveshare's STEP.
# usage: python fitcheck.py <dir with case_raw.stl and board169.stl> batt_x0 batt_y0 batt_z1
import trimesh, numpy as np, sys
D = sys.argv[1]
case = trimesh.load(D + '/case_raw.stl'); b = trimesh.load(D + '/board169.stl')
print('case watertight', case.is_watertight, flush=True)
_contains = case.contains
def contains(q, n=20000):   # in chunks, to keep memory down
    return np.concatenate([_contains(q[i:i + n]) for i in range(0, len(q), n)]) if len(q) else np.zeros(0, bool)
case.contains = contains
p = np.unique(np.round(np.vstack([b.vertices, b.triangles_center]), 3), axis=0)
glass = p[:, 2] > 1.99
def report(name, q):
    inside = case.contains(q)
    if not inside.any(): print(name, 'clear', flush=True); return
    d = trimesh.proximity.signed_distance(case, q[inside][:3000])
    print(name, 'HIT', inside.sum(), 'max depth %.3f' % d.max(), q[inside].min(0).round(2), q[inside].max(0).round(2), flush=True)
report('rest', p)
# press-in path: everything but the glass must be clear; the glass is
# expected to touch only the lips (lip_in + glass_clr deep)
for dz in [0.3, 0.6, 1, 2, 4, 8, 14]:
    report(f'sweep +{dz} board', p[~glass] + [0, 0, dz])
    report(f'sweep +{dz} glass', p[glass] + [0, 0, dz])
bx0, by0, bz1 = map(float, sys.argv[2:5])
g = np.mgrid[0:1:21j, 0:1:31j, 0:1:4j].reshape(3, -1).T
bp = np.array([bx0, by0, bz1 - 3.0]) + g * [20.7, 33.5, 3.0]
report('battery', bp)
for d in [(0.35,0,0),(-0.35,0,0),(0,0.35,0),(0,-0.35,0)]:
    report(f'battery moved {d}', bp + d)
gl = p[glass & (np.abs(p[:,0]) > 14)]
print('glass down 0.05 onto ledge:', case.contains(gl + [0, 0, -0.05]).sum())
print('glass up 0.15 into lips:', case.contains(p[glass] + [0, 0, 0.15]).sum())
btn = p[(p[:, 0] > 15.0) & (p[:, 2] < -2)]
for dx in [0.1, 0.2]: print('buttons +x', dx, case.contains(btn + [dx, 0, 0]).sum())
