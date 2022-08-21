#include <horus/eye.hpp>
#include <horus/log.hpp>
#include <opencv2/highgui.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <algorithm>
#include <chrono>
#include <exception>
#include <execution>
#include <filesystem>
#include <cstdlib>

namespace horus {

void process(const std::string& filename, bool gray = true)
{
  try {
    constexpr auto dc = [](std::chrono::high_resolution_clock::duration duration) {
      using duration_type = std::chrono::duration<float, std::milli>;
      return std::chrono::duration_cast<duration_type>(duration).count();
    };

    cv::Mat scan = cv::imread(filename, cv::IMREAD_UNCHANGED);
    if (scan.empty()) {
      throw std::runtime_error("failed to read image");
    }
    if (scan.rows != eye::sw || scan.cols != eye::sh) {
      throw std::runtime_error(std::format("invalid image size: {} x {}", scan.rows, scan.cols));
    }
    if (scan.channels() != 4) {
      throw std::runtime_error(std::format("invalid image channels number: {}", scan.channels()));
    }
    if (scan.depth() != CV_8U) {
      throw std::runtime_error(std::format("invalid image depth identifier: {}", scan.depth()));
    }

    // Scan image.
    std::vector<std::uint8_t> sd;
    sd.resize(eye::sw * eye::sh * 4);
    cv::Mat si(eye::sw, eye::sh, CV_8UC4, sd.data(), eye::sw * 4);
    cv::cvtColor(scan, si, cv::COLOR_BGRA2RGBA);

    // Debug image.
    std::vector<std::uint8_t> dd;
    dd.resize(eye::sw * eye::sh * 4);
    cv::Mat di(eye::sw, eye::sh, CV_8UC4, dd.data(), eye::sw * 4);
    cv::cvtColor(scan, di, cv::COLOR_BGRA2RGBA);

    eye eye;

    const auto tp0 = std::chrono::high_resolution_clock::now();

    const auto shoot = eye.scan(sd.data(), 2) > 4.0;

    const auto tp1 = std::chrono::high_resolution_clock::now();

    // Draw scan image.
    eye.draw(sd.data(), 0x09BC2430, -1, 0x08DE29B0, -1);
    if (shoot) {
      eye::draw_reticle(sd.data(), 0xFFFFFFFF, 0x1478B7FF);
    }

    const auto tp2 = std::chrono::high_resolution_clock::now();

    // Draw debug image.
    eye::desaturate(dd.data());
    eye.draw(dd.data(), 0x09BC2450, 0xFFFFFFFF, -1, -1);

    // Convert debug and scan images to BGRA.
    cv::cvtColor(di, di, cv::COLOR_RGBA2BGRA);
    cv::cvtColor(si, si, cv::COLOR_RGBA2BGRA);

    // Combine debug and scan images.
    cv::Mat image(cv::Size(eye::dw, eye::dh), CV_8UC4);
    di.copyTo(image(cv::Rect(eye::dw / 2 - eye::sw, eye::sy, eye::sw, eye::sh)));
    si.copyTo(image(cv::Rect(eye::dw / 2, eye::sy, eye::sw, eye::sh)));

    horus::log("{} [{}] {:0.3f} + {:0.3f} ms", filename, shoot ? '+' : ' ', dc(tp1 - tp0), dc(tp2 - tp1));

    cv::imshow("Horus", image);
  }
  catch (const std::exception& e) {
    horus::log("{}: {}", filename, e.what());
  }
}

}  // namespace horus

void process_images(std::filesystem::path path)
{
  path = std::filesystem::canonical(path);
  cv::namedWindow("Horus", cv::WindowFlags::WINDOW_NORMAL);
  cv::setWindowProperty("Horus", cv::WindowPropertyFlags::WND_PROP_FULLSCREEN, cv::WindowFlags::WINDOW_FULLSCREEN);
  std::vector<std::string> files;
  const std::filesystem::directory_iterator end;
  for (std::filesystem::directory_iterator it(path); it != end; ++it) {
    if (!std::filesystem::is_regular_file(it->path())) {
      continue;
    }
    if (it->path().extension() != ".png") {
      continue;
    }
    if (it->path().filename().string().ends_with("L.png")) {
      continue;
    }
    files.push_back(it->path().string());
  }
  std::sort(files.begin(), files.end());
  int64_t i = 0;
  int64_t m = static_cast<int64_t>(files.size());
  while (true) {
    while (i < 0) {
      i += m;
    }
    while (i >= m) {
      i -= m;
    }
    horus::process(files[i]);
    if (const auto key = cv::waitKeyEx(); key == 0x1B) {
      break;
    } else if (key == 0x250000) {
      i -= 1;
    } else {
      i += 1;
    }
  }
  cv::destroyWindow("Horus");
}

void process_dir(std::filesystem::path path)
{
  path = std::filesystem::canonical(path);
  std::vector<std::filesystem::path> paths;
  const std::filesystem::directory_iterator end;
  for (std::filesystem::directory_iterator it(path); it != end; ++it) {
    auto filename = it->path().filename().string();
    if (filename.size() < 3 || !std::isdigit(filename[0]) || !std::isdigit(filename[1])) {
      continue;
    }
    if (std::filesystem::is_directory(path / filename)) {
      paths.emplace_back(path / filename);
    }
  }
  for (auto c : { "01 Clean", "02 Chaotic", "03 VFX Ovrload", "04 Missing Enemies" }) {
    for (const auto& e : paths) {
      std::puts((e / c).string().data());
      process_images(e / c);
    }
  }
}

int main(int argc, char* argv[])
{
  horus::logger logger("C:/OBS/horus.log", true);
  try {
    //std::puts(std::format("{}", horus::image::color_difference_max()).data());
    //process("C:/OBS/img/000005005.png", "C:/OBS/img/tmp/000005005.png", true);
    //process("C:/OBS/img/01 Lijiang Tower/01 Clean/000000000.png", "C:/OBS/img/000005008.png", true);
    //return EXIT_SUCCESS;

    //process_dir("C:/OBS/img");
    process_images("C:/OBS/img/src");


    //if (!std::filesystem::is_directory("C:/OBS/img/tmp")) {
    //  std::filesystem::create_directory("C:/OBS/img/tmp");
    //}

    //const std::filesystem::directory_iterator end;
    //for (std::filesystem::directory_iterator it("C:/OBS/img"); it != end; ++it) {
    //  if (it->path().extension() == ".png") {
    //    const auto name = it->path().filename().string();
    //    process("C:/OBS/img/" + name, "C:/OBS/img/tmp/" + name);
    //  }
    //}

    //std::vector<std::string> remove;
    //const std::filesystem::directory_iterator end;
    //for (std::filesystem::directory_iterator it("C:/OBS/img/src"); it != end; ++it) {
    //  if (it->path().extension() == ".png") {
    //    const auto name = it->path().filename().string();
    //    if (!std::filesystem::exists("C:/OBS/img/tmp/" + name)) {
    //      remove.emplace_back(name);
    //    }
    //  }
    //}
    //for (const auto& e : remove) {
    //  std::puts(("C:/OBS/img/src/" + e).data());
    //  std::filesystem::remove("C:/OBS/img/src/" + e);
    //}
  }
  catch (const std::exception& e) {
    horus::log("error: {}", e.what());
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}