#pragma once
#include "config.hpp"
#include <opencv2/imgproc.hpp>
#include <array>
#include <chrono>
#include <vector>
#include <cstdint>

namespace horus {

class HORUS_API eye {
public:
  // Display and scan sizes (must be synchronized with res/horus.effect).
  static constexpr uint32_t dw = 2560;  // display width
  static constexpr uint32_t dh = 1080;  // display height
  static constexpr uint32_t gw = 1920;  // game width
  static constexpr uint32_t gh = 1080;  // game height
  static constexpr uint32_t sw = 1024;  // scan width
  static constexpr uint32_t sh = 1024;  // scan height

  // Scan offset to display.
  static constexpr uint32_t sx = (dw - sw) / 2;
  static constexpr uint32_t sy = (dh - sh) / 2;

  // Overlay color (magenta, minimum red, maximum green, minimum blue).
  // - 0xCA18C4 clean
  // - 0xE238EE scoped
  // - 0xA080A0 works well for reaper
  static constexpr uint32_t oc = 0xA080A0;

  // Cursor interpolation multiplier (increase when over-shooting the target).
  static constexpr float cm = 2.1f;

  // Minimum contour area before.
  static constexpr double minimum_contour_area = 64.0;

  // Maximum outline ratio.
  static constexpr double maximum_outline_ratio = 0.3;

  // Connect polygons, which have points close to each other.
  static constexpr double polygon_connect_distance = 16.0;

  // Scan result.
  struct target {
    cv::Point point{};
    double distance{ 0.0 };
    int cw{ 0 };
    int ch{ 0 };
  };

  eye();
  eye(eye&& other) = delete;
  eye(const eye& other) = delete;
  eye& operator=(eye&& other) = delete;
  eye& operator=(const eye& other) = delete;
  ~eye() = default;

  /// Searches for enemy outlines.
  ///
  /// @param image Unmodified image from Overwatch (sw x sh 4 byte rgba).
  /// @param mx DirectInput mouse movement relative to the last frame (horizontal).
  /// @param my DirectInput mouse movement relative to the last frame (vertical).
  ///
  /// @return Returns the closest target.
  ///
  std::optional<target> scan(const uint8_t* image, int32_t mx, int32_t my) noexcept;

  /// Draws polygons, contours and filtered outlines from the last @ref scan call over the image.
  ///
  /// @param image Image used in the last @ref scan call.
  /// @param pf 32-bit RGBA color for polygons fill (negative to disable).
  /// @param ps 32-bit RGBA color for polygons strokes (negative to disable).
  /// @param cc 32-bit RGBA color for centers points (negative to disable).
  /// @param mx DirectInput mouse movement relative to the last frame (horizontal).
  /// @param my DirectInput mouse movement relative to the last frame (vertical).
  ///
  void draw(uint8_t* image, int64_t pf, int64_t ps, int64_t cc) noexcept;

  /// Desaturates the image.
  ///
  /// @param image Input and output image (sw x sh 4 byte rgba).
  ///
  static void desaturate(uint8_t* image) noexcept;

private:
  void draw_overlays(uint8_t* image, uint32_t oc) noexcept;

  std::vector<uint8_t> outlines_{ std::vector<uint8_t>(sw * sh) };
  cv::Mat outlines_image_{ sw, sh, CV_8UC1, outlines_.data(), sw };

  std::vector<uint8_t> overlays_{ std::vector<uint8_t>(sw * sh) };
  cv::Mat overlays_image_{ sw, sh, CV_8UC1, overlays_.data(), sw };

  cv::Mat close_kernel_{ cv::getStructuringElement(cv::MORPH_RECT, { 3, 2 }) };
  cv::Mat erode_kernel_{ cv::getStructuringElement(cv::MORPH_RECT, { 16, 4 }) };

  std::vector<cv::Vec4i> hierarchy_;
  std::vector<std::vector<cv::Point>> contours_;
  std::vector<std::vector<cv::Point>> polygons_;
  std::vector<size_t> polygons_fill_count_;
  std::vector<cv::Point> centers_;
};

}  // namespace horus