#include <horus/config.hpp>
#include <horus/eye.hpp>
#include <opencv2/highgui.hpp>
#include <opencv2/imgcodecs.hpp>
#include <windows.h>
#include <format>
#include <stdexcept>

#define DEMO_PREFIX "C:/OBS/horus/res/images/demo"

namespace horus {
namespace {

void blend(const cv::Mat& src, cv::Mat& dst)
{
  if (src.type() != CV_8UC4) {
    throw std::runtime_error("blend: invalid src type");
  }
  if (dst.type() != CV_8UC4) {
    throw std::runtime_error("blend: invalid dst type");
  }
  if (src.rows != dst.rows) {
    throw std::runtime_error("blend: height mismatch");
  }
  if (src.cols != dst.cols) {
    throw std::runtime_error("blend: width mismatch");
  }
  auto si = src.data;
  auto di = dst.data;
  for (size_t y = 0; y < src.rows; y++) {
    for (size_t x = 0; x < src.cols; x++) {
      const auto b = *si++;
      const auto g = *si++;
      const auto r = *si++;
      const auto a = *si++;
      if (a) {
        const auto m = (0xFF - a) / 255.0f;
        *di = static_cast<uchar>(*di * m + r * (1.0f - m));
        ++di;
        *di = static_cast<uchar>(*di * m + g * (1.0f - m));
        ++di;
        *di = static_cast<uchar>(*di * m + b * (1.0f - m));
        di += 2;
      } else {
        di += 4;
      }
    }
    if (src.step > src.cols * 4) {
      si += src.step - src.cols * 4;
    }
    if (dst.step > dst.cols * 4) {
      di += dst.step - dst.cols * 4;
    }
  }
}

}  // namespace

void demo(int argc, char* argv[])
{
  const auto frame = cv::imread(DEMO_PREFIX "/frame.png");
  if (frame.type() != CV_8UC3) {
    throw std::runtime_error("invalid frame type");
  }
  if (frame.rows != eye::sh) {
    throw std::runtime_error("invalid frame height");
  }
  if (frame.cols != eye::sw) {
    throw std::runtime_error("invalid frame width");
  }
  cv::Mat frame_gray(eye::sw, eye::sh, CV_8UC1);
  cv::cvtColor(frame, frame_gray, cv::COLOR_RGB2GRAY);

  const auto hsv = cv::imread(DEMO_PREFIX "/hsv.png");
  if (hsv.type() != CV_8UC3) {
    throw std::runtime_error("invalid hsv type");
  }
  if (hsv.rows != eye::sh) {
    throw std::runtime_error("invalid hsv height");
  }
  if (hsv.cols != eye::sw) {
    throw std::runtime_error("invalid hsv width");
  }

  const auto scan = cv::imread(DEMO_PREFIX "/scan.png", cv::IMREAD_GRAYSCALE);
  if (scan.type() != CV_8UC1) {
    throw std::runtime_error("invalid scan type");
  }
  if (scan.rows != eye::sh) {
    throw std::runtime_error("invalid scan height");
  }
  if (scan.cols != eye::sw) {
    throw std::runtime_error("invalid scan width");
  }
  if (const auto max = *std::max_element(scan.data, scan.data + eye::sh * scan.step); max != 0x01) {
    throw std::runtime_error(std::format("scan max element ({:02X}) is not 0x01", max));
  }
  if (const auto min = *std::min_element(scan.data, scan.data + eye::sh * scan.step); min != 0x00) {
    throw std::runtime_error(std::format("scan min element ({:02X}) is not 0x00", min));
  }

  cv::Mat canvas(eye::sw, eye::sh, CV_8UC4);

  eye eye;
  if (!eye.scan(scan)) {
    throw std::runtime_error("eye::scan failed");
  }

  cv::Mat overlay(eye::vw, eye::vh, CV_8UC4);
  cv::Mat overlay_scan(eye::sw, eye::sh, CV_8UC4);

  //overlay.setTo(cv::Scalar(0, 0, 0, 0));
  //eye.draw_hulls(overlay);
  //cv::resize(overlay, view, cv::Size(eye::sw, eye::sh));
  //background.copyTo(canvas);
  //blend(view, canvas);

  cv::String window = "Horus Demo";
  cv::namedWindow(window);

  enum class view {
    hsv = 0,
    scan,
    mask,
    contours,
    groups,
    hulls,
    shapes,
    polygons,
    none,
  };

  const auto update = [&](view draw) {
    auto overlay_desaturate = true;
    overlay.setTo(eye::scalar(0x00000000));
    switch (draw) {
    case view::hsv:
      overlay_desaturate = false;
      break;
    case view::scan:
      eye.draw_scan(overlay);
      break;
    case view::mask:
      eye.draw_mask(overlay);
      break;
    case view::contours:
      eye.draw_contours(overlay);
      break;
    case view::groups:
      eye.draw_groups(overlay);
      break;
    case view::hulls:
      eye.draw_hulls(overlay);
      break;
    case view::shapes:
      eye.draw_shapes(overlay);
      break;
    case view::polygons:
      eye.draw_polygons(overlay);
      break;
    case view::none:
      overlay_desaturate = false;
      break;
    }
    if (draw == view::hsv) {
      cv::cvtColor(hsv, canvas, cv::COLOR_RGB2RGBA);
    } else if (overlay_desaturate) {
      cv::cvtColor(frame_gray, canvas, cv::COLOR_GRAY2RGBA);
    } else {
      cv::cvtColor(frame, canvas, cv::COLOR_RGB2RGBA);
    }
    cv::resize(overlay, overlay_scan, cv::Size(eye::sw, eye::sh), eye::vf, eye::vf, cv::INTER_NEAREST);
    blend(overlay_scan, canvas);
    cv::imshow(window, canvas);
  };

  update(view::none);
  auto draw = view::none;
  constexpr auto size = static_cast<int>(view::none) + 1;
  while (true) {
    switch (const auto key = static_cast<DWORD>(cv::waitKeyEx())) {
    case 0x250000: {
      draw = static_cast<view>((static_cast<int>(draw) + size - 1) % size);
      update(draw);
    } break;
    case 0x270000: {
      draw = static_cast<view>((static_cast<int>(draw) + 1) % size);
      update(draw);
    } break;
    case VK_ESCAPE:
      cv::destroyWindow(window);
      return;
    case -1:
      return;
    default:
      std::puts(std::format("0x{:02X}", key).data());
      break;
    }
  }
}

}  // namespace horus

extern "C" HORUS_API int demo(int argc, char* argv[])
{
#ifndef NDEBUG
  if (IsDebuggerPresent()) {
    horus::demo(argc, argv);
    return EXIT_SUCCESS;
  }
#endif
  try {
    horus::demo(argc, argv);
  }
  catch (const std::exception& e) {
    std::fputs(e.what(), stderr);
    std::fputs("\r\n", stderr);
    std::fflush(stderr);
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}