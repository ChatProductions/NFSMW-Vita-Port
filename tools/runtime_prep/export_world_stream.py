#!/usr/bin/env python3
"""Prepare the complete available Rockport scenery as spatial tiles, privately.
Requires numpy; source formats are read by prepare_world.py. Textured scenery, spatially clipped and sorted.
"""
import argparse,json,struct,collections,math,hashlib
from pathlib import Path
import numpy as np
from prepare_world import chunks,solid,aligned,scenery
from world_tiles import append_spatial,spatial_order,WORLD_DTYPE,UV_DTYPE
CELL=128

def export(game,output,textured=False):
    mapping=json.loads((game/'para-vita/ux0/data/nfsmw-vita/world/textures/texture-map.json').read_text()) if textured else {}
    textured_triangles=0;invalid_uv_triangles=0
    if output.exists():raise ValueError('use a new output directory; keep the tested pack intact')
    b=(game/'extracted-world/app/TRACKS/STREAML2RA.BUN').read_bytes()
    locations={};input_instances=[]
    for tag,start,end in chunks(b):
        if tag==0x80134000:
            for t,s,e in chunks(b,start,end):
                if t==0x80134010:
                    key=struct.unpack_from('<I',b,aligned(s+8,16)+16)[0]
                    locations.setdefault(key,(s,e))
        elif tag==0x80034100:
            section,items=scenery(b,start,end)
            for it in items:it['section']=section
            input_instances.extend(items)
    inv={'instances':input_instances}
    fallback={};unavailable=[]
    for it in input_instances:
        primary=it['key']
        selected=next((k for k in it['lod_keys'] if k in locations),None)
        if selected is None:unavailable.append(it);continue
        if selected!=primary:fallback.setdefault(str(primary),{'name':it['name'],'selected_key':selected,'instances':0})['instances']+=1
        it['key']=selected
    origin=json.loads((game/'para-vita/ux0/data/nfsmw-vita/world/world.json').read_text())['origin']
    groups=collections.defaultdict(list);seen=set();duplicate=0
    for i in inv['instances']:
        signature=(i['key'],*i['position'],*i['rotation'])
        if signature in seen:duplicate+=1;continue
        seen.add(signature);groups[i['key']].append(i)
    tiles={};resolved=set();empty_instances=0;sky_instances=0;excluded=0;triangles=0;instances=0;mesh_count=0
    boundary_triangles=fragment_triangles=0
    # Stable little-endian interleaved float XYZ[3] and packed RGBA.
    uvdtype=UV_DTYPE;dtype=WORLD_DTYPE
    for tag,start,end in chunks(b):
        if tag!=0x80134000:continue
        for tag,s,e in chunks(b,start,end):
            if tag!=0x80134010:continue
            key=struct.unpack_from('<I',b,aligned(s+8,16)+16)[0]
            if key not in groups or key in resolved:continue
            obj=solid(b,s,e)
            if obj is None:continue
            resolved.add(key);mesh_count+=1;name=obj['name']
            if name.upper().startswith(('SHD','SHADOW')) or 'TRACKBARRIERPLAYER' in name.upper():
                excluded+=len(groups[key]);continue
            # SkyRender detaches these templates from ordinary scenery models.
            # They need camera-relative transforms and replacement sky materials.
            if name in ('SKYDOME','SKYDOME_XENON','SKY_SPECULAR'):
                sky_instances+=len(groups[key]);continue
            source=np.asarray(obj['triangles'],dtype=np.float32)
            if source.size==0:empty_instances+=len(groups[key]);continue
            uv=np.asarray(obj['uvs'],dtype=np.float32)
            bad_uv=~np.isfinite(uv).all(axis=(1,2))|(np.abs(uv).max(axis=(1,2))>65536)
            uv[bad_uv]=0
            # Subtract a common integer offset per triangle: preserves native repeat addressing.
            uv-=np.floor(uv.min(axis=1))[:,None,:]
            bad_uv|=np.abs(uv).max(axis=(1,2))>4096
            uv[bad_uv]=0
            materials=np.array([mapping.get(str(k),0) for k in obj['texture_ids']],dtype=np.uint32)
            materials[bad_uv]=0;invalid_uv_triangles+=int(np.count_nonzero(bad_uv))*len(groups[key])
            for inst in groups[key]:
                points=source@np.asarray(inst['rotation'],dtype=np.float32).reshape(3,3)+np.asarray(inst['position'])-np.asarray(origin)
                if not np.isfinite(points).all() or np.abs(points).max()>=32768:raise ValueError('world coordinate bounds')
                normal=np.cross(points[:,1]-points[:,0],points[:,2]-points[:,0]);mag=np.linalg.norm(normal,axis=1)
                valid=mag>.001;points=points[valid];normal=normal[valid];mag=mag[valid]
                if not len(points):continue
                isroad=name.startswith('TRN') and not any(word in name for word in ('WATER','OCEAN','SKY'))
                base=(83,83,73) if 'ROAD' in name else (107,111,62) if any(x in name for x in ('TREE','GRASS','BUSH')) else (166,148,112)
                shade=.65+.35*np.abs((normal[:,0]*.3+normal[:,1]*.4+normal[:,2]*.866)/mag)
                color=np.full(len(points),0xff000000,dtype=np.uint32)
                for k,c in enumerate(base):color|=(np.minimum(255,c*shade).astype(np.uint32)<<(8*k))
                records=np.empty(len(points),dtype=dtype);records['p']=points;records['c']=color
                uvrecs=np.empty(len(points),dtype=uvdtype);uvrecs['uv']=uv[valid];uvrecs['material']=materials[valid]
                textured_triangles+=int(np.count_nonzero(materials[valid]))
                crossed,fragments=append_spatial(tiles,records,uvrecs,textured)
                boundary_triangles+=crossed;fragment_triangles+=fragments
                # Duplicate support triangles to every overlapping tile for seamless ground queries.
                if isroad:
                    roads=records[np.abs(normal[:,2])/mag>.8]
                    if len(roads):
                        lower=np.floor(roads['p'].min(axis=1)[:,:2]/CELL).astype(int)
                        upper=np.floor(roads['p'].max(axis=1)[:,:2]/CELL).astype(int)
                        for row,lo,hi in zip(roads,lower,upper):
                            if np.prod(hi-lo+1)>256:raise ValueError('oversized ground triangle')
                            raw=row.tobytes()
                            for x in range(lo[0],hi[0]+1):
                                for y in range(lo[1],hi[1]+1):tiles.setdefault((x,y),[bytearray(),bytearray(),bytearray()])[1].extend(raw)
                triangles+=len(points);instances+=1
            if mesh_count%500==0:print('meshes',mesh_count,'instances',instances,'tiles',len(tiles),flush=True)
    output.mkdir(parents=True,exist_ok=True)
    table=[];max_world=max_road=0;size=0
    with (output/'world.nfp').open('wb') as pack:
        for (x,y),(world,road,uvdata) in sorted(tiles.items()):
            nw,nr=len(world)//40,len(road)//40
            if nw>150000 or nr>80000:raise ValueError(f'tile budget exceeded {x},{y}: {nw}/{nr}')
            order=spatial_order(np.frombuffer(world,dtype=dtype))
            world=np.frombuffer(world,dtype=dtype)[order].tobytes()
            if textured:uvdata=np.frombuffer(uvdata,dtype=uvdtype)[order].tobytes()
            data=struct.pack('<4s3I',b'NFWT',2 if textured else 1,nw,nr)+world+uvdata+road
            table.append((x,y,nw,nr,size));pack.write(data);size+=len(data);max_world=max(max_world,nw);max_road=max(max_road,nr)
    if len(table)>65536:raise ValueError('world tile index budget')
    if size>512*1024*1024:raise ValueError('world pack budget')
    with (output/'world.nfi').open('wb') as f:
        f.write(struct.pack('<4s3I',b'NFWI',3 if textured else 2,CELL,len(table)))
        for row in table:f.write(struct.pack('<iiIII',*row))
    report={'spatial_layout':'clipped to 128m tiles, Morton XYZ clusters','boundary_source_triangles':boundary_triangles,'stored_world_triangles':fragment_triangles,'declared_lod_fallbacks':fallback,'excluded_sky_template_instances':sky_instances,'empty_mesh_instances':empty_instances,'invalid_uv_triangles':invalid_uv_triangles,'textured_triangles':textured_triangles,'diffuse_textures':textured,'origin':origin,'cell_metres':CELL,'tiles':len(table),'world_triangles':triangles,'instances_exported':instances,'unique_input_instances':len(seen),'duplicates_removed':duplicate,'excluded_shadow_or_event_barrier_instances':excluded,'unresolved_keys':len(set(groups)-resolved),'unresolved_instances':sum(len(groups[k]) for k in set(groups)-resolved),'max_world_triangles_per_tile':max_world,'max_ground_triangles_per_tile':max_road,'bytes':size,'sections_in_source':len({i['section'] for i in inv['instances']}),'limitations':['diffuse textures optional; no object collision','all available resolved scenery; not all game systems','ground approximated using nearly horizontal TRN mesh surfaces','event-dependent barriers omitted','original visibility, collisions, lighting, sky and dynamic scenery are not reproduced']}
    (output/'stream.json').write_text(json.dumps(report,indent=2));print(json.dumps(report,indent=2),flush=True)
if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('game',type=Path);p.add_argument('output',type=Path);p.add_argument('--textured',action='store_true');a=p.parse_args();export(a.game,a.output,a.textured)
