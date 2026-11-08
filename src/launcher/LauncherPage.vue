<template>
  <div class="native-page" :style="nativeAreaStyle">
    <hole class="native-hole" :style="nativeAreaStyle"></hole>
    <div v-if="!nativeReady && !errorText" class="startup-panel">
      <text class="startup-title">{{ appName }}</text>
      <text class="startup-text">{{ startupText }}</text>
    </div>
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
  data() { return { appName: appConfig.name, errorText: '', running: false, visible: true, keyboardOpen: false, nativeReady: false, startupText: '正在打开…', nativeAreaStyle: {} } },
  mounted() { this.boot() },
  onNewOptions() { this.resume() },
  onShow() { this.resume() },
  onHide() {
    this.visible = false
    this.syncInputFocus(true)
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
      this.watchInputFocus()
      this.setBackPolicy(false)
      this.setNativeEditorPolicy(!!this._nativeEditor && !this.keyboardOpen)
      if (this.running && (typeof launcher.status !== 'function' || !launcher.status().alive)) {
        this.running = false
        this.errorText = ''
      }
      if (!this.running) { if (!this._starting && !this.errorText) this.boot(); return }
      clearInterval(this._poll)
      this._poll = setInterval(() => this.poll(), 20)
      this.send('visibility', { visible: true })
      this.syncInputFocus(true)
    },
    send(type, payload) {
      if (!this.running) return false
      return launcher.send(Object.assign({ v: 1, type, generation: this._generation, requestId: ++this._requestSequence }, payload || {}))
    },
    async boot() {
      if (this._starting || this.running || this._disposed) return
      this._starting = true
      if (this.stopOcr() === false) {
        this._starting = false; this.fail('系统扫描资源尚未释放，请重试'); return
      }
      this.cancelOfficialKeyboard()
      this._generation = ++generationSeed
      this._nativeImage = false; this._navigationSequence = 0
      this.setBackPolicy(false)
      const generation = this._generation
      this.setNativeEditorPolicy(false)
      this._nativeEditor = null; this._editorPolicySequence = 0
      this.nativeReady = false
      this.startupText = '正在打开…'
      this._focusSequence = 0; this._focusSent = null; this._focusAt = 0
      this.watchInputFocus()
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
        this.syncInputFocus(true)
        // Arm before polling: Falcon may deliver an already queued ready event
        // during interval registration. A stale timeout cannot close a ready page.
        this._readyTimer = setTimeout(() => {
          if (this.running && !this.nativeReady && this._generation === generation) this.fail('原生页面启动超时，请重试')
        }, 15000)
        this._poll = setInterval(() => this.poll(), 20)
      } catch (err) { this.fail(err && err.message ? err.message : String(err)) }
      finally { this._starting = false }
    },
    poll() {
      this.syncInputFocus()
      try {
        for (const event of launcher.poll() || []) {
          if (event.generation !== undefined && event.generation !== this._generation && event.type !== 'booted' && event.type !== 'startup' && event.type !== 'fatal' && event.type !== 'exit') continue
          if (event.type === 'booted') {
            const screen = event.screen
            if (!screen || !Number.isInteger(screen.width) || !Number.isInteger(screen.height) || screen.width <= 0 || screen.height <= 0)
              throw new Error('Invalid Falcon display configuration')
            this.nativeAreaStyle = { left: '0px', top: '0px', width: screen.width + 'px', height: screen.height + 'px' }
            this.send('display.area', { x: 0, y: 0, width: screen.width, height: screen.height })
          }
          else if (event.type === 'startup') { this.startupText = ({ display: '正在准备显示…', data: '正在读取数据…', interface: '正在准备页面…', frame: '正在显示页面…' })[event.stage] || '正在打开…' }
          else if (event.type === 'ready') { this.nativeReady = true; this.syncInputFocus(true); clearTimeout(this._readyTimer); this._readyTimer = null; if (this._pendingUpdateResult) { this.send('update.install.result', this._pendingUpdateResult); this._pendingUpdateResult = null } }
          else if (event.type === 'editor.state') this.editorState(event)
          else if (event.type === 'navigation.state') this.navigationState(event)
          else if (event.type === 'keyboard.open') this.openKeyboard(event)
          else if (event.type === 'ocr.start') this.startOcr(event)
          else if (event.type === 'ocr.cancel') this.stopOcr(event.requestId, true)
          else if (event.type === 'update.install') this.installUpdate(event)
          else if (event.type === 'finish') this.finish()
          else if (event.type === 'fatal') this.fail(event.error || '原生页面启动失败')
          else if (event.type === 'exit' && !this._updating) this.fail(event.status < 0 ? '原生程序意外退出，请重试' : `原生程序已退出（${event.status}）`)
        }
      } catch (err) { this.fail('原生页面通信失败') }
    },
    watchInputFocus() {
      if (this._focusListener || !am || typeof am.on !== 'function') return
      this._focusListener = () => this.syncInputFocus(true)
      try { am.on('topApp', this._focusListener) } catch (_) {}
      try { am.on('windowAttachState', this._focusListener) } catch (_) {}
    },
    syncInputFocus(force = false) {
      if (!this.running) return
      let active = false
      try { active = this.nativeReady && this.visible && !this._disposed && !this.keyboardOpen && am.hasWindowFocus() === true } catch (_) {}
      // A system overlay owns input, but its transparent pixels still reveal
      // the compositor's lower LVGL layer. Background/official input withdraw it.
      const displayActive = this.nativeReady && this.visible && !this._disposed && !this.keyboardOpen
      const now = Date.now()
      const elapsed = now - this._focusAt
      if (!force && active === this._focusSent && displayActive === this._displayFocusSent && elapsed >= 0 && elapsed < 100) return
      const sequence = (this._focusSequence || 0) + 1
      if (this.send('input.focus', { sequence, active, displayActive })) {
        this._focusSequence = sequence; this._focusSent = active; this._displayFocusSent = displayActive; this._focusAt = now
      }
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
        const blocked = active || (!!this._nativeImage && this.visible && !this._disposed && !this.keyboardOpen)
        page.setSupportTopPanel(!blocked)
        return true
      } catch (_) { return false }
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
      const allow = !this.visible
      const ok = this.setBackPolicy(allow)
      this.send('navigation.policy', { sequence: event.sequence, ok, blocked: ok && !allow })
    },
    editorState(event) {
      if (event.sequence <= (this._editorPolicySequence || 0)) return
      this._editorPolicySequence = event.sequence
      this._nativeEditor = event.active ? event.session : null
      this.setBackPolicy(!this.visible)
      const active = !!event.active && this.visible && !this.keyboardOpen
      const ok = this.setNativeEditorPolicy(active)
      this.send('editor.policy', { session: event.session, sequence: event.sequence, ok, blocked: ok && active })
    },
    startOcr(event) {
      if (!this.visible || this.keyboardOpen) { this.send('ocr.result', { requestId: event.requestId, confirmed: false, error: '当前无法启动扫描。' }); return }
      if (!this._global) this._global = new globalModule.Global()
      if (this.stopOcr() === false) {
        this.send('ocr.result', { requestId: event.requestId, session: event.session, mode: event.mode, capture: 1, confirmed: false, terminal: true, error: '系统扫描资源尚未释放，请重试。' })
        return
      }
      const generation = this._generation
      this._ocrRequest = event.requestId
      this._ocr = new OcrSession(this._global, appConfig.appid, (type, data) => {
        if (this._disposed || generation !== this._generation || data.requestId !== this._ocrRequest) return
        this.send(type, data)
      })
      this._ocr.start(event.requestId, { session: event.session, mode: event.mode, continuous: event.continuous })
    },
    stopOcr(requestId, acknowledge = false) {
      let ok = true
      if (!requestId || requestId === this._ocrRequest || (this._ocr && this._ocr.pendingCleanup)) {
        if (this._ocr) ok = this._ocr.stop() !== false
        if (ok) { this._ocr = null; this._ocrRequest = null }
      }
      // Already-stopped old IDs are acknowledged without touching a newer lease.
      if (acknowledge) this.send('ocr.stopped', { requestId, ok })
      return ok
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
      if (this.keyboardOpen || !this.visible) { this.send('keyboard.result', { requestId: event.requestId, confirmed: false, text: '' }); return }
      this.keyboardOpen = true
      this.syncInputFocus(true)
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
            this.syncInputFocus(true)
          }
          this._global.textEditFinished.on(this._editFinished)
        }
        this._keyboardUuid = this._global.startTextEdit(JSON.stringify({ text: event.text || '', placeholder: event.placeholder || '请输入', inputType: event.inputType || 'ZhCNPreferred', maxlength: event.maxlength || 4096, autofocus: true, showCursor: true, multiLinesEditVisible: event.multiline === true, cursorIndex: event.cursorIndex || 0, confirmButtonDisabledOnTextEmpty: false, enterButtonText: '确认' }))
      } catch (err) {
        this.send('keyboard.result', { requestId: event.requestId, confirmed: false, text: '' })
        this.keyboardOpen = false
        this.syncInputFocus(true)
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
      this.syncInputFocus(true)
      if (this._focusListener && am && typeof am.off === 'function') {
        try { am.off('topApp', this._focusListener) } catch (_) {}
        try { am.off('windowAttachState', this._focusListener) } catch (_) {}
      }
      this._focusListener = null
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
.startup-panel { position: absolute; left: 0px; top: 0px; width: 100%; height: 100%; background-color: #212121; justify-content: center; align-items: center; }
.startup-title { color: #ececec; font-size: 28px; }
.startup-text { color: #b4b4b4; font-size: 20px; margin-top: 14px; }
.failure-panel { position: absolute; left: 0px; top: 0px; width: 100%; height: 100%; background-color: #212121; padding-left: 32px; padding-top: 20px; }
.failure-title { color: #ececec; font-size: 30px; }
.failure-text { color: #b4b4b4; font-size: 21px; margin-top: 14px; }
.retry { position: absolute; left: 32px; bottom: 25px; width: 180px; height: 45px; background-color: #ececec; border-radius: 12px; justify-content: center; align-items: center; }
.leave { position: absolute; left: 235px; bottom: 25px; width: 120px; height: 45px; background-color: #303030; border-radius: 12px; justify-content: center; align-items: center; }
.retry-label { color: #212121; font-size: 21px; }
.retry-text { color: #ececec; font-size: 21px; }
</style>
