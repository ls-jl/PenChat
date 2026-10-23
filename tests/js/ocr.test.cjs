const test = require('node:test')
const assert = require('node:assert/strict')
const fs = require('node:fs')
const vm = require('node:vm')
function fixture() {
  const timers = new Map(), calls = [], events = []; let clock = 0
  const signal = () => { const callbacks = new Set(); return { callbacks, on: f => callbacks.add(f), off: f => callbacks.delete(f), fire: (...a) => [...callbacks].forEach(f => f(...a)) } }
  const manager = Object.fromEntries(['ocrStartChanged','scanWordResultChanged','ocrCompleteChanged','ocrStopChanged','ocrResultChanged','scanErrorChanged'].map(k => [k,signal()]))
  for (const name of ['addUseOcrAppId','deleteUseOcrAppId','setIsOpenScan','setShowImgScanMode','setCurrentAppId','startScan']) manager[name] = (...args) => calls.push([name,...args])
  const context = vm.createContext({ module: {}, setTimeout: f => { timers.set(++clock,f);return clock }, clearTimeout: id => timers.delete(id) })
  vm.runInContext(fs.readFileSync('src/launcher/ocr.js','utf8').replace('export class OcrSession','module.exports = class OcrSession'),context)
  const session = new context.module.exports(manager,'test-app',(type,data) => events.push({type,...data}))
  return { session, manager, timers, calls, events, flush: () => { for (const [id,f] of [...timers.entries()]) { if(timers.delete(id)) f() } } }
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
