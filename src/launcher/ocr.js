// Adapter for the system pen-tip OCR service. No key parsing or persistence here.
function scanText(value, depth = 0) {
  if (depth > 6 || value == null) return ''
  if (typeof value === 'string') {
    const text = value.trim()
    if (text.length > 8192) throw new Error('OCR_TEXT_TOO_LONG')
    if (/^(data:image\/|\/.*\.(png|jpe?g|bmp)$)/i.test(text)) return ''
    if (/^[\[{]/.test(text)) {
      try { return scanText(JSON.parse(text), depth + 1) } catch (err) { return '' }
    }
    return text
  }
  if (Array.isArray(value)) {
    let text = ''
    for (const item of value) {
      const line = scanText(item, depth + 1)
      if (text.length + line.length + 1 > 8192) throw new Error('OCR_TEXT_TOO_LONG')
      if (line) text += (text ? '\n' : '') + line
    }
    return text
  }
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
  start(requestId, options = {}) {
    this.stop()
    const current = { requestId, subscriptions: [], candidate: '', started: false, capture: 0, delivered: 0, pendingTimers: new Set(), continuous: options.continuous === true, session: options.session || '', mode: options.mode === 'multi' ? 'multi' : 'single' }
    this.active = current
    const metadata = () => ({ requestId, session: current.session, mode: current.mode, capture: current.capture || 1 })
    const finish = result => {
      if (this.active !== current || current.finishing) return
      if (current.continuous && result.terminal !== true && current.delivered === current.capture) return
      clearTimeout(current.timeout); clearTimeout(current.fallback)
      const payload = Object.assign(metadata(), result)
      const keep = current.continuous && result.terminal !== true
      if (keep) {
        current.delivered = current.capture; current.started = false; current.candidate = ''
      } else current.finishing = true
      const timer = setTimeout(() => {
        current.pendingTimers.delete(timer)
        if (this.active !== current) return
        if (!keep) { this.stop(); payload.terminal = true }
        this.emit('ocr.result', payload)
      }, 0)
      current.pendingTimers.add(timer)
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
      try {
        const text = scanText(value)
        if (text) current.candidate = text
      } catch (_) { finish({ confirmed: false, error: '扫描内容过长，请分段扫描。' }) }
    }
    const final = value => {
      if (!current.started) return
      remember(value)
      clearTimeout(current.fallback)
      current.fallback = setTimeout(() => {
        if (current.started) finish(current.candidate ? { confirmed: true, text: current.candidate } : { confirmed: false, error: '未识别到文字，请重新划扫。' })
      }, 900)
    }
    try {
      for (const name of ['addUseOcrAppId', 'deleteUseOcrAppId', 'setIsOpenScan'])
        if (typeof this.manager[name] !== 'function') throw new Error('System OCR unavailable')
      if (!current.continuous && typeof this.manager.startScan !== 'function') throw new Error('System OCR unavailable')
      const startSignal = bind('ocrStartChanged', () => {
        if (current.started) return
        current.started = true; ++current.capture; current.candidate = ''; clearTimeout(current.fallback)
        clearTimeout(current.timeout)
        current.timeout = setTimeout(() => finish({ confirmed: false, error: '扫描超时，请重新扫描。' }), 45000)
        this.emit('ocr.status', Object.assign(metadata(), { started: true }))
      })
      // Older firmware without a start signal can still deliver final word results.
      current.started = !startSignal
      if (!startSignal) current.capture = 1
      if (current.continuous && !startSignal) throw new Error('Continuous OCR requires scan-start boundaries')
      let outputs = 0
      for (const name of ['scanWordResultChanged', 'apolloScanWordResultChanged']) {
        if (bind(name, (kind, result) => {
          remember(result === undefined ? kind : result)
          if (current.started) finish(current.candidate ? { confirmed: true, text: current.candidate } : { confirmed: false, error: '未识别到文字，请重新划扫。' })
        })) ++outputs
      }
      for (const name of ['ocrCompleteChanged', 'ocrStopChanged']) if (bind(name, final)) ++outputs
      bind('ocrResultChanged', remember)
      bind('scanErrorChanged', () => {
        if (current.started) finish({ confirmed: false, error: '系统 OCR 识别失败，请重新扫描。' })
      })
      if (!outputs) throw new Error('System OCR result signals unavailable')
      current.previousOpen = typeof this.manager.isOpenScan === 'boolean' ? this.manager.isOpenScan : false
      current.previousImage = typeof this.manager.showImgScanMode === 'boolean' ? this.manager.showImgScanMode : false
      if (this.manager.setCurrentAppId) this.manager.setCurrentAppId(this.appid, true)
      if (this.manager.setShowImgScanMode) this.manager.setShowImgScanMode(false)
      current.opened = true
      this.manager.setIsOpenScan(true)
      current.registered = true
      if (this.manager.addUseOcrAppId(JSON.stringify({ appid: this.appid, scanMethod: 0, stopSoundPlayerOnHeadButtonPressed: false })) === false) throw new Error('System OCR registration failed')
      if (typeof this.manager.startAlmondMultiScan === 'function') this.manager.startAlmondMultiScan(current.mode === 'multi' ? 1 : 0)
      this.emit('ocr.status', Object.assign(metadata(), { started: false }))
      if (!current.continuous) current.timeout = setTimeout(() => finish({ confirmed: false, error: '扫描超时，请重新扫描或手动输入。' }), 45000)
      // Continuous keyboard OCR waits for the pen tip. startScan actively starts
      // a capture, so calling it here would turn opening the editor into a scan.
      if (!current.continuous && this.manager.startScan() === false) throw new Error('System OCR failed to start')
    } catch (err) {
      finish({ confirmed: false, terminal: true, error: '系统 OCR 启动失败，请重试或手动输入。' })
    }
  }
  stop() {
    const current = this.active
    if (!current) return
    this.active = null
    clearTimeout(current.timeout); clearTimeout(current.fallback)
    for (const timer of current.pendingTimers) clearTimeout(timer)
    current.pendingTimers.clear()
    for (const release of current.subscriptions) { try { release() } catch (err) { /* already gone */ } }
    if (current.registered) { try { this.manager.deleteUseOcrAppId(this.appid) } catch (err) { /* service restarting */ } }
    if (current.opened) {
      try { if (this.manager.startAlmondMultiScan) this.manager.startAlmondMultiScan(0) } catch (err) { /* service restarting */ }
      try { this.manager.setIsOpenScan(current.previousOpen) } catch (err) { /* service restarting */ }
      try { if (this.manager.setShowImgScanMode) this.manager.setShowImgScanMode(current.previousImage) } catch (err) { /* service restarting */ }
    }
    current.candidate = ''
  }
}
