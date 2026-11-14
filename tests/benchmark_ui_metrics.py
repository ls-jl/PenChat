#!/usr/bin/env python3
"""Five independent host trials; reports CPU/RSS and diagnostic latency, no device IO."""
import argparse,json,pathlib,statistics,tempfile,time
import test_lvgl_app as t
from test_chat_style import seed
p=argparse.ArgumentParser();p.add_argument('--binary',type=pathlib.Path,required=True);p.add_argument('--output',type=pathlib.Path,required=True);args=p.parse_args()
t.port=1;trials=[]
for trial in range(5):
    with tempfile.TemporaryDirectory(prefix='penchat-ui-bench-') as folder:
        seed(folder,'# Measured reader\n\n'+('中文 **layout** and $x^2+1$.\n\n'*40))
        a=t.Native(args.binary.resolve(),folder,official_adapter=False,screen={'width':568,'height':1210})
        try:
            def rss():
                return int(next(line.split()[1] for line in pathlib.Path(f'/proc/{a.process.pid}/status').read_text().splitlines() if line.startswith('VmHWM:')))
            b=a.diag()['composer_text_bounds'];a.tap((b[0]+b[2])//2,(b[1]+b[3])//2)
            t.wait_for(lambda:a.diag()['editor']['active'] and a.diag()['editor']['keys'])
            before=a.diag()['editor'];latencies=[]
            for _ in range(100):
                start=time.perf_counter_ns();a.diag();latencies.append((time.perf_counter_ns()-start)/1e6)
            after=a.diag()['editor']
            resolves=None if 'layout_resolves' not in before else after['layout_resolves']-before['layout_resolves']
            if resolves is not None:assert resolves==0,'unchanged input must reuse its geometry'
            a.editor_action('done');a.open_settings();a.setting('models');a.page_action('添加 Provider')
            d=a.diag()
            trials.append({'diagnostic_p50_ms':statistics.median(latencies),'diagnostic_p95_ms':sorted(latencies)[94],
                           'rss_high_water_kib':rss(),'unchanged_keyboard_resolves':resolves,'form_work':d.get('layout_work')})
        finally:a.close()
result={'trials':trials,'median_p95_ms':statistics.median(v['diagnostic_p95_ms'] for v in trials),
        'median_rss_high_water_kib':statistics.median(v['rss_high_water_kib'] for v in trials)}
args.output.parent.mkdir(parents=True,exist_ok=True);args.output.write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps(result,indent=2))
