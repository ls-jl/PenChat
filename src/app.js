import { BasePage } from './platform/falcon/BasePage'
class App extends $falcon.App {
  onLaunch(options) {
    super.onLaunch(options)
    // Native coordinates match the full-size hole; no design viewport scaling.
    $falcon.useDefaultBasePageClass(BasePage)
  }
}
export default App
