// View/controller only. Network, archive and backup work belongs to the SO.
export class RescueController {
  constructor(adapter) { this.a=adapter;this.job='';this.submitted=new Set();this.disposed=false;this.pending=false;this.cancelGeneration=0;this.timer=null;this.releaseWait=null }
  async prepare(release) {
    if(this.pending||this.disposed)return
    this.pending=true
    const generation=this.cancelGeneration
    try {
      await this.a.stop();if(this.disposed||generation!==this.cancelGeneration)return
      const result=this.a.bridge.request('prepare',{id:release.id,force:release.compatible!==true});this.job=result.job
    }catch(err){this.a.error(err&&err.message||'无法准备版本回退安装')}
    finally{this.pending=false}
  }
  async check() {
    if(this.disposed||this.checking||this.reopened)return false
    this.checking=true
    try {
      let info
      try{info=this.a.pm.getPackageInfo(this.a.appid);if(typeof info==='string')info=JSON.parse(info)}catch(_){return false}
      try {
        const result=await this.a.bridge.verifyInstalled(info||{});if(this.disposed)return false
        if(result&&result.verified){
          if(this.a.isForeground&&!this.a.isForeground()){this.reopenPending=true;this.a.status('安装已核实，请返回 PenChat 后重新打开');return true}
          this.reopenPending=false;this.reopened=true
          this.a.status('安装已核实，正在重新打开…');this.a.bridge.reopen();return true
        }
      }catch(_){this.reopened=false}
      this.a.status('安装结果尚未确认，请稍后核对；不会自动重复安装。');return false
    } finally {this.checking=false}
  }
  async tick(state) {
    if(!this.disposed&&this.reopenPending&&(!this.a.isForeground||this.a.isForeground())){await this.check();return}
    if(this.disposed||state.status!=='ready_to_install'||state.job!==this.job||!state.ticket)return
    const ticket=state.ticket;if(this.submitted.has(ticket.operation))return
    this.submitted.add(ticket.operation)
    try {
      this.a.status('正在安装，请稍候…')
      const result=await new Promise(resolve=>{
        this.releaseWait=resolve
        this.timer=this.a.setTimeout(()=>resolve({unknown:true}),120000)
        this.a.bridge.install(ticket).then(value=>{this.a.clearTimeout(this.timer);resolve(value)},error=>{this.a.clearTimeout(this.timer);resolve({failed:true,message:error&&error.message})})
      })
      if(this.disposed)return
      if(result&&typeof result.res==='number'&&result.res!==0){this.a.error('安装失败（'+result.res+'），原数据和备份已保留');return}
      const verified=await this.check()
      if(!verified&&result&&result.failed)this.a.error(result.message||'安装结果待核对；不会自动重复安装，备份已保留')
    }catch(err){if(!this.disposed)this.a.error(err&&err.message||'安装未完成，请核对结果')}
  }
  cancel(){this.cancelGeneration++;this.job='';this.a.bridge.cancel()}
  dispose(){this.disposed=true;this.cancelGeneration++;this.job='';if(this.timer!==null)this.a.clearTimeout(this.timer);if(this.releaseWait)this.releaseWait({unknown:true});this.a.bridge.cancel()}
}
