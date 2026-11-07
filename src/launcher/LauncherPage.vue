<template>
  <div class="native-page">
    <hole class="native-hole"></hole>
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
import { UpdateInstall } from './update'
import pm from 'pm'
import am from '$am'

let generationSeed = 0
function parse(value) {
  if (typeof value !== 'string') return value
  try { return JSON.parse(value) } catch (err) { return null }
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
    this.send('visibility', { visible: false })
    clearInterval(this._poll); this._poll = null
  },
  onUnload() { this.dispose() },
  beforeDestroy() { this.dispose() },
  methods: {
    resume() {
      this._disposed = false
      this.visible = true
      this.setBackPolicy(false)
      this.setNativeEditorPolicy(!!this._nativeEditor && !this.keyboardOpen)
      if (this.running && (typeof launcher.status !== 'function' || !launcher.status().alive)) {
        this.running = false
        this.errorText = ''
      }
      if (!this.running) { if (!this._starting && !this.errorText) this.boot(); return }
      clearInterval(this._poll)
      this._poll = setInterval(() => this.poll(), 20)
      this.syncWindowFocus(true)
      this.send('visibility', { visible: true })
    },
    send(type, payload) {
      if (!this.running) return false
      return launcher.send(Object.assign({ v: 1, type, generation: this._generation, requestId: ++this._requestSequence }, payload || {}))
    },
    async boot() {
      if (this._starting || this.running || this._disposed) return
      this._starting = true
      this.cancelOfficialKeyboard()
      this._generation = ++generationSeed
      this._nativeImage = false; this._navigationSequence = 0
      this.setBackPolicy(false)
      const generation = this._generation
      this.setNativeEditorPolicy(false)
      this._nativeEditor = null; this._editorPolicySequence = 0
      this._nativeReady = false
      this._windowFocused = true; this._focusSequence = 0; this._focusNextCheck = 0; this._focusPending = false; this._overlayStatus = ''
      this._requestSequence = 0
      this.errorText = ''
      try {
        if (this._disposed) return
        let workspace = String(globalThis.$workspace || '')
        try { let info = pm.getPackageInfo(appConfig.appid); if (typeof info === 'string') info = JSON.parse(info); if (info && info.installPath) workspace = String(info.installPath) } catch (err) {}
        await launcher.start({ workspace, dataDir: String(globalThis.$dataDir || '') })
        if (this._disposed) { await launcher.stop(); return }
        this.running = true
        this.send('init', {})
        this.send('visibility', { visible: this.visible })
        this.syncWindowFocus(true)
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
        this.syncWindowFocus()
        for (const event of launcher.poll() || []) {
          if (event.generation !== undefined && event.generation !== this._generation && event.type !== 'booted' && event.type !== 'fatal' && event.type !== 'exit') continue
          if (event.type === 'booted') {
            const screen = event.screen
            if (!screen || !Number.isInteger(screen.width) || !Number.isInteger(screen.height) || screen.width <= 0 || screen.height <= 0)
              throw new Error('Invalid Falcon display configuration')
            this.send('display.area', { x: 0, y: 0, width: screen.width, height: screen.height })
          }
          else if (event.type === 'ready') { this._nativeReady = true; clearTimeout(this._readyTimer); this._readyTimer = null; if (this._pendingUpdateResult) { this.send('update.install.result', this._pendingUpdateResult); this._pendingUpdateResult = null } }
          else if (event.type === 'editor.state') this.editorState(event)
          else if (event.type === 'navigation.state') this.navigationState(event)
          else if (event.type === 'keyboard.open') this.openKeyboard(event)
          else if (event.type === 'ocr.prepare') this.startOcr(event, true)
          else if (event.type === 'ocr.pen') { if (this._ocr) this._ocr.pen(event) }
          else if (event.type === 'ocr.start') this.startOcr(event)
          else if (event.type === 'ocr.cancel') this.stopOcr(event.requestId)
          else if (event.type === 'update.install') this.installUpdate(event)
          else if (event.type === 'finish') this.finish()
          else if (event.type === 'fatal') this.fail(event.error || '原生页面启动失败')
          else if (event.type === 'exit' && !this._updating) this.fail(event.status < 0 ? '原生程序意外退出，请重试' : `原生程序已退出（${event.status}）`)
        }
      } catch (err) { this.fail('原生页面通信失败') }
    },
    async installUpdate(ticket) {
      if (this._updating) return
      this._updating = true
      clearTimeout(this._readyTimer); this._readyTimer = null
      this.stopOcr(); this.setNativeEditorPolicy(false)
      try {
        this._updateBridge = new UpdateInstall({
          appid: appConfig.appid, dataDir: String(globalThis.$dataDir || ''), pm, launcher,
          setTimeout, clearTimeout,
          accept: value => this.send('update.install.accepted', { operation: value.operation }),
          status: text => { this.running = false; this.errorText = text },
          installed: async () => { this._updateInstalled = true; launcher.reopen() },
          failed: (value, text) => {
            this._updating = false; this.running = false; this.errorText = ''
            this._pendingUpdateResult = { operation: value.operation, success: false }
            this.boot()
          },
        })
        await this._updateBridge.start(ticket)
      } catch (err) { this._updating = false; this.running = false; this.fail('更新安装交接失败，请重新打开 PenChat') }
    },
    setNativeEditorPolicy(active) {
      try {
        const page = this.$page && this.$page.$npage
        if (!page || typeof page.setSupportTopPanel !== 'function') return false
        const focused = this._windowFocused !== false
        const blocked = focused && (active || (!!this._nativeImage && this.visible && !this._disposed && !this.keyboardOpen))
        page.setSupportTopPanel(!blocked)
        return true
      } catch (_) { return false }
    },
    syncWindowFocus(force = false) {
      if (!this.running || !this.visible || this._disposed) return
      const now = Date.now()
      if (!force && now < (this._focusNextCheck || 0)) return
      this._focusNextCheck = now + 100
      let focused
      try { if (am && typeof am.hasWindowFocus === 'function') focused = am.hasWindowFocus() } catch (_) {}
      const status = { window_api: !!(am && typeof am.hasWindowFocus === 'function'), window_valid: typeof focused === 'boolean', app_focused: focused === true, panel_api: typeof launcher.systemOverlay === 'function', panel_available: false, panel_supported: false, panel_visible: false }
      // Y07's global panel does not take window focus or trigger onHide. Its
      // actual visibility must also yield the native DRM plane and evdev input.
      try {
        const panel = typeof launcher.systemOverlay === 'function' ? launcher.systemOverlay() : null
        status.panel_available = !!(panel && panel.available === true)
        status.panel_supported = !!(panel && panel.supported === true)
        status.panel_visible = !!(panel && panel.visible === true)
        if (panel && panel.supported === true && typeof panel.visible === 'boolean') focused = focused !== false && !panel.visible
      } catch (_) {}
      const signature = JSON.stringify(status)
      if (signature !== this._overlayStatus && this.send('system.overlay.status', status)) this._overlayStatus = signature
      if (typeof focused !== 'boolean' || (focused === this._windowFocused && !this._focusPending)) return
      this._windowFocused = focused
      this.setBackPolicy(!focused)
      this.setNativeEditorPolicy(!!this._nativeEditor && !this.keyboardOpen)
      if (!focused) this.stopOcr()
      this._focusPending = !this.send('window.focus', { focused, sequence: ++this._focusSequence })
    },
    setBackPolicy(allow) {
      try {
        const page = this.$page && this.$page.$npage
        if (!page || typeof page.setSupportBack !== 'function') return false
        page.setSupportBack(allow)
        return true
      } catch (_) { return false }
    },
    navigationState(event) {
      if (event.sequence <= (this._navigationSequence || 0)) return
      this._navigationSequence = event.sequence
      this._nativeImage = event.imageWorkspace === true
      this.setNativeEditorPolicy(!!this._nativeEditor && this.visible && !this.keyboardOpen)
      const allow = !this.visible || this._windowFocused === false
      const ok = this.setBackPolicy(allow)
      this.send('navigation.policy', { sequence: event.sequence, ok, blocked: ok && !allow })
    },
    editorState(event) {
      if (event.sequence <= (this._editorPolicySequence || 0)) return
      this._editorPolicySequence = event.sequence
      this._nativeEditor = event.active ? event.session : null
      this.setBackPolicy(!this.visible || this._windowFocused === false)
      const active = !!event.active && this.visible && this._windowFocused !== false && !this.keyboardOpen
      const ok = this.setNativeEditorPolicy(active)
      this.send('editor.policy', { session: event.session, sequence: event.sequence, ok, blocked: ok && active })
    },
    startOcr(event, prepareOnly = false) {
      if (!this.visible || this._windowFocused === false || this.keyboardOpen) { this.send('ocr.result', { requestId: event.requestId, confirmed: false, error: '当前无法启动扫描。' }); return }
      try {
      if (!this._global) this._global = new globalModule.Global()
      const generation = this._generation
      this._ocrRequest = event.requestId
      if (!this._ocr) this._ocr = new OcrSession(this._global, appConfig.appid, (type, data) => {
        if (this._disposed || generation !== this._generation || data.requestId !== this._ocrRequest) return
        this.send(type, data)
      }, (down, sequence) => typeof launcher.ocrStroke === 'function' && launcher.ocrStroke(down, sequence),
      active => typeof launcher.ocrRoute === 'function' && launcher.ocrRoute(active))
      this._ocr.start(event.requestId, { session: event.session, mode: event.mode, continuous: event.continuous, nativePen: event.nativePen === true, prepareOnly })
      } catch (err) {
        if (!prepareOnly) throw err
        this.stopOcr()
        this.send('ocr.prepared', { requestId: event.requestId, session: event.session, mode: event.mode, ok: false })
      }
    },
    stopOcr(requestId) {
      if (requestId && requestId !== this._ocrRequest) return
      if (this._ocr) this._ocr.stop()
      this._ocr = null; this._ocrRequest = null
    },
    cancelOfficialKeyboard() {
      if (!this.keyboardOpen) return
      const requestId = this._keyboardRequest
      const uuid = this._keyboardUuid
      this.keyboardOpen = false
      this._keyboardUuid = ''
      this._keyboardRequest = ''
      try { if (this._global && uuid && this._global.closeTextEdit) this._global.closeTextEdit(uuid) } catch (_) {}
      this.send('keyboard.result', { requestId, confirmed: false, text: '' })
    },
    openKeyboard(event) {
      this.setNativeEditorPolicy(false)
      this.stopOcr()
      if (this.keyboardOpen || !this.visible || this._windowFocused === false) { this.send('keyboard.result', { requestId: event.requestId, confirmed: false, text: '' }); return }
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
    fail(message) {
      this._nativeImage = false
      this.setBackPolicy(true)
      this.setNativeEditorPolicy(false)
      this.stopOcr()
      if (this._disposed) return
      this.errorText = message
      this.running = false
      clearInterval(this._poll); clearTimeout(this._readyTimer)
      this._poll = null; this._readyTimer = null
      launcher.stop().catch(() => {})
    },
    async retry() { if (this._updateInstalled) { launcher.reopen(); return }; if (this._starting) return; await launcher.stop(); this.running = false; this.boot() },
    finish() { this.dispose(); this.$page.finish() },
    dispose() {
      if (this._disposed) return
      this._nativeImage = false
      this.setBackPolicy(true)
      this.setNativeEditorPolicy(false)
      this._nativeEditor = null
      this._disposed = true
      if (this._updateBridge) this._updateBridge.dispose()
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
.native-hole { position: absolute; left: 0px; top: 0px; width: 100%; height: 100%; }
.failure-panel { position: absolute; left: 0px; top: 0px; width: 100%; height: 100%; background-color: #212121; padding-left: 32px; padding-top: 20px; }
.failure-title { color: #ececec; font-size: 30px; }
.failure-text { color: #b4b4b4; font-size: 21px; margin-top: 14px; }
.retry { position: absolute; left: 32px; bottom: 25px; width: 180px; height: 45px; background-color: #ececec; border-radius: 12px; justify-content: center; align-items: center; }
.leave { position: absolute; left: 235px; bottom: 25px; width: 120px; height: 45px; background-color: #303030; border-radius: 12px; justify-content: center; align-items: center; }
.retry-label { color: #212121; font-size: 21px; }
.retry-text { color: #ececec; font-size: 21px; }
</style>
