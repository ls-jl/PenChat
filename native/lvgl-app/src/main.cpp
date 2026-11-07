#include "app.hpp"
#include "device_profile.h"
#include <csignal>
#include <fcntl.h>
#include <iostream>
#include <unistd.h>
#ifdef PENCHAT_CVI_PLATFORM
#include <ucontext.h>
#endif
namespace {
volatile sig_atomic_t running = 1, snapshotRequested = 0;
void stop(int) { running = 0; }
#ifdef PENCHAT_CVI_PLATFORM
void mediaFault(int signal,siginfo_t *,void *context) {
  // Address-only diagnostic, no heap allocation or application content.
  auto *cpu=static_cast<ucontext_t *>(context);
  char line[]="S6_NATIVE_FAULT pc=00000000 lr=00000000\n";
  const unsigned long values[2]={cpu->uc_mcontext.arm_pc,cpu->uc_mcontext.arm_lr};
  for(int n=0;n<2;++n)for(int i=0;i<8;++i)
    line[(n?31:19)+i]="0123456789abcdef"[(values[n]>>(28-4*i))&15];
  ::write(STDERR_FILENO,line,sizeof(line)-1);
  _exit(128+signal);
}
#endif
void snapshot(int) { snapshotRequested = 2; }
void diagnostic(int) {
  if (!snapshotRequested)
    snapshotRequested = 1;
}
} // namespace
int main(int argc, char **argv) {
  using namespace penchat;
  std::string data, workspace, fontPath, resources, screenshot, demo,
      screenConfig = "/etc/miniapp/resources/cfg.json";
  int fd = -1, frames = 0;
  bool headless = false;
  std::unique_ptr<Bridge> bridgeOwner;
  try {
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    auto value = [&]() {
      if (++i >= argc)
        throw std::runtime_error("Missing argument");
      return std::string(argv[i]);
    };
    if (arg == "--bridge-fd")
      fd = std::stoi(value());
    else if (arg == "--data-dir")
      data = value();
    else if (arg == "--workspace")
      workspace = value();
    else if (arg == "--font")
      fontPath = value();
    else if (arg == "--tex-resources")
      resources = value();
    else if (arg == "--headless")
      headless = true;
    else if (arg == "--screen-config")
      screenConfig = value();
    else if (arg == "--screenshot")
      screenshot = value();
    else if (arg == "--demo-markdown")
      demo = value();
    else if (arg == "--frames")
      frames = std::stoi(value());
  }
    if (data.empty())
      throw std::runtime_error("Missing data directory");
    ensureDirectory(data);
    beginDiagnostics(data);
    if (workspace.empty())
      workspace = ".";
    if (fontPath.empty())
      fontPath = "/etc/miniapp/resources/fonts/HarmonyOS_Sans_SC_Regular.ttf";
    if (resources.empty())
      resources = join(workspace, "assets/microtex");
#ifdef PENCHAT_CVI_PLATFORM
    struct sigaction fault{};fault.sa_sigaction=mediaFault;fault.sa_flags=SA_SIGINFO;
    sigemptyset(&fault.sa_mask);sigaction(SIGSEGV,&fault,nullptr);sigaction(SIGBUS,&fault,nullptr);
#endif
    signal(SIGUSR1, snapshot);
    signal(SIGUSR2, diagnostic);
    signal(SIGTERM, stop);
    signal(SIGINT, stop);
    signal(SIGPIPE, SIG_IGN);
    const auto configuration = fileText(screenConfig, 64 * 1024);
    if (configuration.empty())
      throw std::runtime_error("Cannot read Falcon screen configuration: " + screenConfig);
    const auto parsedConfiguration = Json::parse(configuration);
    const auto capabilities=camera::deviceCapabilities();
    if(!headless && *capabilities.requiredBoard && fileText("/proc/device-tree/model",4096).find(capabilities.requiredBoard)==std::string::npos)
      throw std::runtime_error("This native build does not support the detected device");
    const bool physicalPanel=capabilities.physicalScreenConfig;
    const auto screen = Screen::fromConfig(parsedConfiguration,physicalPanel);
    const bool standalone = headless && fd < 0;
    bridgeOwner.reset(new Bridge(fd)); fd=-1;
    auto &bridge=*bridgeOwner;
    const auto startupAt=ticks();
    auto startup=[&](const char *stage) {
      const auto elapsed=ticks()-startupAt;
      std::fprintf(stderr,"NATIVE_STARTUP stage=%s elapsed_ms=%lld\n",stage,static_cast<long long>(elapsed));
      bridge.send("startup",{{"stage",stage},{"elapsed_ms",elapsed}});
    };
    // Give Falcon its real viewport before loading fonts, media or the DB. Its
    // startup cover stays visible until the first native frame is submitted.
    bridge.send("booted", {{"screen", screen.descriptor()},{"version",PENCHAT_VERSION},{"appid",PENCHAT_APP_ID}});
    startup("display");
    lv_init();
    lv_tick_set_cb([]() -> uint32_t { return uint32_t(ticks()); });
    initializeFonts(fontPath, join(workspace, "assets/fonts/DejaVuSans.ttf"));
    struct FontCleanup { ~FontCleanup() { lv_deinit(); shutdownFonts(); } } fontCleanup;
    if(!headless&&capabilities.sharedCaptureDisplay)ScanService::recoverHardware(data);
    Display display(screen, headless);
    Input input;
    startup("data");
    Database db(join(data, "penchat.db"));
    startup("interface");
    App app(db, bridge, display, input, screen, workspace, data, fontPath,
            resources, !headless, parsedConfiguration);
    if (!demo.empty())
      app.demo(fileText(demo));
    startup("frame");
    int frame = 0;
    bool firstFrameReported=false;
    while (running && (standalone || bridge.connected())) {
      for (const auto &m : bridge.receive()) {
        if (m.value("type", std::string()) == "exit")
          running = 0;
        app.handle(m);
      }
      // A closed launcher must not dispatch one last batch of physical touches.
      if (!running || (!standalone && !bridge.connected())) break;
      app.tick();
      if(app.updateExit())break;
      const auto nextTimer = lv_timer_handler();
      display.check();
      if(!firstFrameReported&&display.presented()) {
        firstFrameReported=true;
        std::fprintf(stderr,"NATIVE_STARTUP stage=presented elapsed_ms=%lld\n",static_cast<long long>(ticks()-startupAt));
      }
      if (snapshotRequested) {
        const bool includeImage = snapshotRequested == 2;
        snapshotRequested = 0;
        if (includeImage)
          display.screenshot(join(data, "lvgl-diagnostic.png"));
        atomicText(join(data, "lvgl-diagnostic.json"),
                   app.diagnostics().dump(2));
      }
      if (frames && ++frame >= frames)
        break;
      const auto pause = (app.interactive()||display.pending()) ? std::min<uint32_t>(16,nextTimer) : 50;
      app.waitInput(std::max<uint32_t>(1,pause));
    }
    if (!screenshot.empty())
      display.screenshot(screenshot);
    app.shutdown();
    display.visible(false);
    return 0;
  } catch (const std::exception &e) {
    diagnosticRecord("lifecycle", "startup_error"); flushDiagnostics();
    std::cerr << "penchat-lvgl: " << e.what() << std::endl;
    if(bridgeOwner) bridgeOwner->send("fatal", {{"error", e.what()}});
    else if (fd >= 0) {
      Bridge bridge(fd);
      bridge.send("fatal", {{"error", e.what()}});
    }
    return 1;
  }
}
