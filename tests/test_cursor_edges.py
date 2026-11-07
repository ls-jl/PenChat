#!/usr/bin/env python3
"""Insertion hit testing stays independent from the floating magnifier bounds."""
import os,pathlib,struct,tempfile,time,zlib
import test_lvgl_app as t
from image_gestures import capture
t.port=1
BINARY=pathlib.Path(os.environ.get('PENCHAT_TEST_BINARY',t.ROOT/'build/host/penchat-lvgl'))
OUT=t.ROOT/'.work/cursor-edges';OUT.mkdir(parents=True,exist_ok=True)
def green_pixels(path,bounds):
    data=path.read_bytes();width,height,depth,kind=struct.unpack('>IIBB',data[16:26]);assert (depth,kind)==(8,6)
    chunks=[];offset=8
    while offset<len(data):
        size=struct.unpack('>I',data[offset:offset+4])[0]
        if data[offset+4:offset+8]==b'IDAT':chunks.append(data[offset+8:offset+8+size])
        offset+=size+12
    raw=zlib.decompress(b''.join(chunks));previous=bytearray(width*4);points=[]
    for y in range(min(height,bounds[3]+1)):
        offset=y*(width*4+1);kind=raw[offset];row=bytearray(raw[offset+1:offset+1+width*4])
        for i in range(len(row)):
            left=row[i-4] if i>=4 else 0;up=previous[i];diagonal=previous[i-4] if i>=4 else 0
            if kind==1:row[i]=(row[i]+left)&255
            elif kind==2:row[i]=(row[i]+up)&255
            elif kind==3:row[i]=(row[i]+(left+up)//2)&255
            elif kind==4:
                p=left+up-diagonal;distances=(abs(p-left),abs(p-up),abs(p-diagonal))
                row[i]=(row[i]+(left if distances[0]<=min(distances[1:]) else up if distances[1]<=distances[2] else diagonal))&255
            else:assert kind==0
        if y>=bounds[1]:
            points.extend((x,y) for x in range(bounds[0],bounds[2]+1) if row[x*4:x*4+3]==b'\x4a\xde\x80')
        previous=row
    return points
for width,height in [(960,266),(568,1210)]:
    with tempfile.TemporaryDirectory(prefix='penchat-cursor-edges-') as folder:
        a=t.Native(BINARY,folder,official_adapter=False,screen={'width':width,'height':height})
        try:
            def e():return a.diag()['editor']
            def open_input():
                b=a.diag()['composer_text_bounds'];a.tap((b[0]+b[2])//2,(b[1]+b[3])//2)
                t.wait_for(lambda:e()['active'] and bool(e()['keys']))
            open_input()
            for case,text in [('latin','ABCDE'),('chinese','左右边界'),('emoji','ABC👩‍💻'),('multiline','first line\nsecondM'),
                              ('wrap','M'*90),('wrap-cn','中文输入填满这一行并检查右侧字符边界ABC下一行继续输入。'*3)]:
                a.editor_action('official');request=a.receive('keyboard.open')
                a.send('keyboard.result',requestId=request['requestId'],confirmed=True,text=text)
                t.wait_for(lambda:not e()['active']);open_input()
                current=e();bounds=current['text_bounds']
                # Locate the line end from a far-right captured drag first.
                x,y=bounds[0]+6,bounds[1]+6
                a.send('touch',x=x,y=y,pressed=True);time.sleep(.52)
                if case.startswith('wrap'):
                    a.send('touch',x=-80,y=y,pressed=True);time.sleep(.06)
                    first=e()['caret'][1]
                    a.send('touch',x=width+80,y=y,pressed=True);time.sleep(.06)
                    assert 0<e()['cursor']<len(text) and e()['caret'][1]==first,(width,case,'horizontal drag changed visual row',e()['caret'],first)
                    line_end=e()['cursor']
                    # Full rows must reach the same insertion boundary from
                    # the last visible field pixel, not only far outside it.
                    a.send('touch',x=bounds[2],y=y,pressed=True);time.sleep(.06)
                    assert e()['cursor']==line_end and e()['caret'][1]==first,(width,case,'filled row edge',e()['cursor'],line_end)
                    image=OUT/f'{width}-{case}-upstream.png';capture(a,image)
                    end_x=e()['caret'][0]
                    line_height=e()['line_height']
                    pixels=green_pixels(image,bounds)
                    assert len(pixels)>=line_height and all(first-line_height-8<=py<first-8 and abs(px-end_x)<=2 for px,py in pixels),(width,case,'rendered caret left its visual row',pixels[:8])
                    a.send('touch',x=end_x-40,y=y,pressed=True);time.sleep(.06)
                    assert e()['cursor']<line_end and e()['caret'][1]==first
                    a.send('touch',x=bounds[2],y=y,pressed=True);time.sleep(.06)
                    assert e()['cursor']==line_end and e()['caret'][1]==first
                    # Release at the visual line end, then start a fresh drag:
                    # neither rebuilding the controls nor the default LVGL
                    # downstream caret may snap it onto the next line.
                    a.send('touch',x=bounds[2],y=y,pressed=False);time.sleep(.08)
                    assert e()['cursor']==line_end and e()['caret'][1]==first
                    a.send('touch',x=bounds[0]+6,y=y,pressed=True);time.sleep(.52)
                    a.send('touch',x=bounds[2],y=y+line_height+8,pressed=True);time.sleep(.06)
                    assert e()['cursor']>line_end and e()['caret'][1]>first
                a.send('touch',x=width+80,y=bounds[3]-2,pressed=True);time.sleep(.08)
                assert e()['cursor']==len(text),(width,case,'far-right',e()['cursor'],len(text))
                end=e()['caret'];y=end[1]-16
                a.send('touch',x=end[0]-1,y=y,pressed=True);time.sleep(.08)
                assert e()['cursor']==len(text),(width,case,'last glyph right half',e()['cursor'],len(text))
                current=e();bubble=current['magnifier_bounds'];bounds=current['text_bounds']
                assert bounds[0]<=bubble[0]<=bubble[2]<=bounds[2],(bubble,bounds)
                capture(a,OUT/f'{width}-{case}-right.png')
                a.send('touch',x=-80,y=bounds[1]+5,pressed=True);time.sleep(.08)
                assert e()['cursor']==0
                bubble=e()['magnifier_bounds'];assert bounds[0]<=bubble[0]<=bubble[2]<=bounds[2]
                a.send('touch',x=-80,y=bounds[1]+5,pressed=False)
                assert not e()['cursor_drag'] and not e()['magnifier']
                a.editor_action('official');request=a.receive('keyboard.open')
                assert request['text']==text,'Caret dragging must not insert a newline or change the text'
                a.send('keyboard.result',requestId=request['requestId'],confirmed=False)
                t.wait_for(lambda:not a.diag()['keyboard'] and e()['active'])
            if height>width:
                a.editor_action('done');a.open_settings();a.setting('models');a.page_action('添加 Provider')
                bounds=next(b for key,b in e()['targets'].items() if key.startswith('field:API Key:'))
                a.tap((bounds[0]+bounds[2])//2,(bounds[1]+bounds[3])//2)
                t.wait_for(lambda:e()['active'] and e()['secret'] and bool(e()['keys']))
                for letter in 'wii':
                    key=next(key for key in e()['keys'] if key['key']==letter);a.tap(key['x'],key['y'])
                end=e()['caret'];bounds=e()['text_bounds']
                a.send('touch',x=bounds[0]+4,y=bounds[1]+6,pressed=True);time.sleep(.52)
                a.send('touch',x=end[0]-1,y=end[1]-16,pressed=True);time.sleep(.08)
                assert e()['cursor']==3 and e()['caret']==end,'Hidden secret caret must follow rendered bullets, not original glyph widths'
                a.send('touch',x=end[0]-1,y=end[1]-16,pressed=False,cancelled=True)
                a.editor_action('cancel')
            print(f'Cursor {width}x{height}: last glyph insertion, captured outside drag, multiline/emoji and independent lens bounds passed.',flush=True)
        finally:a.close()
