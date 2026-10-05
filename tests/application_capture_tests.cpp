#include "fury/application.hpp"
#include "fury/log.hpp"
#include "test_files.hpp"

#include <SDL.h>

#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace fury;
namespace fs = std::filesystem;

namespace {
constexpr int width = 48;
constexpr int height = 32;
constexpr unsigned frames = 3;

void require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}

struct TemporaryDirectory {
  fs::path path;
  TemporaryDirectory() {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    for (unsigned attempt = 0; attempt < 100; ++attempt) {
      path = fs::temp_directory_path() /
          ("fury-application-capture-" + std::to_string(stamp) + "-" + std::to_string(attempt));
      if (fs::create_directory(path)) return;
    }
    throw std::runtime_error("Create unique capture test directory");
  }
  ~TemporaryDirectory() {
    try { remove_fury_fixture(path); } catch (...) {}
  }
};

struct ErrorCapture {
  std::ostringstream text;
  std::streambuf* previous{std::cerr.rdbuf(text.rdbuf())};
  ~ErrorCapture() { std::cerr.rdbuf(previous); }
};

AppConfig config() {
  AppConfig result;
  result.window.title = "Bounded application frame capture tests";
  result.window.width = width;
  result.window.height = height;
  result.window.resizable = false;
  result.prefer_opengl = false;
  result.preferred_backend = RenderBackendKind::Software;
  result.capture_mouse = false;
  result.enable_collision = false;
  result.log_fps = false;
  result.max_frames = frames;
  result.fixed_timestep = 1.f / 12.f;
  return result;
}

std::string frame_name(unsigned frame) {
  std::ostringstream name;
  name << "frame_" << std::setw(6) << std::setfill('0') << frame << ".ppm";
  return name.str();
}

std::vector<std::uint8_t> read_ppm(const fs::path& path) {
  std::ifstream input(path, std::ios::binary);
  require(bool(input), "Capture exists and is readable");
  std::string magic;
  int actual_width{}, actual_height{}, maximum{};
  input >> magic >> actual_width >> actual_height >> maximum;
  require(magic == "P6" && actual_width == width && actual_height == height && maximum == 255,
          "Capture has exact P6 RGB header and dimensions");
  require(input.get() == '\n', "Capture header has one newline before pixel data");
  const std::vector<std::uint8_t> rgb{std::istreambuf_iterator<char>(input), {}};
  require(rgb.size() == std::size_t(width) * height * 3, "Capture contains exactly one RGB framebuffer");
  return rgb;
}

void expect_failure(AppConfig settings, const char* diagnostic) {
  unsigned updates = 0;
  Application app(std::move(settings));
  app.on_update = [&](float, const InputState&) { ++updates; app.request_quit(); };
  ErrorCapture errors;
  require(app.run() != 0, "Invalid capture request returns failure");
  require(updates == 0, "Invalid capture request fails before simulation updates");
  require(errors.text.str().find(diagnostic) != std::string::npos, "Capture failure includes actionable diagnostic");
}

void invalid_requests(const fs::path& root) {
  auto settings = config();
  settings.capture_sequence_directory = (root / "unbounded").string();
  settings.max_frames = 0;
  {
    Application app(settings);
    ErrorCapture errors;
    require(!app.init(), "Unbounded sequence rejected at initialization");
    require(!app.window().valid(), "Invalid sequence does not create a window");
  }
  expect_failure(settings, "max_frames > 0");
  require(!fs::exists(settings.capture_sequence_directory), "Unbounded request creates no output directory");

  const auto file = root / "regular-file";
  { std::ofstream output(file); output << "sentinel"; }
  settings = config();
  settings.capture_sequence_directory = file.string();
  expect_failure(settings, "Frame sequence directory failed");
  settings.capture_sequence_directory = (file / "nested").string();
  expect_failure(settings, "Frame sequence directory failed");

  const auto nonempty = root / "nonempty";
  fs::create_directory(nonempty);
  { std::ofstream output(nonempty / frame_name(1)); output << "preserve me"; }
  settings.capture_sequence_directory = nonempty.string();
  expect_failure(settings, "must be empty");
  std::ifstream sentinel(nonempty / frame_name(1));
  require(std::string(std::istreambuf_iterator<char>(sentinel), {}) == "preserve me",
          "Existing sequence contents are never overwritten");

  settings.capture_sequence_directory = (root / "same-destination").string();
  settings.capture_path = (fs::path(settings.capture_sequence_directory) / frame_name(1)).string();
  expect_failure(settings, "must be outside");
  settings.capture_path = (fs::path(settings.capture_sequence_directory) / "nested" / "last.ppm").string();
  expect_failure(settings, "must be outside");

  settings = config();
  settings.capture_sequence_directory = std::string("bad\0directory", 13);
  expect_failure(settings, "NUL");
  settings.capture_sequence_directory.clear();
  settings.capture_path = std::string("bad\0capture", 11);
  expect_failure(settings, "NUL");

  // A caller may edit config after initializing an otherwise valid app.
  settings = config();
  Application app(settings);
  require(app.init(), "Initialize before capture configuration changes");
  app.config().capture_sequence_directory = (root / "late-unbounded").string();
  app.config().max_frames = 0;
  ErrorCapture errors;
  require(app.run() != 0 && errors.text.str().find("max_frames > 0") != std::string::npos,
          "run revalidates changed configuration even after initialization");
}

std::vector<std::vector<std::uint8_t>> capture_motion(const fs::path& directory,
    const fs::path& last, RenderBackendKind backend, bool mutate_options = false) {
  auto settings = config();
  settings.preferred_backend = backend;
  settings.capture_sequence_directory = directory.string();
  settings.capture_path = last.string();
  Application app(settings);
  app.camera().position = {0, 0, 4};
  require(app.init(), "Initialize bounded capture application");
  if (backend == RenderBackendKind::CpuRayTracing) {
    auto rendering = app.renderer().settings();
    rendering.trace_mode = TraceMode::RayTraced;
    rendering.samples_per_pixel = 1;
    rendering.denoise = false;
    require(app.renderer().configure(rendering), "Configure deterministic CPU ray capture");
  }

  Mesh mesh = make_box({.8f, .8f, .3f}, {1, .15f, .05f});
  unsigned step = 0;
  std::vector<std::vector<std::uint8_t>> expected;
  app.on_update = [&](float dt, const InputState&) {
    require(std::abs(dt - 1.f / 12.f) < 1e-7f, "Fixed timestep reaches simulation unchanged");
    ++step;
    if (step > frames) app.request_quit();  // Bound the regression test even if the capture limit breaks.
    if (mutate_options) {
      app.config().max_frames = 0;
      app.config().capture_sequence_directory.clear();
      app.config().capture_path.clear();
    }
  };
  app.on_render = [&] {
    app.renderer().draw_mesh(mesh, translate({(float(step) - 2.f) * .9f, 0, 0}), {}, 1);
  };
  app.on_hud = [&] {
    app.renderer().draw_hud_rect(0, 0, 4, 4, {17, 219, 83, 255});
    int actual_width{}, actual_height{};
    expected.emplace_back();
    require(app.renderer().read_rgb_framebuffer(expected.back(), actual_width, actual_height),
            "Read independently rendered frame after HUD");
    require(actual_width == width && actual_height == height, "Renderer dimensions match requested capture");
  };
  require(app.run() == 0, "Bounded sequence capture succeeds");
  require(step == frames && expected.size() == frames, "Exactly the bounded number of frames rendered");
  require(std::distance(fs::directory_iterator(directory), fs::directory_iterator{}) == frames,
          "Exactly one file per rendered frame, with no hidden or stale files");
  for (unsigned index = 0; index < frames; ++index) {
    const auto actual = read_ppm(directory / frame_name(index + 1));
    require(actual == expected[index], "Sequence file is byte-identical to the actual renderer readback");
    require(actual[0] == 17 && actual[1] == 219 && actual[2] == 83,
            "Capture includes the current frame HUD with top-left origin");
  }
  require(expected[0] != expected[1] && expected[1] != expected[2],
          "Moving rendered geometry changes successive captured frames");
  require(read_ppm(last) == expected.back(), "Ordinary final capture matches the final sequence frame exactly");
  if (backend == RenderBackendKind::CpuRayTracing) {
    require(app.renderer().statistics().frame_index == frames,
            "Readback and presentation do not render CPU ray frames twice");
  }
  return expected;
}

void runtime_failures(const fs::path& root) {
  auto settings = config();
  settings.capture_sequence_directory = (root / "write-failure").string();
  {
    Application app(settings);
    app.on_update = [&](float, const InputState&) {
      fs::create_directory(fs::path(settings.capture_sequence_directory) / frame_name(1));
    };
    ErrorCapture errors;
    require(app.run() != 0 && errors.text.str().find("Frame capture open failed") != std::string::npos,
            "A sequence output that cannot be opened reports failure");
    require(!fs::exists(fs::path(settings.capture_sequence_directory) / frame_name(2)),
            "Sequence stops at the first failed write");
  }
  settings.capture_sequence_directory = (root / "read-failure").string();
  {
    Application app(settings);
    app.on_hud = [&] { app.renderer().destroy(); };
    ErrorCapture errors;
    require(app.run() != 0 && errors.text.str().find("framebuffer read failed") != std::string::npos,
            "Unavailable renderer readback reports failure instead of silently omitting frames");
    require(fs::is_empty(settings.capture_sequence_directory), "Failed readback writes no bogus image");
  }
  settings.capture_sequence_directory.clear();
  settings.capture_path = (root / "last-is-directory").string();
  fs::create_directory(settings.capture_path);
  {
    Application app(settings);
    ErrorCapture errors;
    require(app.run() != 0 && errors.text.str().find("Frame capture open failed") != std::string::npos,
            "Ordinary last-frame capture errors remain visible");
  }
}

void legacy_and_hud(const fs::path& root) {
  auto settings = config();
  settings.capture_path = (root / "legacy" / "last.ppm").string();
  settings.show_hud = false;
  unsigned updates = 0;
  unsigned hud_calls = 0;
  {
    Application app(settings);
    app.on_update = [&](float, const InputState&) { ++updates; };
    app.on_hud = [&] { ++hud_calls; app.renderer().draw_hud_rect(0, 0, width, height, {255, 0, 0, 255}); };
    require(app.run() == 0 && updates == frames && hud_calls == 0,
            "Legacy last-frame capture preserves bounded rendering and show_hud=false");
  }
  const auto image = read_ppm(settings.capture_path);
  require(image[0] == settings.clear_color.r && image[1] == settings.clear_color.g && image[2] == settings.clear_color.b,
          "Disabled HUD is absent from ordinary last-frame capture");

  settings.capture_sequence_directory = (root / "quit-early").string();
  settings.capture_path = (root / "early-final.ppm").string();
  {
    Application app(settings);
    app.on_update = [&](float, const InputState&) { app.request_quit(); };
    require(app.run() == 0, "Requested early quit preserves normal application behavior");
  }
  require(fs::exists(fs::path(settings.capture_sequence_directory) / frame_name(1)) &&
          !fs::exists(fs::path(settings.capture_sequence_directory) / frame_name(2)) &&
          !fs::exists(settings.capture_path),
          "Early quit captures only actual frames and does not invent a final bounded frame");

  settings.capture_sequence_directory = (root / "wide-numbering").string();
  settings.max_frames = 1000000;
  settings.capture_path.clear();
  {
    Application app(settings);
    app.on_update = [&](float, const InputState&) { app.request_quit(); };
    app.on_hud = [&] { throw std::runtime_error("Disabled HUD must not run during sequence capture"); };
    require(app.run() == 0, "Wide frame numbering remains bounded by ordinary early quit");
  }
  require(read_ppm(fs::path(settings.capture_sequence_directory) / "frame_0000001.ppm") == image,
          "Padding expands to fit the configured bound and sequences honor show_hud=false");
}
}  // namespace

int main(int, char**) {
  try {
    SDL_setenv("SDL_VIDEODRIVER", "dummy", 1);
    SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);
    SDL_setenv("FURY_CPU_THREADS", "2", 1);
    SDL_setenv("FURY_SURFACE_DETAIL", "0", 1);
    Log::set_level(LogLevel::Error);
    TemporaryDirectory temporary;
    invalid_requests(temporary.path);
    for (const auto backend : {RenderBackendKind::Software, RenderBackendKind::CpuRayTracing}) {
      const std::string name = backend == RenderBackendKind::Software ? "raster" : "ray";
      const auto first = capture_motion(temporary.path / (name + "-first"), temporary.path / (name + "-last.ppm"), backend);
      fs::create_directory(temporary.path / (name + "-second"));
      const auto second = capture_motion(temporary.path / (name + "-second"), temporary.path / (name + "-last2.ppm"), backend, true);
      require(first == second, "Repeated fixed-step captures are deterministic on each CPU backend");
    }
    runtime_failures(temporary.path);
    legacy_and_hud(temporary.path);
    std::cout << "Application frame capture checks passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "Application frame capture check failed: " << error.what() << '\n';
    return 1;
  }
}
