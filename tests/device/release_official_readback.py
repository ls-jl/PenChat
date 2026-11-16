"""Explicit owned MiniApp primary readback; separate from SDK/LCD evidence."""
import json
import pathlib
import re
import shlex
import subprocess
import time
import uuid
import struct
import zlib


def capture(d, name):
  s=d.state()
  owned_fullscreen = (s['keyboard'] and s['editor']['active'] and s['editor']['official'] and
                      not s['editor']['docked'] and s['system_overlay'].get('app_focused'))
  assert (s['page_visible'] or owned_fullscreen) and not s['system_overlay'].get('panel_visible',False)
  assert (s['keyboard'] and s['editor']['active'] and s['editor']['official']) or (s['input_focused'] and s['touch_input']['active'])
  state=d.sh('cat /sys/kernel/debug/dri/0/state')
  candidates=[]
  for block in re.split(r'(?=^plane\[)',state,flags=re.M):
    match=re.match(r'plane\[(\d+)\]',block)
    if match and re.search(r'allocated by\s*=\s*miniapp\s*$',block,re.M) and not re.search(r'crtc=\(null\)',block):
      candidates.append(int(match.group(1)))
  assert len(candidates)==1, 'Expected exactly one observed active MiniApp primary'
  token=uuid.uuid4().hex;remote='/tmp/penchat-primary-'+token+'.raw';base=d.out/(name+'-primary-'+token)
  native_before=s;started=time.time_ns()
  receipt=d.sh(d.readback_remote+' /dev/dri/card0 '+str(candidates[0])+' '+remote+' 2>&1')
  for suffix in ('','.json'):
    subprocess.run(['adb','-s',d.serial,'pull',remote+suffix,str(base)+'.raw'+suffix],check=True,capture_output=True)
  d.sh('rm -f '+remote+' '+remote+'.json')
  meta=json.loads(base.with_suffix('.raw.json').read_text())
  assert meta['plane']==candidates[0] and meta['plane_fb_stable']
  raw=base.with_suffix('.raw').read_bytes();assert len(raw)==meta['bytes']
  width,height=meta['physical_width'],meta['physical_height']
  pixels=bytearray(width*height*3)
  for y in range(height):
    row=raw[y*meta['pitch']:y*meta['pitch']+width*4];rgb=bytearray(width*3)
    rgb[0::3]=row[2::4];rgb[1::3]=row[1::4];rgb[2::3]=row[0::4]
    pixels[y*width*3:(y+1)*width*3]=rgb
  def png(path, data, w, h):
    def chunk(kind, payload):
      return struct.pack('>I',len(payload))+kind+payload+struct.pack('>I',zlib.crc32(kind+payload)&0xffffffff)
    scan=b''.join(b'\0'+data[y*w*3:(y+1)*w*3] for y in range(h))
    pathlib.Path(path).write_bytes(b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',w,h,8,2,0,0,0))+chunk(b'IDAT',zlib.compress(scan))+chunk(b'IEND',b''))
  png(str(base)+'-physical.png',pixels,width,height)
  rotation=int(d.screen.get('direction',0));assert rotation in (0,90,180,270)
  rotated=bytearray(len(pixels));rw,rh=(height,width) if rotation in (90,270) else (width,height)
  for y in range(height):
    for x in range(width):
      tx,ty=(x,y) if rotation==0 else (width-1-x,height-1-y) if rotation==180 else (y,width-1-x) if rotation==90 else (height-1-y,x)
      rotated[(ty*rw+tx)*3:(ty*rw+tx+1)*3]=pixels[(y*width+x)*3:(y*width+x+1)*3]
  logical=str(base)+'-logical.png';png(logical,rotated,rw,rh)
  # Tall-image previews can omit the lower panel. Keep an independently
  # inspectable crop derived from the reported logical IME boundary.
  panel_top=s.get('display_top_height',0)
  panel_image=None
  if s['keyboard'] and 0 < panel_top < rh:
    panel_image=str(base)+'-keyboard.png'
    png(panel_image,rotated[panel_top*rw*3:],rw,rh-panel_top)
  base.with_suffix('.state.txt').write_text(state+'\n')
  evidence={'kind':'read-only primary-plane pixel storage, not SDK capture or composed LCD','raw_metadata':meta,
    'falcon_screen':d.screen,'logical_rotation_degrees':rotation,'started_ns':started,'completed_ns':time.time_ns(),
    'receipt':receipt,'native_before':native_before,'native_after':d.state(),'state_file':str(base.with_suffix('.state.txt')),
    'physical_png':str(base)+'-physical.png','logical_png':logical,'keyboard_png':panel_image,'limitations':'Separate native overlay excluded; stable FB identity does not guarantee atomic pixels.'}
  base.with_suffix('.capture.json').write_text(json.dumps(evidence,ensure_ascii=False,indent=2)+'\n')
  # Do not ask Falcon to render a capture between observations: that can
  # perturb the very primary surface whose editing lifetime we are measuring.
  return panel_image or logical
