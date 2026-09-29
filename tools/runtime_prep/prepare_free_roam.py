#!/usr/bin/env python3
"""Private original road-network radar, including shortcuts; no invented streets."""
import argparse,json,struct,math
from pathlib import Path
import numpy as np
from prepare_world import chunks

def prepare(game,out):
    if out.exists():raise ValueError('use a new output file')
    b=(game/'extracted-world/app/TRACKS/L2RA.BUN').read_bytes()
    a,end=next((s,e) for t,s,e in chunks(b) if t==0x3b800)
    def table(tag,stride):
        p=b.find(tag,a,a+256)
        if p<0:raise ValueError('missing road table')
        flags,n,off=struct.unpack_from('<III',b,p+4);start=p+off
        if flags>>8!=n*stride or start+n*stride>end:raise ValueError('road table bounds')
        return start,n
    def convert(p):return np.asarray([p[2],-p[0],p[1]],dtype=float)
    origin=np.array(json.loads((game/'para-vita/ux0/data/nfsmw-vita/world/world.json').read_text())['origin'])
    start,n=table(b'dnNR',32);nodes=[]
    for i in range(n):
        x,y,z,index=struct.unpack_from('<3fh',b,start+i*32)
        if index!=i:raise ValueError('road node index')
        nodes.append(convert((x,y,z)))
    start,n=table(b'gsNR',22);roads=[]
    for i in range(n):
        n0,n1,length,road,index,flags,eh,sh,*handles=struct.unpack_from('<HHHhHHHH6b',b,start+i*22)
        if index!=i or max(n0,n1)>=len(nodes):raise ValueError('road segment index')
        p0,p3=nodes[n0],nodes[n1];steps=max(2,math.ceil(np.linalg.norm(p3-p0)/12));t=np.linspace(0,1,steps+1)[:,None]
        if flags&256:
            p1=p0+convert(handles[3:])*sh*(500/(127*65535));p2=p3+convert(handles[:3])*eh*(500/(127*65535));curve=(1-t)**3*p0+3*(1-t)**2*t*p1+3*(1-t)*t*t*p2+t**3*p3
        else:curve=p0*(1-t)+p3*t
        curve-=origin;roads.extend(zip(curve[:-1],curve[1:]))
    if not 0<len(roads)<=65536:raise ValueError('radar road budget')
    data=np.asarray(roads,dtype='<f4')
    if not np.isfinite(data).all() or np.abs(data).max()>=32768:raise ValueError('road coordinate bounds')
    out.parent.mkdir(parents=True,exist_ok=True);out.write_bytes(struct.pack('<4sII',b'NFRM',1,len(roads))+data.tobytes());print('Radar:',len(nodes),'nodes,',n,'original segments,',len(roads),'line pieces')
if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('game',type=Path);p.add_argument('output',type=Path);a=p.parse_args();prepare(a.game,a.output)
