const test = require('node:test')
const assert = require('node:assert/strict')
const fs = require('node:fs')
const vm = require('node:vm')
function fixture(stroke,route) {
  const timers = new Map(), calls = [], events = []; let clock = 0
  const signal = () => { const callbacks = new Set(); return { callbacks, on: f => callbacks.add(f), off: f => callbacks.delete(f), fire: (...a) => [...callbacks].forEach(f => f(...a)) } }
  const manager = Object.fromEntries(['ocrStartChanged','scanWordResultChanged','ocrCompleteChanged','ocrStopChanged','ocrResultChanged','scanErrorChanged'].map(k => [k,signal()]))
  for (const name of ['addUseOcrAppId','deleteUseOcrAppId','setIsOpenScan','setShowImgScanMode','setCurrentAppId','startScan']) manager[name] = (...args) => calls.push([name,...args])
  const context = vm.createContext({ module: {}, setTimeout: (f,delay=0) => { const call=()=>f();call.delay=delay;timers.set(++clock,call);return clock }, clearTimeout: id => timers.delete(id) })
  vm.runInContext(fs.readFileSync('src/launcher/ocr.js','utf8').replace('export class OcrSession','module.exports = class OcrSession'),context)
  const session = new context.module.exports(manager,'test-app',(type,data) => events.push({type,...data}), stroke, route)
  return { session, manager, timers, calls, events, flush: (delay) => { for (const [id,f] of [...timers.entries()]) { if((delay===undefined||f.delay===delay)&&timers.delete(id)) f() } } }
}
test('single-line system OCR preserves case and cleans up before delivering text', () => {
  const f=fixture();f.session.start('one')
  const config=JSON.parse(f.calls.find(c => c[0]==='addUseOcrAppId')[1]);assert.equal(config.scanMethod,0)
  f.manager.scanWordResultChanged.fire(0,'stale');assert(!f.events.some(e=>e.type==='ocr.result'))
  f.manager.ocrStartChanged.fire();f.manager.scanWordResultChanged.fire(0,JSON.stringify({text:'sk-A0lI'}))
  assert(!f.events.some(e=>e.type==='ocr.result'));f.flush()
  assert.equal(f.events.at(-1).text,'sk-A0lI');assert.equal(f.timers.size,0)
  assert(f.calls.some(c=>c[0]==='deleteUseOcrAppId'));assert(f.calls.some(c=>c[0]==='setIsOpenScan'&&c[1]===false))
  assert.equal(f.manager.scanWordResultChanged.callbacks.size,0)
})
test('cancel/restart rejects callbacks from previous scan and timeout unregisters', () => {
  const f=fixture();f.session.start('old');const old=[...f.manager.scanWordResultChanged.callbacks][0]
  f.session.start('new');old(0,'must not arrive');assert(!f.events.some(e=>e.type==='ocr.result'))
  const timeout=[...f.timers.values()][0];timeout();f.flush()
  assert.equal(f.events.at(-1).requestId,'new');assert.equal(f.events.at(-1).confirmed,false)
  assert.equal(f.manager.ocrStartChanged.callbacks.size,0)
})
test('only text payloads pass through; final completion waits for the final word signal', () => {
  const f=fixture();f.session.start('one');f.manager.ocrStartChanged.fire()
  f.manager.ocrCompleteChanged.fire(JSON.stringify({data:{lines:[{text:'sk-A'},{text:'123'}]}}))
  assert(!f.events.some(e=>e.type==='ocr.result'))
  f.manager.scanWordResultChanged.fire(0,{text:'sk-A123'})
  f.flush();assert.equal(f.events.at(-1).text,'sk-A123')
})
test('startup failures and image-only results never leak image data into a key', () => {
  const f=fixture();f.manager.startScan=()=>{throw new Error('unavailable')};f.session.start('error');f.flush()
  assert.equal(f.events.at(-1).confirmed,false);assert.equal(f.timers.size,0)
  const g=fixture();g.session.start('image');g.manager.ocrStartChanged.fire();g.manager.scanWordResultChanged.fire(0,{image:'data:image/png;base64,secret'})
  assert(!g.events.some(e=>e.type==='ocr.result'));g.session.stop()
})
test('continuous editor OCR keeps its lease, deduplicates each stroke and accepts identical later text', () => {
  const f=fixture();f.session.start('editor', {continuous:true,session:'input-1',mode:'multi'})
  f.manager.ocrStartChanged.fire();f.manager.scanWordResultChanged.fire(0,{text:'相同文字'})
  f.manager.ocrCompleteChanged.fire({text:'相同文字'});f.flush()
  let results=f.events.filter(e=>e.type==='ocr.result');assert.equal(results.length,1);assert.equal(results[0].capture,1)
  assert.equal(results[0].session,'input-1');assert.equal(results[0].mode,'multi');assert(f.session.active)
  f.manager.scanWordResultChanged.fire(0,{text:'相同文字'});f.flush();assert.equal(f.events.filter(e=>e.type==='ocr.result').length,1)
  f.manager.ocrStartChanged.fire();f.manager.scanWordResultChanged.fire(0,{text:'相同文字'});f.flush()
  results=f.events.filter(e=>e.type==='ocr.result');assert.equal(results.length,2);assert.equal(results[1].capture,2)
  f.session.stop();assert.equal(f.manager.ocrStartChanged.callbacks.size,0)
})
test('switching editor OCR sessions invalidates scheduled results and changes multiline registration', () => {
  const f=fixture();f.manager.startAlmondMultiScan=(mode)=>f.calls.push(['multi',mode])
  f.session.start('old',{continuous:true,session:'input',mode:'single'})
  f.manager.ocrStartChanged.fire();f.manager.scanWordResultChanged.fire(0,{text:'stale'})
  f.session.start('new',{continuous:true,session:'input',mode:'multi'});f.flush()
  assert(!f.events.some(e=>e.type==='ocr.result'));assert(f.calls.some(c=>c[0]==='multi'&&c[1]===1))
  f.session.stop()
})
test('oversize OCR is rejected explicitly rather than truncated', () => {
  const f=fixture();f.session.start('long',{continuous:true,session:'input',mode:'multi'})
  f.manager.ocrStartChanged.fire();f.manager.scanWordResultChanged.fire(0,{lines:[{text:'a'.repeat(5000)},{text:'b'.repeat(5000)}]});f.flush()
  const result=f.events.filter(e=>e.type==='ocr.result').at(-1)
  assert.equal(result.confirmed,false);assert(result.error.includes('过长'));assert(f.session.active);f.session.stop()
})

test('empty stroke reports promptly and leaves continuous OCR armed for the next stroke', () => {
  const f=fixture();f.session.start('retry',{continuous:true,session:'input'})
  f.manager.ocrStartChanged.fire();f.manager.ocrCompleteChanged.fire('');f.flush();f.flush()
  const result=f.events.filter(e=>e.type==='ocr.result').at(-1);assert.equal(result.confirmed,false);assert(f.session.active)
  f.manager.ocrStartChanged.fire();f.manager.scanWordResultChanged.fire(0,{text:'再次扫描'});f.flush()
  assert.equal(f.events.at(-1).text,'再次扫描');f.session.stop()
})

test('opening the keyboard only arms OCR; idle completion/error signals stay silent', () => {
  const f=fixture()
  f.manager.startScan=()=>{throw new Error('Keyboard must not initiate a capture')}
  f.session.start('idle',{continuous:true,session:'input'})
  for(let i=0;i<5;++i) {
    f.manager.ocrCompleteChanged.fire('')
    f.manager.ocrStopChanged.fire('')
    f.manager.scanErrorChanged.fire()
    f.manager.scanWordResultChanged.fire(0,'')
    f.flush()
  }
  assert(f.session.active);assert.equal(f.timers.size,0)
  assert(!f.events.some(e=>e.type==='ocr.result'||e.started===true))
  f.manager.ocrStartChanged.fire();f.manager.scanWordResultChanged.fire(0,'正常划扫');f.flush()
  assert.equal(f.events.at(-1).text,'正常划扫')
  f.manager.scanErrorChanged.fire();f.flush()
  assert.equal(f.events.filter(e=>e.type==='ocr.result').length,1)
  f.manager.ocrStartChanged.fire();f.manager.scanErrorChanged.fire();f.flush()
  assert.equal(f.events.at(-1).confirmed,false);assert(f.session.active)
  f.session.stop();assert.equal(f.timers.size,0)
})

test('prepared routing precedes capture and remains registered through activation and mode changes', () => {
  const f=fixture();f.manager.startAlmondMultiScan=mode=>f.calls.push(['multi',mode])
  f.session.start('prepare',{continuous:true,session:'editor',mode:'single',prepareOnly:true})
  assert(f.events.some(e=>e.type==='ocr.prepared'&&e.ok))
  assert(f.calls.some(c=>c[0]==='addUseOcrAppId'))
  assert(!f.calls.some(c=>c[0]==='setIsOpenScan'||c[0]==='startScan'))
  const first=f.calls.length
  f.session.start('prepare',{continuous:true,session:'editor',mode:'single'})
  f.session.start('multi',{continuous:true,session:'editor',mode:'multi',prepareOnly:true})
  f.session.start('multi',{continuous:true,session:'editor',mode:'multi'})
  assert(!f.calls.slice(first).some(c=>c[0]==='deleteUseOcrAppId'||(c[0]==='setIsOpenScan'&&c[1]===false)))
  const registrations=f.calls.filter(c=>c[0]==='addUseOcrAppId').map(c=>JSON.parse(c[1]))
  assert(registrations.every(c=>c.useOcr===true));assert.equal(registrations.at(-1).scanMethod,1)
  f.session.stop();assert(f.calls.some(c=>c[0]==='deleteUseOcrAppId'))
  assert(f.calls.some(c=>c[0]==='setIsOpenScan'&&c[1]===false))
})
test('failed preparation explicitly rejects the handoff and cancels its listeners', () => {
  const f=fixture();f.manager.addUseOcrAppId=()=>false
  f.session.start('failed',{continuous:true,session:'editor',prepareOnly:true})
  assert(f.events.some(e=>e.type==='ocr.prepared'&&!e.ok))
  assert(f.session.active);assert.equal(f.manager.ocrStartChanged.callbacks.size,0)
  assert(f.calls.some(c=>c[0]==='setIsOpenScan'&&c[1]===false));f.session.stop()
  assert(!f.calls.some(c=>c[0]==='setIsOpenScan'&&c[1]===true))
})
test('preflight failure blocks pen capture temporarily and restores its original flags', () => {
  const f=fixture();f.manager.isOpenScan=true;f.manager.showImgScanMode=true
  delete f.manager.addUseOcrAppId
  f.session.start('unsupported',{continuous:true,session:'editor',prepareOnly:true})
  assert(f.events.some(e=>e.type==='ocr.prepared'&&!e.ok))
  assert(f.calls.some(c=>c[0]==='setIsOpenScan'&&c[1]===false))
  f.session.stop()
  assert.equal(f.calls.filter(c=>c[0]==='setIsOpenScan').at(-1)[1],true)
  assert.equal(f.calls.filter(c=>c[0]==='setShowImgScanMode').at(-1)[1],true)
})

test('direct pen strokes require matching editor, sequence and press/release boundaries', () => {
  const strokes=[];const f=fixture((down,sequence)=>{strokes.push([down,sequence]);return true})
  f.session.start('one',{continuous:true,session:'editor'})
  const pen=(down,sequence,extra={})=>f.session.pen({down,sequence,requestId:'one',session:'editor',...extra})
  pen(true,1,{requestId:'stale'});pen(false,1);assert.equal(strokes.length,0)
  pen(true,1);pen(true,1);pen(false,2);pen(false,1);pen(false,1)
  assert.deepEqual(strokes,[[true,1],[false,1]])
  pen(true,1);assert.equal(strokes.length,2)
  pen(true,2);f.session.stop();assert.deepEqual(strokes.at(-1),[false,2])
  pen(true,3);assert.equal(strokes.length,4)
})
test('unavailable direct OCR interface keeps input and reports a terminal failure', () => {
  const f=fixture(()=>false);f.session.start('one',{continuous:true,session:'editor'})
  f.session.pen({down:true,sequence:1,requestId:'one',session:'editor'})
  assert(f.events.some(e=>e.type==='ocr.result'&&!e.confirmed&&e.terminal))
  assert(f.calls.some(c=>c[0]==='setIsOpenScan'&&c[1]===false));f.session.stop()
})

test('native pen preparation masks only host dispatch before camera handoff and restores it on stop',()=>{
  const routes=[];const f=fixture(()=>true,enabled=>{routes.push(enabled);return true})
  f.session.start('prepare',{continuous:true,session:'editor',prepareOnly:true,nativePen:true})
  assert(f.events.some(e=>e.type==='ocr.prepared'&&e.ok));assert.deepEqual(routes,[true])
  assert(!f.calls.some(c=>c[0]==='setIsOpenScan'&&c[1]===true))
  f.session.start('active',{continuous:true,session:'editor',nativePen:true})
  f.session.start('mode',{continuous:true,session:'editor',nativePen:true,mode:'multi'})
  assert.deepEqual(routes,[true]);f.session.stop();assert.deepEqual(routes,[true,false])
  f.session.stop();assert.deepEqual(routes,[true,false])
})
test('unavailable native event mask refuses camera handoff rather than exposing the home handler',()=>{
  const f=fixture(()=>true,()=>false)
  f.session.start('fail',{continuous:true,session:'editor',prepareOnly:true,nativePen:true})
  assert(f.events.some(e=>e.type==='ocr.prepared'&&!e.ok))
  assert(!f.calls.some(c=>c[0]==='setIsOpenScan'&&c[1]===true));f.session.stop()
})

test('forwarded pen edges reassert text mode after the vendor press forces image mode',()=>{
  let f;f=fixture(()=>{f.manager.showImgScanMode=true;return true})
  f.manager.setShowImgScanMode=enabled=>{f.manager.showImgScanMode=enabled}
  f.session.start('one',{continuous:true,session:'editor'})
  for(const down of [true,false]) {
    f.session.pen({down,sequence:1,requestId:'one',session:'editor'})
    assert.equal(f.manager.showImgScanMode,false)
  }
  f.session.stop()
})

test('registration-only firmware does not require newer scan-control methods', () => {
  const f=fixture();delete f.manager.setIsOpenScan;delete f.manager.setShowImgScanMode;delete f.manager.startScan
  f.session.start('s6',{continuous:true,session:'input'})
  f.flush();assert(f.session.active);assert(!f.events.some(e=>e.type==='ocr.result'))
  f.manager.ocrStartChanged.fire(false);f.manager.ocrCompleteChanged.fire('');f.flush()
  assert(!f.events.some(e=>e.type==='ocr.result'||e.started===true))
  f.manager.ocrStartChanged.fire(true);f.manager.ocrResultChanged.fire({text:'识别文字'})
  f.manager.ocrStopChanged.fire();f.flush(900);f.flush(0)
  assert.equal(f.events.at(-1).text,'识别文字');assert(f.session.active)
  f.session.stop();assert(f.calls.some(c=>c[0]==='deleteUseOcrAppId'))
})

test('registration-only multiline cleanup restores optional modes without an open setter', () => {
  const f=fixture();delete f.manager.setIsOpenScan
  f.manager.showImgScanMode=true
  f.manager.startAlmondMultiScan=mode=>f.calls.push(['multi',mode])
  f.session.start('multi',{continuous:true,session:'input',mode:'multi'})
  assert(f.calls.some(c=>c[0]==='multi'&&c[1]===1))
  f.session.stop()
  assert(f.calls.some(c=>c[0]==='multi'&&c[1]===0))
  assert(f.calls.some(c=>c[0]==='setShowImgScanMode'&&c[1]===true))
  assert.equal(f.manager.ocrStartChanged.callbacks.size,0)
  assert.equal(f.timers.size,0)
})

test('missing both scan-control paths fails cleanly instead of pretending OCR is armed', () => {
  const f=fixture();delete f.manager.setIsOpenScan;delete f.manager.setCurrentAppId
  f.session.start('unsupported',{continuous:true,session:'input'});f.flush()
  const result=f.events.filter(e=>e.type==='ocr.result').at(-1)
  assert.equal(result.confirmed,false);assert(result.terminal)
  assert.equal(f.manager.ocrStartChanged.callbacks.size,0);assert.equal(f.timers.size,0)
})

test('failed unregister retains cleanup for retry and never reports release success', () => {
  const f=fixture();f.session.start('cleanup',{continuous:true,session:'input'})
  let fail=true;f.manager.deleteUseOcrAppId=()=>{if(fail)throw new Error('service busy')}
  assert.equal(f.session.stop(),false);assert.equal(f.session.active,null)
  assert.equal(f.manager.ocrStartChanged.callbacks.size,0);assert.equal(f.timers.size,0)
  assert.equal(f.session.stop(),false)
  fail=false;assert.equal(f.session.stop(),true);assert.equal(f.session.stop(),true)
})

test('empty word notification keeps the stroke alive for a delayed real result', () => {
  const f=fixture();f.session.start('late',{continuous:true,session:'editor'})
  f.manager.ocrStartChanged.fire(true)
  f.manager.scanWordResultChanged.fire(0,'')
  f.flush(0);f.flush(900)
  assert(!f.events.some(e=>e.type==='ocr.result'))
  f.manager.scanWordResultChanged.fire(0,{text:'稍后到达的文字'});f.flush(0)
  const results=f.events.filter(e=>e.type==='ocr.result')
  assert.equal(results.length,1);assert.equal(results[0].text,'稍后到达的文字')
  f.flush(2500);f.flush(0);assert.equal(f.events.filter(e=>e.type==='ocr.result').length,1)
  f.session.stop()
})

test('stop before the text property waits for recognition, and cancellation rejects delayed timers', () => {
  const f=fixture();f.session.start('late-property',{continuous:true,session:'editor'})
  f.manager.ocrStartChanged.fire(true);f.manager.ocrStopChanged.fire('')
  f.flush(900);f.flush(0);assert(!f.events.some(e=>e.type==='ocr.result'))
  f.manager.ocrResultChanged.fire({text:'后到的属性'});f.flush(120);f.flush(0)
  assert.equal(f.events.at(-1).text,'后到的属性')
  f.manager.ocrStartChanged.fire(true);f.manager.ocrCompleteChanged.fire('')
  const stale=[...f.timers.values()].find(t=>t.delay===2500)
  f.session.start('replacement',{continuous:true,session:'other-editor'})
  stale();f.flush(0)
  assert.equal(f.events.filter(e=>e.type==='ocr.result').length,1)
  f.session.stop()
})

test('prepare activation retains listeners and an early stroke without registering twice', () => {
  const f=fixture(()=>true,()=>true)
  const options={continuous:true,session:'editor',nativePen:true}
  f.session.start('handoff',{...options,prepareOnly:true})
  const active=f.session.active,listener=[...f.manager.ocrStartChanged.callbacks][0]
  f.manager.ocrStartChanged.fire(true);f.manager.ocrResultChanged.fire({text:'交接中的笔画'})
  f.session.start('handoff',options)
  assert.equal(f.session.active,active);assert.equal([...f.manager.ocrStartChanged.callbacks][0],listener)
  assert.equal(f.calls.filter(c=>c[0]==='addUseOcrAppId').length,1)
  assert.equal(f.session.active.capture,1);assert(f.session.active.started)
  f.manager.ocrStopChanged.fire();f.flush(900);f.flush(0)
  assert.equal(f.events.at(-1).text,'交接中的笔画');assert.equal(f.events.at(-1).capture,1)
  f.session.stop()
})

test('duplicate activation leaves an ongoing stroke intact', () => {
  const f=fixture();const options={continuous:true,session:'editor'}
  f.session.start('same',options);const active=f.session.active
  f.manager.ocrStartChanged.fire(true);f.manager.ocrResultChanged.fire({text:'正在识别'})
  f.session.start('same',options)
  assert.equal(f.session.active,active);assert.equal(f.calls.filter(c=>c[0]==='addUseOcrAppId').length,1)
  f.manager.scanWordResultChanged.fire(0,{text:'完整文字'});f.flush(0)
  assert.equal(f.events.at(-1).text,'完整文字');assert.equal(f.events.at(-1).capture,1)
  f.session.stop()
})
