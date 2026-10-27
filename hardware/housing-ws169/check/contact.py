import trimesh, numpy as np
tub=trimesh.load('tub_raw.stl'); lid=trimesh.load('lid_raw.stl'); b=trimesh.load('board169.stl')
p=np.unique(np.round(np.vstack([b.vertices,b.triangles_center]),3),axis=0)
def n(mesh,q): return int(mesh.contains(q).sum())
glass=p[(p[:,2]>1.95)&(np.abs(p[:,0])>14)]
print('glass down 0.05 into tub:',n(tub,glass+[0,0,-0.05]))
print('glass up 0.10 into lid:',n(lid,p[p[:,2]>3.0]+[0,0,0.10]), ' up 0.03:',n(lid,p[p[:,2]>3.0]+[0,0,0.03]))
btn=p[(p[:,0]>15.0)&(p[:,2]<-2)]
for dx in [0.1,0.2,0.3]: print('buttons +x',dx,':',n(tub,btn+[dx,0,0]))
usb=p[(p[:,0]<-15.0)&(p[:,2]<-2)]
for dx in [0.2,0.35,0.5]: print('usb -x',dx,':',n(tub,usb+[-dx,0,0]))
g=p[p[:,2]>1.9]
for d in [0.1,0.2]:
  print('glass shift x',d,n(tub,g+[d,0,0]),' -x',n(tub,g+[-d,0,0]),' y',n(tub,g+[0,d,0]),' -y',n(tub,g+[0,-d,0]))
