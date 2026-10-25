<template>
  <div class="native-page" @touchstart="touchStart" @touchmove="touchMove" @touchend="touchEnd" @touchcancel="touchCancel">
    <hole class="native-hole" @touchstart.stop="touchStart" @touchmove.stop="touchMove" @touchend.stop="touchEnd" @touchcancel.stop="touchCancel"></hole>
    <div class="touch-surface" @touchstart.stop="touchStart" @touchmove.stop="touchMove" @touchend.stop="touchEnd" @touchcancel.stop="touchCancel"></div>
    <div v-if="errorText" class="failure-panel">
      <text class="failure-title">{{ appName }}</text>
      <text class="failure-text">{{ errorText }}</text>
      <div class="retry" @click="retry"><text class="retry-label">重新启动</text></div>
      <div class="leave" @click="finish"><text class="retry-text">返回</text></div>
    </div>
  </div>
</template>
<script>
import { launcher } from 'penchatLauncher'
import appConfig from '../../package.json'
import globalModule from 'global'
import { OcrSession } from './ocr'

let generationSeed = 0
function parse(value) {
  if (typeof value !== 'string') return value
  try { return JSON.parse(value) } catch (err) { return null }
}
function point(event) {
  const e = event || {}
  const detail = e.detail || e
  const touches = e.changedTouches || e.touches || detail.changedTouches || detail.touches
  const p = (touches && touches[0]) || detail
  const x = Number(p.pageX !== undefined ? p.pageX : p.clientX !== undefined ? p.clientX : p.x)
  const y = Number(p.pageY !== undefined ? p.pageY : p.clientY !== undefined ? p.clientY : p.y)
  return Number.isFinite(x) && Number.isFinite(y) ? { x, y } : null
}
export default {
  data() { return { appName: appConfig.name, errorText: '', running: false, visible: true, keyboardOpen: false } },
  mounted() { this.boot() },
  onNewOptions() { this.resume() },
  onShow() { this.resume() },
  onHide() {
    this.visible = false
    this.setBackPolicy(true)
    this.setNativeEditorPolicy(false)
    this.stopOcr()
    this.visible = false
    this._touchPoints = []
    this.send('touch', { x: 0, y: 0, pressed: false, cancelled: true, points: [] })
    this.send('visibility', { visible: false })
    clearInterval(this._poll); this._poll = null
  },
  onUnload() { this.dispose() },
  beforeDestroy() { this.dispose() },
  methods: {
    resume() {
      this._disposed = false
      this.visible = true
      this.bindBack()
      this.setBackPolicy(!!this._nativeRoot && !this._nativeEditor && !this.keyboardOpen)
      this.setNativeEditorPolicy(!!this._nativeEditor && !this.keyboardOpen)
      if (this.running && (typeof launcher.status !== 'function' || !launcher.status().alive)) {
        this.running = false
        this.errorText = ''
      }
      if (!this.running) { if (!this._starting && !this.errorText) this.boot(); return }
      clearInterval(this._poll)
      this._poll = setInterval(() => this.poll(), 20)
      this.send('visibility', { visible: true })
    },
    send(type, payload) {
      if (!this.running) return false
      return launcher.send(Object.assign({ v: 1, type, generation: this._generation, requestId: ++this._requestSequence }, payload || {}))
    },
    async boot() {
      if (this._starting || this.running || this._disposed) return
      this._starting = true
      this._generation = ++generationSeed
      this._nativeRoot = false; this._nativeImage = false; this._navigationSequence = 0
      this._touchSerial = 0; this._touchPoints = []; this._touchId = 0
      this.bindBack()
      this.setBackPolicy(false)
      const generation = this._generation
      this.setNativeEditorPolicy(false)
      this._nativeEditor = null; this._editorPolicySequence = 0
      this._nativeReady = false
      this._screen = null
      this._requestSequence = 0
      this.errorText = ''
      try {
        if (this._disposed) return
        await launcher.start({ workspace: String(globalThis.$workspace || ''), dataDir: String(globalThis.$dataDir || '') })
        if (this._disposed) { await launcher.stop(); return }
        this.running = true
        this.send('init', {})
        this.send('visibility', { visible: this.visible })
        // Arm before polling: Falcon may deliver an already queued ready event
        // during interval registration. A stale timeout cannot close a ready page.
        this._readyTimer = setTimeout(() => {
          if (this.running && !this._nativeReady && this._generation === generation) this.fail('原生页面启动超时，请重试')
        }, 15000)
        this._poll = setInterval(() => this.poll(), 20)
      } catch (err) { this.fail(err && err.message ? err.message : String(err)) }
      finally { this._starting = false }
    },
    poll() {
      try {
        for (const event of launcher.poll() || []) {
          if (event.generation !== undefined && event.generation !== this._generation && event.type !== 'booted' && event.type !== 'fatal' && event.type !== 'exit') continue
          if (event.type === 'booted') {
            const screen = event.screen
            if (!screen || !Number.isInteger(screen.width) || !Number.isInteger(screen.height) || screen.width <= 0 || screen.height <= 0)
              throw new Error('Invalid Falcon display configuration')
            this._screen = screen
            this.send('display.area', { x: 0, y: 0, width: screen.width, height: screen.height })
          }
          else if (event.type === 'ready') { this._nativeReady = true; clearTimeout(this._readyTimer); this._readyTimer = null }
          else if (event.type === 'editor.state') this.editorState(event)
          else if (event.type === 'navigation.state') this.navigationState(event)
          else if (event.type === 'keyboard.open') this.openKeyboard(event)
          else if (event.type === 'ocr.start') this.startOcr(event)
          else if (event.type === 'ocr.cancel') this.stopOcr(event.requestId)
          else if (event.type === 'finish') this.finish()
          else if (event.type === 'fatal') this.fail(event.error || '原生页面启动失败')
          else if (event.type === 'exit') this.fail(event.status < 0 ? '原生程序意外退出，请重试' : `原生程序已退出（${event.status}）`)
        }
      } catch (err) { this.fail('原生页面通信失败') }
    },
    setNativeEditorPolicy(active) {
      try {
        const page = this.$page && this.$page.$npage
        if (!page || typeof page.setSupportTopPanel !== 'function') return false
        const blocked = active || (!!this._nativeImage && this.visible && !this._disposed && !this.keyboardOpen)
        page.setSupportTopPanel(!blocked)
        this._topPanelBlocked = blocked
        return true
      } catch (_) { return false }
    },
    setBackPolicy(allow) {
      try {
        const page = this.$page && this.$page.$npage
        if (!page || typeof page.setSupportBack !== 'function') return false
        page.setSupportBack(allow)
        this._systemBackAllowed = allow
        return true
      } catch (_) { return false }
    },
    bindBack() {
      const page = this.$page && this.$page.$npage
      if (this._backPage || !page || typeof page.on !== 'function') return
      this._backPressed = () => {
        if (this.running && this.visible && !this.keyboardOpen && !this._systemBackAllowed)
          this.send('navigation.back', { sequence: this._navigationSequence, gesture: this._touchSerial || 0 })
      }
      page.on('backpressed', this._backPressed)
      this._backPage = page
    },
    unbindBack() {
      if (this._backPage && typeof this._backPage.off === 'function') this._backPage.off('backpressed', this._backPressed)
      this._backPage = null; this._backPressed = null
    },
    navigationState(event) {
      if (event.sequence <= (this._navigationSequence || 0)) return
      this._navigationSequence = event.sequence
      this._nativeRoot = event.root === true
      this._nativeImage = event.imageWorkspace === true
      this.setNativeEditorPolicy(!!this._nativeEditor && this.visible && !this.keyboardOpen)
      const allow = !this.visible || (!!this._nativeRoot && !this._nativeEditor && !this.keyboardOpen)
      const ok = this.setBackPolicy(allow)
      this.send('navigation.policy', { sequence: event.sequence, ok, blocked: ok && !allow })
    },
    editorState(event) {
      if (event.sequence <= (this._editorPolicySequence || 0)) return
      this._editorPolicySequence = event.sequence
      this._nativeEditor = event.active ? event.session : null
      this.setBackPolicy(!this.visible || (!!this._nativeRoot && !this._nativeEditor && !this.keyboardOpen))
      const active = !!event.active && this.visible && !this.keyboardOpen
      const ok = this.setNativeEditorPolicy(active)
      this.send('editor.policy', { session: event.session, sequence: event.sequence, ok, blocked: ok && active })
    },
    startOcr(event) {
      if (!this.visible || this.keyboardOpen) { this.send('ocr.result', { requestId: event.requestId, confirmed: false, error: '当前无法启动扫描。' }); return }
      if (!this._global) this._global = new globalModule.Global()
      this.stopOcr()
      const generation = this._generation
      this._ocrRequest = event.requestId
      this._ocr = new OcrSession(this._global, appConfig.appid, (type, data) => {
        if (this._disposed || generation !== this._generation || data.requestId !== this._ocrRequest) return
        this.send(type, data)
      })
      this._ocr.start(event.requestId, { session: event.session, mode: event.mode, continuous: event.continuous })
    },
    stopOcr(requestId) {
      if (requestId && requestId !== this._ocrRequest) return
      if (this._ocr) this._ocr.stop()
      this._ocr = null; this._ocrRequest = null
    },
    openKeyboard(event) {
      this.setNativeEditorPolicy(false)
      this.stopOcr()
      if (this.keyboardOpen || !this.visible) { this.send('keyboard.result', { requestId: event.requestId, confirmed: false, text: '' }); return }
      this.keyboardOpen = true
      this.setBackPolicy(false)
      this._keyboardRequest = event.requestId
      try {
        if (!this._global) this._global = new globalModule.Global()
        if (!this._editFinished) {
          this._editFinished = (uuid, json) => {
            const data = parse(json === undefined ? uuid : json) || {}
            const callbackUuid = json === undefined ? this._keyboardUuid : uuid
            if (!this.keyboardOpen || String(callbackUuid) !== String(this._keyboardUuid)) return
            this.send('keyboard.result', { requestId: this._keyboardRequest, confirmed: data.editConfirmed === true || data.editConfirmed === 'true', text: String(data.text !== undefined ? data.text : data.content || ''), cursorIndex: data.cursorIndex })
            this.keyboardOpen = false
            this._keyboardUuid = ''
          }
          this._global.textEditFinished.on(this._editFinished)
        }
        this._keyboardUuid = this._global.startTextEdit(JSON.stringify({ text: event.text || '', placeholder: event.placeholder || '请输入', inputType: event.inputType || 'ZhCNPreferred', maxlength: event.maxlength || 4096, autofocus: true, showCursor: true, multiLinesEditVisible: event.multiline === true, cursorIndex: event.cursorIndex || 0, confirmButtonDisabledOnTextEmpty: false, enterButtonText: '确认' }))
      } catch (err) {
        this.send('keyboard.result', { requestId: event.requestId, confirmed: false, text: '' })
        this.keyboardOpen = false
      }
    },
    touchStart(e) { this.forwardTouch(e, true) },
    touchMove(e) { this.forwardTouch(e, true) },
    touchEnd(e) { this.forwardTouch(e, false) },
    touchCancel(e) { this.forwardTouch(e, false, true) },
    forwardTouch(event, pressed, cancelled = false) {
      if (!this.visible || this.keyboardOpen || this.errorText) return
      const detail = (event && event.detail) || event || {}
      const active = (event && event.touches) || detail.touches
      const list = active ? Array.prototype.slice.call(active) : pressed ? [point(event)] : []
      const previous = this._touchPoints || []
      const used = new Set()
      const points = []
      for (const raw of list.slice(0, 3)) {
        const p = point(raw)
        if (!p) { cancelled = true; break }
        let id = raw.identifier !== undefined ? 'i' + raw.identifier : raw.id !== undefined ? 'i' + raw.id : null
        if (id === null) {
          let nearest = null, distance = Infinity
          for (const old of previous) {
            const d = (old.x - p.x) ** 2 + (old.y - p.y) ** 2
            if (!used.has(old.id) && d < distance) { nearest = old; distance = d }
          }
          id = nearest ? nearest.id : 'p' + (this._touchId = (this._touchId || 0) + 1)
        }
        if (used.has(id)) { cancelled = true; break }
        used.add(id)
        points.push({ id, x: Math.round(p.x), y: Math.round(p.y) })
      }
      if (!previous.length && points.length) this._touchSerial = (this._touchSerial || 0) + 1
      const p = points[0] || point(event) || this._lastPoint
      if (!p && !cancelled) return
      this._touchPoints = cancelled ? [] : points
      if (p) this._lastPoint = p
      this.send('touch', { x: p ? p.x : 0, y: p ? p.y : 0,
        pressed: !cancelled && points.length > 0, cancelled,
        points: cancelled ? [] : points, gesture: this._touchSerial || 0 })
    },
    fail(message) {
      this._nativeImage = false
      this.setBackPolicy(true)
      this.unbindBack()
      this.setNativeEditorPolicy(false)
      this.stopOcr()
      if (this._disposed) return
      this.errorText = message
      this.running = false
      clearInterval(this._poll); clearTimeout(this._readyTimer)
      this._poll = null; this._readyTimer = null
      launcher.stop().catch(() => {})
    },
    async retry() { if (this._starting) return; await launcher.stop(); this.running = false; this.boot() },
    finish() { this.dispose(); this.$page.finish() },
    dispose() {
      if (this._disposed) return
      this._nativeImage = false
      this.setBackPolicy(true)
      this.unbindBack()
      this.setNativeEditorPolicy(false)
      this._nativeEditor = null
      this._disposed = true
      this.stopOcr()
      this.send('exit')
      this.running = false
      clearInterval(this._poll); clearTimeout(this._readyTimer)
      if (this._global && this._editFinished) this._global.textEditFinished.off(this._editFinished)
      if (this._global && this._keyboardUuid && this._global.closeTextEdit) this._global.closeTextEdit(this._keyboardUuid)
      this._editFinished = null
      this._keyboardUuid = ''
      this.keyboardOpen = false
      launcher.stop().catch(() => {})
    },
  },
}
</script>
<style lang="less" scoped>
.native-page { width: 100vw; height: 100vh; position: relative; background-color: #212121; }
.touch-surface { position: absolute; left: 0px; top: 0px; width: 100%; height: 100%; background-color: rgba(0,0,0,0); }
.native-hole { position: absolute; left: 0px; top: 0px; width: 100%; height: 100%; }
.failure-panel { position: absolute; left: 0px; top: 0px; width: 100%; height: 100%; background-color: #212121; padding-left: 32px; padding-top: 20px; }
.failure-title { color: #ececec; font-size: 30px; }
.failure-text { color: #b4b4b4; font-size: 21px; margin-top: 14px; }
.retry { position: absolute; left: 32px; bottom: 25px; width: 180px; height: 45px; background-color: #ececec; border-radius: 12px; justify-content: center; align-items: center; }
.leave { position: absolute; left: 235px; bottom: 25px; width: 120px; height: 45px; background-color: #303030; border-radius: 12px; justify-content: center; align-items: center; }
.retry-label { color: #212121; font-size: 21px; }
.retry-text { color: #ececec; font-size: 21px; }
</style>
