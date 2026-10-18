const test = require('node:test')
const assert = require('node:assert/strict')
const fs = require('node:fs')
const vm = require('node:vm')
function fixture() {
  const calls = [], timers = new Map()
  let clock = 0, editCallback
  const native = { alive: true, status: () => ({ alive: native.alive }), start: async value => { native.alive = true; calls.push(['start', value]) }, send: value => { calls.push(['send', value]); return true }, poll: () => [], stop: async () => calls.push(['stop']) }
  const keyboard = { textEditFinished: { on: fn => { editCallback = fn }, off: () => calls.push(['off']) }, startTextEdit: config => { calls.push(['startTextEdit', JSON.parse(config)]); return 'edit-1' }, closeTextEdit: () => calls.push(['closeEdit']) }
  const context = vm.createContext({
    console, appConfig: JSON.parse(fs.readFileSync('package.json', 'utf8')), launcher: native, globalModule: { Global: function () { return keyboard } }, module: {},
    $workspace: '/package', $dataDir: '/data', $falcon: { jsapi: { storage: { getStorage: async () => { throw new Error('Storage must not be read') } } } },
    setInterval: fn => { timers.set(++clock, fn); return clock }, clearInterval: id => timers.delete(id),
    setTimeout: fn => { timers.set(++clock, fn); return clock }, clearTimeout: id => timers.delete(id),
  })
  vm.runInContext(fs.readFileSync('src/launcher/ocr.js', 'utf8').replace('export class OcrSession', 'globalThis.OcrSession = class OcrSession'), context)
  const script = fs.readFileSync('src/launcher/LauncherPage.vue', 'utf8').match(/<script>([\s\S]*?)<\/script>/)[1]
    .replace(/^import .*$/gm, '').replace('export default', 'module.exports =')
  vm.runInContext(script, context)
  const options = context.module.exports
  const page = Object.assign(options.data(), options.methods, { $page: { finish: () => calls.push(['finish']) } })
  return { page, options, calls, native, context, timers, edit: (...args) => editCallback(...args) }
}
test('Falcon pageX/pageY touches retain logical coordinates through the full-screen hole', () => {
  const f = fixture(); f.page.running = true; f.page._generation = 7
  f.page.touchStart({ changedTouches: [{ pageX: 40, pageY: 100 }] })
  f.page.touchEnd({ changedTouches: [{ pageX: 40, pageY: 100 }] })
  assert.deepEqual(f.calls.map(c => [c[1].x, c[1].y, c[1].pressed]), [[40,100,true],[40,100,false]])
})
test('startup is deduplicated, sends clean initialization, and disposal closes exactly once', async () => {
  const f = fixture(); await Promise.all([f.page.boot(), f.page.boot()])
  assert.equal(f.calls.filter(c => c[0] === 'start').length, 1)
  assert(f.calls.some(c => c[0] === 'send' && c[1].type === 'init' && !('legacy' in c[1])))
  f.native.poll = () => [{ type: 'ready', generation: f.page._generation }]
  f.page.poll(); f.page.dispose(); f.page.dispose()
  assert.equal(f.calls.filter(c => c[0] === 'stop').length, 1)
  assert.equal(f.timers.size, 0)
})
test('system keyboard rejects stale completion, supports cancel, and suppresses touch forwarding', async () => {
  const f = fixture(); await f.page.boot()
  f.page.openKeyboard({ requestId: 'native-request', text: '', placeholder: '请输入' })
  const count = f.calls.length
  f.page.touchStart({ changedTouches: [{ pageX: 10, pageY: 10 }] })
  assert.equal(f.calls.length, count)
  f.edit('old-edit', JSON.stringify({ editConfirmed: true, text: 'stale' }))
  assert(f.page.keyboardOpen)
  f.edit('edit-1', JSON.stringify({ editConfirmed: false, text: 'discard' }))
  assert.equal(f.page.keyboardOpen, false)
  const result = f.calls.filter(c => c[0] === 'send' && c[1].type === 'keyboard.result').pop()[1]
  assert.equal(result.requestId, 'native-request'); assert.equal(result.confirmed, false)
  f.page.dispose()
})
test('background releases pointer, pauses native display, and foreground restores visibility', async () => {
  const f = fixture(); await f.page.boot()
  f.options.onHide.call(f.page); f.options.onShow.call(f.page)
  const visibility = f.calls.filter(c => c[0] === 'send' && c[1].type === 'visibility').map(c => c[1].visible)
  assert.deepEqual(visibility, [true, false, true])
  f.page.dispose()
})
test('native errors show retry UI and stop polling', async () => {
  const f = fixture(); await f.page.boot()
  f.native.poll = () => [{ type: 'fatal', error: 'fixture failure' }]
  f.page.poll(); assert.equal(f.page.errorText, 'fixture failure'); assert.equal(f.page.running, false); assert.equal(f.timers.size, 0)
})

test('a Falcon-cached component can restart after unload and foreground reentry', async () => {
  const f = fixture(); await f.page.boot(); f.options.onUnload.call(f.page)
  assert.equal(f.page.running, false)
  f.options.onShow.call(f.page)
  await new Promise(resolve => setImmediate(resolve))
  assert.equal(f.calls.filter(c => c[0] === 'start').length, 2)
  f.page.dispose()
})

test('foreground recovery checks native health when background polling was paused', async () => {
  const f = fixture(); await f.page.boot(); f.options.onHide.call(f.page)
  f.native.alive = false
  f.options.onShow.call(f.page)
  await new Promise(resolve => setImmediate(resolve))
  assert.equal(f.calls.filter(c => c[0] === 'start').length, 2)
  f.page.dispose()
})

test('cached launcher reattaches IME listener after disposal with an open editor', async () => {
  const f = fixture(); await f.page.boot()
  f.page.openKeyboard({ requestId: 'old' }); f.page.dispose()
  assert.equal(f.page.keyboardOpen, false)
  assert.equal(f.page._editFinished, null)
  f.options.onShow.call(f.page)
  await new Promise(resolve => setImmediate(resolve))
  f.page.openKeyboard({ requestId: 'new' })
  f.edit('edit-1', JSON.stringify({ editConfirmed: true, text: '重新输入' }))
  assert.equal(f.page.keyboardOpen, false)
  assert(f.calls.some(c => c[0] === 'send' && c[1].type === 'keyboard.result' && c[1].requestId === 'new'))
  f.page.dispose()
})

test('API key editors request English input while chat defaults to Chinese', async () => {
  const f = fixture(); await f.page.boot()
  f.page.openKeyboard({ requestId: 'key', inputType: 'EnUSOnly', maxlength: 4096 })
  assert.equal(f.calls.find(c => c[0] === 'startTextEdit')[1].inputType, 'EnUSOnly')
  assert.equal(f.calls.find(c => c[0] === 'startTextEdit')[1].multiLinesEditVisible, false)
  f.edit('edit-1', JSON.stringify({ editConfirmed: false }))
  f.page.openKeyboard({ requestId: 'chat' })
  assert.equal(f.calls.filter(c => c[0] === 'startTextEdit').pop()[1].inputType, 'ZhCNPreferred')
  f.page.dispose()
})

test('touch cancellation and background release are distinct from a committed pull release', async () => {
  const f = fixture(); await f.page.boot()
  f.page.touchStart({ changedTouches: [{ pageX: 500, pageY: 190 }] })
  f.page.touchCancel({ changedTouches: [{ pageX: 500, pageY: 80 }] })
  let event = f.calls.filter(c => c[0] === 'send' && c[1].type === 'touch').pop()[1]
  assert.equal(event.pressed, false); assert.equal(event.cancelled, true)
  f.options.onHide.call(f.page)
  event = f.calls.filter(c => c[0] === 'send' && c[1].type === 'touch').pop()[1]
  assert.equal(event.cancelled, true)
  f.page.dispose()
})


test('ready delivered during poll registration cancels the already armed timeout', async () => {
  const f = fixture()
  let timeout
  f.context.setTimeout = fn => { timeout = fn; f.timers.set(999, fn); return 999 }
  f.context.setInterval = fn => { f.timers.set(998, fn); fn(); return 998 }
  f.native.poll = () => [{ type: 'ready', generation: f.page._generation }]
  await f.page.boot()
  assert.equal(f.page._nativeReady, true)
  assert.equal(f.timers.has(999), false)
  timeout() // A queued callback may still run after clearTimeout.
  assert.equal(f.page.running, true)
  assert.equal(f.page.errorText, '')
  f.page.dispose()
})

test('a timeout from a previous startup cannot close a later generation', async () => {
  const f = fixture()
  await f.page.boot()
  const expired = f.timers.get(f.page._readyTimer)
  f.page.dispose()
  f.options.onShow.call(f.page)
  await new Promise(resolve => setImmediate(resolve))
  expired()
  assert.equal(f.page.running, true)
  f.page.dispose()
})
