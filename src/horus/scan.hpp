#pragma once
#include "config.hpp"
#include <opencv2/core/types.hpp>
#include <vector>
#include <cstdint>

namespace horus::scan {

// Display and scan sizes (must be synchronized with res/horus.effect).
constexpr uint32_t dw = 2560;  // display width
constexpr uint32_t dh = 1080;  // display height
constexpr uint32_t sw = 1024;  // scan width
constexpr uint32_t sh = 1024;  // scan height

// Scan offset to display (horizontal).
constexpr uint32_t sx = (dw - sw) / 2;

// Scan offset to display (vertical).
constexpr uint32_t sy = (dh - sh) / 2;

// Converts a scan::sw x scan::sh 4 byte rgba image to scan::sw x scan::sh 1 byte image.
// - Sets all magenta pixels with 1-4 adjacent magenta pixels to white.
// - Sets all other pixels to black.
// - Makes sure that all magenta pixels have `depth + 1` adjacent magenta pixels.
HORUS_API void filter(const uint8_t* src, uint8_t* dst, unsigned depth = 0) noexcept;

// Searches a scan::sw x scan::sh 1 byte image for countours and polygons.
// - Expects `void filter(const uint8_t* src, uint8_t* dst)` output as `src` input.
// - Searches for countours using OpenCV algorithms.
// - Merges contours that belong to a single enemy.
// - Creates polygons based on found contours.
// - If the center of the image is inside a polygon, returns the distance between the center of
//   the image and the nearest polygon edge. If the center of the image is not inside a polygon,
//   returns -1.0.
HORUS_API double find(
  uint8_t* src,
  std::vector<cv::Vec4i>& hierarchy,
  std::vector<std::vector<cv::Point>>& contours,
  std::vector<std::vector<cv::Point>>& polygons) noexcept;

// Draws contours and hulls.
// - Fills hull areas on a scan::sw x scan::sh 1 byte gray image provided as `overlay`.
// - Draws hull areas on a scan::sw x scan::sh 4 byte rgba image provided as `dst` using `alpha`.
// - Draws contours and hulls as strokes over `dst`.
// - Desaturates `dst` if `gray` is true.
// - Draws crosshair if `cross` is true.
HORUS_API void draw(
  const std::vector<std::vector<cv::Point>>& contours,
  const std::vector<std::vector<cv::Point>>& polygons,
  uint8_t* overlay,
  uint8_t* dst,
  float alpha,
  bool cross,
  bool gray) noexcept;

}  // namespace horus::scan