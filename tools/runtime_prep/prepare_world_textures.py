#!/usr/bin/env python3
"""Create a private low-resolution diffuse texture pack for the entire map."""
import argparse,struct,json,io
from pathlib import Path
from PIL import Image
from read_car_textures import read

def world_alpha(image):
    # Source-copy materials ignore the image alpha, except punch-through cutouts.
    usage=image.info.get('mw_alpha_usage');blend=image.info.get('mw_alpha_blend')
    corrected=blend==0 and usage in (0,2) and image.getextrema()[3][0]!=255
    if corrected:
        image=image.copy();image.putalpha(255)
    return image,corrected

def prepare(game,out,raw128=False):
    out.mkdir(parents=True,exist_ok=True)
    textures=read(game/'extracted-world/app/TRACKS/STREAML2RA.BUN')
    textures.update(read(game/'extracted-world/app/TRACKS/L2RA.BUN'))
    table=[];mapping={};report=[];offset=0
    with (out/'textures.nft').open('wb') as f:
        for i,(key,(name,img)) in enumerate(sorted(textures.items()),1):
            usage=img.info.get('mw_alpha_usage');blend=img.info.get('mw_alpha_blend')
            img,corrected=world_alpha(img)
            native=img.size
            dims=tuple(max(8,min(128,1<<(v.bit_length()-1))) for v in native) if raw128 else (64,64)
            img=img.resize(dims,Image.Resampling.LANCZOS).convert('RGBA')
            if raw128:raw=img.tobytes()
            else:
                buf=io.BytesIO();img.save(buf,format='PNG');raw=buf.getvalue()
            table.append((offset,len(raw),*dims,int(img.getextrema()[3][0]==255)) if raw128 else (offset,len(raw)));mapping[str(key)]=i;f.write(raw);offset+=len(raw);report.append({'id':i,'hash':key,'name':name,'native_size':list(native),'size':list(dims),'alpha_usage':usage,'alpha_blend':blend,'source_copy_alpha_corrected':corrected})
    with (out/'textures.nfi').open('wb') as f:
        f.write(struct.pack('<4sII',b'NFTP',2 if raw128 else 1,len(table)))
        for row in table:f.write(struct.pack('<5I' if raw128 else '<II',*row))
    (out/'texture-map.json').write_text(json.dumps(mapping));(out/'textures.json').write_text(json.dumps({'count':len(table),'size_limit':[128,128] if raw128 else [64,64],'encoding':'RGBA8 raw' if raw128 else 'PNG','images':report,'limitations':['diffuse only','reduced resolution','no original normal/specular/lighting shaders']},indent=2));print('Prepared',len(table),'textures,',offset,'bytes',flush=True)
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('game',type=Path);p.add_argument('output',type=Path);p.add_argument('--raw128',action='store_true');a=p.parse_args();prepare(a.game,a.output,a.raw128)
