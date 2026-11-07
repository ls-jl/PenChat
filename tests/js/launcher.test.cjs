const test = require('node:test')
const assert = require('node:assert/strict')
const fs = require('node:fs')
const vm = require('node:vm')
function fixture() {
  const calls = [], timers = new Map()
  let clock = 0, editCallback
  const native = { alive: true, focused: true, panelVisible: false, focusQueries: 0, systemOverlay: () => ({supported:true,visible:native.panelVisible}), status: () => ({ alive: native.alive }), start: async value => { native.alive = true; calls.push(['start', value]) }, send: value => { calls.push(['send', value]); return true }, poll: () => [], stop: async () => calls.push(['stop']) }
  const keyboard = { textEditFinished: { on: fn => { editCallback = fn }, off: () => calls.push(['off']) }, startTextEdit: config => { calls.push(['startTextEdit', JSON.parse(config)]); return 'edit-1' }, closeTextEdit: () => calls.push(['closeEdit']) }
  const context = vm.createContext({
    console, appConfig: JSON.parse(fs.readFileSync('package.json', 'utf8')), launcher: native, am: { hasWindowFocus: () => { ++native.focusQueries; return native.focused } }, globalModule: { Global: function () { return keyboard } }, module: {},
    $workspace: '/package', $dataDir: '/data', $falcon: { jsapi: { storage: { getStorage: async () => { throw new Error('Storage must not be read') } } } },
    setInterval: fn => { timers.set(++clock, fn); return clock }, clearInterval: id => timers.delete(id),
    setTimeout: fn => { timers.set(++clock, fn); return clock }, clearTimeout: id => timers.delete(id),
  })
  vm.runInContext(fs.readFileSync('src/launcher/ocr.js', 'utf8').replace('export class OcrSession', 'globalThis.OcrSession = class OcrSession'), context)
  const script = fs.readFileSync('src/launcher/LauncherPage.vue', 'utf8').match(/<script>([\s\S]*?)<\/script>/)[1]
    .replace(/^import .*$/gm, '').replace('export default', 'module.exports =')
  vm.runInContext(script, context)
  const options = context.module.exports
  const page = Object.assign(options.data(), options.methods, { $page: { finish: () => calls.push(['finish']), $npage: {setSupportTopPanel: value => calls.push(['topPanel',value]), setSupportBack: value => calls.push(['systemBack',value]), on: (name,cb)=>calls.push(['listen',name,cb]), off: name=>calls.push(['unlisten',name])} } })
  return { page, options, calls, native, context, timers, edit: (...args) => editCallback(...args) }
}
test('startup is deduplicated, sends clean initialization, and disposal closes exactly once', async () => {
  const f = fixture(); await Promise.all([f.page.boot(), f.page.boot()])
  assert.equal(f.calls.filter(c => c[0] === 'start').length, 1)
  assert(f.calls.some(c => c[0] === 'send' && c[1].type === 'init' && !('legacy' in c[1])))
  f.native.poll = () => [{ type: 'ready', generation: f.page._generation }]
  f.page.poll(); f.page.dispose(); f.page.dispose()
  assert.equal(f.calls.filter(c => c[0] === 'stop').length, 1)
  assert.equal(f.timers.size, 0)
})
test('native input owns root and child back gestures; lifecycle restores system policy', async () => {
  const f=fixture(); await f.page.boot()
  const last=()=>f.calls.filter(c=>c[0]==='systemBack').at(-1)[1]
  assert.equal(last(),false)
  f.page.navigationState({sequence:1,root:true});assert.equal(last(),false)
  f.page.navigationState({sequence:2,root:false});assert.equal(last(),false)
  f.page.navigationState({sequence:1,root:true});assert.equal(last(),false)
  f.options.onHide.call(f.page);assert.equal(last(),true)
  f.page.resume();assert.equal(last(),false)
  f.page.navigationState({sequence:3,root:true});assert.equal(last(),false)
  f.page.editorState({sequence:1,session:'edit',active:true});assert.equal(last(),false)
  f.page.navigationState({sequence:4,root:true});assert.equal(last(),false)
  f.page.editorState({sequence:2,session:'edit',active:false});assert.equal(last(),false)
  f.page.fail('test');assert.equal(last(),true)
  f.page.dispose();assert.equal(last(),true)
})
test('system keyboard rejects stale completion, supports cancel, and suppresses touch forwarding', async () => {
  const f = fixture(); await f.page.boot()
  f.page.openKeyboard({ requestId: 'native-request', text: '', placeholder: '请输入' })
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
test('native keyboard is independent of Falcon shared input methods', () => {
  const source=fs.readFileSync('src/launcher/LauncherPage.vue','utf8')
  assert(!source.includes("import('im')"))
  assert(!source.includes('queryIme'))
  const f=fixture()
  assert.equal(typeof f.page.queryIme,'undefined')
})
test('official fallback receives independent multiline and UTF-16 cursor configuration', async () => {
  const f = fixture(); await f.page.boot()
  f.page.openKeyboard({ requestId: 'k', text: '中😀', inputType: 'ZhCNPreferred', multiline: false, cursorIndex: 3 })
  const config = f.calls.find(c => c[0] === 'startTextEdit')[1]
  assert.equal(config.multiLinesEditVisible, false); assert.equal(config.cursorIndex, 3)
  f.page.dispose()
})

test('native editor locks only its page top panel, rejects old policies and releases on hide/failure', async () => {
  const f=fixture();await f.page.boot()
  f.page.editorState({session:'one',sequence:1,active:true});assert.equal(f.calls.filter(c=>c[0]==='topPanel').at(-1)[1],false)
  f.page.editorState({session:'two',sequence:3,active:true});const count=f.calls.length
  f.page.editorState({session:'one',sequence:2,active:false});assert.equal(f.calls.length,count)
  f.options.onHide.call(f.page);assert.equal(f.calls.filter(c=>c[0]==='topPanel').at(-1)[1],true)
  f.options.onShow.call(f.page);f.page.editorState({session:'two',sequence:4,active:true})
  f.page.fail('fixture');assert.equal(f.calls.filter(c=>c[0]==='topPanel').at(-1)[1],true)
})

for (const [width,height] of [[960,266],[936,280],[1024,320]]) {
  test(`hole geometry comes from native Falcon config (${width}x${height}), without a boot fallback`, async () => {
    const f=fixture();await f.page.boot()
    assert(!f.calls.some(c=>c[0]==='send'&&c[1].type==='display.area'))
    f.native.poll=()=>[{type:'booted',screen:{width,height,rotation:270,xoffset:0,yoffset:107}}]
    f.page.poll()
    const area=f.calls.find(c=>c[0]==='send'&&c[1].type==='display.area')[1]
    assert.equal(area.width,width);assert.equal(area.height,height);assert.equal(area.x,0);assert.equal(area.y,0)
    f.page.dispose()
  })
}
test('missing Falcon geometry fails instead of supplying a fixed resolution', async () => {
  const f=fixture();await f.page.boot();f.native.poll=()=>[{type:'booted'}];f.page.poll()
  assert.equal(f.page.running,false)
  assert(!f.calls.some(c=>c[0]==='send'&&c[1].type==='display.area'))
})

test('image workspace blocks top panel for crop handles and restores it on close/hide/failure', async () => {
  const f=fixture();await f.page.boot()
  f.page.navigationState({sequence:1,root:false,imageWorkspace:true})
  assert.equal(f.calls.filter(c=>c[0]==='topPanel').at(-1)[1],false)
  f.options.onHide.call(f.page)
  assert.equal(f.calls.filter(c=>c[0]==='topPanel').at(-1)[1],true)
  f.options.onShow.call(f.page)
  assert.equal(f.calls.filter(c=>c[0]==='topPanel').at(-1)[1],false)
  f.page.navigationState({sequence:2,root:true,imageWorkspace:false})
  assert.equal(f.calls.filter(c=>c[0]==='topPanel').at(-1)[1],true)
  f.page.navigationState({sequence:3,root:false,imageWorkspace:true});f.page.fail('fixture')
  assert.equal(f.calls.filter(c=>c[0]==='topPanel').at(-1)[1],true)
  f.page.dispose()
})

test('launcher never forwards or reconstructs native touch coordinates', async () => {
  const source=fs.readFileSync('src/launcher/LauncherPage.vue','utf8')
  for (const removed of ['@touchstart','@touchmove','@touchend','@touchcancel','forwardTouch','changedTouches','navigation.back','backpressed']) assert(!source.includes(removed),removed)
  const f=fixture();await f.page.boot();f.options.onHide.call(f.page);f.page.resume();f.page.dispose()
  assert(!f.calls.some(c=>c[0]==='send'&&c[1].type==='touch'))
  assert(f.calls.some(c=>c[0]==='send'&&c[1].type==='visibility'&&c[1].visible===false))
})

test('cached native restart closes only the obsolete official editor and ignores late results', async () => {
  const f=fixture();await f.page.boot()
  f.page.openKeyboard({requestId:'edit-obsolete',text:'fixture'})
  f.options.onHide.call(f.page)
  assert.equal(f.page.keyboardOpen,true) // Official UI can itself hide this page.
  f.page.running=false;f.native.alive=false
  await f.page.boot()
  assert.equal(f.page.keyboardOpen,false)
  assert.equal(f.calls.filter(c=>c[0]==='closeEdit').length,1)
  const results=()=>f.calls.filter(c=>c[0]==='send'&&c[1].type==='keyboard.result')
  f.edit('edit-1',JSON.stringify({editConfirmed:true,text:'late'}))
  assert.equal(results().length,0)
  f.page.resume();f.page.editorState({session:'new',sequence:1,active:true})
  assert.equal(f.calls.filter(c=>c[0]==='topPanel').at(-1)[1],false)
  f.page.dispose()
})

test('portrait OCR preparation rejects constructor failure without stopping the page', async () => {
  const f=fixture();await f.page.boot()
  f.context.globalModule.Global=function(){throw new Error('fixture constructor')}
  f.native.poll=()=>[{type:'ocr.prepare',requestId:'prepare-1',session:'editor-1',mode:'single',continuous:true}]
  f.page.poll()
  assert.equal(f.page.running,true);assert.equal(f.page.errorText,'')
  const response=f.calls.find(c=>c[0]==='send'&&c[1].type==='ocr.prepared')[1]
  assert.equal(response.requestId,'prepare-1');assert.equal(response.ok,false)
  assert(!f.calls.some(c=>c[0]==='stop'));f.page.dispose()
})

test('global window focus yields native overlay/input without a fake page hide', async () => {
  const f=fixture();await f.page.boot()
  f.native.focused=false;f.page.syncWindowFocus(true)
  let packets=f.calls.filter(c=>c[0]==='send'&&c[1].type==='window.focus').map(c=>c[1])
  assert.equal(packets.length,1);assert.equal(packets[0].focused,false)
  assert.equal(f.page.visible,true);assert.equal(f.timers.size,2)
  assert.equal(f.calls.filter(c=>c[0]==='systemBack').at(-1)[1],true)
  f.page.editorState({session:'editor',sequence:1,active:true})
  f.page.navigationState({sequence:1,imageWorkspace:true})
  assert.equal(f.calls.filter(c=>c[0]==='topPanel').at(-1)[1],true)
  f.page.syncWindowFocus(true)
  assert.equal(f.calls.filter(c=>c[0]==='send'&&c[1].type==='window.focus').length,1)
  f.native.focused=true;f.page.syncWindowFocus(true)
  packets=f.calls.filter(c=>c[0]==='send'&&c[1].type==='window.focus').map(c=>c[1])
  assert.equal(packets.length,2);assert.equal(packets[1].focused,true);assert(packets[1].sequence>packets[0].sequence)
  assert.equal(f.calls.filter(c=>c[0]==='topPanel').at(-1)[1],false)
  assert.equal(f.calls.filter(c=>c[0]==='send'&&c[1].type==='visibility'&&!c[1].visible).length,0)
  f.page.dispose();assert.equal(f.timers.size,0)
})

test('window focus queries are bounded and unavailable APIs preserve old behavior', async () => {
  const f=fixture();await f.page.boot();let now=1000;f.context.Date={now:()=>now}
  f.page.syncWindowFocus(true);const before=f.native.focusQueries
  for(let i=0;i<20;++i)f.page.poll()
  assert.equal(f.native.focusQueries,before)
  now=1100;f.page.poll();assert.equal(f.native.focusQueries,before+1)
  f.context.am={hasWindowFocus:()=>{throw new Error('unsupported')}}
  f.page.syncWindowFocus(true);assert.equal(f.page.running,true);assert.equal(f.page._windowFocused,true)
  f.context.am={};f.native.systemOverlay=()=>({supported:false});f.page.syncWindowFocus(true);assert.equal(f.page._windowFocused,true)
  f.page.dispose()
})

test('focus handoff retries a rejected IPC send and rejects late input handoffs under overlay', async () => {
  const f=fixture();await f.page.boot();const send=f.native.send;let rejected=false
  f.native.send=value=>{if(value.type==='window.focus'&&!rejected){rejected=true;return false};return send(value)}
  f.native.focused=false;f.page.syncWindowFocus(true);assert.equal(f.page._focusPending,true)
  f.page.syncWindowFocus(true);assert.equal(f.page._focusPending,false)
  assert(f.calls.some(c=>c[0]==='send'&&c[1].type==='window.focus'&&!c[1].focused))
  f.page.openKeyboard({requestId:'late',text:'fixture'})
  assert(!f.calls.some(c=>c[0]==='startTextEdit'))
  f.page.startOcr({requestId:'late-ocr',session:'editor',mode:'single'})
  assert(!f.page._ocr)
  f.page.dispose()
})

test('Y07 global panel visibility overrides unchanged window focus', async () => {
  const f=fixture();await f.page.boot();f.native.panelVisible=true;f.page.syncWindowFocus(true)
  assert.equal(f.native.focused,true);assert.equal(f.page._windowFocused,false)
  let packet=f.calls.filter(c=>c[0]==='send'&&c[1].type==='window.focus').at(-1)[1]
  assert.equal(packet.focused,false)
  f.native.panelVisible=false;f.page.syncWindowFocus(true)
  packet=f.calls.filter(c=>c[0]==='send'&&c[1].type==='window.focus').at(-1)[1];assert.equal(packet.focused,true)
  f.native.systemOverlay=()=>({supported:false,visible:false});f.native.focused=false;f.page.syncWindowFocus(true)
  assert.equal(f.page._windowFocused,false)
  f.page.dispose()
})
