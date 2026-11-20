#include "app.hpp"
#include "ui_scale_settings.hpp"
#include "device_profile.h"
#include "../../platform/identity.hpp"
#include <csignal>
#include <fcntl.h>
#include <iostream>
#include <unistd.h>
#if defined(__aarch64__)
#include <ucontext.h>
#endif
#ifdef PENCHAT_CVI_PLATFORM
#include <ucontext.h>
#include "surface.hpp"
#endif
namespace {
volatile sig_atomic_t running = 1, snapshotRequested = 0, terminationSignal = 0;
int faultRecordFd = -1;
void recordFault(int signal) {
  char line[]="signal=00\n";
  line[7]='0'+(signal/10)%10;line[8]='0'+signal%10;
  if(faultRecordFd>=0)::write(faultRecordFd,line,sizeof(line)-1);
}
void stop(int signal) { terminationSignal=signal;running = 0; }
#ifdef PENCHAT_CVI_PLATFORM
void mediaFault(int signal,siginfo_t *,void *context) {
  recordFault(signal);
  // Address-only diagnostic, no heap allocation or application content.
  auto *cpu=static_cast<ucontext_t *>(context);
  char line[]="S6_NATIVE_FAULT pc=00000000 lr=00000000\n";
  const unsigned long values[2]={cpu->uc_mcontext.arm_pc,cpu->uc_mcontext.arm_lr};
  for(int n=0;n<2;++n)for(int i=0;i<8;++i)
    line[(n?31:19)+i]="0123456789abcdef"[(values[n]>>(28-4*i))&15];
  ::write(STDERR_FILENO,line,sizeof(line)-1);
  _exit(128+signal);
}
#elif defined(__aarch64__)
void mediaFault(int signal,siginfo_t *,void *context) {
  recordFault(signal);
  auto *cpu=static_cast<ucontext_t *>(context);
  char line[]="RK_NATIVE_FAULT pc=0000000000000000 lr=0000000000000000\n";
  const unsigned long values[2]={cpu->uc_mcontext.pc,cpu->uc_mcontext.regs[30]};
  char *digits=line;
  for(int n=0;n<2;++n) {
    while(*digits&&*digits!='=')++digits;++digits;
    for(int i=0;i<16;++i)digits[i]="0123456789abcdef"[(values[n]>>(60-4*i))&15];
    digits+=16;
  }
  ::write(STDERR_FILENO,line,sizeof(line)-1);_exit(128+signal);
}
#else
void mediaFault(int signal,siginfo_t *,void *) { recordFault(signal);_exit(128+signal); }
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
  UiMetrics uiMetrics;
#ifndef PENCHAT_NATIVE_DEVICE
  bool uiScaleSpecified = false;
#endif
#ifdef PENCHAT_CVI_PLATFORM
  int compositorFd = -1, compositorMemory = -1;
#endif
  bool headless = false,enteredLoop=false;
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
#ifdef PENCHAT_CVI_PLATFORM
    else if (arg == "--compositor-fd") compositorFd = std::stoi(value());
    else if (arg == "--compositor-memory") compositorMemory = std::stoi(value());
#endif
    else if (arg == "--data-dir")
      data = value();
    else if (arg == "--workspace")
      workspace = value();
    else if (arg == "--font")
      fontPath = value();
    else if (arg == "--tex-resources")
      resources = value();
    else if (arg == "--ui-scale-percent") {
#ifdef PENCHAT_NATIVE_DEVICE
      throw std::runtime_error("UI scaling is only available in host headless tests");
#else
      const auto raw = value();
      size_t consumed = 0;
      const int percent = std::stoi(raw, &consumed);
      if (consumed != raw.size() || percent < 75 || percent > 150)
        throw std::runtime_error("UI scale must be an integer between 75 and 150");
      uiMetrics = UiMetrics(100, percent);
      uiScaleSpecified = true;
#endif
    }
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
#ifndef PENCHAT_NATIVE_DEVICE
    if (uiScaleSpecified && !headless)
      throw std::runtime_error("UI scaling requires host headless mode");
#endif
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
    faultRecordFd=open(join(data,"native-fault.txt").c_str(),O_WRONLY|O_CREAT|O_TRUNC|O_CLOEXEC|O_NOFOLLOW,0600);
    if(faultRecordFd>=0) {
      const auto header="pid="+std::to_string(getpid())+"\n";
      ::write(faultRecordFd,header.data(),header.size());
    }
    struct sigaction fault{};fault.sa_sigaction=mediaFault;fault.sa_flags=SA_SIGINFO;
    sigemptyset(&fault.sa_mask);sigaction(SIGSEGV,&fault,nullptr);sigaction(SIGBUS,&fault,nullptr);
    sigaction(SIGABRT,&fault,nullptr);sigaction(SIGILL,&fault,nullptr);sigaction(SIGFPE,&fault,nullptr);
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
#ifdef PENCHAT_CVI_PLATFORM
    const auto hardware=platform::identify(fileText("/proc/device-tree/model",4096),fileText("/proc/device-tree/compatible",4096));
    if(!headless&&hardware.family!=platform::Family::Cvi)
      throw std::runtime_error("CVI native build requires a verified CVI hardware identity");
#endif
    const bool physicalPanel=capabilities.physicalScreenConfig;
    const auto screen = Screen::fromConfig(parsedConfiguration,physicalPanel);
    const bool standalone = headless && fd < 0;
    bridgeOwner.reset(new Bridge(fd)); fd=-1;
    auto &bridge=*bridgeOwner;
    const auto startupAt=ticks();
    auto startup=[&](const char *stage,Json details=Json::object()) {
      const auto elapsed=ticks()-startupAt;
      std::fprintf(stderr,"NATIVE_STARTUP stage=%s elapsed_ms=%lld\n",stage,static_cast<long long>(elapsed));
      details["stage"]=stage;details["elapsed_ms"]=elapsed;bridge.send("startup",details);
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
#ifdef PENCHAT_CVI_PLATFORM
    if(!headless)penchat_compositor::configureDisplay(compositorFd, compositorMemory);
#endif
    Display display(screen, headless);
    Input input;
    const auto displayInfo=display.information();
    std::fprintf(stderr,"DISPLAY_PROBE %s\n",displayInfo.dump().c_str());
    startup("data",{{"display",displayInfo}});
    BackupRestore::recover(data,BackupRestore::publicDirectory(data));
    Database db(join(data, "penchat.db"));
#ifndef PENCHAT_NATIVE_DEVICE
    if (!uiScaleSpecified)
#endif
      uiMetrics = UiMetrics(100, savedUiScale(db.settings()));
    uiMetrics = UiMetrics(uiMetrics.densityPercent(), uiMetrics.scalePercent(), savedTextScale(db.settings()));
    const auto appearance=db.settings().value("theme_mode",Json());
    theme::setMode(appearance.is_string()?theme::parse(appearance.get<std::string>()):theme::Mode::Dark);
    startup("interface");
    App app(db, bridge, display, input, screen, workspace, data, fontPath,
            resources, !headless, parsedConfiguration, uiMetrics);
    if (!demo.empty())
      app.demo(fileText(demo));
    startup("frame");
    int frame = 0;
    std::string exitReason="normal";
    bool firstFrameReported=false;
    enteredLoop=true;
    while (running && (standalone || bridge.connected())) {
      for (const auto &m : bridge.receive()) {
        if (m.value("type", std::string()) == "exit") {
          exitReason="explicit_exit";running = 0;
        }
        app.handle(m);
      }
      // A closed launcher must not dispatch one last batch of physical touches.
      if (!running || (!standalone && !bridge.connected())) break;
      app.tick();
      if(app.updateExit()||app.backupExit())break;
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
    if(terminationSignal)exitReason="signal";
    else if(!standalone&&!bridge.connected())exitReason="ipc_eof";
    else if(app.updateExit())exitReason="update";
    else if(app.backupExit())exitReason="backup_restore";
    bridge.send("native.exit",{{"reason",exitReason},{"signal",int(terminationSignal)}});
    auto persistExit=[&](const char *phase) {
      try {atomicText(join(data,"native-exit.json"),Json({{"pid",getpid()},{"reason",exitReason},
        {"signal",int(terminationSignal)},{"phase",phase},{"time_ms",nowMs()}}).dump());}catch(...){}
    };
    persistExit("shutdown_started");
    diagnosticRecord("lifecycle","exit",{{"signal",int(terminationSignal)}});flushDiagnostics();
    app.shutdown();
    if(app.backupExit()&&!terminationSignal)app.finishBackupRestore();
    display.visible(false);
    persistExit("shutdown_complete");
    return 0;
  } catch (const std::exception &e) {
    try {if(!data.empty())atomicText(join(data,"native-exit.json"),Json({{"pid",getpid()},
      {"reason",enteredLoop?"runtime_error":"startup_error"},{"phase","exception"},{"time_ms",nowMs()}}).dump());}catch(...){}
    diagnosticRecord("lifecycle", enteredLoop?"runtime_error":"startup_error"); flushDiagnostics();
    std::cerr << "penchat-lvgl: " << e.what() << std::endl;
    Json failure={{"error",e.what()}};
    if(const auto display=dynamic_cast<const camera::DisplayInitializationError *>(&e)) {
      failure["display"]=displayInformationJson(display->info);
      std::fprintf(stderr,"DISPLAY_PROBE %s\n",failure["display"].dump().c_str());
    }
    if(bridgeOwner) bridgeOwner->send("fatal", failure);
    else if (fd >= 0) {
      Bridge bridge(fd);
      bridge.send("fatal", failure);
    }
    return 1;
  }
}
