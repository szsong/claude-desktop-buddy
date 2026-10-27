import trimesh, numpy as np, sys
S=sys.argv[1]
tub=trimesh.load(S+'/tub_raw.stl'); lid=trimesh.load(S+'/lid_raw.stl')
print('tub watertight',tub.is_watertight,'lid',lid.is_watertight)
b=trimesh.load(S+'/board169.stl')
pts=np.vstack([b.vertices, b.triangles_center])
# drop duplicates
pts=np.unique(np.round(pts,3),axis=0)
print('board points',len(pts))
def hits(mesh,p,shrink=0.0):
    # points inside mesh; ignore those within tolerance of surface by also testing small offsets
    return mesh.contains(p)
def report(name,mesh,p):
    inside=hits(mesh,p)
    q=p[inside]
    if len(q)==0: print(name,'clear'); return
    # penetration depth approx via closest distance
    d=trimesh.proximity.signed_distance(mesh,q[:5000])
    print(name,'HIT',len(q),'max depth %.3f'%d.max(),'bbox',q.min(0).round(2),q.max(0).round(2))
report('tub rest',tub,pts)
report('lid rest',lid,pts)
for dz in [0.5,1,2,3,4,6,8,10,14,18]:
    report(f'tub sweep +{dz}',tub,pts+[0,0,dz])
# battery box
bx0,by0,bz1=float(sys.argv[2]),float(sys.argv[3]),float(sys.argv[4])
g=np.mgrid[0:1:21j,0:1:31j,0:1:4j].reshape(3,-1).T
bp=np.array([bx0,by0,bz1-3.0])+g*[20,30,3.0]
report('battery vs tub',tub,bp)
for dz in [2,5,9]:
    report(f'battery sweep +{dz}',tub,bp+[0,0,dz])
# battery vs board
lo=np.array([bx0,by0,bz1-3]); hi=lo+[20,30,3]
inb=np.all((pts>lo)&(pts<hi),axis=1); print('board points inside battery',inb.sum(), pts[inb].max(0) if inb.any() else '')
