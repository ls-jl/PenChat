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
  constructor(manager, appid, emit, stroke = null, route = null) {
    this.route = route; this.nativeRouting = false
    this.stroke = stroke
    this.manager = manager; this.appid = appid; this.emit = emit; this.active = null
  }
  start(requestId, options = {}) {
    const previous = this.active
    const retained = !!(previous && previous.continuous && options.continuous === true && (previous.registered || previous.opened))
    this.stop(retained)
    const current = { requestId, subscriptions: [], candidate: '', started: false, capture: 0, delivered: 0, pendingTimers: new Set(), continuous: options.continuous === true, session: options.session || '', mode: options.mode === 'multi' ? 'multi' : 'single' }
    if (retained) {
      current.registered = previous.registered; current.opened = previous.opened
      current.previousOpen = previous.previousOpen; current.previousImage = previous.previousImage
    } else {
      current.previousOpen = typeof this.manager.isOpenScan === 'boolean' ? this.manager.isOpenScan : false
      current.previousImage = typeof this.manager.showImgScanMode === 'boolean' ? this.manager.showImgScanMode : false
    }
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
        if (!keep) {
          if (current.continuous) {
            this.stop(true); this.active = current; current.subscriptions = []
            try { this.manager.setIsOpenScan(false); current.opened = true } catch (_) {}
          } else this.stop()
          payload.terminal = true
        }
        this.emit('ocr.result', payload)
      }, 0)
      current.pendingTimers.add(timer)
    }
    const bindSignal = (signal, callback) => {
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
    const bind = (name, callback) => bindSignal(this.manager[name], callback)
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
      // Establish routing before enabling capture. A prepared keyboard keeps
      // its native key lease until this registration has been acknowledged.
      if (options.nativePen === true && !this.nativeRouting) {
        if (!this.route || this.route(true) !== true) throw new Error('Native OCR route unavailable')
        this.nativeRouting = true
      }
      current.registered = true
      if (this.manager.addUseOcrAppId(JSON.stringify({ appid: this.appid, scanMethod: current.mode === 'multi' ? 1 : 0, useOcr: true, stopSoundPlayerOnHeadButtonPressed: false })) === false) throw new Error('System OCR registration failed')
      if (this.manager.setCurrentAppId) this.manager.setCurrentAppId(this.appid, true)
      if (options.prepareOnly === true) {
        this.emit('ocr.prepared', Object.assign(metadata(), { ok: true }))
        return
      }
      if (this.manager.setShowImgScanMode) this.manager.setShowImgScanMode(false)
      current.opened = true
      this.manager.setIsOpenScan(true)
      if (typeof this.manager.startAlmondMultiScan === 'function') this.manager.startAlmondMultiScan(current.mode === 'multi' ? 1 : 0)
      this.emit('ocr.status', Object.assign(metadata(), { started: false }))
      if (!current.continuous) current.timeout = setTimeout(() => finish({ confirmed: false, error: '扫描超时，请重新扫描或手动输入。' }), 45000)
      // Continuous keyboard OCR waits for the pen tip. startScan actively starts
      // a capture, so calling it here would turn opening the editor into a scan.
      if (!current.continuous && this.manager.startScan() === false) throw new Error('System OCR failed to start')
    } catch (err) {
      if (options.prepareOnly === true) {
        // Keep the page-local routing lease, but block pen capture until retry.
        // Releasing it here would send the very next stroke to the stock app.
        this.stop(true)
        this.active = current
        current.subscriptions = []; current.finishing = false
        try { this.manager.setIsOpenScan(false); current.opened = true } catch (_) {}
        this.emit('ocr.prepared', Object.assign(metadata(), { ok: false, error: '系统 OCR 准备失败，可重试或手动输入。' }))
      } else finish({ confirmed: false, terminal: true, error: '系统 OCR 启动失败，请重试或手动输入。' })
    }
  }
  pen(event) {
    const current = this.active
    if (!current || !current.continuous || current.finishing || event.requestId !== current.requestId || event.session !== current.session || !this.stroke) return
    if (typeof event.down !== 'boolean' || !Number.isInteger(event.sequence) || event.sequence <= 0) return
    if (event.down) {
      if (current.penDown || event.sequence <= (current.penSequence || 0)) return
      current.penDown = true; current.penSequence = event.sequence
    } else {
      if (!current.penDown || event.sequence !== current.penSequence) return
      current.penDown = false
    }
    let ok = false
    try {
      ok = this.stroke(event.down, event.sequence) === true
      // Y07's named press entry sets image mode even without the home handler.
      // Restore text mode after that call, before the capture is finalized.
      if (ok && this.manager.setShowImgScanMode) this.manager.setShowImgScanMode(false)
    } catch (_) { ok = false }
    if (!ok) {
      try { this.manager.setIsOpenScan(false); current.opened = true } catch (_) {}
      this.emit('ocr.result', { requestId: current.requestId, session: current.session, mode: current.mode, capture: current.capture || 1, confirmed: false, terminal: true, error: '当前固件的文字扫描接口不可用，请使用官方键盘。' })
    }
  }
  stop(retainRouting = false) {
    const current = this.active
    if (!retainRouting && this.nativeRouting) {
      try { if (this.route) this.route(false) } catch (_) {}
      this.nativeRouting = false
    }
    if (!current) return
    this.active = null
    if (current.penDown && this.stroke) { try { this.stroke(false, current.penSequence) } catch (_) {} }
    current.penDown = false
    clearTimeout(current.timeout); clearTimeout(current.fallback)
    for (const timer of current.pendingTimers) clearTimeout(timer)
    current.pendingTimers.clear()
    for (const release of current.subscriptions) { try { release() } catch (err) { /* already gone */ } }
    if (!retainRouting && current.registered) { try { this.manager.deleteUseOcrAppId(this.appid) } catch (err) { /* service restarting */ } }
    if (!retainRouting && current.opened) {
      try { if (this.manager.startAlmondMultiScan) this.manager.startAlmondMultiScan(0) } catch (err) { /* service restarting */ }
      try { this.manager.setIsOpenScan(current.previousOpen) } catch (err) { /* service restarting */ }
      try { if (this.manager.setShowImgScanMode) this.manager.setShowImgScanMode(current.previousImage) } catch (err) { /* service restarting */ }
    }
    current.candidate = ''
  }
}
