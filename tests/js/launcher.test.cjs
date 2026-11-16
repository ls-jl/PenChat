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
    console, appConfig: JSON.parse(fs.readFileSync('package.json', 'utf8')), launcher: native, am: { getTopApp: () => 'other-app', hasWindowFocus: () => { ++native.focusQueries; return native.focused } }, globalModule: { Global: function () { return keyboard } }, module: {},
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
  return { page, options, calls, native, keyboard, context, timers, edit: (...args) => editCallback(...args) }
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
  assert.equal(f.page.nativeReady, true)
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
test('legacy official fallback preserves initial text and isolates cancel/reopen identities', async () => {
  const f = fixture(); await f.page.boot()
  let sequence = 0
  f.keyboard.startTextEdit = config => {
    f.calls.push(['startTextEdit', JSON.parse(config)])
    return 'legacy-' + (++sequence)
  }
  const firstText = '中😀 fixture-first-75daaaaa'
  f.page.openKeyboard({ requestId: 'first', inline: false, text: firstText, inputType: 'ZhCNPreferred', multiline: false, cursorIndex: 3 })
  const config = f.calls.find(c => c[0] === 'startTextEdit')[1]
  assert.equal(config.text, firstText)
  assert.equal(config.multiLinesEditVisible, false); assert.equal(config.cursorIndex, 3)
  assert.equal(f.page._keyboardUuid, 'legacy-1')
  f.edit('legacy-1', JSON.stringify({ editConfirmed: false, text: 'discarded fake edit' }))
  assert.equal(f.page.keyboardOpen, false)
  assert.equal(f.page._keyboardUuid, '')
  const secondText = 'fixture-second-a2009152'
  f.page.openKeyboard({ requestId: 'second', inline: false, text: secondText, inputType: 'EnUSOnly', multiline: true, cursorIndex: secondText.length })
  const reopened = f.calls.filter(c => c[0] === 'startTextEdit')
  assert.equal(reopened.length, 2)
  assert.equal(reopened[1][1].text, secondText)
  assert.equal(reopened[1][1].cursorIndex, secondText.length)
  assert.equal(reopened[1][1].multiLinesEditVisible, true)
  assert.equal(reopened[1][1].inputType, 'EnUSOnly')
  assert.equal(f.page._keyboardUuid, 'legacy-2')
  const results = () => f.calls.filter(c => c[0] === 'send' && c[1].type === 'keyboard.result')
  const beforeStale = results().length
  f.edit('legacy-1', JSON.stringify({ editConfirmed: true, text: firstText }))
  assert.equal(results().length, beforeStale)
  assert.equal(f.page.keyboardOpen, true)
  assert.equal(f.page._keyboardRequest, 'second')
  f.edit('legacy-2', JSON.stringify({ editConfirmed: true, text: secondText }))
  assert.equal(f.page.keyboardOpen, false)
  assert.equal(results().at(-1)[1].requestId, 'second')
  assert.equal(results().at(-1)[1].text, secondText)
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
  f.page._global=null
  f.context.globalModule.Global=function(){throw new Error('fixture constructor')}
  f.native.poll=()=>[{type:'ocr.prepare',requestId:'prepare-1',session:'editor-1',mode:'single',continuous:true}]
  f.page.poll()
  assert.equal(f.page.running,true);assert.equal(f.page.errorText,'')
  const response=f.calls.find(c=>c[0]==='send'&&c[1].type==='ocr.prepared')[1]
  assert.equal(response.requestId,'prepare-1');assert.equal(response.ok,false)
  assert(!f.calls.some(c=>c[0]==='stop'));f.page.dispose()
})

test('startup forwards read-only OCR capabilities and constructor failure stays local', async () => {
  const f=fixture()
  for(const name of ['addUseOcrAppId','deleteUseOcrAppId','setCurrentAppId','startAlmondMultiScan'])
    f.keyboard[name]=()=>{throw new Error('capability probe changed scan state')}
  for(const name of ['ocrStartChanged','ocrStopChanged'])f.keyboard[name]={on(){},off(){}}
  await f.page.boot()
  const init=f.calls.find(c=>c[0]==='send'&&c[1].type==='init')[1]
  assert.equal(init.ocrCapabilities.single,true);assert.equal(init.ocrCapabilities.multi,true)
  assert.equal(f.page._ocr,null);f.page.dispose()
  const g=fixture();g.context.globalModule.Global=function(){throw new Error('missing module')}
  await g.page.boot()
  const unknown=g.calls.find(c=>c[0]==='send'&&c[1].type==='init')[1]
  assert.equal(unknown.ocrCapabilities.multi,false);assert.equal(g.page.running,true);g.page.dispose()
})
test('OCR start reports scoped capabilities and rejects an unavailable multi request', async () => {
  const f=fixture();await f.page.boot()
  for(const name of ['addUseOcrAppId','deleteUseOcrAppId','setCurrentAppId'])
    f.keyboard[name]=()=>{throw new Error('unsupported multi must not register')}
  for(const name of ['ocrStartChanged','ocrStopChanged'])f.keyboard[name]={on(){},off(){}}
  f.page.startOcr({requestId:'ocr-1',session:'field-1',mode:'multi',continuous:true},true)
  const capability=f.calls.find(c=>c[0]==='send'&&c[1].type==='ocr.capabilities')[1]
  assert.equal(capability.requestId,'ocr-1');assert.equal(capability.session,'field-1')
  assert.equal(capability.single,true);assert.equal(capability.multi,false)
  const result=f.calls.find(c=>c[0]==='send'&&c[1].type==='ocr.prepared')[1]
  assert.equal(result.ok,false);assert.equal(f.page._ocr,null)
  assert.equal(f.page.running,true);f.page.dispose()
})
test('rejected multi replacement releases the old registration and retries cleanup under the new ID', async () => {
  const f=fixture();await f.page.boot()
  const signal=()=>({on(){},off(){}})
  for(const name of ['ocrStartChanged','ocrStopChanged'])f.keyboard[name]=signal()
  f.keyboard.addUseOcrAppId=()=>true;f.keyboard.setCurrentAppId=()=>{}
  let released=false, attempts=0
  f.keyboard.deleteUseOcrAppId=()=>{++attempts;return released}
  f.page.startOcr({requestId:'old',session:'field',mode:'single',continuous:true})
  assert(f.page._ocr.active)
  f.page.startOcr({requestId:'new',session:'field',mode:'multi',continuous:true},true)
  assert.equal(f.page._ocr.active,null);assert(f.page._ocr.pendingCleanup)
  assert.equal(attempts,1)
  f.page.stopOcr('new',true)
  let ack=f.calls.filter(c=>c[0]==='send'&&c[1].type==='ocr.stopped').at(-1)[1]
  assert.equal(ack.requestId,'new');assert.equal(ack.ok,false)
  released=true;f.page.stopOcr('new',true)
  ack=f.calls.filter(c=>c[0]==='send'&&c[1].type==='ocr.stopped').at(-1)[1]
  assert.equal(ack.ok,true);assert.equal(f.page._ocr,null);assert.equal(attempts,3)
  f.page.dispose()
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



function portraitOfficial(f) {
  f.page._screen={width:568,height:1210}
  f.page.$nextTick=fn=>fn()
  f.page.$refs={officialInput:{setCursorPosition:n=>f.calls.push(['cursor',n])}}
  f.native.officialKeyboard=()=>({supported:true,visible:true,height:550,foreground:true})
  f.native.closeOfficialKeyboard=()=>f.calls.push(['hideOfficial'])
}
test('portrait official IME attaches to a real field and confirms once after callback unwinds',async()=>{
 const f=fixture();await f.page.boot();portraitOfficial(f)
 f.page.openKeyboard({requestId:'portrait',text:'中😀',multiline:true,cursorIndex:3})
 assert(f.page.officialInline);assert.equal(f.page.officialText,'中😀')
 assert(!f.calls.some(c=>c[0]==='startTextEdit'))
 assert(f.calls.some(c=>c[0]==='cursor'&&c[1]===3))
 f.page.inlineOfficialInput('修改');f.page.finishInlineOfficial();f.page.finishInlineOfficial()
 assert.equal(f.calls.filter(c=>c[0]==='send'&&c[1].type==='keyboard.result').length,0)
 f.timers.get(f.page._keyboardTimer)()
 const results=f.calls.filter(c=>c[0]==='send'&&c[1].type==='keyboard.result')
 assert.equal(results.length,1);assert.equal(results[0][1].text,'修改');assert(results[0][1].confirmed)
 assert(!f.page.officialInline);assert.equal(f.page.officialText,'');assert.equal(f.page._keyboardMonitor,null)
 f.page.dispose()
})
test('portrait official dismissal retains secret-field edits and rejects late confirmation',async()=>{
 const f=fixture();await f.page.boot();portraitOfficial(f)
 f.page.openKeyboard({requestId:'secret',text:'fixture',sensitive:true,inputType:'EnUSOnly',multiline:false})
 assert(f.page.officialSensitive&&f.page.officialSingleLine)
 assert.equal(f.page.officialInputType,'EnUSOnly')
 f.page.finishInlineOfficial('discard');const stale=f.timers.get(f.page._keyboardTimer)
 f.page.cancelOfficialKeyboard();stale()
 const results=f.calls.filter(c=>c[0]==='send'&&c[1].type==='keyboard.result')
 assert.equal(results.length,1);assert(results[0][1].confirmed);assert.equal(results[0][1].text,'discard')
 assert.equal(f.page._keyboardMonitor,null);assert.equal(f.page.officialText,'')
 f.page.dispose()
})
test('portrait official monitoring survives page hide and recovers an unavailable panel',async()=>{
 const f=fixture();await f.page.boot();portraitOfficial(f)
 f.page.openKeyboard({requestId:'unavailable',text:'保留'})
 const monitor=f.page._keyboardMonitor
 f.options.onHide.call(f.page)
 assert(f.timers.has(monitor))
 f.native.officialKeyboard=()=>({supported:true,visible:false})
 f.page._officialDeadline=0;f.page._keyboardWatchAt=1;f.timers.get(monitor)()
 const results=f.calls.filter(c=>c[0]==='send'&&c[1].type==='keyboard.result')
 assert.equal(results.length,1);assert(!results[0][1].confirmed);assert(results[0][1].error)
 assert.equal(f.page._keyboardMonitor,null)
 f.page.dispose()
})
for(const failure of ['throw','empty-uuid','missing-window']) {
 test(`official editor ${failure} restores native editing once`,async()=>{
  const f=fixture();await f.page.boot()
  f.native.officialKeyboard=()=>({supported:true,visible:false})
  if(failure==='empty-uuid')f.keyboard.startTextEdit=()=>''
  if(failure==='throw')f.keyboard.startTextEdit=()=>{throw Error('fixture')}
  f.page.openKeyboard({requestId:'failed',text:'保留'})
  if(failure==='missing-window'){f.page._keyboardWatchAt=1;f.page.checkOfficialKeyboard()}
  assert.equal(f.page.keyboardOpen,false)
  const results=f.calls.filter(c=>c[0]==='send'&&c[1].type==='keyboard.result')
  assert.equal(results.length,1);assert.equal(results[0][1].confirmed,false);assert(results[0][1].error)
  f.page.dispose()
 })
}

test('portrait official field stays in the original page and forwards live text/cursor',async()=>{
 const f=fixture();await f.page.boot();portraitOfficial(f)
 f.page.openKeyboard({requestId:'inline',text:'原文',field:{x:24,y:520,width:520,height:44}})
 assert.equal(f.page.officialInputStyle.top,'520px')
 f.page.inlineOfficialInput({text:'修改😀',cursorPosition:4})
 const change=f.calls.filter(c=>c[0]==='send'&&c[1].type==='keyboard.change').at(-1)[1]
 assert.equal(change.text,'修改😀');assert.equal(change.cursorIndex,4)
 f.page._keyboardWatchAt=1;f.page.checkOfficialKeyboard()
 const layout=f.calls.filter(c=>c[0]==='send'&&c[1].type==='keyboard.layout').at(-1)[1]
 assert(layout.foreground);assert.equal(layout.height,550)
 f.page.inlineOfficialField({requestId:'stale',x:1,y:2,width:30,height:40})
 assert.equal(f.page.officialInputStyle.top,'520px')
 f.page.inlineOfficialField({requestId:'inline',x:24,y:530,width:520,height:44})
 assert.equal(f.page.officialInputStyle.top,'530px')
 f.page.dispose()
})

test('portrait proxy retains its connection with vendor confirmation and rejects stale control events',async()=>{
 const f=fixture();await f.page.boot();portraitOfficial(f)
 f.page.openKeyboard({requestId:'first',text:'原文',multiline:true})
 const oldHandlers=f.page.officialInputHandlers,oldKey=f.page.officialControlKey
 f.page._keyboardWatchAt=1;f.page.checkOfficialKeyboard()
 assert.equal(f.calls.filter(c=>c[0]==='startTextEdit').length,1)
 assert(!('text' in f.calls.find(c=>c[0]==='startTextEdit')[1]))
 oldHandlers.input('修改')
 assert(f.page._keyboardUuid);assert(!f.page._keyboardCompleting)
 f.edit('stale-id',JSON.stringify({editConfirmed:true}));assert(!f.page._keyboardCompleting)
 f.edit(f.page._keyboardUuid,JSON.stringify({editConfirmed:true,text:'ignored vendor buffer'}))
 assert(f.page._keyboardCompleting&&f.page.keyboardOpen)
 f.page._keyboardWatchAt=1;f.native.officialKeyboard=()=>({supported:true,visible:false})
 f.page.checkOfficialKeyboard();assert(f.page.keyboardOpen)
 f.timers.get(f.page._keyboardTimer)()
 const result=f.calls.filter(c=>c[0]==='send'&&c[1].type==='keyboard.result').at(-1)[1]
 assert(result.confirmed);assert.equal(result.text,'修改')
 f.page.openKeyboard({requestId:'second',text:'原文'})
 assert.notEqual(f.page.officialControlKey,oldKey)
 oldHandlers.input('stale');oldHandlers.confirm('stale')
 assert.equal(f.page.officialText,'原文');assert(!f.page._keyboardCompleting)
 f.native.officialKeyboard=()=>({supported:true,visible:true,height:550,foreground:true})
 f.page._keyboardWatchAt=1;f.page.checkOfficialKeyboard()
 f.native.officialKeyboard=()=>({supported:true,visible:false})
 f.page._keyboardWatchAt=1;f.page.checkOfficialKeyboard()
 assert(!f.page.keyboardOpen)
 assert(f.calls.filter(c=>c[0]==='send'&&c[1].type==='keyboard.result').at(-1)[1].confirmed)
 f.page.dispose()
})

test('startup cover is reactive and remains until a successfully presented native page is ready', async () => {
  const f=fixture();await f.page.boot();assert.equal(f.page.nativeReady,false)
  f.native.poll=()=>[{type:'startup',stage:'data'}];f.page.poll()
  assert.equal(f.page.startupText,'正在读取数据…');assert.equal(f.page.nativeReady,false)
  f.native.poll=()=>[{type:'ready',generation:f.page._generation}];f.page.poll()
  assert.equal(f.page.nativeReady,true);f.page.dispose()
})

test('OCR cancel acknowledges completed cleanup and leaves a newer registration intact', async () => {
  const f=fixture();await f.page.boot();let stops=0,ok=false
  f.page._ocrRequest='one';f.page._ocr={stop:()=>{++stops;return ok}}
  f.native.poll=()=>[{type:'ocr.cancel',requestId:'one',generation:f.page._generation}]
  f.page.poll();let ack=f.calls.filter(c=>c[0]==='send'&&c[1].type==='ocr.stopped').at(-1)[1]
  assert.equal(ack.ok,false);assert(f.page._ocr)
  ok=true;f.page.poll();ack=f.calls.filter(c=>c[0]==='send'&&c[1].type==='ocr.stopped').at(-1)[1]
  assert.equal(ack.ok,true);assert.equal(f.page._ocr,null)
  f.page._ocrRequest='new';f.page._ocr={stop:()=>{throw new Error('Must preserve newer OCR')}}
  f.page.poll();assert.equal(f.page._ocrRequest,'new');assert.equal(stops,2)
  f.page._ocr=null;f.page.dispose()
})

test('restart does not launch native capture while old OCR cleanup is unconfirmed', async () => {
  const f=fixture();f.page._ocr={pendingCleanup:true,stop:()=>false};f.page._ocrRequest='old'
  await f.page.boot();assert(!f.calls.some(c=>c[0]==='start'));assert(f.page.errorText.includes('尚未释放'))
  f.page._ocr=null;f.page.dispose()
})

test('focus combines system panel visibility with composited display lease and fails input closed', async () => {
  const f=fixture();await f.page.boot();f.page.nativeReady=true
  const latest=()=>f.calls.filter(c=>c[0]==='send'&&c[1].type==='input.focus').at(-1)[1]
  f.page.syncWindowFocus(true);f.page.syncInputFocus(true);assert(latest().active)
  f.native.panelVisible=true;f.page.syncWindowFocus(true)
  assert.equal(latest().active,false);assert.equal(latest().displayActive,true)
  f.native.panelVisible=false;f.page.syncWindowFocus(true);assert(latest().active)
  f.native.focused=false;f.page.syncWindowFocus(true)
  assert.equal(latest().active,false);assert.equal(latest().displayActive,true)
  f.options.onHide.call(f.page);assert.equal(latest().displayActive,false)
  f.page.dispose()
})

test('portrait official display follows its owned IME without granting input focus',async()=>{
 const f=fixture();await f.page.boot();portraitOfficial(f)
 let top='fixture-ime-owner'
 f.context.am.getTopApp=()=>top;f.native.focused=false
 f.native.officialKeyboard=()=>({supported:true,visible:true,height:590,foreground:false})
 f.page.openKeyboard({requestId:'owned-ime',text:''})
 const layout=()=>{f.page._keyboardWatchAt=1;f.page.checkOfficialKeyboard();return f.calls.filter(c=>c[0]==='send'&&c[1].type==='keyboard.layout').at(-1)[1]}
 assert(layout().foreground)
 assert(!f.calls.filter(c=>c[0]==='send'&&c[1].type==='input.focus').at(-1)[1].active)
 top='another-app';assert(!layout().foreground)
 top='fixture-ime-owner';f.native.panelVisible=true;assert(!layout().foreground)
 f.native.panelVisible=false;assert(layout().foreground)
 f.options.onHide.call(f.page);assert(!layout().foreground)
 f.page.dispose()
})
test('native appearance uses resolved metrics and preserves official field coordinates', () => {
  const f=fixture();const page=f.page
  const colors={background:'#ffffff',text:'#20242d',muted:'#626b78',surface:'#eef1f5',primaryText:'#ffffff',caret:'#2457c5'}
  const metrics={title:35,body:26,caption:25,gap:18,inset:40,top:25,bottom:31,retryWidth:225,leaveX:294,leaveWidth:150,controlHeight:56,radius:15}
  page.nativeAreaStyle={width:'568px',height:'1210px'}
  page.applyAppearance({mode:'light',colors,metrics})
  assert.equal(page.pageStyle.backgroundColor,'#ffffff')
  assert.equal(page.pageStyle.width,'568px')
  assert.equal(page.appearanceStyles.retry.width,'225px')
  page.officialInline=true;page._keyboardRequest='r'
  page.inlineOfficialField({requestId:'r',x:12,y:68,width:300,height:44})
  assert.equal(page.officialInputStyle.left,'12px')
  assert.equal(page.officialInputStyle.color,'transparent')
  assert.equal(page.officialInputStyle.opacity,0)
  assert.equal(page.appearanceStyles.officialPanel.backgroundColor,'transparent')
  const previous=page.appearanceStyles
  page.applyAppearance({mode:'light',colors:{...colors,text:'invalid'},metrics})
  assert.equal(page.appearanceStyles,previous)
})


test('portrait official edits retain the Falcon seed and unchanged field style', async () => {
  const f = fixture(); await f.page.boot(); portraitOfficial(f)
  const field = {x:24,y:520,width:520,height:44}
  f.page.openKeyboard({requestId:'stable',text:'原文',field})
  const style = f.page.officialInputStyle
  f.page.inlineOfficialInput({text:'修改',cursorPosition:2})
  assert.equal(f.page.officialInitialText,'原文')
  assert.equal(f.page.officialText,'修改')
  assert(!Object.hasOwn(f.options.data(),'officialText'),'Live text must not trigger Vue patches')
  const template = fs.readFileSync('src/launcher/LauncherPage.vue','utf8').split('<script>')[0]
  assert.equal((template.match(/:value="officialInitialText"/g)||[]).length,2)
  assert.equal((template.match(/:showCursor="false"/g)||[]).length,2)
  assert.equal((template.match(/:autofocus="true"/g)||[]).length,2)
  assert.equal(style.opacity,0)
  assert.equal(style.width,'520px')
  assert.equal(style.height,'44px')
  assert(!/display:\s*none|visibility:\s*hidden/.test(template))
  f.page.inlineOfficialField({...field,requestId:'stable',cursorIndex:2})
  assert.equal(f.page.officialInputStyle,style)
  f.page.inlineOfficialField({...field,requestId:'stable',y:530})
  assert.notEqual(f.page.officialInputStyle,style)
  f.page.cancelOfficialKeyboard()
  assert.equal(f.page.officialInitialText,'')
  f.page.openKeyboard({requestId:'next',text:'下次',field})
  assert.equal(f.page.officialInitialText,'下次')
  assert.equal(f.page.officialText,'下次')
  f.page.dispose()
})


test('official proxy uses native field pixels and retains them over appearance updates', async () => {
  const f=fixture();await f.page.boot();portraitOfficial(f)
  const field={x:24,y:520,width:520,height:44,fontSize:31,fontLineHeight:36}
  f.page.openKeyboard({requestId:'font',text:'text',field})
  const style=f.page.officialInputStyle
  assert.equal(style.fontSize,'31px');assert(!('lineHeight' in style))
  f.page.inlineOfficialField({...field,requestId:'font'})
  assert.equal(f.page.officialInputStyle,style)
  const colors={background:'#ffffff',text:'#20242d',muted:'#626b78',surface:'#eef1f5',primaryText:'#ffffff',caret:'#2457c5'}
  const metrics={title:35,body:26,caption:25,gap:18,inset:40,top:25,bottom:31,retryWidth:225,leaveX:294,leaveWidth:150,controlHeight:56,radius:15}
  f.page.applyAppearance({colors,metrics})
  assert.equal(f.page.officialInputStyle,style)
  f.page.inlineOfficialField({...field,requestId:'stale',fontSize:50})
  assert.equal(f.page.officialInputStyle,style)
  for(const fontSize of [undefined,0,-1,Infinity,4097,'30']) {
    f.page.inlineOfficialField({...field,requestId:'font',fontSize})
    assert.equal(f.page.officialInputStyle,style)
  }
  f.page.inlineOfficialField({...field,requestId:'font',fontSize:33})
  assert.equal(f.page.officialInputStyle.fontSize,'33px')
  f.page.cancelOfficialKeyboard()
  f.page.openKeyboard({requestId:'next-font',text:'next',field:{...field,fontSize:22}})
  assert.equal(f.page.officialInputStyle.fontSize,'22px')
  f.page.dispose()
})


test('official viewport keeps full Falcon geometry while validating native keyboard leases', async()=>{
 const f=fixture();await f.page.boot();portraitOfficial(f)
 f.page.nativeAreaStyle={left:'0px',top:'0px',width:'568px',height:'1210px'}
 f.page.updatePageStyle()
 const base=f.page.nativeAreaStyle
 const field={x:24,y:520,width:520,height:44,fontSize:26}
 f.page.openKeyboard({requestId:'clip',text:'text',field})
 const inputStyle=f.page.officialInputStyle
 const poll=height=>{f.native.officialKeyboard=()=>({supported:true,visible:true,height,foreground:true});f.page._keyboardWatchAt=1;f.page.checkOfficialKeyboard()}
 for(const height of [0,-1,1110,1210,Infinity,NaN,550.5,'550']) poll(height)
 assert.equal(f.page.nativeAreaStyle.height,'1210px')
 assert(!f.calls.some(c=>c[0]==='send'&&c[1].type==='keyboard.layout'))
 poll(550)
 assert.equal(f.page.officialHoleStyle.height,'660px')
 assert.equal(f.calls.filter(c=>c[0]==='send'&&c[1].type==='keyboard.layout').at(-1)[1].height,550)
 assert.equal(f.page.nativeAreaStyle.height,'1210px');assert.equal(f.page.pageStyle.height,'1210px')
 assert.equal(f.page.nativeAreaStyle,base);assert.equal(base.height,'1210px');assert.equal(f.page._screen.height,1210)
 const viewport=f.page.nativeAreaStyle,page=f.page.pageStyle
 poll(550);assert.equal(f.page.nativeAreaStyle,viewport);assert.equal(f.page.pageStyle,page)
 f.page.inlineOfficialField({...field,requestId:'clip'})
 assert.equal(f.page.officialInputStyle,inputStyle)
 const colors={background:'#ffffff',text:'#20242d',muted:'#626b78',surface:'#eef1f5',primaryText:'#ffffff',caret:'#2457c5'}
 const metrics={title:35,body:26,caption:25,gap:18,inset:40,top:25,bottom:31,retryWidth:225,leaveX:294,leaveWidth:150,controlHeight:56,radius:15}
 f.page.applyAppearance({mode:'light',colors,metrics})
 assert.equal(f.page.nativeAreaStyle,viewport);assert.equal(f.page.pageStyle.height,'1210px')
 assert.equal(f.page.officialInputStyle,inputStyle)
 poll(590);assert.equal(f.page.pageStyle.height,'1210px');assert.equal(f.page.officialHoleStyle.height,'620px')
 assert.equal(f.calls.filter(c=>c[0]==='send'&&c[1].type==='keyboard.layout').at(-1)[1].height,590)
 f.page.cancelOfficialKeyboard();assert.equal(f.page.pageStyle.height,'1210px');assert.equal(f.page.nativeAreaStyle.height,'1210px')
 f.page.openKeyboard({requestId:'reclip',text:'again',field});assert.equal(f.page.pageStyle.height,'1210px')
 poll(550);assert.equal(f.page.pageStyle.height,'1210px')
 const template=fs.readFileSync('src/launcher/LauncherPage.vue','utf8').split('<script>')[0]
 assert(template.includes('<hole class="native-hole" :style="officialInline ? officialHoleStyle : nativeAreaStyle">'))
 f.page.dispose()
})


test('invalid official heights expire at opening deadline or existing display lease', async()=>{
 const f=fixture();await f.page.boot();portraitOfficial(f)
 const field={x:24,y:520,width:520,height:44,fontSize:26}
 const poll=height=>{f.native.officialKeyboard=()=>({supported:true,visible:true,height,foreground:true});f.page._keyboardWatchAt=1;f.page.checkOfficialKeyboard()}
 f.page.openKeyboard({requestId:'bad-open',text:'original',field})
 poll(0);assert(f.page.keyboardOpen)
 f.page._officialDeadline=0;poll(0)
 assert(!f.page.keyboardOpen)
 let result=f.calls.filter(c=>c[0]==='send'&&c[1].type==='keyboard.result').at(-1)[1]
 assert(!result.confirmed);assert(result.error.includes('高度无效'))
 f.page.openKeyboard({requestId:'recover',text:'original',field})
 poll(550);const viewport=f.page.nativeAreaStyle
 let layouts=f.calls.filter(c=>c[0]==='send'&&c[1].type==='keyboard.layout').length
 poll(0);assert(f.page.keyboardOpen);assert.equal(f.page.nativeAreaStyle,viewport)
 assert.equal(f.calls.filter(c=>c[0]==='send'&&c[1].type==='keyboard.layout').length,layouts)
 f.page._officialValidLayoutAt=Date.now()-751
 poll(550);assert(f.page.keyboardOpen);assert(Date.now()-f.page._officialValidLayoutAt<750)
 poll(0);assert(f.page.keyboardOpen)
 f.page._officialValidLayoutAt=Date.now()-751;poll(0)
 assert(!f.page.keyboardOpen)
 result=f.calls.filter(c=>c[0]==='send'&&c[1].type==='keyboard.result').at(-1)[1]
 assert.equal(result.requestId,'recover');assert(!result.confirmed);assert(result.error.includes('高度无效'))
 f.page.dispose()
})


test('portrait official proxy exposes native page above IME and clears floating preedit below',async()=>{
 const f=fixture();await f.page.boot();portraitOfficial(f)
 f.page.nativeAreaStyle={left:'0px',top:'0px',width:'568px',height:'1210px'}
 f.page.updatePageStyle()
 const normalColor=f.page.pageStyle.backgroundColor
 assert.notEqual(normalColor,'transparent')
 f.page.openKeyboard({requestId:'isolate',text:'original',field:{x:24,y:520,width:520,height:44,fontSize:26}})
 assert(f.page.officialInline);assert.equal(f.page.pageStyle.backgroundColor,normalColor)
 const root=f.page.pageStyle,field=f.page.officialInputStyle,handlers=f.page.officialInputHandlers
 handlers.input('edited')
 assert.equal(f.page.pageStyle,root);assert.equal(f.page.officialInputStyle,field)
 f.page._keyboardWatchAt=1;f.page.checkOfficialKeyboard()
 assert.equal(f.page.pageStyle.backgroundColor,normalColor);assert.equal(f.page.pageStyle.height,'1210px')
 const colors={background:'#ffffff',text:'#20242d',muted:'#626b78',surface:'#eef1f5',primaryText:'#ffffff',caret:'#2457c5'}
 const metrics={title:35,body:26,caption:25,gap:18,inset:40,top:25,bottom:31,retryWidth:225,leaveX:294,leaveWidth:150,controlHeight:56,radius:15}
 f.page.applyAppearance({mode:'light',colors,metrics})
 assert.equal(f.page.pageStyle.backgroundColor,'#ffffff');assert.equal(f.page.officialInputStyle,field)
 f.page.cancelOfficialKeyboard()
 assert(!f.page.officialInline);assert.equal(f.page.pageStyle.backgroundColor,'#ffffff')
 assert.equal(f.page.pageStyle.height,'1210px')
 f.page.openKeyboard({requestId:'legacy-hole',text:'landscape',inline:false})
 assert(!f.page.officialInline);assert.equal(f.page.pageStyle.backgroundColor,'#ffffff')
 assert.equal(f.calls.filter(c=>c[0]==='startTextEdit').length,2)
 f.page.dispose()
})


test('official composer actions commit the latest buffer once with matching identity',async()=>{
 const f=fixture();await f.page.boot();portraitOfficial(f)
 f.page.openKeyboard({requestId:'action-owner',text:'old'})
 let commits=0
 f.page.$refs.officialInput.commitComplete=()=>{++commits;f.page.officialInputHandlers.confirm('最新输入')}
 f.page.commitOfficialAction({requestId:'stale',actionId:'stale',action:'send'});assert.equal(commits,0)
 const event={requestId:'action-owner',actionId:'one',action:'send'}
 f.page.commitOfficialAction(event);f.page.commitOfficialAction(event);assert.equal(commits,1)
 f.timers.get(f.page._keyboardTimer)()
 const result=f.calls.filter(c=>c[0]==='send'&&c[1].type==='keyboard.result').at(-1)[1]
 assert(result.confirmed);assert.equal(result.actionId,'one');assert.equal(result.text,'最新输入')
 f.page.dispose()
})
test('failed official commit leaves editing intact and cannot execute a later stale action',async()=>{
 const f=fixture();await f.page.boot();portraitOfficial(f)
 f.page.openKeyboard({requestId:'pending',text:'保留'})
 f.page.$refs.officialInput.commitComplete=()=>{}
 f.page.commitOfficialAction({requestId:'pending',actionId:'timeout',action:'attachments'})
 f.timers.get(f.page._officialCommitTimer)()
 assert(f.page.keyboardOpen);assert.equal(f.page.officialText,'保留')
 assert(f.calls.some(c=>c[0]==='send'&&c[1].type==='keyboard.action.error'&&c[1].actionId==='timeout'))
 const old=f.page.officialInputHandlers
 f.page.commitOfficialAction({requestId:'pending',actionId:'cancelled',action:'send'})
 f.page.cancelOfficialKeyboard();old.confirm('stale')
 assert(!f.page.keyboardOpen)
 assert(!f.calls.some(c=>c[0]==='send'&&c[1].type==='keyboard.result'&&c[1].actionId))
 f.page.dispose()
})


test('official actions preserve unconfirmed keys and accept a later real text acknowledgement',async()=>{
 const f=fixture();await f.page.boot();portraitOfficial(f)
 f.page.openKeyboard({requestId:'composition',text:'正文'})
 let commits=0
 f.page.$refs.officialInput.commitComplete=()=>{++commits;f.page.officialInputHandlers.confirm('正文已选词')}
 const keyEventTime=Date.now()
 f.page.commitOfficialAction({requestId:'composition',actionId:'pending-key',action:'attachments',uncommitted:true,keyEventTime})
 assert.equal(commits,0);assert(f.page.keyboardOpen);assert.equal(f.page.officialText,'正文')
 f.page.inlineOfficialInput('正文已选词')
 f.page.commitOfficialAction({requestId:'composition',actionId:'acknowledged',action:'attachments',uncommitted:true,keyEventTime})
 assert.equal(commits,1);f.timers.get(f.page._keyboardTimer)()
 const result=f.calls.filter(c=>c[0]==='send'&&c[1].type==='keyboard.result').at(-1)[1]
 assert.equal(result.actionId,'acknowledged');assert.equal(result.text,'正文已选词')
 f.page.dispose()
})


test('official composer confirmation rechecks the live system panel before committing',async()=>{
 const f=fixture();await f.page.boot();portraitOfficial(f)
 f.page.openKeyboard({requestId:'foreground',text:'保留'})
 let commits=0;f.page.$refs.officialInput.commitComplete=()=>{++commits}
 f.native.systemOverlay=()=>({supported:true,visible:true})
 f.page.commitOfficialAction({requestId:'foreground',actionId:'covered',action:'send'})
 assert.equal(commits,0);assert(f.page.keyboardOpen)
 assert(f.calls.some(c=>c[0]==='send'&&c[1].type==='keyboard.action.error'&&c[1].actionId==='covered'))
 f.page.dispose()
})

test('inline official dismissal retains latest edits while legacy cancellation stays explicit',async()=>{
 const f=fixture();await f.page.boot();portraitOfficial(f)
 f.page.openKeyboard({requestId:'retain',text:'original'})
 f.page.inlineOfficialInput('刚输入的内容');f.page.cancelOfficialKeyboard()
 const result=f.calls.filter(c=>c[0]==='send'&&c[1].type==='keyboard.result').pop()[1]
 assert.equal(result.text,'刚输入的内容');assert(result.confirmed&&result.retained);assert(!result.actionId)
 f.page.openKeyboard({requestId:'retain-error',text:'original'})
 f.page.inlineOfficialInput('错误前的内容');f.page.cancelOfficialKeyboard('panel failed')
 const error=f.calls.filter(c=>c[0]==='send'&&c[1].type==='keyboard.result').pop()[1]
 assert.equal(error.text,'错误前的内容');assert(error.retained&&!error.confirmed)
 f.page.dispose()
})

test('native official selection synchronizes cursor, clipboard edits and committed replacement',async()=>{
 const f=fixture();await f.page.boot();portraitOfficial(f)
 f.page.$refs.officialInput.commitText=text=>f.calls.push(['replace',text])
 f.page.openKeyboard({requestId:'native-edit',text:'a中😀bc',field:{x:10,y:400,width:500,height:60,nativeCaret:true}})
 assert.equal(f.page.officialInputStyle.width,'1px')
 const edit=(sequence,base,text,start,end,cursor=end)=>f.page.editOfficial({requestId:'native-edit',sequence,base,text,selectionStart:start,selectionEnd:end,cursorIndex:cursor})
 edit(1,'a中😀bc','a中😀bc',1,4)
 assert.equal(f.calls.filter(c=>c[0]==='cursor').at(-1)[1],4)
 f.page.inlineOfficialInput({text:'a中😀Xbc',cursorPosition:5})
 assert.equal(f.page.officialText,'aXbc')
 assert.equal(f.calls.filter(c=>c[0]==='cursor').at(-1)[1],2)
 edit(2,'aXbc','aXbc',0,2)
 f.page.inlineOfficialInput({text:'abc',cursorPosition:1})
 assert.equal(f.page.officialText,'bc')
 edit(3,'bc','粘贴bc',2,2,2)
 assert.equal(f.page.officialText,'粘贴bc');assert(f.page.keyboardOpen)
 edit(4,'stale','lost',0,0,0)
 assert.equal(f.page.officialText,'粘贴bc')
 const ack=f.calls.filter(c=>c[0]==='send'&&c[1].type==='keyboard.edit.result').at(-1)[1]
 assert(!ack.ok)
 edit(3,'粘贴bc','duplicate',0,0,0);assert.equal(f.page.officialText,'粘贴bc')
 f.page.dispose()
})

test('official string-only input keeps a middle cursor among repeated letters',async()=>{
 const f=fixture();await f.page.boot();portraitOfficial(f)
 f.page.$refs.officialInput.commitText=()=>{}
 f.page.openKeyboard({requestId:'repeat',text:'aaaa',cursorIndex:2})
 f.page.inlineOfficialInput('aaaaa')
 const change=f.calls.filter(c=>c[0]==='send'&&c[1].type==='keyboard.change').at(-1)[1]
 assert.equal(change.cursorIndex,3)
 f.page.inlineOfficialInput('aaaa')
 assert.equal(f.calls.filter(c=>c[0]==='send'&&c[1].type==='keyboard.change').at(-1)[1].cursorIndex,2)
 f.page.dispose()
})

test('official gap uses theme backing and changes hole only with native geometry acknowledgement',async()=>{
 const f=fixture();await f.page.boot();portraitOfficial(f)
 f.page.openKeyboard({requestId:'boundary',text:'draft',field:{x:20,y:480,width:520,height:50,nativeCaret:true,holeHeight:0}})
 f.page._keyboardWatchAt=1;f.page.checkOfficialKeyboard()
 assert.equal(f.page.officialHoleStyle.height,'0px')
 f.page.inlineOfficialField({requestId:'boundary',x:20,y:480,width:520,height:50,nativeCaret:true,holeHeight:616})
 assert.equal(f.page.officialHoleStyle.height,'616px')
 assert.equal(f.page.officialBackingStyle.top,'616px');assert.equal(f.page.officialBackingStyle.height,'594px')
 assert.equal(f.page.officialBackingStyle.backgroundColor,f.page.appearanceColors.background)
 const key=f.page.officialControlKey,handlers=f.page.officialInputHandlers
 f.native.officialKeyboard=()=>({supported:true,visible:true,height:590,foreground:true})
 f.page._keyboardWatchAt=1;f.page.checkOfficialKeyboard()
 assert.equal(f.page.officialHoleStyle.height,'616px')
 f.page.inlineOfficialField({requestId:'old',x:20,y:480,width:520,height:50,nativeCaret:true,holeHeight:900})
 assert.equal(f.page.officialHoleStyle.height,'616px')
 assert.equal(f.page.officialControlKey,key);assert.equal(f.page.officialInputHandlers,handlers)
 const template=fs.readFileSync('src/launcher/LauncherPage.vue','utf8').split('<script>')[0]
 assert(template.indexOf('class="official-backing"')>template.indexOf('<hole'))
 f.page.cancelOfficialKeyboard();assert.equal(f.page.officialBackingStyle.height,'0px')
 f.page.dispose()
})

test('system panel uses native underlay without a snapshot image on the Page', async () => {
 const f=fixture();await f.page.boot();f.native.panelVisible=true;f.page.syncWindowFocus(true)
 assert(f.page.visible);assert.equal(f.page._windowFocused,false)
 assert(!fs.readFileSync('src/launcher/LauncherPage.vue','utf8').includes('class="overlay-frame"'))
 f.native.panelVisible=false;f.page.syncWindowFocus(true);assert(f.page.visible)
 f.page.dispose()
})

test('panel-induced Page hide retains native lifecycle and deduplicates Page show', async () => {
 const f=fixture();await f.page.boot()
 f.context.am.getTopApp=()=>f.context.appConfig.appid
 f.native.panelVisible=true;f.options.onHide.call(f.page)
 assert(f.page.visible);assert(f.page._overlayHidden)
 const hidden=()=>f.calls.filter(c=>c[0]==='send'&&c[1].type==='visibility'&&!c[1].visible)
 assert.equal(hidden().length,0)
 assert.equal(f.calls.filter(c=>c[0]==='send'&&c[1].type==='window.focus').at(-1)[1].focused,false)
 assert(f.page._poll)
 f.native.panelVisible=false;f.page.syncWindowFocus(true);f.page.poll()
 assert(f.page.visible);assert(!f.page._pageHidden)
 const visible=()=>f.calls.filter(c=>c[0]==='send'&&c[1].type==='visibility').length
 const before=visible();f.options.onShow.call(f.page);f.options.onShow.call(f.page)
 assert.equal(visible(),before);assert.equal(hidden().length,0)
 assert.equal(f.calls.filter(c=>c[0]==='start').length,1)
 assert.equal(f.calls.filter(c=>c[0]==='stop').length,0)
 f.page.dispose()
})

test('hide-before-panel ordering yields input immediately without a teardown deadline',async()=>{
 const f=fixture();await f.page.boot();let now=1000
 f.context.Date={now:()=>now};f.context.am.getTopApp=()=>f.context.appConfig.appid
 f.options.onHide.call(f.page)
 assert(f.page.visible);assert(f.page._pageHidden)
 let focus=f.calls.filter(c=>c[0]==='send'&&c[1].type==='input.focus').at(-1)[1]
 assert.equal(focus.active,false)
 now=1100;f.native.panelVisible=true;f.page.poll();assert(f.page._overlayHidden)
 now=5000;f.page.poll();assert(f.page.visible)
 f.native.panelVisible=false;now=5100;f.page.poll();assert(!f.page._pageHidden)
 f.options.onHide.call(f.page);now=5400;f.page.poll()
 assert(f.page.visible);assert(f.page.running)
 f.context.am.getTopApp=()=>'other-app';f.options.onHide.call(f.page)
 assert(!f.page.visible);assert.equal(f.page._poll,null)
 f.page.dispose()
})

test('panel visibility wins over transient top ownership; real background still releases hardware',async()=>{
 for(const owner of ['other-app',null]) {
  const f=fixture();await f.page.boot();let now=1000
  f.context.Date={now:()=>now};let top=f.context.appConfig.appid
  f.context.am.getTopApp=()=>top;f.native.panelVisible=true;f.options.onHide.call(f.page)
  assert(f.page.visible)
  top=owner;now=1300;f.page.poll();assert(f.page.visible)
  f.native.panelVisible=false;now=1500;f.page.poll()
  if(owner) {assert(!f.page.visible);assert.equal(f.page._poll,null)}
  else {assert(f.page.visible);assert.equal(f.page._windowFocused,false)}
  f.page.dispose()
 }
 const f=fixture();await f.page.boot();f.context.am.getTopApp=()=>f.context.appConfig.appid
 f.native.systemOverlay=()=>({supported:false});f.options.onHide.call(f.page)
 assert(f.page.visible);assert.equal(f.page._windowFocused,false);f.page.dispose()
})

test('transient panel does not complete official input or turn a hidden system IME into failure',async()=>{
 const f=fixture();await f.page.boot()
 f.native.officialKeyboard=()=>({supported:true,visible:false})
 f.page.openKeyboard({requestId:'retained-system-edit',text:'draft'})
 f.page._keyboardWatchAt=1;f.native.panelVisible=true;f.page.syncWindowFocus(true)
 f.page.checkOfficialKeyboard();assert(f.page.keyboardOpen)
 assert.equal(f.calls.filter(c=>c[0]==='send'&&c[1].type==='keyboard.result').length,0)
 f.page.dispose()
})


test('Page unload detaches while explicit application finish stops native exactly once',async()=>{
 const f=fixture();await f.page.boot();f.context.am.getTopApp=()=>f.context.appConfig.appid
 f.native.panelVisible=true;f.options.onHide.call(f.page);assert(f.page._overlayHidden)
 f.options.onUnload.call(f.page);f.options.beforeDestroy.call(f.page)
 assert.equal(f.calls.filter(c=>c[0]==='stop').length,0)
 assert(f.calls.some(c=>c[0]==='send'&&c[1].type==='page.detach'))
 f.page.dispose()
 assert.equal(f.calls.filter(c=>c[0]==='stop').length,1)
 assert.equal(f.calls.filter(c=>c[0]==='send'&&c[1].type==='exit').length,1)
 assert.equal(f.timers.size,0)
})


test('real background after a panel does not strand the official keyboard monitor',async()=>{
 const f=fixture();await f.page.boot();portraitOfficial(f)
 let top=f.context.appConfig.appid;f.context.am.getTopApp=()=>top
 f.page.openKeyboard({requestId:'background-system-edit',text:'retained draft'})
 f.native.panelVisible=true;f.options.onHide.call(f.page);assert(f.page._overlayHidden)
 top='other-app';f.native.panelVisible=false;f.page.syncWindowFocus(true);assert(!f.page.visible)
 f.native.officialKeyboard=()=>({supported:true,visible:false})
 f.page._officialDeadline=0;f.page._keyboardWatchAt=1;f.page.checkOfficialKeyboard()
 assert(!f.page.keyboardOpen);assert.equal(f.page._keyboardMonitor,null)
 f.page.dispose()
})


test('retry after a failure under the panel starts with fresh lifecycle classification',async()=>{
 const f=fixture();await f.page.boot();f.context.am.getTopApp=()=>f.context.appConfig.appid
 f.native.panelVisible=true;f.options.onHide.call(f.page);assert(f.page._overlayHidden)
 f.page.fail('fixture native failure');f.native.panelVisible=false;await f.page.boot()
 assert(!f.page._pageHidden);assert(!f.page._overlayHidden)
 assert(f.page.visible);assert(f.page.running)
 assert.equal(f.calls.filter(c=>c[0]==='start').length,2)
 f.page.dispose()
})


test('observer handoff acknowledges exact request after filtering only the host head key',async()=>{
 const f=fixture();await f.page.boot();let observing=false
 f.native.panelVisible=true;f.page.syncWindowFocus(true)
 f.native.observePen=active=>{observing=active;return true}
 f.native.poll=()=>[{type:'pen.observe',generation:f.page._generation,requestId:'observer-1',enabled:true}]
 f.page.poll();assert(observing)
 const ack=f.calls.filter(c=>c[0]==='send'&&c[1].type==='pen.observe.result').at(-1)[1]
 assert.equal(ack.requestId,'observer-1');assert(ack.ok&&ack.enabled)
 f.native.observePen=()=>false
 f.native.poll=()=>[{type:'pen.observe',generation:f.page._generation,requestId:'observer-2',enabled:true}]
 f.page.poll();assert.equal(f.calls.filter(c=>c[0]==='send'&&c[1].type==='pen.observe.result').at(-1)[1].ok,false)
 f.page.dispose()
})

test('a remounted Page reuses the ready runtime and viewport without a startup cover',async()=>{
 const f=fixture();await f.page.boot();f.options.onUnload.call(f.page)
 f.native.status=()=>({alive:true,ready:true,screen:{width:960,height:266}})
 f.native.start=async()=>({ok:true,reused:true,pid:123})
 const page=Object.assign(f.options.data(),f.options.methods,{$page:f.page.$page})
 await page.boot();assert(page.nativeReady);assert.equal(page.nativeAreaStyle.width,'960px')
 const init=f.calls.filter(c=>c[0]==='send'&&c[1].type==='init').at(-1)[1]
 assert(init.reattach);assert.equal(f.calls.filter(c=>c[0]==='stop').length,0)
 page.dispose()
})


test('explicit Finish closes only this application context after stopping its runtime',async()=>{
 const f=fixture();await f.page.boot();let closed
 f.context.am.closeApp=(id,force)=>{closed=[id,force]}
 f.page.finish();assert.equal(closed[0],f.context.appConfig.appid);assert.equal(closed[1],false)
 assert.equal(f.calls.filter(c=>c[0]==='stop').length,1)
})


test('a trailing exit cannot replace the concrete startup failure or re-fail an intentional exit',async()=>{
 const f=fixture();await f.page.boot()
 f.native.poll=()=>[{type:'fatal',error:'DRM order unavailable'},{type:'exit',status:0}]
 f.page.poll();assert.equal(f.page.errorText,'DRM order unavailable');f.page.dispose()
 const g=fixture();await g.page.boot();g.native.poll=()=>[{type:'finish'},{type:'exit',status:0}]
 g.page.poll();assert.equal(g.page.errorText,'')
})

test('a start completing after Page detach retains the runtime and yields display',async()=>{
 const f=fixture();let complete
 f.native.start=()=>new Promise(resolve=>{complete=resolve})
 const pending=f.page.boot();f.options.onUnload.call(f.page);complete({ok:true})
 await pending
 assert.equal(f.calls.filter(c=>c[0]==='stop').length,0)
 assert(f.calls.some(c=>c[0]==='send'&&c[1].type==='page.detach'))
 assert(!f.page.running);assert.equal(f.timers.size,0)
 f.page.dispose()
})

test('an old detached start cannot detach a newer mounted Page',async()=>{
 const f=fixture();let complete
 f.native.start=()=>new Promise(resolve=>{complete=resolve})
 const pending=f.page.boot();f.options.onUnload.call(f.page)
 f.native.start=async()=>({ok:true,reused:true})
 const page=Object.assign(f.options.data(),f.options.methods,{$page:f.page.$page})
 await page.boot();const offset=f.calls.length;complete({ok:true});await pending
 assert(!f.calls.slice(offset).some(c=>c[0]==='stop'||c[0]==='send'&&c[1].type==='page.detach'))
 page.dispose()
})
