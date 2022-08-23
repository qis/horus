#pragma once
#include "config.hpp"
#include <opencv2/core/mat.hpp>
#include <opencv2/core/types.hpp>
#include <array>
#include <chrono>
#include <vector>
#include <cstdint>

namespace horus {

struct ammo {
  // Best match.
  unsigned count = 0;

  // Difference between scan and best match reference image.
  unsigned error = -1.0;
};

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

  // Ammo offset and size.
  static constexpr uint32_t ax = 2090;
  static constexpr uint32_t ay = 889;
  static constexpr uint32_t aw = 38;
  static constexpr uint32_t ah = 38;

  // Overlay color (minimum red, maximum green, minimum blue).
  static constexpr uint32_t oc = 0xA060A0;

  eye();
  eye(eye&& other) = delete;
  eye(const eye& other) = delete;
  eye& operator=(eye&& other) = delete;
  eye& operator=(const eye& other) = delete;
  ~eye() = default;

  /// Searches for enemy outlines.
  ///
  /// @param image Unmodified image from Overwatch (sw x sh 4 byte rgba).
  ///
  /// @return Returns true if the middle of the image is likely to be on a target.
  ///
  bool scan(const uint8_t* image) noexcept;

  /// Tries to detect the current ammo count.
  ///
  /// @param image Unmodified image from Overwatch (sw x sh 4 byte rgba).
  ///
  /// @return Returns the best guess.
  ///
  ammo ammo(uint8_t* image) noexcept;

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
  void draw_outlines(uint8_t* image, uint32_t oc) noexcept;
  void draw_overlays(uint8_t* image, uint32_t oc) noexcept;

  std::vector<uint8_t> outlines_buffer_;
  std::vector<uint8_t> outlines_;
  cv::Mat outlines_image_;

  std::vector<uint8_t> overlays_;
  cv::Mat overlays_image_;

  cv::Mat close_kernel_;
  cv::Mat merge_kernel_;

  std::vector<cv::Vec4i> hierarchy_;
  std::vector<std::vector<cv::Point>> contours_;
  std::vector<std::vector<cv::Point>> polygons_;

  cv::Mat ammo_scan_;
  std::array<cv::Mat, 13> ammo_scans_;
  std::array<cv::Mat, 13> ammo_masks_;
};

}  // namespace horus