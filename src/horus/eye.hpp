#pragma once
#include "config.hpp"
#include <opencv2/core/mat.hpp>
#include <opencv2/core/types.hpp>
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
  static constexpr uint32_t sx = (dw - sw) / 2;

  // Scan offset to display (vertical).
  static constexpr uint32_t sy = (dh - sh) / 2;

  eye();
  eye(eye&& other) = delete;
  eye(const eye& other) = delete;
  eye& operator=(eye&& other) = delete;
  eye& operator=(const eye& other) = delete;
  ~eye() = default;

  /// Searches for enemy outlines.
  ///
  /// @param image Unmodified image from Overwatch (sw x sh 4 byte rgba).
  /// @param depth Makes sure that all outline pixels have `depth + 1` adjacent outline pixels.
  ///
  /// @return Distance between the center of image and the nearest containing polygon edge (or -1.0).
  ///
  double scan(const uint8_t* image, unsigned depth = 0) noexcept;

  /// Draws polygons, contours and filtered outlines from the last @ref scan call over the image.
  ///
  /// @param image Image used in the last @ref scan call.
  /// @param pf 32-bit RGBA color for polygon fill (negative to disable).
  /// @param os 32-bit RGBA color for outline strokes (negative to disable).
  /// @param ps 32-bit RGBA color for polygon strokes (negative to disable).
  /// @param cs 32-bit RGBA color for contour strokes (negative to disable).
  ///
  void draw(uint8_t* image, int64_t pf, int64_t os, int64_t ps, int64_t cs) noexcept;

  /// Draws reticle over the image.
  ///
  /// @param image Input and output image (sw x sh 4 byte rgba).
  /// @param oc 32-bit RGBA color for outer circle.
  /// @param ic 32-bit RGBA color for inner cross.
  ///
  static void draw_reticle(uint8_t* image, uint32_t oc, uint32_t ic) noexcept;

  /// Desaturates the image.
  ///
  /// @param image Input and output image (sw x sh 4 byte rgba).
  ///
  static void desaturate(uint8_t* image) noexcept;

private:
  static void draw(const uint8_t* overlays, uint8_t* image, uint32_t oc) noexcept;

  std::vector<uint8_t> outlines_;
  cv::Mat outlines_image_;

  std::vector<uint8_t> overlays_;
  cv::Mat overlays_image_;

  cv::Mat dilate_kernel_;
  cv::Mat erode_kernel_;

  std::vector<cv::Vec4i> hierarchy_;
  std::vector<std::vector<cv::Point>> contours_;
  std::vector<std::vector<cv::Point>> polygons_;
};

}  // namespace horus