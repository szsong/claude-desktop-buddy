import sys
from OCP.STEPControl import STEPControl_Reader
from OCP.BRepMesh import BRepMesh_IncrementalMesh
from OCP.StlAPI import StlAPI_Writer
from OCP.TopExp import TopExp_Explorer
from OCP.TopAbs import TopAbs_SOLID
from OCP.Bnd import Bnd_Box
from OCP.BRepBndLib import BRepBndLib
from OCP.BRepAlgoAPI import BRepAlgoAPI_Section
from OCP.gp import gp_Pln, gp_Pnt, gp_Dir
r = STEPControl_Reader(); r.ReadFile(sys.argv[1]); r.TransferRoots(); s = r.OneShape()
def bb(sh):
    b=Bnd_Box(); BRepBndLib.Add_s(sh,b,False); p,q=b.CornerMin(),b.CornerMax(); return [round(v,2) for v in (p.X(),p.Y(),p.Z(),q.X(),q.Y(),q.Z())]
# find the LCD module solids (those reaching z > 0.5)
ex=TopExp_Explorer(s,TopAbs_SOLID); lcd=[]
while ex.More():
    so=ex.Current(); b=bb(so)
    if b[5]>0.5: lcd.append(so); print("lcd solid",b)
    ex.Next()
for so in lcd:
  for z in [-3.5,-3,-2,-1,-0.5,0.2,0.6,1,1.5,2,2.5,3,3.3,3.6,3.75]:
    sec=BRepAlgoAPI_Section(so, gp_Pln(gp_Pnt(0,0,z),gp_Dir(0,0,1))); sec.Build()
    try: print(" z",z,bb(sec.Shape()))
    except Exception as e: print(" z",z,"empty")
BRepMesh_IncrementalMesh(s,0.05,False,0.3,True)
w=StlAPI_Writer(); w.Write(s, sys.argv[2])
