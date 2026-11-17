const test=require('node:test'),assert=require('node:assert/strict'),fs=require('node:fs'),vm=require('node:vm')
function fixture(){
 const calls=[],timers=new Map();let timer=0,resolveInstall
 const bridge={request:(op,args)=>{calls.push([op,args]);return {job:'job-1'}},cancel:()=>calls.push(['cancel']),install:ticket=>{calls.push(['install',ticket]);return new Promise(resolve=>{resolveInstall=resolve})},verifyInstalled:async()=>({verified:false}),reopen:()=>calls.push(['reopen'])}
 const context=vm.createContext({module:{}});vm.runInContext(fs.readFileSync('src/launcher/rescue.js','utf8').replace('export class RescueController','module.exports=class RescueController'),context)
 const controller=new context.module.exports({bridge,appid:'app',pm:{getPackageInfo:()=>({appid:'app',version:'0.3.2'})},stop:async()=>calls.push(['stop']),status:msg=>calls.push(['status',msg]),error:msg=>calls.push(['error',msg]),setTimeout:fn=>{timers.set(++timer,fn);return timer},clearTimeout:id=>timers.delete(id)})
 return {controller,bridge,calls,timers,resolve:value=>resolveInstall(value)}
}
test('explicit force applies only to a selected incompatible release after stopping native',async()=>{
 const f=fixture();await f.controller.prepare({id:'sha',compatible:false})
 assert.deepEqual(JSON.parse(JSON.stringify(f.calls)),[['stop'],['prepare',{id:'sha',force:true}]])
})
test('stale job or unconfirmed old prepared ticket never installs',async()=>{
 const f=fixture();await f.controller.tick({status:'ready_to_install',job:'old',ticket:{operation:'op'}});assert(!f.calls.some(c=>c[0]==='install'))
 await f.controller.prepare({id:'sha',compatible:true});f.controller.cancel();await f.controller.tick({status:'ready_to_install',job:'job-1',ticket:{operation:'op'}})
 assert(!f.calls.some(c=>c[0]==='install'))
})
test('one prepared operation submits once; matching version alone is not success',async()=>{
 const f=fixture();await f.controller.prepare({id:'sha',compatible:true});const state={status:'ready_to_install',job:'job-1',ticket:{operation:'op'}}
 const pending=f.controller.tick(state);await f.controller.tick(state);assert.equal(f.calls.filter(c=>c[0]==='install').length,1)
 f.resolve({res:0});await pending;assert(!f.calls.some(c=>c[0]==='reopen'));assert(f.calls.some(c=>c[0]==='status'&&c[1].includes('尚未确认')))
})
test('verified installed content reopens; timeout never automatically resubmits',async()=>{
 const f=fixture();await f.controller.prepare({id:'sha',compatible:true});const state={status:'ready_to_install',job:'job-1',ticket:{operation:'op'}}
 const pending=f.controller.tick(state);f.bridge.verifyInstalled=async()=>({verified:true});f.resolve({res:0});await pending;assert(f.calls.some(c=>c[0]==='reopen'))
 await f.controller.tick(state);assert.equal(f.calls.filter(c=>c[0]==='install').length,1)
})
test('leaving cancels unsubmitted work and rejects late UI completion',async()=>{
 const f=fixture();f.controller.dispose();assert(f.calls.some(c=>c[0]==='cancel'))
 await f.controller.prepare({id:'sha',compatible:true});assert(!f.calls.some(c=>c[0]==='prepare'))
})

test('background installation is submitted once and reopen waits for foreground',async()=>{
 const f=fixture();let foreground=false;f.controller.a.isForeground=()=>foreground
 await f.controller.prepare({id:'sha',compatible:true});f.bridge.verifyInstalled=async()=>({verified:true})
 const pending=f.controller.tick({status:'ready_to_install',job:'job-1',ticket:{operation:'op'}})
 f.resolve({res:0});await pending;assert(!f.calls.some(c=>c[0]==='reopen'))
 foreground=true;await f.controller.tick({status:'installed'});assert(f.calls.some(c=>c[0]==='reopen'))
})
test('cancel while stopping native never starts a late preparation',async()=>{
 const f=fixture();let release;f.controller.a.stop=()=>new Promise(resolve=>{release=resolve})
 const preparing=f.controller.prepare({id:'sha',compatible:true});f.controller.cancel();release();await preparing
 assert(!f.calls.some(c=>c[0]==='prepare'))
})

test('concurrent result checks reuse one verification and reopen once',async()=>{
 const f=fixture();let complete,count=0;f.bridge.verifyInstalled=()=>{count++;return new Promise(resolve=>{complete=resolve})}
 const check=f.controller.check();await f.controller.check();assert.equal(count,1)
 complete({verified:true});await check;await f.controller.check();assert.equal(count,1);assert.equal(f.calls.filter(c=>c[0]==='reopen').length,1)
})
