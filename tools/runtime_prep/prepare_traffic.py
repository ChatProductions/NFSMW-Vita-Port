#!/usr/bin/env python3
"""Convert private stock traffic models and connected road pieces; no bundled game data."""
import argparse,json,struct,math
from pathlib import Path
from collections import defaultdict
from PIL import Image
from prepare_world import objects
from read_car_textures import read
from prepare_opponent import hash_name

def models(game,out):
    for model in ['TRAF4DSEDA','TRAFTAXI']:
        root=game/'extracted-traffic/app/CARS'/model
        all_objects=objects((root/'GEOMETRY.BIN').read_bytes())
        selected=[o for o in all_objects if o['name'] in {model+'_BASE_C',model+'_KIT00_BODY_C',model+'_KIT00_FRONT_WINDOW_C'}]
        wheel=next(o for o in all_objects if o['name']==model+'_KIT00_FRONT_TIRE_C')
        keys={k for o in selected+[wheel] for k in o['texture_ids']}
        textures=read(root/'TEXTURES.BIN')
        for p in [game/'extracted-world/app/CARS/TEXTURES.BIN',game/'extracted-probe/app/GLOBAL/GlobalB.lzc',game/'extracted-frontend/app/GLOBAL/GLOBALA.BUN']:textures.update(read(p,keys|{hash_name('WINDOW_FRONT')}))
        remap={hash_name(n):hash_name('WINDOW_FRONT') for n in ['WINDOW_LEFT_FRONT','WINDOW_LEFT_REAR','WINDOW_RIGHT_FRONT','WINDOW_RIGHT_REAR']}
        used=sorted({remap.get(k,k) for k in keys})
        if len(used)>16:raise ValueError('atlas budget')
        missing=[hex(k) for k in used if k not in textures]
        if missing:raise ValueError(f'{model}: unresolved materials {missing}')
        atlas=Image.new('RGBA',(512,512));slots={}
        for i,k in enumerate(used):
            name,img=textures[k];img=img.copy()
            # Opaque traffic materials; shader glass/reflections remain approximate.
            if 'WINDOW' in name:
                bg=Image.new('RGBA',img.size,(29,39,46,255));bg.alpha_composite(img);img=bg
            img.putalpha(255);img=img.resize((124,124),Image.Resampling.LANCZOS)
            x=(i%4)*128+2;y=(i//4)*128+2;atlas.paste(img,(x,y));slots[k]=(x,y)
        tris=[];parts=[];wy=[v[1] for t in wheel['triangles'] for v in t];ys=min(wy)+max(wy)
        def add(o,part=0,offset=(0,0,0),mirror=False):
            for tri,uv,key in zip(o['triangles'],o['uvs'],o['texture_ids']):
                x,y=slots[remap.get(key,key)];coords=[]
                for px,py,pz in tri:
                    if part:py=ys-py
                    if mirror:py=-py
                    coords.extend((-(py+offset[1]),px+offset[0],pz+offset[2]+.07))
                mapped=[q for u,v in uv for q in ((x+min(1,max(0,u))*123+.5)/512,(y+min(1,max(0,v))*123+.5)/512)]
                tris.append((*coords,*mapped,0xffffffff));parts.append(part)
        for o in selected:add(o)
        pivots=[]
        for axle,y in enumerate([1.35,-1.35]):
            for side,x in enumerate([.72,-.72]):
                offset=(y,x,.29);pivots.extend((-x,y,.36));add(wheel,1+axle*2+side,offset,side==1)
        dest=out/model;dest.mkdir(parents=True,exist_ok=True);atlas.save(dest/'car.png')
        (dest/'car.nfc').write_bytes(struct.pack('<4sII',b'NFCT',2,len(tris))+b''.join(struct.pack('<15fI',*t) for t in tris))
        radius=max(math.hypot(v[0],v[2]) for t in wheel['triangles'] for v in t)
        (dest/'car.nfa').write_bytes(struct.pack('<4sII15f',b'NFAW',1,len(parts),*pivots,radius,0,0)+bytes(parts))
        (dest/'car.json').write_text(json.dumps({'source':model,'triangles':len(tris),'materials':[textures[k][0] for k in used],'atlas':512,'limitations':['provisional axle offsets','opaque glass, no reflections','no original traffic brake-light material switching']},indent=2))
        print(model,len(tris),'triangles',len(used),'resolved materials')

def graph(game,out):
    data=(game/'para-vita/ux0/data/nfsmw-vita/world/roam.nfm').read_bytes();magic,version,n=struct.unpack_from('<4sII',data);assert magic==b'NFRM' and version==1 and len(data)==12+24*n
    roads=[]
    for v in struct.iter_unpack('<6f',data[12:]):
        a,b=v[:3],v[3:]
        if math.dist(a,b)<.1:continue
        roads.extend([(a,b),(b,a)])
    def node(p):return tuple(round(v,2) for v in p)
    starts=defaultdict(list)
    for i,(a,b) in enumerate(roads):starts[node(a)].append(i)
    packed=[]
    for i,(a,b) in enumerate(roads):
        direction=[b[k]-a[k] for k in range(3)];norm=math.dist(a,b)
        candidates=[]
        for j in starts[node(b)]:
            if j==(i^1):continue
            c,d=roads[j];dot=sum(direction[k]*(d[k]-c[k]) for k in range(3))/(norm*math.dist(c,d))
            if dot>-.5:candidates.append((dot,j))
        links=[j for dot,j in sorted(candidates,reverse=True)[:4]];links+=[0xffffffff]*(4-len(links))
        packed.append(struct.pack('<6f4I',*a,*b,*links))
    out.mkdir(parents=True,exist_ok=True);(out/'roads.nft').write_bytes(struct.pack('<4sII',b'NFTR',1,len(roads))+b''.join(packed))
    print('Traffic directed road pieces',len(roads))
if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('game',type=Path);p.add_argument('output',type=Path);a=p.parse_args();models(a.game,a.output);graph(a.game,a.output)
