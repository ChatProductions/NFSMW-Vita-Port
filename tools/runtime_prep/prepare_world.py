#!/usr/bin/env python3
"""Private MW2005 PC geometry probe. Format references: NFSTools/NFS-ModTools.
Independent bounded reader; no game code execution. Original files stay private.
"""
import struct, math, json, argparse
from pathlib import Path

def chunks(b,a=0,z=None,depth=0):
    z=len(b) if z is None else z
    if depth>12:raise ValueError('chunk nesting')
    while a<z:
        if a+8>z:raise ValueError('short chunk')
        tag,n=struct.unpack_from('<II',b,a);s=a+8;e=s+n
        if e>z:raise ValueError(f'chunk overflow {a:x}/{tag:x}')
        yield tag,s,e
        a=e

def leaves(b,a,z):
    for t,s,e in chunks(b,a,z):
        if t==0x80134100:yield from leaves(b,s,e)
        else:yield t,s,e

def aligned(s,n):return (s+n-1)&~(n-1)
def solid(b,a,z):
    mats=[];buffers=[];idx=None;name='';key=0;textures=[]
    for t,s,e in leaves(b,a,z):
        if t==0x134011:
            s=aligned(s,16)
            if e-s<161 or b[s+12]!=0x16:raise ValueError('solid header')
            key=struct.unpack_from('<I',b,s+16)[0]
            name=b[s+160:e].split(b'\0')[0].decode('ascii')
        elif t==0x134012:
            textures=[struct.unpack_from('<I',b,p)[0] for p in range(s,e,8)]
        elif t==0x134b02:
            s=aligned(s,16)
            if (e-s)%104:raise ValueError('material size')
            stream=0;last=None
            for p in range(s,e,104):
                effect=struct.unpack_from('<I',b,p+48)[0]
                if last is not None and effect!=last:stream+=1
                nv,nt=struct.unpack_from('<II',b,p+60)
                mats.append((stream,nv,nt,effect,b[p+24]));last=effect
        elif t==0x134b01:buffers.append((aligned(s,128),e))
        elif t==0x134b03:idx=aligned(s,16);idx_end=e
    if not name or idx is None:return None
    counts=[0]*len(buffers)
    for st,nv,nt,ef,tex in mats:
        if st>=len(counts):raise ValueError('stream index')
        counts[st]+=nv
    arrays=[];uv_arrays=[]
    for (s,e),n in zip(buffers,counts):
        if not n or (e-s)%n:raise ValueError(f'{name}: vertex stride')
        stride=(e-s)//n
        if stride not in (24,36,44,60):raise ValueError(f'{name}: stride {stride}')
        vs=[struct.unpack_from('<3f',b,s+i*stride) for i in range(n)]
        if not all(math.isfinite(x) and abs(x)<100000 for v in vs for x in v):raise ValueError('vertex value')
        arrays.append(vs)
        uv_offset=28 if stride>=36 else 16
        uv_arrays.append([struct.unpack_from('<2f',b,s+i*stride+uv_offset) for i in range(n)])
    tris=[];uvs=[];texture_ids=[]
    for st,nv,nt,ef,tex in mats:
        if idx+nt*6>idx_end:raise ValueError('index overflow')
        for p in range(idx,idx+nt*6,6):
            ids=struct.unpack_from('<3H',b,p)
            if max(ids)>=len(arrays[st]):raise ValueError('vertex index')
            tris.append(tuple(arrays[st][i] for i in ids))
            uvs.append(tuple(uv_arrays[st][i] for i in ids))
            texture_ids.append(textures[tex] if tex<len(textures) else 0)
        idx+=nt*6
    return {'name':name,'key':key,'triangles':tris,'uvs':uvs,'texture_ids':texture_ids}

def objects(b):
    def walk(a,z):
        for t,s,e in chunks(b,a,z):
            if t==0x80134010:
                obj=solid(b,s,e)
                if obj:yield obj
            elif t in (0x80134000,0x80134001):yield from walk(s,e)
    return list(walk(0,len(b)))

def scenery(b,a,z):
    infos=[];instances=[];section=None
    for t,s,e in chunks(b,a,z):
        if t==0x34101:section=struct.unpack_from('<i',b,s+12)[0]
        elif t==0x34102:
            if (e-s)%72:raise ValueError('scenery info')
            infos=[(b[p:p+24].split(b'\0')[0].decode('ascii'),list(struct.unpack_from('<4I',b,p+24))) for p in range(s,e,72)]
        elif t==0x34103:
            s=aligned(s,16)
            if (e-s)%64:raise ValueError('scenery instance')
            for p in range(s,e,64):
                position=struct.unpack_from('<3f',b,p+32)
                rotation=struct.unpack_from('<9h',b,p+44)
                info=struct.unpack_from('<h',b,p+62)[0]
                instances.append((info,position,rotation))
    out=[]
    for i,p,r in instances:
        if i<0 or i>=len(infos):raise ValueError('scenery info index')
        name,keys=infos[i];out.append({'name':name,'key':keys[0],'lod_keys':keys,'position':p,'rotation':[x/8192 for x in r]})
    return section,out

def inventory(root,out):
    world=(root/'TRACKS/STREAML2RA.BUN').read_bytes()
    os=objects(world);instances=[]
    for t,s,e in chunks(world):
        if t==0x80034100:
            section,items=scenery(world,s,e)
            for it in items:it['section']=section
            instances.extend(items)
    report={'objects':[{'name':o['name'],'key':o['key'],'triangles':len(o['triangles'])} for o in os], 'instances':instances}
    out.write_text(json.dumps(report,indent=2));print(len(os),'objects',len(instances),'instances')
    return os,instances
if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('root',type=Path);p.add_argument('report',type=Path);a=p.parse_args();inventory(a.root,a.report)
