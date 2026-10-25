"""Real-touch archive browsing inside display_config's private/public isolation."""
import time,types
from settings_ui import activate

def run(env):
    d=types.SimpleNamespace(**env);W,H=d.W,d.H
    def bounds(b):d.tap((b[0]+b[2])//2,(b[1]+b[3])//2)
    def action(name):
        for _ in range(30):
            s=d.state();b=s['page']['actions'][name]
            if 0<=b[1] and b[3]<H:bounds(b);return
            d.settings_swipe(100 if b[1]<0 else -100)
        raise AssertionError('Archive page action unreachable: '+name)
    active=d.state()['session'];native=d.pid()
    activate(d.state,d.tap,d.settings_swipe,'archives');d.wait(lambda:d.state()['view']=='archives')
    d.capture('archives-view-entry');listScroll=d.state()['scroll_y']
    bounds(d.state()['archive_views']['archived']);d.wait(lambda:d.state()['view']=='archive_chat')
    s=d.state();assert s['session']==active and s['archive_reader']['messages']==['archived1','archived2','archived3']
    assert not s['reply_actions']['rows'] and not s['message_interactions']['bubbles']
    d.capture('archives-reader')
    for _ in range(12):
        photo=d.state()['chat_images'][0]
        if photo['y']>=4 and photo['y']+photo['height']<=H-4:break
        d.settings_swipe(-60 if photo['y']+photo['height']>H-4 else 60)
    d.tap(photo['x']+photo['width']//2,photo['y']+photo['height']//2)
    d.wait(lambda:d.state()['image_viewer']['loaded']);d.capture('archives-original-photo');d.tap(W-38,30)
    d.wait(lambda:not d.state()['image_viewer']['open'])
    for _ in range(12):
        s=d.state()
        if 24<=s['reasoning_header_y']<=H-24:break
        d.settings_swipe(-90 if s['reasoning_header_y']>H-24 else 90)
    s=d.state();d.tap(s['reasoning_header_x'],s['reasoning_header_y']);d.wait(lambda:d.state()['reasoning_expanded_count']==1)
    d.capture('archives-reasoning');s=d.state();d.tap(s['reasoning_header_x'],s['reasoning_header_y'])
    d.wait(lambda:d.state()['reasoning_expanded_count']==0)
    d.side_back();d.wait(lambda:d.state()['view']=='archives');assert d.state()['scroll_y']==listScroll
    bounds(d.state()['archive_views']['paged']);d.wait(lambda:d.state()['view']=='archive_chat')
    assert d.state()['archive_reader']['messages']==['paged'+str(n) for n in range(36,66)]
    action('加载更早消息');d.wait(lambda:d.state()['archive_reader']['offset']==30)
    action('加载更早消息');d.wait(lambda:d.state()['archive_reader']['offset']==60)
    assert d.state()['archive_reader']['messages']==['paged'+str(n) for n in range(1,6)]
    d.capture('archives-pagination');action('加载较新消息');d.wait(lambda:d.state()['archive_reader']['offset']==30)
    d.side_back();d.wait(lambda:d.state()['view']=='archives');d.side_back();d.wait(lambda:d.state()['view']=='settings')
    assert d.state()['session']==active and d.pid()==native
    d.result['archive_reader_view_image_reasoning_pagination_back']=True
