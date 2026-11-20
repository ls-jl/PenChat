#!/usr/bin/env python3
"""Long draft reflow retains visibility; only the local text scrolls after 3 lines."""
import os,pathlib,tempfile,time
import test_lvgl_app as t
from test_chat_style import seed
from image_gestures import capture

t.port=1
binary=pathlib.Path(os.environ.get('PENCHAT_TEST_BINARY',t.ROOT/'build/host/penchat-lvgl'))
out=t.ROOT/'.work/input-stability/composer';out.mkdir(parents=True,exist_ok=True)
for width,height in [(960,266),(936,280),(640,260)]:
 for scale in [75,100,125,150]:
  with tempfile.TemporaryDirectory(prefix='penchat-draft-limit-') as folder:
   seed(folder,'\n\n'.join('阅读段落 '+str(i) for i in range(80)))
   a=t.Native(binary,folder,official_adapter=False,screen={'width':width,'height':height},ui_scale_percent=scale)
   try:
    for text in ['短文','第一行\n第二行','第一行\n第二行\n第三行','第一行\n第二行\n第三行\n第四行','中文段落 English '*80,'\n'.join('文本 '+str(i) for i in range(40))]+(['A'*8192] if width==960 and scale==100 else []):
     d=a.diag();assert not d['composer_hidden']
     b=d['composer_text_bounds'];a.tap((b[0]+b[2])//2,(b[1]+b[3])//2)
     t.wait_for(lambda:a.diag()['editor']['active'])
     a.editor_action('official');r=a.receive('keyboard.open')
     a.send('keyboard.result',requestId=r['requestId'],confirmed=True,text=text)
     t.wait_for(lambda:not a.diag()['editor']['active'])
     time.sleep(.35);d=a.diag();b=d['composer_bounds'];r=d['composer_text_bounds'];line=d['composer_line_height']
     assert not d['composer_hidden'] and b[3]<height,(width,scale,len(text),d['composer_bounds'])
     assert r[3]-r[1]+1<=3*line,(width,scale,r,line)
     if len(text)>200 or text.count('\n')>=3:
      assert d['composer_scroll_bottom']>0
      before=d['scroll_y'];x=(r[0]+r[2])//2;y=r[3]-3
      for i in range(8):a.send('touch',x=x,y=y-i*4,pressed=True);time.sleep(.02)
      a.send('touch',x=x,y=y-28,pressed=False);time.sleep(.2)
      d=a.diag();assert d['composer_scroll']>0 and d['scroll_y']==before and not d['composer_hidden']
    capture(a,out/f'{width}-{scale}-three-lines.png')
    print('LANDSCAPE_THREE_LINE_DRAFT_VISIBLE_LOCAL_SCROLL',width,height,scale,flush=True)
   finally:a.close()
