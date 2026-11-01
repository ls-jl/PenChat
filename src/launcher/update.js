// Installation-only system bridge. Discovery/download/UI remain native.
export class UpdateInstall {
  constructor(adapter) { this.adapter = adapter; this.operations = new Set(); this.disposed = false }
  dispose() { this.disposed = true; if (this.cancelWait) this.cancelWait() }
  async start(ticket) {
    const a = this.adapter
    if (this.disposed || !ticket || ticket.appid !== a.appid ||
        typeof ticket.operation !== 'string' || !/^[0-9a-f-]{36}$/i.test(ticket.operation) ||
        !/^\d+\.\d+\.\d+$/.test(ticket.version || '') ||
        ticket.path !== a.dataDir.replace(/\/$/, '') + '/.updates/package.amr' ||
        !/^[0-9a-f]{64}$/i.test(ticket.sha256 || '')) throw new Error('无效的安装交接')
    if (this.operations.has(ticket.operation)) return
    this.operations.add(ticket.operation)
    if (typeof a.launcher.install !== 'function' && (!a.pm || typeof a.pm.installPackage !== 'function')) throw new Error('此固件不支持应用安装接口')
    a.accept(ticket)
    await a.launcher.stop()
    if (this.disposed) return
    a.status('正在安装更新，请稍候…')
    const result = await new Promise(resolve => {
      let done = false
      const timer = a.setTimeout(() => { if (!done) { done = true; resolve({ unknown: true }) } }, 120000)
      const complete = value => { if (!done) { done = true; a.clearTimeout(timer); this.cancelWait = null; resolve(value || {}) } }
      this.cancelWait = () => complete({ disposed: true })
      try {
        if (typeof a.launcher.install === 'function') a.launcher.install(ticket).then(complete, () => complete({ failed: true }))
        else a.pm.installPackage(ticket.path, complete)
      } catch (err) { complete({ failed: true }) }
    })
    if (this.disposed) return
    let info
    try { info = a.pm.getPackageInfo(a.appid); if (typeof info === 'string') info = JSON.parse(info) } catch (err) {}
    if (info && String(info.appid) === a.appid && info.version === ticket.version) {
      a.status('更新已安装，正在重新打开…')
      await a.installed(ticket)
    } else a.failed(ticket, result.unknown ? '安装结果尚未确认，请重新打开后检查' : '安装未成功，旧数据已保留')
  }
}
