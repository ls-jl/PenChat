"""Bounded whole-reader scroll evidence; physical display judgement stays separate."""
import json
import time


def run(d):
  assert d.serial == '7G12000010905426'
  results = []
  themes = ('dark',) if d.features == ['y07-scroll-dark'] else ('light', 'dark')
  for theme in themes:
    s=d.chat()
    scale=s['ui_scale']['applied'];dp=lambda n:(n*scale+50)//100
    x=s['screen_safety']['navigation_x'] if d.h>d.w else dp(16)
    y=s['safe_insets'][1] if d.h>d.w else dp(8)
    d.touch(x+max(44,dp(48))/2,y+max(44,dp(48))/2)
    d.wait(lambda:d.state()['navigation_open'] and 'settings' in d.state()['navigation_footer'])
    d.tap(d.state()['navigation_footer']['settings']['bounds'])
    d.wait(lambda:d.state()['view']=='settings')
    d.tap(d.reveal(lambda s: s['settings']['rows']['theme']['bounds']))
    d.wait(lambda: d.state()['context_menu']['kind'] == 'choice:theme')
    d.tap(d.state()['context_menu']['choices'][theme]['bounds'])
    d.wait(lambda: d.state()['appearance']['theme'] == theme and not d.state()['appearance']['pending'])
    s = d.chat()
    b = list(s['chat_header']['reader_bounds'])
    b[3] = min(b[3], s['composer_bounds'][1]-12)
    x = (b[0]+b[2])/2; height = b[3]-b[1]
    for _ in range(12):
      s = d.state()
      if s['scroll_y'] > height*.7 and s['scroll_bottom'] > height*.7:
        break
      a, z = (.35, .6) if s['scroll_bottom'] <= height*.7 else (.65, .4)
      d.inject((*d.raw(x, b[1]+height*a), *d.raw(x, b[1]+height*z), 120), 'surface-scroll-prepare')
      time.sleep(.35)
    before = d.state()
    assert before['scroll_y'] > height*.7 and before['scroll_bottom'] > height*.7
    assert not before['editor']['active'] and not before['navigation_open']
    d.system_capture('whole-reader-'+theme+'-before')
    for cycle in range(8):
      a, z = (.35, .65) if cycle % 2 == 0 else (.65, .35)
      d.inject((*d.raw(x, b[1]+height*a), *d.raw(x, b[1]+height*z), 16), 'whole-reader-'+theme)
    after = d.state()
    assert after['pid'] == before['pid'] and after['session'] == before['session']
    assert after['visible'] and after['input_focused'] and after['touch_input']['active']
    assert after['view'] == 'chat' and not after['editor']['active']
    assert after['render_timing']['presented']-before['render_timing']['presented'] >= 8, 'Scrolling did not progress through DRM flip completions'
    d.capture('whole-reader-'+theme+'-native-after')
    image = d.system_capture('whole-reader-'+theme+'-after')
    results.append({'theme': theme, 'drags': 8, 'whole_reader_bounds': b,
      'sdk_after': str(image), 'state_before': before, 'state_after': after,
      'physical_flash_acceptance': 'Not established by SDK capture; physical observation required'})
    (d.out/'whole-reader-light-dark-results.json').write_text(json.dumps(results, ensure_ascii=False, indent=2)+'\n')
  path = d.out/'whole-reader-light-dark-results.json'
  path.write_text(json.dumps(results, ensure_ascii=False, indent=2)+'\n')
  return {'themes': list(themes), 'drags_per_theme': 8, 'evidence': str(path),
    'physical_flash_acceptance': 'pending physical observation'}
