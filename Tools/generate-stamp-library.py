"""Owned quantitative 16-bit height fields; no photos, baked lighting or external data."""
from pathlib import Path
import hashlib,json,math,struct,zlib
ROOT=Path(__file__).resolve().parents[1]/'SourceArt/Stamps/LibraryV1'
N=257

def smooth(t):
    t=max(0,min(1,t));return t*t*(3-2*t)
def shape(name,x,y):
    edge=smooth((64-abs(x))/10)*smooth((64-abs(y))/10)
    if name=='Ridge':
        axis=6*math.sin(x/23)
        h=16*math.exp(-((y-axis)/13)**2)*(.78+.22*math.cos(x/21))
        h+=2*math.exp(-((abs(y-axis)-14)/13)**2)*(.5+.5*math.cos((x+abs(y)*.9)/7))**3
    elif name=='Massif':
        r=math.hypot(x*.95,y*1.12);a=math.atan2(y,x)
        h=26*math.exp(-(r/27)**2)*(1+.1*math.cos(7*a))
        h+=5*math.exp(-((x-22)**2+(y+13)**2)/150)
    elif name=='Mesa':
        r=math.hypot(x,y*1.13)
        h=(12+.16*math.sin(x/8)*math.cos(y/10))*smooth((54-r)/13)
    elif name=='Butte':
        r=math.hypot(x*1.07,y);a=math.atan2(y,x)
        outline=32*(1+.1*math.cos(3*a))
        h=4*smooth((57-r)/20)+17*smooth((outline-r)/7)
    else:
        def crater(a,b,scale):
            r=math.hypot((x-a)/scale,(y-b)/scale)
            return -12*math.exp(-(r/23)**4)+5*math.exp(-((r-35)/6)**2)
        h=crater(0,0,1) if name=='Crater' else min(crater(-17,9,.67),crater(18,-10,.77)) + 2*math.exp(-((math.hypot(x,y)-41)/9)**2)
    return h*edge

def png(path,values):
    def chunk(t,d):return struct.pack('>I',len(d))+t+d+struct.pack('>I',zlib.crc32(t+d)&0xffffffff)
    raw=b''.join(b'\0'+struct.pack('>'+str(N)+'H',*values[j*N:(j+1)*N]) for j in range(N))
    path.write_bytes(b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',N,N,16,0,0,0,0))+chunk(b'IDAT',zlib.compress(raw,9))+chunk(b'IEND',b''))

ROOT.mkdir(parents=True,exist_ok=True)
entries=[]
for name,low,high,blend in [('Ridge',0,20,'Max'),('Massif',0,30,'Max'),('Mesa',0,15,'Max'),('Butte',0,25,'Max'),('Crater',-14,6,'Override'),('DoubleCrater',-14,8,'Override')]:
    values=[round(max(0,min(1,(shape(name,(i-128)*.5,(j-128)*.5)-low)/(high-low)))*65535) for j in range(N) for i in range(N)]
    path=ROOT/(name+'.png');png(path,values)
    entries.append(dict(id=name,name=name,resolution=[N,N],scale_xy_cm=50,scale_z_cm=(high-low)*100,offset_z_cm=low*100,reference_relief_m=high-low,blend=blend,png_sha256=hashlib.sha256(path.read_bytes()).hexdigest()))
(ROOT/'sources.json').write_text(json.dumps(dict(schemaVersion=1,provenance='Authored procedural elevation data for Catalyst Landform. No external sources.',encoding='linear uint16 grayscale; physical 0.5m texels before instance sizing',sources=entries),indent=2)+'\n')
print('Generated six owned native stamp sources')
