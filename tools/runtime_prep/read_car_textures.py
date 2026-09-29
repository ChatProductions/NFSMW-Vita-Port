"""MW2005 PC TPK image decoder (private preparation, Pillow).
Supports uncompressed and per-texture JDLZ/HUFF containers; see THIRD_PARTY.md.
"""
import struct,io
from PIL import Image
from extract_frontend import chunks,jdlz
from huff_texture import huff

def decode(h,pixels,compressed=False):
    ident=struct.unpack_from('<I',h,36)[0];name=h[12:36].split(b'\0')[0].decode('ascii')
    off,paloff,size,palsize=struct.unpack_from('<4I',h,48);w,height=struct.unpack_from('<HH',h,68);fmt=h[74]
    if not 0<w<=2048 or not 0<height<=2048:raise ValueError('texture dimensions')
    if compressed:paloff-=off;off=0
    raw=pixels[off:off+size]
    if fmt in (0x22,0x24,0x26):
        fourcc={0x22:b'DXT1',0x24:b'DXT3',0x26:b'DXT5'}[fmt];n=((w+3)//4)*((height+3)//4)*(8 if fmt==0x22 else 16)
        if len(raw)<n:raise ValueError('DXT bounds')
        hdr=bytearray(128);hdr[:4]=b'DDS ';struct.pack_into('<7I',hdr,4,124,0x81007,height,w,n,0,1);struct.pack_into('<II4s5I',hdr,76,32,4,fourcc,0,0,0,0,0);struct.pack_into('<I',hdr,108,0x1000)
        image=Image.open(io.BytesIO(hdr+raw[:n])).convert('RGBA')
    elif fmt in (0x08,0x80,0x81):
        pal=pixels[paloff:paloff+palsize]
        if len(raw)<w*height or len(pal)!=palsize or not palsize or palsize%4 or max(raw[:w*height])>=palsize//4:raise ValueError('palette bounds')
        image=Image.frombytes('RGBA',(w,height),b''.join(pal[i*4:i*4+4] for i in raw[:w*height]),'raw','BGRA')
    elif fmt==0x20:
        if len(raw)<w*height*4:raise ValueError('BGRA bounds')
        image=Image.frombytes('RGBA',(w,height),raw[:w*height*4],'raw','BGRA')
    else:raise ValueError(f'{name}: unsupported pixel format {fmt:x}')
    image.info['mw_alpha_usage']=h[0x55]
    image.info['mw_alpha_blend']=h[0x56]
    return ident,name,image

def read(path,wanted=None):
    b=path.read_bytes();result={}
    for tag,tpk,_ in chunks(b):
        if tag!=0xb3300000:continue
        parts={}
        for t,p,_ in chunks(tpk):
            if t in (0xb3310000,0xb3320000):
                for sub,c,_ in chunks(p):
                    if sub:parts[sub]=c
        if 0x33310003 in parts:
            c=parts[0x33310003]
            if len(c)%24:raise ValueError('compressed texture table')
            for p in range(0,len(c),24):
                key,off,size,usize,_,_=struct.unpack_from('<6I',c,p)
                if wanted is not None and key not in wanted:continue
                if off+size>len(b):raise ValueError('compressed texture offset')
                packed=b[off:off+size];raw=jdlz(packed) if packed[:4]==b'JDLZ' else huff(packed)
                if len(raw)!=usize or len(raw)<156:raise ValueError('decompressed texture size')
                ident,name,image=decode(raw[-156:-32],raw[:-156],True)
                if ident!=key:raise ValueError('texture hash mismatch')
                result[ident]=(name,image)
        else:
            headers=parts.get(0x33310004,b'');pixels=parts.get(0x33320002,b'')[120:]
            if len(headers)%124:raise ValueError('texture headers')
            for p in range(0,len(headers),124):
                key=struct.unpack_from('<I',headers,p+36)[0]
                if wanted is not None and key not in wanted:continue
                ident,name,image=decode(headers[p:p+124],pixels);result[ident]=(name,image)
    return result
