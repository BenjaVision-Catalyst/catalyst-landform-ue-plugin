#!/usr/bin/env python3
"""Prepare NASA Viking/MDIM colour context using the original ASU tile extents."""
from pathlib import Path
import argparse,json,hashlib,urllib.request
from PIL import Image
parser=argparse.ArgumentParser();parser.add_argument('--source',type=Path,required=True);parser.add_argument('--output',type=Path,required=True);parser.add_argument('--fetch',action='store_true');args=parser.parse_args();p=args.source.resolve();out=args.output.resolve();p.mkdir(parents=True,exist_ok=True);out.mkdir(parents=True,exist_ok=True)
lock=json.loads(Path(__file__).with_name('color-sources.json').read_text());base=Image.new('RGB',(8192,4096))
for y,h in enumerate(['n','s']):
 for x,lon in enumerate([45,135,225,315]):
  name='mars45'+h+str(lon).zfill(3)+'.png';f=p/name;r=lock['sources'][name]
  if not f.exists() and args.fetch:
   temporary=f.with_suffix('.download');urllib.request.urlretrieve(r['url'],temporary)
   assert temporary.stat().st_size==r['bytes'] and hashlib.sha256(temporary.read_bytes()).hexdigest()==r['sha256'],name;temporary.replace(f)
  assert f.stat().st_size==r['bytes'] and hashlib.sha256(f.read_bytes()).hexdigest()==r['sha256'],name
  im=Image.open(f);assert im.size==(5760,5760);base.paste(im.resize((2048,2048),Image.Resampling.LANCZOS),(x*2048,y*2048))
base.save(out/'MarsColor8k.png');assert hashlib.sha256((out/'MarsColor8k.png').read_bytes()).hexdigest()==lock['outputSha256']
(out/'color-sources.json').write_text(json.dumps(lock,indent=2)+'\n')
