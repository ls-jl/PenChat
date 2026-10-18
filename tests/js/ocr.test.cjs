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
  return { session, manager, timers, calls, events, flush: () => { for (const f of [...timers.values()]) f() } }
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
