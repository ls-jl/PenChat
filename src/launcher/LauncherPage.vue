<template>
  <div class="native-page" :style="pageStyle">
    <hole class="native-hole" :style="officialInline ? officialHoleStyle : nativeAreaStyle"></hole>
    <div v-if="officialInline" class="official-editor" :style="appearanceStyles.officialPanel">
      <input v-if="officialSingleLine" :key="officialControlKey" ref="officialInput" class="official-input" :style="officialInputStyle" :type="officialSensitive ? 'password' : 'text'" :value="officialInitialText" :inputType="officialInputType" :maxlength="officialLimit" :autofocus="true" :softInputEnable="true" :showCursor="false" cursorColor="transparent" @input="officialInputHandlers.input" @confirm="officialInputHandlers.confirm" />
      <textarea v-else :key="officialControlKey" ref="officialInput" class="official-input" :style="officialInputStyle" :value="officialInitialText" :inputType="officialInputType" :maxlength="officialLimit" :autofocus="true" :softInputEnable="true" :showCursor="false" cursorColor="transparent" @input="officialInputHandlers.input" @confirm="officialInputHandlers.confirm" />
    </div>
    <div v-if="!nativeReady && !errorText" class="startup-panel" :style="appearanceStyles.panel">
      <text class="startup-title" :style="appearanceStyles.title">{{ appName }}</text>
      <text class="startup-text" :style="appearanceStyles.hint">{{ startupText }}</text>
    </div>
    <div v-if="errorText" class="failure-panel" :style="appearanceStyles.failure">
      <text class="failure-title" :style="appearanceStyles.title">{{ appName }}</text>
      <text class="failure-text" :style="appearanceStyles.hint">{{ errorText }}</text>
      <div class="retry" :style="appearanceStyles.retry" @click="retry"><text class="retry-label" :style="appearanceStyles.primaryText">重新启动</text></div>
      <div class="leave" :style="appearanceStyles.leave" @click="finish"><text class="retry-text" :style="appearanceStyles.text">返回</text></div>
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
// Before the native bridge starts, use the named 100% startup design. Once the
// appearance packet arrives its UiMetrics-resolved logical values replace these
// unchanged; Falcon screen and official field coordinates are never scaled here.
const startupDesign = Object.freeze({ title: 28, body: 21, caption: 20, gap: 14, inset: 32, top: 20, bottom: 25, retryWidth: 180, leaveX: 235, leaveWidth: 120, controlHeight: 45, radius: 12 })
const startupColors = Object.freeze({ background: '#212121', text: '#ececec', muted: '#b4b4b4', surface: '#303030', primaryText: '#212121', caret: '#4ade80' })
function resolvedAppearance(c, m) {
  const pixel = value => value + 'px' // already resolved logical pixels
  return {
    page: { backgroundColor: c.background }, panel: { backgroundColor: c.background },
    officialPanel: { backgroundColor: 'transparent' },
    // A real focused InputConnection owns system editing; LVGL paints the field.
    // Falcon documents opacity for both input controls. Keep nonzero bounds
    // and autofocus instead of hiding/removing the connection.
    officialInput: { backgroundColor: 'transparent', color: 'transparent', opacity: 0, fontSize: pixel(m.caption) },
    title: { color: c.text, fontSize: pixel(m.title) },
    hint: { color: c.muted, fontSize: pixel(m.body), marginTop: pixel(m.gap) },
    failure: { backgroundColor: c.background, paddingLeft: pixel(m.inset), paddingTop: pixel(m.top) },
    retry: { left: pixel(m.inset), bottom: pixel(m.bottom), width: pixel(m.retryWidth), height: pixel(m.controlHeight), borderRadius: pixel(m.radius), backgroundColor: c.text },
    leave: { left: pixel(m.leaveX), bottom: pixel(m.bottom), width: pixel(m.leaveWidth), height: pixel(m.controlHeight), borderRadius: pixel(m.radius), backgroundColor: c.surface },
    primaryText: { color: c.primaryText, fontSize: pixel(m.body) }, text: { color: c.text, fontSize: pixel(m.body) }
  }
}
export default {
  data() { return { appName: appConfig.name, errorText: '', running: false, visible: true, keyboardOpen: false, nativeReady: false, startupText: '正在打开…', nativeAreaStyle: {}, pageStyle: {}, appearanceStyles: resolvedAppearance(startupColors, startupDesign), appearanceColors: startupColors, officialControlKey: '', officialInputHandlers: {}, officialInline: false, officialHoleStyle: {}, officialInitialText: '', officialSingleLine: false, officialSensitive: false, officialInputType: 'ZhCNPreferred', officialLimit: 4096, officialInputStyle: {} } },
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
      this.syncWindowFocus(true)
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
      this._windowFocused = true; this._windowSequence = 0; this._focusNextCheck = 0; this._focusPending = false; this._overlayStatus = ''
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
        this.send('init', { ocrCapabilities: this.ocrCapabilities() })
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
      try {
        this.syncWindowFocus()
        this.syncInputFocus()
        this.checkOfficialKeyboard()
        for (const event of launcher.poll() || []) {
          if (event.generation !== undefined && event.generation !== this._generation && event.type !== 'booted' && event.type !== 'startup' && event.type !== 'fatal' && event.type !== 'exit') continue
          if (event.type === 'booted') {
            const screen = event.screen
            if (!screen || !Number.isInteger(screen.width) || !Number.isInteger(screen.height) || screen.width <= 0 || screen.height <= 0)
              throw new Error('Invalid Falcon display configuration')
            this._screen = screen
            this.nativeAreaStyle = { left: '0px', top: '0px', width: screen.width + 'px', height: screen.height + 'px' }
            this.updatePageStyle()
            this.send('display.area', { x: 0, y: 0, width: screen.width, height: screen.height })
          }
          else if (event.type === 'appearance') this.applyAppearance(event)
          else if (event.type === 'startup') { this.startupText = ({ display: '正在准备显示…', data: '正在读取数据…', interface: '正在准备页面…', frame: '正在显示页面…' })[event.stage] || '正在打开…' }
          else if (event.type === 'ready') { this.nativeReady = true; this.syncWindowFocus(true); this.syncInputFocus(true); clearTimeout(this._readyTimer); this._readyTimer = null; if (this._pendingUpdateResult) { this.send('update.install.result', this._pendingUpdateResult); this._pendingUpdateResult = null } }
          else if (event.type === 'editor.state') this.editorState(event)
          else if (event.type === 'navigation.state') this.navigationState(event)
          else if (event.type === 'keyboard.open') this.openKeyboard(event)
          else if (event.type === 'keyboard.commit-action') this.commitOfficialAction(event)
          else if (event.type === 'keyboard.field') this.inlineOfficialField(event)
          else if (event.type === 'ocr.prepare') this.startOcr(event, true)
          else if (event.type === 'ocr.pen') { if (this._ocr) this._ocr.pen(event) }
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
      this._focusListener = () => { this.syncWindowFocus(true); this.syncInputFocus(true) }
      try { am.on('topApp', this._focusListener) } catch (_) {}
      try { am.on('windowAttachState', this._focusListener) } catch (_) {}
    },
    syncInputFocus(force = false) {
      if (!this.running) return
      let active = false
      try { active = this.nativeReady && this.visible && !this._disposed && !this.keyboardOpen && this._windowFocused !== false && am.hasWindowFocus() === true } catch (_) {}
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
      this._focusPending = !this.send('window.focus', { focused, sequence: ++this._windowSequence })
      this.syncInputFocus(true)
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
    ocrCapabilities() {
      try {
        if (!this._global) this._global = new globalModule.Global()
        return OcrSession.capabilities(this._global)
      } catch (_) { return { single: false, multi: false } }
    },
    startOcr(event, prepareOnly = false) {
      const failed = error => this.send(prepareOnly ? 'ocr.prepared' : 'ocr.result', {
        requestId: event.requestId, session: event.session, mode: event.mode,
        capture: 1, ok: false, confirmed: false, terminal: true, error
      })
      if (!this.visible || this._windowFocused === false || this.keyboardOpen) { failed('当前无法启动扫描，请返回输入框重试。'); return }
      try {
      if (!this._global) this._global = new globalModule.Global()
      const capabilities = OcrSession.capabilities(this._global)
      this.send('ocr.capabilities', Object.assign({ requestId: event.requestId, session: event.session }, capabilities))
      if (event.mode === 'multi' && !capabilities.multi) {
        // A portrait replacement can still hold the previous registration.
        // Bind cleanup retries to this request before reporting its rejection.
        this._ocrRequest = event.requestId
        const released = this.stopOcr()
        failed(released ? '当前固件未提供多行扫描接口，请使用单行扫描。' : '系统扫描资源尚未释放，请重试。')
        return
      }
      if (this._ocr && this._ocr.pendingCleanup && this.stopOcr() === false) {
        failed('系统扫描资源尚未释放，请重试。')
        return
      }
      const generation = this._generation
      this._ocrRequest = event.requestId
      if (!this._ocr) this._ocr = new OcrSession(this._global, appConfig.appid, (type, data) => {
        if (this._disposed || generation !== this._generation || data.requestId !== this._ocrRequest) return
        this.send(type, data)
      }, (down, sequence) => typeof launcher.ocrStroke === 'function' && launcher.ocrStroke(down, sequence),
      active => typeof launcher.ocrRoute === 'function' && launcher.ocrRoute(active))
      this._ocr.start(event.requestId, { session: event.session, mode: event.mode, continuous: event.continuous, nativePen: event.nativePen === true, prepareOnly })
      } catch (err) {
        this.stopOcr()
        failed('系统 OCR 启动失败，请重试或手动输入。')
      }
    },
    stopOcr(requestId, acknowledge = false) {
      let ok = true
      if (!requestId || requestId === this._ocrRequest || (this._ocr && this._ocr.pendingCleanup && this._ocr.pendingCleanup.requestId === requestId)) {
        if (this._ocr) ok = this._ocr.stop() !== false
        if (ok) { this._ocr = null; this._ocrRequest = null }
      }
      // Already-stopped old IDs are acknowledged without touching a newer lease.
      if (acknowledge) this.send('ocr.stopped', { requestId, ok })
      return ok
    },
    closeKeyboardState(closePanel = true) {
      clearTimeout(this._officialCommitTimer); this._officialCommitTimer = null; this._officialAction = null
      clearTimeout(this._keyboardTimer); this._keyboardTimer = null
      clearInterval(this._keyboardMonitor); this._keyboardMonitor = null
      this._officialControlToken = null; this.officialInline = false; this.officialInitialText = ''; this.officialText = ''; this._officialOwner = ''; this._officialFontSize = 0
      this.keyboardOpen = false
      this.updatePageStyle()
      this._keyboardUuid = ''; this._keyboardRequest = ''; this._keyboardWatchAt = 0; this._keyboardCompleting = false; this._officialOpenConfig = null
      try { if (closePanel && typeof launcher.closeOfficialKeyboard === 'function') launcher.closeOfficialKeyboard() } catch (_) {}
    },
    cancelOfficialKeyboard(error = '') {
      if (!this.keyboardOpen) return
      const requestId = this._keyboardRequest, uuid = this._keyboardUuid
      // Clear identity before closing: Falcon may emit its cancellation inline.
      this.closeKeyboardState()
      try { if (this._global && uuid && this._global.closeTextEdit) this._global.closeTextEdit(uuid) } catch (_) {}
      this.send('keyboard.result', { requestId, confirmed: false, text: '', error })
    },
    inlineOfficialForeground(state) {
      if (!this.visible || this._disposed) return false
      try {
        const overlay = typeof launcher.systemOverlay === 'function' ? launcher.systemOverlay() : null
        if (overlay && overlay.supported && overlay.visible) return false
        if (!overlay || !overlay.supported || !am || typeof am.getTopApp !== 'function')
          return state.foreground === true && this._windowFocused !== false
        const top = am.getTopApp()
        if (typeof top !== 'string' || !top) return false
        // A system IME may own the top window while our page remains shown.
        // Remember only the first IME owner during this request's opening window.
        if (top !== appConfig.appid && !this._officialOwner && Date.now() <= this._officialDeadline)
          this._officialOwner = top
        return top === appConfig.appid || top === this._officialOwner
      } catch (_) { return false }
    },
    checkOfficialKeyboard() {
      if (!this.keyboardOpen || this._keyboardCompleting || !this._keyboardWatchAt || Date.now() < this._keyboardWatchAt) return
      this._keyboardWatchAt = Date.now() + 200
      try {
        const state = launcher.officialKeyboard()
        if (this.officialInline) {
          if (state && state.supported && state.visible) {
            // Invalid geometry must not renew the native top-region lease.
            // Falcon retains the configured full-screen input coordinates.
            if (!this._screen || !Number.isInteger(state.height) || state.height <= 0 || state.height >= this._screen.height - 100) {
              const now = Date.now()
              if ((!this._officialSeen && now >= this._officialDeadline) ||
                  (this._officialSeen && now - this._officialValidLayoutAt >= 750))
                this.cancelOfficialKeyboard('官方键盘高度无效，已恢复内置键盘')
              return
            }
            const height = this._screen.height - state.height
            if (this.officialHoleStyle.height !== height + 'px')
              this.officialHoleStyle = Object.assign({}, this.nativeAreaStyle, { height: height + 'px' })
            this._officialValidLayoutAt = Date.now()
            this._officialSeen = true
            // Register the vendor's confirmation result only after the proxy
            // owns a real InputConnection. Do not supply or rewrite its text.
            if (!this._keyboardUuid && this._officialOpenConfig) {
              const uuid = this._global.startTextEdit(this._officialOpenConfig)
              if (typeof uuid !== 'string' || !uuid) throw new Error('official request rejected')
              this._keyboardUuid = uuid
            }
            this.send('keyboard.layout', { requestId: this._keyboardRequest, height: state.height, foreground: this.inlineOfficialForeground(state) })
            const input = this.$refs && this.$refs.officialInput
            const requestId = this._keyboardRequest, generation = this._generation
            if (input && input.getCursorPosition && !this._officialCursorPending) {
              this._officialCursorPending = true
              input.getCursorPosition(position => {
                if (!this.keyboardOpen || !this.officialInline || requestId !== this._keyboardRequest || generation !== this._generation) return
                this._officialCursorPending = false
                const cursor = Number.isInteger(position) ? position : position && position.cursorPosition
                if (Number.isInteger(cursor)) this.send('keyboard.change', { requestId, text: this.officialText, cursorIndex: cursor })
              })
            }
          } else if (this._officialSeen || Date.now() >= this._officialDeadline) {
            this.cancelOfficialKeyboard(this._officialSeen ? '' : '官方键盘未能显示，已恢复内置键盘')
          }
        } else if (!state || !state.supported || !state.visible)
          this.cancelOfficialKeyboard('官方键盘未能显示，已恢复内置键盘')
      } catch (_) { this.cancelOfficialKeyboard('官方键盘状态异常，已恢复内置键盘') }
    },
    updatePageStyle() {
      // Native's independent top-region plane paints the retained official page.
      // Punch a hole only above the IME; keep opaque backing beneath its panel: its floating preedit
      // extends outside the keyboard bounds and must be erased on selection.
      const page = Object.assign({}, this.nativeAreaStyle, this.appearanceStyles.page || {})
      if (Object.keys(page).some(key => page[key] !== this.pageStyle[key]) || Object.keys(page).length !== Object.keys(this.pageStyle).length) this.pageStyle = page
    },
    applyAppearance(event) {
      if (!event || !['dark', 'light'].includes(event.mode)) return
      const c = event.colors || {}, m = event.metrics || {}
      const colors = ['background', 'text', 'muted', 'surface', 'primaryText', 'caret']
      const sizes = ['title', 'body', 'caption', 'gap', 'inset', 'top', 'bottom', 'retryWidth', 'leaveX', 'leaveWidth', 'controlHeight', 'radius']
      if (!colors.every(k => typeof c[k] === 'string' && /^#[0-9a-fA-F]{6}$/.test(c[k])) ||
          !sizes.every(k => Number.isInteger(m[k]) && m[k] >= 0 && m[k] <= 4096)) return
      this.appearanceColors = c
      this.appearanceStyles = resolvedAppearance(c, m)
      this.updatePageStyle()
      if (this.officialInline) {
        const style = Object.assign({}, this.officialInputStyle, this.appearanceStyles.officialInput)
        if (this._officialFontSize) style.fontSize = this._officialFontSize + 'px'
        if (Object.keys(style).some(key => style[key] !== this.officialInputStyle[key])) this.officialInputStyle = style
      }
    },
    inlineOfficialField(field) {
      if (!this.officialInline || field.requestId !== this._keyboardRequest) return
      if (![field.x, field.y, field.width, field.height].every(Number.isFinite) || field.width <= 0 || field.height <= 0) return
      // Native supplies the focused field's resolved pixel size, including UI/text
      // scaling. Caption metrics cannot represent Composer/FieldValue/Button.
      if (Number.isFinite(field.fontSize) && field.fontSize > 0 && field.fontSize <= 4096) this._officialFontSize = field.fontSize
      const style = Object.assign({}, this.appearanceStyles.officialInput || {}, { left: field.x + 'px', top: field.y + 'px', width: field.width + 'px', height: field.height + 'px' })
      if (this._officialFontSize) style.fontSize = this._officialFontSize + 'px'
      // Layout and cursor reports may echo unchanged field bounds. Keep the
      // Falcon input's style identity while its InputConnection is active.
      if (Object.keys(style).some(key => style[key] !== this.officialInputStyle[key])) this.officialInputStyle = style
    },
    openInlineOfficial(event) {
      // Portrait uses a transparent Falcon proxy to retain the real system
      // InputConnection. The retained LVGL field paints text and caret, while
      // Falcon alone owns IME editing and touch/cursor input in these bounds.
      // Do not also enable native touch: that would create two caret owners.
      // Falcon owns the live buffer. Its value property seeds a new control;
      // feeding @input back through that property can replace the active text
      // and relayout the system InputConnection on every edit. Keep the live
      // result outside Vue data and leave the seed fixed until this edit closes.
      this._officialInputTime = 0
      this.officialText = event.text || ''
      this.officialInitialText = this.officialText
      this.officialSingleLine = !event.multiline
      this.officialSensitive = event.sensitive === true
      this.officialInputType = event.inputType || 'ZhCNPreferred'
      this.officialLimit = event.maxlength || 4096
      const token = {}, ownerRequest = this._keyboardRequest, ownerGeneration = this._generation
      this._officialControlToken = token
      this.officialControlKey = ownerGeneration + ':' + ownerRequest
      const ownsControl = () => this.keyboardOpen && this.officialInline && this._officialControlToken === token && this._keyboardRequest === ownerRequest && this._generation === ownerGeneration
      this.officialInputHandlers = {
        input: value => { if (ownsControl()) this.inlineOfficialInput(value) },
        confirm: value => { if (ownsControl()) this.finishInlineOfficial(value) }
      }
      this._officialFontSize = 0
      this.officialHoleStyle = Object.assign({}, this.nativeAreaStyle, { height: '0px' })
      this.officialInline = true
      this.updatePageStyle()
      this.inlineOfficialField(Object.assign({ requestId: this._keyboardRequest }, event.field || {}))
      const requestId = this._keyboardRequest, generation = this._generation
      this.$nextTick(() => {
        if (!this.keyboardOpen || this._keyboardRequest !== requestId || generation !== this._generation) return
        const input = this.$refs && this.$refs.officialInput
        if (input && input.setCursorPosition) input.setCursorPosition(event.cursorIndex || 0)
      })
      this._officialSeen = false; this._officialValidLayoutAt = 0; this._officialCursorPending = false; this._officialOwner = ''
      this._officialDeadline = Date.now() + 3000
      this._keyboardWatchAt = Date.now() + 100
      this._keyboardMonitor = setInterval(() => this.checkOfficialKeyboard(), 200)
    },
    inlineOfficialInput(value) {
      if (!this.keyboardOpen || !this.officialInline) return
      if (typeof value === 'string') this.officialText = value
      else if (value && typeof value.value === 'string') this.officialText = value.value
      else if (value && typeof value.text === 'string') this.officialText = value.text
      const cursorIndex = value && Number.isInteger(value.cursorPosition) ? value.cursorPosition : this.officialText.length
      const change = { requestId: this._keyboardRequest, text: this.officialText, cursorIndex }
      if (typeof value === 'string' || (value && (typeof value.value === 'string' || typeof value.text === 'string'))) change.inputEventTime = this._officialInputTime = Date.now()
      this.send('keyboard.change', change)
    },
    commitOfficialAction(event) {
      if (!this.keyboardOpen || !this.officialInline || this._keyboardCompleting || this._officialAction || event.requestId !== this._keyboardRequest ||
          typeof event.actionId !== 'string' || !event.actionId || !['attachments', 'send', 'effort'].includes(event.action)) return
      const requestId = this._keyboardRequest, generation = this._generation
      try {
        const state = launcher.officialKeyboard()
        if (!state || !state.visible || !this.inlineOfficialForeground(state)) {
          this.send('keyboard.action.error', { requestId, actionId: event.actionId }); return
        }
      } catch (_) { this.send('keyboard.action.error', { requestId, actionId: event.actionId }); return }
      const input = this.$refs && this.$refs.officialInput
      this._officialAction = event
      const failed = (message = '未能确认官方输入，请重试') => {
        if (!this.keyboardOpen || this._keyboardRequest !== requestId || this._generation !== generation || !this._officialAction || this._officialAction.actionId !== event.actionId) return
        clearTimeout(this._officialCommitTimer); this._officialCommitTimer = null; this._officialAction = null
        this.send('keyboard.action.error', { requestId, actionId: event.actionId })
        try { if (this._global && this._global.showToast) this._global.showToast(message) } catch (_) {}
      }
      if (event.uncommitted && !(Number.isFinite(event.keyEventTime) && this._officialInputTime >= event.keyEventTime)) { failed('请先完成选词或确认官方输入，再点击操作'); return }
      this._officialCommitTimer = setTimeout(failed, 1500)
      try {
        if (!input || !input.commitComplete) { failed(); return }
        input.commitComplete()
      } catch (_) { failed() }
    },
    finishInlineOfficial(value) {
      if (!this.keyboardOpen || !this.officialInline || this._keyboardCompleting) return
      this.inlineOfficialInput(value)
      this._keyboardCompleting = true
      const requestId = this._keyboardRequest, generation = this._generation
      const actionId = this._officialAction && this._officialAction.actionId
      clearTimeout(this._officialCommitTimer); this._officialCommitTimer = null
      // Finish after the system IME's confirm callback has unwound.
      this._keyboardTimer = setTimeout(() => {
        this._keyboardCompleting = false
        if (!this.keyboardOpen || this._keyboardRequest !== requestId || this._generation !== generation) return
        const text = this.officialText
        this.closeKeyboardState()
        const result = { requestId, confirmed: true, text }
        if (actionId) result.actionId = actionId
        this.send('keyboard.result', result)
      }, 0)
    },
    openKeyboard(event) {
      if (this.keyboardOpen || !this.visible || this._windowFocused === false) { this.send('keyboard.result', { requestId: event.requestId, confirmed: false, text: '' }); return }
      this.setNativeEditorPolicy(false)
      this.stopOcr()
      this.keyboardOpen = true
      this.syncInputFocus(true)
      this.setBackPolicy(false)
      this._keyboardRequest = event.requestId
      const generation = this._generation
      try {
        if (!this._global) this._global = new globalModule.Global()
        if (!this._editFinished) {
          this._editFinished = (uuid, json) => {
            const data = parse(json === undefined ? uuid : json) || {}
            const callbackUuid = json === undefined ? this._keyboardUuid : uuid
            if (!this.keyboardOpen || String(callbackUuid) !== String(this._keyboardUuid)) return
            const requestId = this._keyboardRequest
            if (this.officialInline) {
              if (data.editConfirmed === true || data.editConfirmed === 'true') this.finishInlineOfficial()
              else {
                this._keyboardCompleting = true
                this._keyboardTimer = setTimeout(() => {
                  this._keyboardCompleting = false
                  if (this.keyboardOpen && this._keyboardRequest === requestId) this.cancelOfficialKeyboard()
                }, 0)
              }
              return
            }
            // The official confirm/cancel handler owns its window teardown.
            // Hiding it reentrantly here can destroy the executing IME page.
            this.closeKeyboardState(false)
            this.send('keyboard.result', { requestId, confirmed: data.editConfirmed === true || data.editConfirmed === 'true', text: String(data.text !== undefined ? data.text : data.content || ''), cursorIndex: data.cursorIndex })
          }
          this._global.textEditFinished.on(this._editFinished)
        }
        if (event.inline !== false && this._screen && this._screen.height > this._screen.width) {
          this._officialOpenConfig = JSON.stringify({ inputType: event.inputType || 'ZhCNPreferred', confirmButtonDisabledOnTextEmpty: false, enterButtonText: '确认' })
          this.openInlineOfficial(event); return
        }
        const start = () => {
          this._keyboardTimer = null
          if (!this.keyboardOpen || this._keyboardRequest !== event.requestId || this._generation !== generation || this._disposed) return
          try {
            const uuid = this._global.startTextEdit(JSON.stringify({ text: event.text || '', placeholder: event.placeholder || '请输入', inputType: event.inputType || 'ZhCNPreferred', maxlength: event.maxlength || 4096, autofocus: true, showCursor: true, multiLinesEditVisible: event.multiline === true, cursorIndex: event.cursorIndex || 0, confirmButtonDisabledOnTextEmpty: false, enterButtonText: '确认' }))
            if (typeof uuid !== 'string' || !uuid) throw new Error('official editor rejected request')
            this._keyboardUuid = uuid
            this._keyboardWatchAt = typeof launcher.officialKeyboard === 'function' ? Date.now() + 2000 : 0
          } catch (_) { this.cancelOfficialKeyboard('未能打开官方键盘，输入已保留') }
        }
        start()
      } catch (_) { this.cancelOfficialKeyboard('未能打开官方键盘，输入已保留') }
    },
    fail(message) {
      this._nativeImage = false
      this.setBackPolicy(true)
      this.setNativeEditorPolicy(false)
      this.stopOcr()
      this.cancelOfficialKeyboard()
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
      this.cancelOfficialKeyboard()
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
.native-page { width: 100vw; height: 100vh; position: relative; }
.native-hole { position: absolute; left: 0px; top: 0px; width: 100%; height: 100%; }
.official-editor { position: absolute; left: 0px; top: 0px; width: 100%; height: 100%; }
.official-input { position: absolute; padding: 0px; }
.startup-panel { position: absolute; left: 0px; top: 0px; width: 100%; height: 100%; justify-content: center; align-items: center; }
.startup-title { }
.startup-text { }
.failure-panel { position: absolute; left: 0px; top: 0px; width: 100%; height: 100%; }
.failure-title { }
.failure-text { }
.retry { position: absolute; justify-content: center; align-items: center; }
.leave { position: absolute; justify-content: center; align-items: center; }
.retry-label { }
.retry-text { }
</style>
