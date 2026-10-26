#!/usr/bin/env python3
"""Fixed, non-user corpus. Rankings are examples, not a system-IME parity claim."""
import json,pathlib,statistics,subprocess,os
ROOT=pathlib.Path(__file__).resolve().parents[1]
rows=json.loads((ROOT/'native/pinyin/evaluation.json').read_text())
rows+=json.loads((ROOT/'native/pinyin/evaluation-heldout.json').read_text())
keys=['abc','def','ghi','jkl','mno','pqrs','tuv','wxyz']
def digits(s):return ''.join(str(next(i for i,k in enumerate(keys,2) if c in k)) for c in s if c.isalpha())
cases=[(p,w,'zh26') for p,w in rows]+[(digits(p),w,'zh9') for p,w in rows if p not in ('sh','nh','jsj')]
binary=pathlib.Path(os.environ.get('PENCHAT_PINYIN_BENCH_BINARY',ROOT/'build/host/penchat-pinyin-bench'))
r=subprocess.run([str(binary),str(ROOT/'assets/pinyin/pinyin.txt'),str(ROOT/'assets/pinyin/dictionary.data')],input='\n'.join(p for p,w,l in cases)+'\n',capture_output=True,text=True,check=True)
lines=r.stdout.splitlines();results=[]
for line,(p,w,layout) in zip(lines,cases):
    f=line.split('\t');assert f[0]==p
    words=[v.split(':')[0] for v in f[2:]]
    results.append(dict(input=p,expected=w,layout=layout,ms=float(f[1]),rank=words.index(w)+1 if w in words else None,top=words[:5]))
report={}
for layout in ('zh26','zh9'):
    group=[r for r in results if r['layout']==layout];times=sorted(r['ms'] for r in group)
    report[layout]=dict(cases=len(group),top1=sum(r['rank']==1 for r in group),top5=sum(r['rank'] is not None and r['rank']<=5 for r in group),p50_ms=statistics.median(times),p95_ms=times[int((len(times)-1)*.95)],max_ms=max(times))
    assert report[layout]['top5']/len(group)>=.75, report[layout]
report['rss_kib']=int(lines[-1].split('\t')[1]);report['examples']=results
out=ROOT/'.work/ime-audit';out.mkdir(exist_ok=True,parents=True);(out/'benchmark.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n')
print(json.dumps({k:v for k,v in report.items() if k!='examples'},ensure_ascii=False))
