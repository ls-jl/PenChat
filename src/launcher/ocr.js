// Adapter for the system pen-tip OCR service. No key parsing or persistence here.
function scanText(value, depth = 0) {
  if (depth > 6 || value == null) return ''
  if (typeof value === 'string') {
    const text = value.trim()
    if (text.length > 8192 || /^(data:image\/|\/.*\.(png|jpe?g|bmp)$)/i.test(text)) return ''
    if (/^[\[{]/.test(text)) {
      try { return scanText(JSON.parse(text), depth + 1) } catch (err) { return '' }
    }
    return text
  }
  if (Array.isArray(value)) return value.map(v => scanText(v, depth + 1)).filter(Boolean).join('\n').slice(0, 8192)
  if (typeof value === 'object') {
    for (const key of ['word', 'query', 'mainQuery', 'text', 'ocrResult', 'result', 'content', 'data', 'words', 'lines']) {
      if (value[key] !== undefined) {
        const text = scanText(value[key], depth + 1)
        if (text) return text
      }
    }
  }
  return ''
}
export class OcrSession {
  constructor(manager, appid, emit) {
    this.manager = manager; this.appid = appid; this.emit = emit; this.active = null
  }
  start(requestId) {
    this.stop()
    const current = { requestId, subscriptions: [], candidate: '', started: false }
    this.active = current
    const finish = result => {
      if (this.active !== current || current.finishing) return
      current.finishing = true
      clearTimeout(current.timeout); clearTimeout(current.fallback)
      // Unregister only after the native signal callback has unwound.
      current.finishTimer = setTimeout(() => {
        if (this.active !== current) return
        this.stop()
        this.emit('ocr.result', Object.assign({ requestId }, result))
      }, 0)
    }
    const bind = (name, callback) => {
      const signal = this.manager[name]
      if (!signal) return false
      const handler = (...args) => { if (this.active === current && !current.finishing) callback(...args) }
      if (typeof signal.on === 'function' && typeof signal.off === 'function') {
        signal.on(handler); current.subscriptions.push(() => signal.off(handler)); return true
      }
      if (typeof signal.connect === 'function' && typeof signal.disconnect === 'function') {
        const token = signal.connect(handler)
        current.subscriptions.push(() => signal.disconnect(token === undefined ? handler : token)); return true
      }
      return false
    }
    const remember = value => {
      if (!current.started) return
      const text = scanText(value)
      if (text) current.candidate = text
    }
    const final = value => {
      remember(value)
      clearTimeout(current.fallback)
      current.fallback = setTimeout(() => {
        if (current.started && current.candidate) finish({ confirmed: true, text: current.candidate })
      }, 900)
    }
    try {
      for (const name of ['addUseOcrAppId', 'deleteUseOcrAppId', 'setIsOpenScan', 'startScan'])
        if (typeof this.manager[name] !== 'function') throw new Error('System OCR unavailable')
      const startSignal = bind('ocrStartChanged', () => {
        current.started = true; current.candidate = ''; clearTimeout(current.fallback)
        this.emit('ocr.status', { requestId, started: true })
      })
      // Older firmware without a start signal can still deliver final word results.
      current.started = !startSignal
      let outputs = 0
      for (const name of ['scanWordResultChanged', 'apolloScanWordResultChanged']) {
        if (bind(name, (kind, result) => {
          remember(result === undefined ? kind : result)
          if (current.started && current.candidate) finish({ confirmed: true, text: current.candidate })
        })) ++outputs
      }
      for (const name of ['ocrCompleteChanged', 'ocrStopChanged']) if (bind(name, final)) ++outputs
      bind('ocrResultChanged', remember)
      bind('scanErrorChanged', () => finish({ confirmed: false, error: '系统 OCR 识别失败，请重新扫描。' }))
      if (!outputs) throw new Error('System OCR result signals unavailable')
      current.previousOpen = typeof this.manager.isOpenScan === 'boolean' ? this.manager.isOpenScan : false
      current.previousImage = typeof this.manager.showImgScanMode === 'boolean' ? this.manager.showImgScanMode : false
      if (this.manager.setCurrentAppId) this.manager.setCurrentAppId(this.appid, true)
      if (this.manager.setShowImgScanMode) this.manager.setShowImgScanMode(false)
      current.opened = true
      this.manager.setIsOpenScan(true)
      current.registered = true
      if (this.manager.addUseOcrAppId(JSON.stringify({ appid: this.appid, scanMethod: 0, stopSoundPlayerOnHeadButtonPressed: false })) === false) throw new Error('System OCR registration failed')
      if (typeof this.manager.startAlmondMultiScan === 'function') this.manager.startAlmondMultiScan(0)
      this.emit('ocr.status', { requestId, started: false })
      current.timeout = setTimeout(() => finish({ confirmed: false, error: '扫描超时，请重新扫描或手动输入。' }), 45000)
      if (this.manager.startScan() === false) throw new Error('System OCR failed to start')
    } catch (err) {
      finish({ confirmed: false, error: '系统 OCR 启动失败，请重试或手动输入。' })
    }
  }
  stop() {
    const current = this.active
    if (!current) return
    this.active = null
    clearTimeout(current.timeout); clearTimeout(current.fallback); clearTimeout(current.finishTimer)
    for (const release of current.subscriptions) { try { release() } catch (err) { /* already gone */ } }
    if (current.registered) { try { this.manager.deleteUseOcrAppId(this.appid) } catch (err) { /* service restarting */ } }
    if (current.opened) {
      try { this.manager.setIsOpenScan(current.previousOpen) } catch (err) { /* service restarting */ }
      try { if (this.manager.setShowImgScanMode) this.manager.setShowImgScanMode(current.previousImage) } catch (err) { /* service restarting */ }
    }
    current.candidate = ''
  }
}
