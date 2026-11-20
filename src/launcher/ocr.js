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
  static capabilities(manager) {
    // Read-only interface detection: never register, switch modes or start a
    // capture to probe support. A missing multi setter is unverified, so the
    // UI conservatively offers single-line only; platform limits still apply.
    const result = { single: false, multi: false }
    try {
      if (!manager) return result
      const method = name => typeof manager[name] === 'function'
      const signal = name => {
        const value = manager[name]
        return !!value && ((typeof value.on === 'function' && typeof value.off === 'function') ||
          (typeof value.connect === 'function' && typeof value.disconnect === 'function'))
      }
      result.single = method('addUseOcrAppId') && method('deleteUseOcrAppId') &&
        (method('setIsOpenScan') || method('setCurrentAppId')) && signal('ocrStartChanged') &&
        ['scanWordResultChanged', 'apolloScanWordResultChanged', 'ocrCompleteChanged', 'ocrStopChanged'].some(signal)
      result.multi = result.single && method('startAlmondMultiScan')
    } catch (_) { /* Inaccessible optional exports cannot establish support. */ }
    return result
  }
  constructor(manager, appid, emit, stroke = null, route = null) {
    this.route = route; this.nativeRouting = false
    this.stroke = stroke
    this.manager = manager; this.appid = appid; this.emit = emit; this.active = null; this.pendingCleanup = null
  }
  start(requestId, options = {}) {
    const previous = this.active
    const mode = options.mode === 'multi' ? 'multi' : 'single'
    // Prepare/activate are two phases of one edit session, not two scans.
    // Replacing listeners here can erase a pen-down/start that arrived during
    // the native hardware handoff. Duplicate activation must be idempotent too.
    if (previous && !previous.finishing && previous.requestId === requestId &&
        previous.session === (options.session || '') && previous.mode === mode &&
        previous.nativePen === (options.nativePen === true) &&
        previous.continuous === (options.continuous === true) &&
        options.prepareOnly !== true && (previous.prepared || previous.activated)) {
      if (!previous.activated) {
        try { previous.activate() } catch (_) { previous.startFailure() }
      }
      return
    }
    const retained = !!(previous && previous.continuous && options.continuous === true && (previous.registered || previous.opened))
    if(this.stop(retained) === false) {
      this.emit('ocr.result', { requestId, session: options.session || '', mode: options.mode === 'multi' ? 'multi' : 'single', capture: 1, confirmed: false, terminal: true, error: '系统扫描资源尚未释放，请重试。' })
      return
    }
    const current = { requestId, subscriptions: [], candidate: '', started: false, capture: 0, delivered: 0, pendingTimers: new Set(), continuous: options.continuous === true, session: options.session || '', mode, nativePen: options.nativePen === true, prepared: false, activated: false, ending: false }
    if (retained) {
      current.registered = previous.registered; current.opened = previous.opened
      current.imageChanged = previous.imageChanged; current.multiChanged = previous.multiChanged
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
        current.delivered = current.capture; current.started = false; current.ending = false; current.candidate = ''
      } else current.finishing = true
      const timer = setTimeout(() => {
        current.pendingTimers.delete(timer)
        if (this.active !== current) return
        if (!keep) {
          if (current.continuous) {
            this.stop(true); this.active = current; current.subscriptions = []
            try { this.manager.setIsOpenScan(false); current.opened = true; current.imageChanged = typeof this.manager.setShowImgScanMode === 'function' } catch (_) {}
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
    const settle = delay => {
      clearTimeout(current.fallback)
      const capture = current.capture
      current.fallback = setTimeout(() => {
        if (this.active === current && current.started && current.capture === capture)
          finish(current.candidate ? { confirmed: true, text: current.candidate } : { confirmed: false, error: '未识别到文字，请重新划扫。' })
      }, delay)
    }
    const remember = value => {
      if (!current.started) return
      try {
        const text = scanText(value)
        if (text) {
          current.candidate = text
          // Some firmware announces stop/complete before its text property.
          // Once text arrives, settle the final value without waiting for a
          // second completion signal. No timer is armed while the pen is idle.
          if (current.ending) settle(120)
          return true
        }
      } catch (_) { finish({ confirmed: false, error: '扫描内容过长，请分段扫描。' }) }
      return false
    }
    const final = value => {
      if (!current.started) return
      remember(value)
      if (!current.started) return
      current.ending = true
      // Empty notifications are not proof of an empty scan. Keep listeners
      // alive for delayed recognition instead of discarding the later text.
      settle(current.candidate ? 900 : 2500)
    }
    current.startFailure = () => finish({ confirmed: false, terminal: true, error: '系统 OCR 启动失败，请重试或手动输入。' })
    current.activate = () => {
      if (current.activated) return
      if (typeof this.manager.setShowImgScanMode === 'function') { current.imageChanged = true; this.manager.setShowImgScanMode(false) }
      // Older Global bridges (including S6) arm through currentAppId + OCR
      // registration and do not expose the newer scan-open setter.
      if (typeof this.manager.setIsOpenScan === 'function') {
        current.opened = true
        this.manager.setIsOpenScan(true)
      } else if (typeof this.manager.setCurrentAppId !== 'function') throw new Error('System OCR registration control unavailable')
      if (typeof this.manager.startAlmondMultiScan === 'function') { current.multiChanged = true; this.manager.startAlmondMultiScan(current.mode === 'multi' ? 1 : 0) }
      current.prepared = false; current.activated = true
      this.emit('ocr.status', Object.assign(metadata(), { started: current.started }))
      if (!current.continuous) current.timeout = setTimeout(() => finish({ confirmed: false, error: '扫描超时，请重新扫描或手动输入。' }), 45000)
      // Opening a continuous editor only arms the physical pen, never scans.
      if (!current.continuous && this.manager.startScan() === false) throw new Error('System OCR failed to start')
    }
    try {
      for (const name of ['addUseOcrAppId', 'deleteUseOcrAppId'])
        if (typeof this.manager[name] !== 'function') throw new Error('System OCR unavailable')
      if (!current.continuous && typeof this.manager.startScan !== 'function') throw new Error('System OCR unavailable')
      const startSignal = bind('ocrStartChanged', active => {
        if (active === false || current.started) return
        current.started = true; current.ending = false; ++current.capture; current.candidate = ''; clearTimeout(current.fallback)
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
          const populated = remember(result === undefined ? kind : result)
          if (current.started) {
            if (populated) finish({ confirmed: true, text: current.candidate })
            else final()
          }
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
        current.prepared = true
        this.emit('ocr.prepared', Object.assign(metadata(), { ok: true }))
        return
      }
      current.activate()
    } catch (err) {
      if (options.prepareOnly === true) {
        // Keep the page-local routing lease, but block pen capture until retry.
        // Releasing it here would send the very next stroke to the stock app.
        this.stop(true)
        this.active = current
        current.subscriptions = []; current.finishing = false
        try { this.manager.setIsOpenScan(false); current.opened = true; current.imageChanged = typeof this.manager.setShowImgScanMode === 'function' } catch (_) {}
        this.emit('ocr.prepared', Object.assign(metadata(), { ok: false, error: '系统 OCR 准备失败，可重试或手动输入。' }))
      } else current.startFailure()
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
      try { this.manager.setIsOpenScan(false); current.opened = true; current.imageChanged = typeof this.manager.setShowImgScanMode === 'function' } catch (_) {}
      this.emit('ocr.result', { requestId: current.requestId, session: current.session, mode: current.mode, capture: current.capture || 1, confirmed: false, terminal: true, error: '当前固件的文字扫描接口不可用，请使用官方键盘。' })
    }
  }
  stop(retainRouting = false) {
    const current = this.active || this.pendingCleanup
    if (!retainRouting && this.nativeRouting) {
      try { if (this.route) this.route(false) } catch (_) {}
      this.nativeRouting = false
    }
    if (!current) return true
    this.active = null
    if (current.penDown && this.stroke) { try { this.stroke(false, current.penSequence) } catch (_) {} }
    current.penDown = false
    clearTimeout(current.timeout); clearTimeout(current.fallback)
    for (const timer of current.pendingTimers) clearTimeout(timer)
    current.pendingTimers.clear()
    for (const release of current.subscriptions) { try { release() } catch (err) { /* already gone */ } }
    current.subscriptions = []
    let released = true
    const restore = (flag, action) => {
      if (!current[flag]) return
      try { if (action() === false && flag === 'registered') throw new Error('OCR unregister rejected'); current[flag] = false }
      catch (_) { released = false }
    }
    if(retainRouting) { current.candidate = ''; return true }
    restore('registered', () => this.manager.deleteUseOcrAppId(this.appid))
    restore('multiChanged', () => this.manager.startAlmondMultiScan(0))
    restore('imageChanged', () => this.manager.setShowImgScanMode(current.previousImage))
    restore('opened', () => this.manager.setIsOpenScan(current.previousOpen))
    this.pendingCleanup = released ? null : current
    current.candidate = ''
    return released
  }
}
