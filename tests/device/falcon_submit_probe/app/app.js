import { probe } from 'penchatSubmitProbe'
import { BasePage } from './platform/falcon/BasePage'
class App extends $falcon.App {
  onLaunch(options) {
    super.onLaunch(options)
    const screen = probe.geometry()
    if (!screen || screen.error) throw new Error(screen && screen.error || 'No Falcon geometry')
    this.probeScreen = screen
    this.setViewPort(screen.width)
    $falcon.useDefaultBasePageClass(BasePage)
  }
}
export default App
