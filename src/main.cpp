#include <horus/eye.hpp>
#include <horus/log.hpp>
#include <horus/mouse.hpp>
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

    cv::Mat image = cv::imread(filename, cv::IMREAD_UNCHANGED);
    if (image.empty()) {
      throw std::runtime_error("failed to read image");
    }
    if (image.rows != eye::sw || image.cols != eye::sh) {
      throw std::runtime_error(std::format("invalid image size: {} x {}", image.rows, image.cols));
    }
    if (image.channels() != 4) {
      throw std::runtime_error(std::format("invalid image channels number: {}", image.channels()));
    }
    if (image.depth() != CV_8U) {
      throw std::runtime_error(std::format("invalid image depth identifier: {}", image.depth()));
    }

    // Source image.
    std::vector<std::uint8_t> sd;
    sd.resize(eye::sw * eye::sh * 4);
    cv::Mat si(eye::sw, eye::sh, CV_8UC4, sd.data(), eye::sw * 4);
    cv::cvtColor(image, si, cv::COLOR_BGRA2RGBA);

    // Filter image.
    std::vector<std::uint8_t> fd;
    fd.resize(eye::sw * eye::sh * 4);
    cv::Mat fi(eye::sw, eye::sh, CV_8UC4, fd.data(), eye::sw * 4);
    cv::cvtColor(image, fi, cv::COLOR_BGRA2RGBA);

    // Scan source image.
    eye eye;

    const auto tp0 = std::chrono::high_resolution_clock::now();
    const auto shoot = eye.scan(sd.data());

    // Draw scan as overlay.
    const auto tp1 = std::chrono::high_resolution_clock::now();
    eye.draw(sd.data(), 0x09BC2460, -1, 0x08DE29C0, -1);
    if (shoot) {
      eye::draw_reticle(sd.data(), 0xFFFFFFFF, 0x00A5E7FF);
    }

    // Log scan and draw durations.
    const auto tp2 = std::chrono::high_resolution_clock::now();
    horus::log("{} [{}] {:0.3f} + {:0.3f} ms", filename, shoot ? '+' : ' ', dc(tp1 - tp0), dc(tp2 - tp1));

    // Draw filter as overlay.
    eye::desaturate(fd.data());
    eye.draw(fd.data(), 0x09BC2460, 0xFFFFFFFF, -1, -1);

    // Convert filter and scan images to BGRA.
    cv::cvtColor(fi, fi, cv::COLOR_RGBA2BGRA);
    cv::cvtColor(si, si, cv::COLOR_RGBA2BGRA);

    // Show filter and scan images.
    cv::Mat visual(cv::Size(eye::dw, eye::dh), CV_8UC4);
    fi.copyTo(visual(cv::Rect(eye::dw / 2 - eye::sw, eye::sy, eye::sw, eye::sh)));
    si.copyTo(visual(cv::Rect(eye::dw / 2, eye::sy, eye::sw, eye::sh)));
    cv::imshow("Horus", visual);
  }
  catch (const std::exception& e) {
    horus::log("{}: {}", filename, e.what());
  }
}

}  // namespace horus

int main(int argc, char* argv[])
{
  horus::logger logger("C:/OBS/horus.log", true);
  try {
    /*
    cv::namedWindow("Horus", cv::WindowFlags::WINDOW_NORMAL);
    const auto property = cv::WindowPropertyFlags::WND_PROP_FULLSCREEN;
    const auto value = cv::WindowFlags::WINDOW_FULLSCREEN;
    cv::setWindowProperty("Horus", property, value);

    std::vector<std::string> files;
    const std::filesystem::directory_iterator end;
    for (std::filesystem::directory_iterator it("C:/OBS/img/src"); it != end; ++it) {
      if (!std::filesystem::is_regular_file(it->path())) {
        continue;
      }
      if (it->path().extension() != ".png" || it->path().filename().string().ends_with("L.png")) {
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
      if (const auto key = cv::waitKeyEx(); key == 0x1B || key == 0x71) {
        break;
      } else if (key == 0x250000) {
        i -= 1;
      } else {
        i += 1;
      }
    }

    cv::destroyWindow("Horus");
    */

    horus::mouse mouse;

  }
  catch (const std::exception& e) {
    horus::log("error: {}", e.what());
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}