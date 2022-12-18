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
  static constexpr uint32_t sw = 1024;  // scan width
  static constexpr uint32_t sh = 1024;  // scan height

  // Scan offset to display (horizontal).
  static constexpr uint32_t sx = (dw - sw) / 2;  // 768

  // Scan offset to display (vertical).
  static constexpr uint32_t sy = (dh - sh) / 2;  // 28

  // Hero portrait offset and size.
  static constexpr uint32_t hx = (2560 - 1920) / 2 + 71;
  static constexpr uint32_t hy = 932;
  static constexpr uint32_t hw = 64;
  static constexpr uint32_t hh = 64;

  // Chat input box offset and size.
  static constexpr uint32_t cx = (2560 - 1920) / 2 + 55;
  static constexpr uint32_t cy = 664;
  static constexpr uint32_t cw = 96;
  static constexpr uint32_t ch = 32;

  // Overlay color (magenta, minimum red, maximum green, minimum blue).
  // - 0xC217BF scoped, close
  // - 0xCA3FD9 scoped, distant
  // - 0xA080A0 scoped, recommended
  static constexpr uint32_t oc = 0xA080A0;

  // Cursor interpolation multiplier (increase when over-shooting the target).
  static constexpr float cm = 2.1f;

  // Minimum contour area before.
  static constexpr double minimum_contour_area = 64.0;

  // Maximum outline ratio.
  static constexpr double maximum_outline_ratio = 0.3;

  // Connect polygons, which have points close to each other.
  static constexpr double polygon_connect_distance = 16.0;

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
  /// @return Returns true if the cursor will target an enemy on the next frame.
  ///
  bool scan(const uint8_t* image, int32_t mx, int32_t my) noexcept;

  /// Draws polygons, contours and filtered outlines from the last @ref scan call over the image.
  ///
  /// @param image Image used in the last @ref scan call.
  /// @param pf 32-bit RGBA color for polygons fill (negative to disable).
  /// @param os 32-bit RGBA color for outlines strokes (negative to disable).
  /// @param ps 32-bit RGBA color for polygons strokes (negative to disable).
  /// @param cs 32-bit RGBA color for contours strokes (negative to disable).
  /// @param mx DirectInput mouse movement relative to the last frame (horizontal).
  /// @param my DirectInput mouse movement relative to the last frame (vertical).
  ///
  void draw(uint8_t* image, int64_t pf, int64_t os, int64_t ps, int64_t cs) noexcept;

  /// Draws reticle over the image.
  ///
  /// @param image Input and output image (sw x sh 4 byte rgba).
  /// @param oc 32-bit RGBA color for outer circle.
  /// @param ic 32-bit RGBA color for inner cross.
  ///
  void draw_reticle(uint8_t* image, uint32_t oc, uint32_t ic) noexcept;

  /// Desaturates the image.
  ///
  /// @param image Input and output image (sw x sh 4 byte rgba).
  ///
  static void desaturate(uint8_t* image) noexcept;

private:
  void draw_outlines(uint8_t* image, uint32_t oc) noexcept;
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
  std::vector<cv::Point> hull_;

  std::array<cv::Point2f, 7> cursor_interpolation_;
};

}  // namespace horus