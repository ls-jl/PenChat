#!/usr/bin/env python3
"""Fixed typo corpus and paired real-engine regression/performance comparison."""
import json,os,pathlib,statistics,subprocess
ROOT=pathlib.Path(__file__).resolve().parents[1]
BINARY=pathlib.Path(os.environ.get('PENCHAT_PINYIN_BENCH_BINARY',ROOT/'build/host/penchat-pinyin-bench'))
normal=json.loads((ROOT/'native/pinyin/evaluation.json').read_text())+json.loads((ROOT/'native/pinyin/evaluation-heldout.json').read_text())
base=[{'input':p,'expected':w,'kind':'normal'} for p,w in normal]
keys=['abc','def','ghi','jkl','mno','pqrs','tuv','wxyz']
base += [{'input':''.join(str(next(i for i,k in enumerate(keys,2) if c in k)) for c in p if c.isalpha()),'expected':w,'kind':'nine'} for p,w in normal if p not in ('sh','nh','jsj')]
typos=json.loads((ROOT/'native/pinyin/evaluation-correction.json').read_text())

def evaluate(rows,enabled):
    command=[str(BINARY),str(ROOT/'assets/pinyin/pinyin.txt'),str(ROOT/'assets/pinyin/dictionary.data')]+(['--correction'] if enabled else [])
    output=subprocess.check_output(command,input='\n'.join(r['input'] for r in rows)+'\n',text=True).splitlines()
    result=[]
    for row,line in zip(rows,output):
        fields=line.split('\t');assert fields[0]==row['input']
        words=[v.split(':')[0] for v in fields[2:]]
        details=[dict(word=f[0],pinyin=f[1],consumed=int(f[2]),corrected=bool(int(f[3]))) for f in (v.split(':') for v in fields[2:])]
        assert sum(d['corrected'] for d in details)<=8
        result.append(dict(row,ms=float(fields[1]),rank=words.index(row['expected'])+1 if row['expected'] in words else None,candidates=details))
    counters=dict(line.split('\t') for line in output[len(rows):]);assert int(counters['#correction_states'])<=16384
    return result,{'rss_kib':int(counters['#rss_kib']),'max_correction_states':int(counters['#correction_states'])}

def summary(rows):
    times=sorted(r['ms'] for r in rows)
    return dict(cases=len(rows),top1=sum(r['rank']==1 for r in rows),top5=sum(r['rank'] is not None and r['rank']<=5 for r in rows),p50_ms=statistics.median(times),p95_ms=times[int((len(times)-1)*.95)])

old,old_counters=evaluate(base,False);current,counters=evaluate(base,True)
for before,after in zip(old,current):
    if before['kind']=='nine':
        assert before['candidates']==after['candidates'],f'Nine-key input changed: {before["input"]}'
    else:
        count=2 if len(before['candidates'])>=2 and all(c['consumed']==len(before['input']) for c in before['candidates'][:2]) else 1
        assert before['candidates'][:count]==after['candidates'][:count],f'Normal leading choices changed: {before["input"]}'
old_typos,old_typo_counters=evaluate(typos,False);new_typos,typo_counters=evaluate(typos,True)
assert summary(current)['top1']==summary(old)['top1'] and summary(current)['top5']>=summary(old)['top5']
assert summary(new_typos)['top5']>=35,'Fixed mixed typo/valid-spelling corpus regressed'
assert summary(new_typos)['top5']>summary(old_typos)['top5']
# Alternate process order, use medians; cold dictionary construction is excluded.
samples={False:[],True:[]}
for n in range(9):
    for enabled in ([False,True] if n%2==0 else [True,False]):
        rows,_=evaluate(base[:len(normal)],enabled);samples[enabled].append(summary(rows)['p95_ms'])
paired={str(k):statistics.median(v) for k,v in samples.items()}
# Measurements are reported rather than making an unpinned CI CPU a flaky gate.
# Release/device acceptance checks warm-query P95 stays below 20ms, including valid-spelling alternatives.
report={'normal_disabled':summary(old),'normal_enabled':summary(current),'typo_disabled':summary(old_typos),'typo_enabled':summary(new_typos),
        'paired_normal_p95_ms':paired,'normal_p95_ratio':paired['True']/paired['False'],'normal_resources':counters,'normal_disabled_resources':old_counters,'typo_resources':typo_counters,'typo_disabled_resources':old_typo_counters,
        'by_kind':{kind:summary([r for r in new_typos if r['kind']==kind]) for kind in sorted(set(r['kind'] for r in typos))},'examples':new_typos}
folder=ROOT/'.work/pinyin-correction';folder.mkdir(parents=True,exist_ok=True);(folder/'benchmark.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n')
print(json.dumps({k:v for k,v in report.items() if k!='examples'},ensure_ascii=False))
