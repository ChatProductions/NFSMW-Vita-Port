"""Pack decoded original car images, reserving more pixels for the body."""
from PIL import Image

def build_atlas(textures,used,body_key,misc_key,brakes):
    atlas=Image.new('RGBA',(2048,2048),(25,28,31,255));occupied=set();slots={};report=[]
    def desired(k):return 4 if k==body_key else 2 if k==misc_key else 1
    for key in sorted(used,key=lambda k:(-desired(k),k)):
        blocks=desired(key);found=None
        for y in range(9-blocks):
            for x in range(9-blocks):
                cells={(x+a,y+b) for a in range(blocks) for b in range(blocks)}
                if not occupied&cells:found=(x,y,cells);break
            if found:break
        if not found:raise ValueError('car atlas capacity')
        x,y,cells=found;occupied|=cells;x*=256;y*=256;extent=blocks*256;pixels=extent-4
        name,img=textures.get(key,(f'unresolved_{key:08x}',Image.new('RGBA',(8,8),(45,49,54,255))))
        native=img.size
        if 'WINDOW' in name or 'DEFROSTER' in name:
            bg=Image.new('RGBA',img.size,(29,39,46,255));bg.alpha_composite(img);img=bg
        image=img.resize((pixels,pixels),Image.Resampling.LANCZOS)
        atlas.paste(image,(x+2,y+2));atlas.paste(image.crop((0,0,1,pixels)).resize((2,pixels)),(x,y+2));atlas.paste(image.crop((pixels-1,0,pixels,pixels)).resize((2,pixels)),(x+extent-2,y+2));atlas.paste(image.crop((0,0,pixels,1)).resize((pixels,2)),(x+2,y));atlas.paste(image.crop((0,pixels-1,pixels,pixels)).resize((pixels,2)),(x+2,y+extent-2))
        slots[key]=(x+2,y+2,pixels-1);report.append({'material_hash':f'{key:08x}','source':name,'resolved':key in textures,'opaque':img.getextrema()[3][0]==255,'native_size':list(native),'atlas_size':[pixels,pixels]})
    if slots[brakes[0]][2]!=slots[brakes[1]][2]:raise ValueError('brake atlas sizes differ')
    return atlas,slots,report
