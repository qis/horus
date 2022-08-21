#include <horus/log.hpp>
#include <horus/scan.hpp>
#include <opencv2/highgui.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <algorithm>
#include <chrono>
#include <exception>
#include <execution>
#include <filesystem>
#include <cstdlib>

void process(const std::string& filename, bool gray = true)
{
  try {
    constexpr auto dc = [](std::chrono::high_resolution_clock::duration duration) {
      using duration_type = std::chrono::duration<float, std::milli>;
      return std::chrono::duration_cast<duration_type>(duration).count();
    };

    // Scan data (rgba).
    std::vector<std::uint8_t> sd;
    sd.resize(horus::scan::sw * horus::scan::sh * 4);

    // Scan image (rgba).
    cv::Mat si(horus::scan::sw, horus::scan::sh, CV_8UC4, sd.data(), horus::scan::sw * 4);

    // Filter data (gray).
    std::vector<std::uint8_t> fd;
    fd.resize(horus::scan::sw * horus::scan::sh);

    // Filter image (gray).
    cv::Mat fi(horus::scan::sw, horus::scan::sh, CV_8UC1, fd.data(), horus::scan::sw);

    // Read scan image.
    cv::Mat scan = cv::imread(filename, cv::IMREAD_UNCHANGED);
    if (scan.empty()) {
      throw std::runtime_error("failed to read image");
    }
    if (scan.rows != horus::scan::sw || scan.cols != horus::scan::sh) {
      throw std::runtime_error(std::format("invalid image size: {} x {}", scan.rows, scan.cols));
    }
    if (scan.channels() != 4) {
      throw std::runtime_error(std::format("invalid image channels number: {}", scan.channels()));
    }
    if (scan.depth() != CV_8U) {
      throw std::runtime_error(std::format("invalid image depth identifier: {}", scan.depth()));
    }
    cv::cvtColor(scan, si, cv::COLOR_BGRA2RGBA);

    // Reusable variables.
    std::vector<cv::Vec4i> hierarchy;
    hierarchy.reserve(1024);

    std::vector<std::vector<cv::Point>> contours;
    contours.reserve(1024);

    std::vector<std::vector<cv::Point>> polygons;
    polygons.reserve(1024);

    // Measure duration of image processing.
    const auto tp0 = std::chrono::high_resolution_clock::now();

    // Apply filters.
    horus::scan::filter(sd.data(), fd.data());

    // Find contours and polygons.
    const auto shoot = horus::scan::find(fd.data(), hierarchy, contours, polygons) > 4.0;

    // Find contours.
    cv::findContours(fi, contours, hierarchy, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

    // Find polygons.
    polygons.resize(contours.size());
    for (size_t i = 0; i < contours.size(); i++) {
      cv::convexHull(cv::Mat(contours[i]), polygons[i]);
    }

    // Measure duration of visualization.
    const auto tp1 = std::chrono::high_resolution_clock::now();

    // Draw overlay.
    std::vector<uint8_t> overlay(horus::scan::sw * horus::scan::sh);
    horus::scan::draw(contours, polygons, overlay.data(), sd.data(), 0.3f, shoot, gray);

    // Report durations and number of contours.
    const auto tp2 = std::chrono::high_resolution_clock::now();
    const auto d0 = dc(tp1 - tp0);
    const auto d1 = dc(tp2 - tp1);
    const auto c0 = contours.size();
    const auto c1 = polygons.size();
    horus::log("{} {:0.3f} + {:0.3f} ms ({}/{}, {})", filename, d0, d1, c0, c1, shoot);

    // Show modified scan image.
    cv::cvtColor(si, si, cv::COLOR_RGBA2BGRA);
    cv::imshow("Horus", si);
    cv::waitKey();
  }
  catch (const std::exception& e) {
    horus::log("{}: {}", filename, e.what());
  }
}

void process_images(std::filesystem::path path)
{
  path = std::filesystem::canonical(path);
  cv::namedWindow("Horus", cv::WindowFlags::WINDOW_FULLSCREEN);
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
    process(it->path().string());
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