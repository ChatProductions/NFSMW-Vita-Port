#!/usr/bin/env python3
"""Prepare a provisional textured Razor Mustang from private geometry/textures. See revision 00.15 limits."""
import argparse,json,struct,math
from pathlib import Path
from PIL import Image
from prepare_world import objects
from read_car_textures import read
from car_atlas import build_atlas

def hash_name(name):
    h=0xffffffff
    for c in name.encode():h=(h*33+c)&0xffffffff
    return h

def prepare(game,out,lod="C"):
    root=game/'extracted-race/app/CARS/MUSTANGGT';all_objs=objects((root/'GEOMETRY.BIN').read_bytes())
    names={'MUSTANGGT_BASE_'+lod,'MUSTANGGT_KIT00_BODY_'+lod,'MUSTANGGT_KIT00_INTERIOR_'+lod,'MUSTANGGT_KIT00_HOOD_'+lod,'MUSTANGGT_KIT00_SPOILER_'+lod,'MUSTANGGT_KIT00_FRONT_WINDOW_'+lod,'MUSTANGGT_KIT00_REAR_WINDOW_'+lod}
    for side in ['LEFT','RIGHT']:
        for part in ['HEADLIGHT','HEADLIGHT_GLASS','BRAKELIGHT']:names.add(f'MUSTANGGT_KIT00_{side}_{part}_'+lod)
    selected=[o for o in all_objs if o['name'] in names]
    wheel=next(o for o in all_objs if o['name']=='MUSTANGGT_KIT00_FRONT_TIRE_'+lod)
    wheel_y=[p[1] for tri in wheel['triangles'] for p in tri]
    wheel_y_sum=min(wheel_y)+max(wheel_y)
    wanted={k for o in selected+[wheel] for k in o['texture_ids']}
    wanted.add(hash_name('WINDOW_FRONT'))
    textures=read(root/'TEXTURES.BIN')
    for path in [game/'extracted-world/app/CARS/TEXTURES.BIN',game/'extracted-probe/app/GLOBAL/GlobalB.lzc',game/'extracted-frontend/app/GLOBAL/GLOBALA.BUN']:
        textures.update(read(path,wanted))
    vinyl=read(root/'VINYLS.BIN',{0xbdf8e37b})[0xbdf8e37b][1]
    skin=Image.new('RGBA',vinyl.size,(23,24,25,255));skin.alpha_composite(vinyl)
    textures[hash_name('MUSTANGGT_SKIN1')]=('MUSTANGGT_01RAZORCALLAHAN on approximate black base',skin)
    # Original undamaged side-window aliases (CarRender::SetCarGlassDamageState).
    remap={hash_name(name):hash_name('WINDOW_FRONT') for name in ['WINDOW_LEFT_FRONT','WINDOW_LEFT_REAR','WINDOW_RIGHT_FRONT','WINDOW_RIGHT_REAR']}
    for name in ['HEADLIGHT_LEFT','HEADLIGHT_RIGHT']:
        remap[hash_name(name)]=hash_name('MUSTANGGT_KIT00_HEADLIGHT_ON')
    for name in ['HEADLIGHT_GLASS_LEFT','HEADLIGHT_GLASS_RIGHT']:
        remap[hash_name(name)]=hash_name('MUSTANGGT_KIT00_HEADLIGHT_GLASS_ON')
    for name in ['BRAKELIGHT_LEFT','BRAKELIGHT_RIGHT','BRAKELIGHT_CENTRE']:
        remap[hash_name(name)]=hash_name('MUSTANGGT_KIT00_BRAKELIGHT_OFF')
    # Atlas with larger body slot and gutters; preserves material UVs.
    brake_off=hash_name('MUSTANGGT_KIT00_BRAKELIGHT_OFF');brake_on=hash_name('MUSTANGGT_KIT00_BRAKELIGHT_ON')
    if brake_on not in textures:raise ValueError('Original brake ON texture missing')
    used=sorted({remap.get(k,k) for k in wanted}|{brake_on})
    atlas,slots,report=build_atlas(textures,used,hash_name('MUSTANGGT_SKIN1'),hash_name('MUSTANGGT_MISC'),(brake_off,brake_on))
    opaque={int(item["material_hash"],16):item["opaque"] for item in report}
    tris=[];parts=[]
    def add(obj,offset=(0,0,0),mirror=False,part=0):
        for t,uv,key in zip(obj['triangles'],obj['uvs'],obj['texture_ids']):
            key=remap.get(key,key);x,y,span=slots[key]
            coords=[]
            for p in t:
                px,py,pz=p
                if 1<=part<=4:py=wheel_y_sum-py  # Put the exterior face outside, preserving tire bounds.
                if mirror:py=-py
                px+=offset[0];py+=offset[1];pz+=offset[2]
                coords.extend((-py,px,pz+.07))
            mapped=[]
            for u,v in uv:
                if not math.isfinite(u+v) or abs(u)>8 or abs(v)>8:raise ValueError('car UV bounds')
                mapped.extend(((x+max(0,min(1,u))*span+.5)/2048,(y+max(0,min(1,v))*span+.5)/2048))
            tris.append((*coords,*mapped,0xffffffff if opaque[key] else 0xfffffffe));parts.append(5 if key==brake_off else part)
    for o in selected:add(o)
    # Axle locations are provisional; replacing with VLT TireOffsets is tracked below.
    pivots=[]
    for axis,axle in enumerate([1.3,-1.4]):
        for side in range(2):
            offset=(axle,.67 if side==0 else -.67,.267)
            pivots.extend((-offset[1],offset[0],offset[2]+.07))
            add(wheel,offset,side==1,1+axis*2+side)
    if len(tris)>6000:raise ValueError('car triangle budget')
    out.mkdir(parents=True,exist_ok=True);atlas.save(out/'car.png')
    with (out/'car.nfc').open('wb') as f:
        f.write(struct.pack('<4sII',b'NFCT',2,len(tris)))
        for tri in tris:f.write(struct.pack('<15fI',*tri))
    radius=max(math.hypot(v[0],v[2]) for t in wheel['triangles'] for v in t)
    if not .1<radius<1:raise ValueError('Wheel radius bounds')
    off,on=slots[brake_off],slots[brake_on]
    with (out/'car.nfa').open('wb') as f:
        f.write(struct.pack('<4sII15f',b'NFAW',1,len(parts),*pivots,radius,(on[0]-off[0])/2048,(on[1]-off[1])/2048));f.write(bytes(parts))
    meta={'wheel_exterior_facing_out':True,'lod':lod,'wheel_radius':radius,'animated_parts':{str(i):parts.count(i) for i in range(6)},'triangles':len(tris),'atlas':[2048,2048],'textures':report,'vinyl':'MUSTANGGT_01RAZORCALLAHAN','limitations':['black paint approximate; original career preset not decoded','wheel axle positions provisional (not yet VLT TireOffsets)','wheel rotation and steering use prototype motion, not original tire slip','headlights fixed ON; brakes switch original OFF/ON atlas images','window material approximated; no reflections','atlas UVs clamped at material borders','world textures not changed']}
    (out/'car.json').write_text(json.dumps(meta,indent=2));print(json.dumps(meta,indent=2))
if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('game',type=Path);p.add_argument('output',type=Path);p.add_argument('--lod',choices=['C','D'],default='C');a=p.parse_args();prepare(a.game,a.output,a.lod)
