"""Bake original planet surfaces and nebulae; Python standard library only."""
import math
import random
import struct
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1] / 'assets'
ROOT.mkdir(exist_ok=True)

def png(path, width, height, pixels):
    def chunk(tag, data):
        return struct.pack('>I', len(data)) + tag + data + struct.pack('>I', zlib.crc32(tag + data))
    raw = b''.join(b'\0' + bytes(pixels[y * width * 4:(y + 1) * width * 4]) for y in range(height))
    path.write_bytes(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>2I5B', width, height, 8, 6, 0, 0, 0)) + chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b''))

def wave(x, y, z, seed):
    return sum(math.sin(x*f + math.sin(y*f*0.81 + seed)*1.4 + z*f*.7 + seed) * a
               for f, a in [(3, .52), (7, .25), (17, .13), (39, .07), (83, .03)])

def mix(a, b, t):
    t = max(0, min(1, t))
    return [a[i]*(1-t)+b[i]*t for i in range(3)]

N = 256
for kind in range(5):
    pixels = bytearray()
    for iy in range(N):
        for ix in range(N):
            x, y = (ix-N/2)/(N*.435), (iy-N/2)/(N*.435)
            r = math.hypot(x,y)
            if r > 1:
                glow = max(0, 1-(r-1)/.13)**3 * 55
                tint = [(67,164,245),(222,155,84),(132,208,255),(244,82,39),(166,154,225)][kind]
                pixels.extend((*tint, int(glow)))
                continue
            z = math.sqrt(max(0, 1-r*r))
            n = wave(x,y,z,kind*3.7)
            detail = wave(x*2,y*2,z*2,8)
            if kind == 0:
                color = mix((7,34,79),(21,110,146),n+.5) if n < .09 else mix((34,67,40),(128,144,80),n)
                cloud = max(0, wave(x,y,z,17)-.32)*1.8
                color = mix(color,(219,237,240),cloud)
                if abs(y) > .82: color = mix(color,(220,238,242),(abs(y)-.82)*5)
            elif kind == 1:
                color = mix((96,46,26),(226,173,103),(n+1)/2)
                color = mix(color,(69,45,36),max(0, detail-.4))
            elif kind == 2:
                color = mix((39,85,126),(208,235,244),(n+1)/2)
                color = mix(color,(29,78,108),max(0, .06-abs(detail))*10)
            elif kind == 3:
                color = mix((23,24,32),(77,47,43),(n+1)/2)
                lava = max(0, .11-abs(n+.12))*9
                color = mix(color,(255,102,19),lava)
            else:
                band = math.sin(y*40 + n*3)
                color = mix((101,77,117),(224,183,150),(band+1)/2)
                color = mix(color,(244,213,177),max(0, detail)*.4)
            light = max(0, -x*.55-y*.4+z*.72)
            shade = .12 + .88*light
            rim = (1-z)**3 * max(0, -x*.6+.3)
            color = [min(255,int(c*shade+rim*40)) for c in color]
            pixels.extend((*color,255))
    png(ROOT/f'planet_{kind}.png',N,N,pixels)

W,H = 1280,800
pixels = bytearray()
for y in range(H):
    for x in range(W):
        u,v=x/W,y/H
        n=(math.sin(u*10+math.sin(v*7)*2)+math.sin(v*17-u*6)*.4)/1.4
        cloud=math.exp(-((v-.65+.22*math.sin(u*5))/.24)**2)*(n+1)/2
        violet=math.exp(-((u-.65)/.4)**2)
        pixels.extend((int(5+cloud*(9+violet*17)),int(9+cloud*13),int(20+cloud*(25+violet*13)),255))
png(ROOT/'nebula.png',W,H,pixels)
print('Baked five planet textures and nebula background')
