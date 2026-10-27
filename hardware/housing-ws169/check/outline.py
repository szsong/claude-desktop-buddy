import trimesh, sys, numpy as np
from trimesh.intersections import mesh_plane
from shapely.geometry import LineString
from shapely.ops import polygonize, unary_union
m = trimesh.load(sys.argv[1])
for z in [2.5, 3.3, 3.6, 3.75, 1.0]:
    seg=mesh_plane(m,[0,0,1],[0,0,z])
    polys=list(polygonize(unary_union([LineString(np.round(s[:,:2],3)) for s in seg if np.linalg.norm(s[0]-s[1])>1e-6])))
    P=max(polys,key=lambda p:p.area).simplify(0.02)
    b=P.bounds
    print(z, [round(v,2) for v in b], round(P.area,1), len(P.exterior.coords))
    if z==2.5:
        pts=np.array(P.exterior.coords)[:-1]
        open('glass_outline.scad','w').write("// cover glass outline at z = 2.5, sliced from Waveshare's STEP\nglass_pts = [" + ", ".join(f"[{x:.2f}, {y:.2f}]" for x,y in pts) + "];\n")
    # offset of outline vs 2.5
    if z!=2.5:
        pass
