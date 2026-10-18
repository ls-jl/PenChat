// Lifecycle adapter for the thin launcher. Business pages live in the native process.
export class BasePage extends $falcon.Page {
  notifyRoot(name, options) {
    const root = this.$root
    if (!root) return
    const hook = typeof root[name] === 'function' ? root[name] : root.$options && root.$options[name]
    if (typeof hook === 'function') return hook.call(root, options)
  }
  notifyVisibility(visible) {
    if (!this.$root) return
    if (this._visibleRoot === this.$root && this._rootVisible === visible) return
    this._visibleRoot = this.$root
    this._rootVisible = visible
    this.notifyRoot(visible ? 'onShow' : 'onHide')
  }
  onNewOptions(options) {
    super.onNewOptions(options)
    this.notifyRoot('onNewOptions', options)
  }
  onShow() { super.onShow(); this.notifyVisibility(true) }
  onStart(options) { if (super.onStart) super.onStart(options); this.notifyVisibility(true) }
  onHide() { super.onHide(); this.notifyVisibility(false) }
  onStop() { if (super.onStop) super.onStop(); this.notifyVisibility(false) }
  onUnload() {
    try { this.notifyRoot('onUnload') } finally { this._visibleRoot = null; this._rootVisible = false; super.onUnload() }
  }
}
