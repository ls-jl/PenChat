#include "app.hpp"
#include <csignal>
#include <fcntl.h>
#include <iostream>
#include <unistd.h>
namespace {
volatile sig_atomic_t running = 1, snapshotRequested = 0;
void stop(int) { running = 0; }
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
    if (workspace.empty())
      workspace = ".";
    if (fontPath.empty())
      fontPath = "/etc/miniapp/resources/fonts/HarmonyOS_Sans_SC_Regular.ttf";
    if (resources.empty())
      resources = join(workspace, "assets/microtex");
    signal(SIGUSR1, snapshot);
    signal(SIGUSR2, diagnostic);
    signal(SIGTERM, stop);
    signal(SIGINT, stop);
    signal(SIGPIPE, SIG_IGN);
    const auto configuration = fileText(screenConfig, 64 * 1024);
    if (configuration.empty())
      throw std::runtime_error("Cannot read Falcon screen configuration: " + screenConfig);
    const auto screen = Screen::fromConfig(Json::parse(configuration));
    lv_init();
    lv_tick_set_cb([]() -> uint32_t { return uint32_t(ticks()); });
    initializeFonts(fontPath, join(workspace, "assets/fonts/DejaVuSans.ttf"));
    struct FontCleanup { ~FontCleanup() { lv_deinit(); shutdownFonts(); } } fontCleanup;
    Display display(screen, headless);
    Input input;
    const bool standalone = headless && fd < 0;
    bridgeOwner.reset(new Bridge(fd)); fd=-1;
    auto &bridge=*bridgeOwner;
    Database db(join(data, "penchat.db"));
    App app(db, bridge, display, input, screen, workspace, data, fontPath,
            resources);
    if (!demo.empty())
      app.demo(fileText(demo));
    bridge.send("booted", {{"screen", screen.descriptor()}});
    int frame = 0;
    while (running && (standalone || bridge.connected())) {
      for (const auto &m : bridge.receive()) {
        if (m.value("type", std::string()) == "exit")
          running = 0;
        app.handle(m);
      }
      app.tick();
      const auto nextTimer = lv_timer_handler();
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
      const auto pause = app.interactive() ? std::min<uint32_t>(16,nextTimer) : 50;
      usleep(std::max<uint32_t>(1,pause)*1000);
    }
    if (!screenshot.empty())
      display.screenshot(screenshot);
    app.shutdown();
    display.visible(false);
    return 0;
  } catch (const std::exception &e) {
    std::cerr << "penchat-lvgl: " << e.what() << std::endl;
    if(bridgeOwner) bridgeOwner->send("fatal", {{"error", e.what()}});
    else if (fd >= 0) {
      Bridge bridge(fd);
      bridge.send("fatal", {{"error", e.what()}});
    }
    return 1;
  }
}
