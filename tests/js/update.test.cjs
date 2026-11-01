const test = require('node:test')
const assert = require('node:assert/strict')
const vm = require('node:vm')
const fs = require('node:fs')
function fixture() {
  const c = vm.createContext({ Set });vm.runInContext(fs.readFileSync('src/launcher/update.js','utf8').replace('export class UpdateInstall','globalThis.UpdateInstall = class UpdateInstall'),c)
  const calls=[],timers=new Map();let n=0,callback,version='0.2.20'
  const a={appid:'8001792332371649',dataDir:'/data',pm:{installPackage:(path,cb)=>{calls.push('install');callback=cb},getPackageInfo:()=>({appid:a.appid,version})},launcher:{stop:async()=>calls.push('stop')},setTimeout:fn=>{timers.set(++n,fn);return n},clearTimeout:id=>timers.delete(id),accept:()=>calls.push('accept'),status:()=>{},installed:async()=>calls.push('installed'),failed:()=>calls.push('failed')}
  const ticket={appid:a.appid,path:'/data/.updates/package.amr',version:'0.2.21',operation:'00000000-0000-4000-8000-000000000001',sha256:'a'.repeat(64)}
  return {installer:new c.UpdateInstall(a),a,ticket,calls,timers,complete:()=>{version='0.2.21';callback({res:0})},bad:()=>callback({res:0})}
}
test('installer validates handoff and deduplicates, confirms actual package version',async()=>{
 const f=fixture();await assert.rejects(f.installer.start({...f.ticket,path:'/elsewhere.amr'}));assert.equal(f.calls.length,0)
 const run=f.installer.start(f.ticket);await new Promise(setImmediate);await f.installer.start(f.ticket);f.complete();await run
 assert.deepEqual(f.calls,['accept','stop','install','installed']);assert.equal(f.timers.size,0)
})
test('callback success without installed version is not success',async()=>{const f=fixture();const run=f.installer.start(f.ticket);await new Promise(setImmediate);f.bad();await run;assert.equal(f.calls.at(-1),'failed')})
test('timeout never repeats install and disposal clears callback/timer',async()=>{
 const f=fixture();const run=f.installer.start(f.ticket);await new Promise(setImmediate);[...f.timers.values()][0]();await run;assert.equal(f.calls.at(-1),'failed');assert.equal(f.calls.filter(x=>x==='install').length,1)
 const g=fixture();const wait=g.installer.start(g.ticket);await new Promise(setImmediate);g.installer.dispose();await wait;assert.equal(g.timers.size,0);g.complete();assert(!g.calls.includes('installed'))
})
test('native installer is preferred; a rejected submission never silently falls back',async()=>{
 const f=fixture();f.a.launcher.install=async ticket=>{assert.equal(ticket.operation,f.ticket.operation);f.calls.push('native');throw new Error('failed')}
 await f.installer.start(f.ticket);assert(f.calls.includes('failed'));assert(!f.calls.includes('install'));assert.equal(f.calls.filter(x=>x==='native').length,1)
})
