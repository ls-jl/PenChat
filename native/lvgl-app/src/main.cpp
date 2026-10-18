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
  std::string data, workspace, fontPath, resources, screenshot, demo;
  int fd = -1, frames = 0;
  bool headless = false;
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
    else if (arg == "--screenshot")
      screenshot = value();
    else if (arg == "--demo-markdown")
      demo = value();
    else if (arg == "--frames")
      frames = std::stoi(value());
  }
  try {
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
    Screen screen;
    if (!headless) {
      auto cfg = Json::parse(fileText("/etc/miniapp/resources/cfg.json"));
      screen = Screen::fromConfig(cfg);
      if (screen.width != 960 || screen.height != 266)
        throw std::runtime_error(
            "This build currently supports ORANGE 960x266");
    } else
      screen.rotation = 0;
    lv_init();
    lv_tick_set_cb([]() -> uint32_t { return uint32_t(ticks()); });
    initializeFonts(fontPath);
    Display display(screen, headless);
    Input input;
    Bridge bridge(fd);
    Database db(join(data, "penchat.db"));
    App app(db, bridge, display, input, screen, workspace, data, fontPath,
            resources);
    if (!demo.empty())
      app.demo(fileText(demo));
    bridge.send("booted");
    int frame = 0;
    while (running && ((headless && fd < 0) || bridge.connected())) {
      for (const auto &m : bridge.receive()) {
        if (m.value("type", std::string()) == "exit")
          running = 0;
        app.handle(m);
      }
      app.tick();
      lv_timer_handler();
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
      usleep(8000);
    }
    if (!screenshot.empty())
      display.screenshot(screenshot);
    app.shutdown();
    display.visible(false);
    return 0;
  } catch (const std::exception &e) {
    std::cerr << "penchat-lvgl: " << e.what() << std::endl;
    if (fd >= 0) {
      Bridge bridge(fd);
      bridge.send("fatal", {{"error", e.what()}});
    }
    return 1;
  }
}
