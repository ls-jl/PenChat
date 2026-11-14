"""Navigate the production scaling controls using live logical bounds."""
import time
import test_lvgl_app as t

def tap(a,b):a.tap((b[0]+b[2])//2,(b[1]+b[3])//2)
def open_settings(a):
    d=a.diag();assert d['view']=='chat'
    scale=d.get('ui_scale',{}).get('applied',100)
    dp=lambda n:(n*scale+50)//100
    x=d['screen_safety']['navigation_x'] if d['height']>d['width'] else dp(16)
    y=d['safe_insets'][1] if d['height']>d['width'] else dp(8)
    a.tap(x+max(44,dp(48))//2,y+max(44,dp(48))//2)
    t.wait_for(lambda:a.diag()['navigation_open'])
    tap(a,a.diag()['navigation_footer']['settings']['bounds'])
    t.wait_for(lambda:a.diag()['view']=='settings')
def back(a):
    for _ in range(30):
        d=a.diag()
        if d['scroll_y']<=0:break
        a.settings_swipe(120);time.sleep(.1)
    scale=d.get('ui_scale',{}).get('applied',100);dp=lambda n:(n*scale+50)//100
    if d['height']>d['width']:
        a.tap(d['screen_safety']['navigation_x']+max(44,dp(44))//2,d['safe_insets'][1]+dp(2)+max(44,dp(44))//2)
    else:a.tap(dp(16)+max(44,dp(48))//2,dp(8)+max(44,dp(48))//2)
    d=a.diag()
    if d['navigation_open']:
        a.tap(d['width']-2,d['height']//2);t.wait_for(lambda:not a.diag()['navigation_open'])
def reveal(a,get_bounds):
    for _ in range(40):
        d=a.diag();b=get_bounds(d);v=d['page']['viewport']
        if d['page']['scrolling']:time.sleep(.1);continue
        if b[1]>=v[1]+20 and b[3]<=v[3]-20:return b
        a.settings_swipe(-100 if b[3]>v[3]-20 else 100);time.sleep(.12)
    raise AssertionError(('unreachable',b,v))
def choose(a,percent):
    b=reveal(a,lambda d:d['ui_scale']['slider'])
    a.tap(b[0]+round((percent-75)*(b[2]-b[0])/75),(b[1]+b[3])//2)
    for _ in range(4):
        value=a.diag()['ui_scale']['draft']
        if value==percent:return
        a.page_action('增大' if value<percent else '减小')
    assert a.diag()['ui_scale']['draft']==percent

def save(a,percent):
    a.page_action('保存')
    t.wait_for(lambda:a.diag()['view']=='settings' and a.diag()['ui_scale']['applied']==percent)
