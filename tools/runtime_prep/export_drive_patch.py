#!/usr/bin/env python3
"""Export a private, small original Rockport patch and BMW for the driving test.
Route is newly derived for a patrol test; it is NOT the original 16.1.0 event.
"""
import argparse,json,struct,math,heapq,hashlib
from pathlib import Path
from prepare_world import chunks,solid,aligned,objects

def dist(a,b):return math.dist(a[:2],b[:2])
def ground(road,x,y,z):
    best=None
    for a,b,c in road:
        det=(b[1]-c[1])*(a[0]-c[0])+(c[0]-b[0])*(a[1]-c[1])
        if abs(det)<.001:continue
        u=((b[1]-c[1])*(x-c[0])+(c[0]-b[0])*(y-c[1]))/det
        v=((c[1]-a[1])*(x-c[0])+(a[0]-c[0])*(y-c[1]))/det
        if u<-.0001 or v<-.0001 or u+v>1.0001:continue
        h=u*a[2]+v*b[2]+(1-u-v)*c[2]
        if abs(h-z)<4 and (best is None or abs(h-z)<abs(best-z)):best=h
    return best

def route(road):
    centers=[tuple(sum(p[k] for p in tri)/3 for k in range(3)) for tri in road]
    edges={};adj=[set() for _ in road]
    for i,tri in enumerate(road):
        for j in range(3):
            key=tuple(sorted(tuple(round(v*50) for v in p) for p in (tri[j],tri[(j+1)%3])))
            for other in edges.get(key,[]):adj[i].add(other);adj[other].add(i)
            edges.setdefault(key,[]).append(i)
    def far(start):
        ds={start:0};prev={};q=[(0,start)]
        while q:
            d,i=heapq.heappop(q)
            if d!=ds[i]:continue
            for j in adj[i]:
                nd=d+dist(centers[i],centers[j])
                if nd<ds.get(j,math.inf):ds[j]=nd;prev[j]=i;heapq.heappush(q,(nd,j))
        return max(ds,key=ds.get),prev,ds
    _,_,component=max((far(i) for i in range(len(road))),key=lambda f:len(f[2]))
    a,_,_=far(next(iter(component)));b,prev,_=far(a);ids=[b]
    while ids[-1]!=a:ids.append(prev[ids[-1]])
    points=[centers[i] for i in reversed(ids)]
    def clear(a,b):
        steps=max(2,math.ceil(dist(a,b)*2))
        return all(ground(road,a[0]+(b[0]-a[0])*i/steps,a[1]+(b[1]-a[1])*i/steps,a[2]+(b[2]-a[2])*i/steps) is not None for i in range(steps+1))
    smooth=[points[0]];i=0
    while i+1<len(points):
        j=next((j for j in range(len(points)-1,i,-1) if clear(points[i],points[j])),i+1)
        smooth.append(points[j]);i=j
    # Resample and project to real road height, ensuring no airborne patrol segments.
    out=[smooth[0]]
    for a,b in zip(smooth,smooth[1:]):
        steps=max(1,math.ceil(dist(a,b)/3))
        for i in range(1,steps+1):
            t=i/steps;x=a[0]+(b[0]-a[0])*t;y=a[1]+(b[1]-a[1])*t;z=a[2]+(b[2]-a[2])*t
            h=ground(road,x,y,z)
            if h is None:raise ValueError('patrol route leaves road')
            out.append((x,y,h))
    if len(out)>128:raise ValueError('route cap')
    return out

def export(game,out):
    inv=json.loads((game/'world-inventory.json').read_text());items=[i for i in inv['instances'] if i['section']==101];keys={i['key'] for i in items}
    b=(game/'extracted-world/app/TRACKS/STREAML2RA.BUN').read_bytes();objs={}
    for t,s,e in chunks(b):
        if t!=0x80134000:continue
        for t,s,e in chunks(b,s,e):
            if t==0x80134010:
                key=struct.unpack_from('<I',b,aligned(s+8,16)+16)[0]
                if key in keys:objs[key]=solid(b,s,e)
    world=[];road=[];seen=set();names=[]
    for item in items:
        key=item['key'];signature=(key,*item['position'],*item['rotation'])
        if signature in seen or key not in objs:continue
        seen.add(signature);o=objs[key];name=o['name']
        if name.startswith(('SHD','SHADOW')) or 'TRACKBARRIERPLAYER' in name:continue
        r=item['rotation'];p=item['position']
        def transform(v):return tuple(sum(v[j]*r[j*3+k] for j in range(3))+p[k] for k in range(3))
        tris=[tuple(transform(v) for v in tri) for tri in o['triangles']]
        isroad=name.startswith('TRN_CT_ROAD')
        for tri in tris:
            a,b,c=tri;u=[b[i]-a[i] for i in range(3)];v=[c[i]-a[i] for i in range(3)]
            n=(u[1]*v[2]-u[2]*v[1],u[2]*v[0]-u[0]*v[2],u[0]*v[1]-u[1]*v[0]);mag=math.sqrt(sum(x*x for x in n))
            if mag<.001:continue
            if isroad and abs(n[2])/mag>.8:road.append(tri)
            base=(83,83,73) if isroad else (107,111,62) if any(x in name for x in ('TREE','GRASS','BUSH')) else (166,148,112)
            shade=.65+.35*abs((n[0]*.3+n[1]*.4+n[2]*.866)/mag)
            color=0xff000000|sum(min(255,int(c*shade))<<(8*i) for i,c in enumerate(base))
            world.append((tri,color))
        names.append(name)
    path=route(road);origin=path[0]
    def local(t):return tuple(tuple(v[i]-origin[i] for i in range(3)) for v in t)
    world=[(local(t),c) for t,c in world];road=[local(t) for t in road];path=[tuple(v[i]-origin[i] for i in range(3)) for v in path]
    carobjs=objects((game/'extracted-world/app/CARS/BMWM3GTR/GEOMETRY.BIN').read_bytes())
    car=[];car_names=[]
    for o in carobjs:
        if o['name'] not in ('BMWM3GTR_BASE_D','BMWM3GTR_KIT00_BODY_E'):continue
        car_names.append(o['name'])
        for t in o['triangles']:car.append((t,0xffb8b4a2))
    # PC car uses X forward, Z up; runtime uses Y forward, Z up.
    minz=min(v[2] for t,_ in car for v in t)
    car=[(tuple((-v[1],v[0],v[2]-minz) for v in t),c) for t,c in car]
    if not(0<len(world)<=30000 and 0<len(car)<=3000 and 0<len(road)<=6000):raise ValueError('scene budget')
    out.mkdir(parents=True,exist_ok=True)
    with (out/'rockport.nfw').open('wb') as f:
        f.write(struct.pack('<4s5I',b'NFW3',1,len(world),len(car),len(road),len(path)))
        for tris in (world,car,[(t,0) for t in road]):
            for t,c in tris:f.write(struct.pack('<9fI',*(x for v in t for x in v),c))
        for v in path:f.write(struct.pack('<3f',*v))
    report={'section':101,'origin':origin,'world_triangles':len(world),'car_triangles':len(car),'road_triangles':len(road),'path_points':len(path),'path_metres':sum(dist(a,b) for a,b in zip(path,path[1:])),'objects':names,'car_parts':car_names,'limitations':['flat colors, no textures','small section, not streamed city','derived patrol route, not original race','low LOD car body; separate wheels and damage parts pending']}
    (out/'world.json').write_text(json.dumps(report,indent=2));print(json.dumps(report,indent=2))
if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('game',type=Path);p.add_argument('output',type=Path);a=p.parse_args();export(a.game,a.output)
