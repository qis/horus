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
  // Clock used for timings.
  using clock = std::chrono::high_resolution_clock;

  // Set of points used to represent contours and polygons.
  using polygon = std::vector<cv::Point>;

  // Display size.
  static constexpr uint32_t dw = 2560;
  static constexpr uint32_t dh = 1080;

  // Game size and offset to display.
  static constexpr uint32_t gw = 1920;
  static constexpr uint32_t gh = 1080;
  static constexpr uint32_t gx = (dw - gw) / 2;
  static constexpr uint32_t gy = (dh - gh) / 2;

  // Scan size and offset to display.
  static constexpr uint32_t sw = 1024;
  static constexpr uint32_t sh = 1024;
  static constexpr uint32_t sx = (dw - sw) / 2;
  static constexpr uint32_t sy = (dh - sh) / 2;

  // User interface position and size relative to the game.
  static constexpr uint32_t ux = 1296;
  static constexpr uint32_t uy = 904;
  static constexpr uint32_t uw = 562;
  static constexpr uint32_t uh = 94;

  // Enemy outlines color (minimum red, maximum green, minimum blue).
  static constexpr uint32_t oc = 0xA030A0;

  // Minimum contour area before.
  static constexpr double minimum_contour_area = 64.0;

  // Maximum outline ratio.
  static constexpr double maximum_outline_ratio = 0.3;

  // Distance between polygons that should be connected.
  static constexpr double polygon_connect_distance = 32.0;

  eye();
  eye(eye&& other) = delete;
  eye(const eye& other) = delete;
  eye& operator=(eye&& other) = delete;
  eye& operator=(const eye& other) = delete;
  ~eye() = default;

  /// Scans the image for enemy outlines.
  ///
  /// @param image Unmodified image from the center of the screen (sw x sh 4 byte rgba).
  ///
  /// @return Returns number of targets.
  ///
  size_t scan(const uint8_t* image) noexcept;

  /// Draws statistics from the last @ref scan call over the image.
  ///
  /// @param image Image used in the last @ref scan call.
  /// @param color RGBA color.
  ///
  void draw_stats(uint8_t* image, uint32_t color) noexcept;

  void draw_color(uint8_t* image, uint32_t color) noexcept
  {
    draw(image, color, color_);
  }

  void draw_color_mask(uint8_t* image, uint32_t color) noexcept
  {
    draw(image, color, color_mask_);
  }

  void draw_outlines(uint8_t* image, uint32_t color) noexcept
  {
    draw(image, color, outlines_);
  }

  void draw_regions(uint8_t* image, uint32_t color) noexcept
  {
    draw(image, color, regions_);
  }

  void draw_contours(uint8_t* image, uint32_t color) noexcept
  {
    std::memset(contours_overlay_.data(), 0, sw * sh);
    for (size_t i = 0, size = contours_.size(); i < size; i++) {
      cv::drawContours(contours_overlay_image_, contours_, i, cv::Scalar(255), 1, cv::LINE_AA);
    }
    draw(image, color, contours_overlay_);
  }

  void draw_polygons(uint8_t* image, uint32_t color) noexcept
  {
    std::memset(polygons_overlay_.data(), 0, sw * sh);
    cv::polylines(polygons_overlay_image_, polygons_, false, cv::Scalar(255), 1, cv::LINE_AA);
    draw(image, color, polygons_overlay_);
  }

  void draw_hulls(uint8_t* image, uint32_t color) noexcept
  {
    std::memset(hulls_overlay_.data(), 0, sw * sh);
    for (size_t i = 0, size = hulls_.size(); i < size; i++) {
      cv::polylines(hulls_overlay_image_, hulls_[i], false, cv::Scalar(255), 1, cv::LINE_AA);
    }
    draw(image, color, hulls_overlay_);
  }

  void draw_shapes(uint8_t* image, uint32_t color) noexcept
  {
    std::memset(shapes_overlay_.data(), 0, sw * sh);
    for (size_t i = 0, size = shapes_.size(); i < size; i++) {
      cv::fillPoly(shapes_overlay_image_, shapes_[i], cv::Scalar(255), cv::LINE_AA);
    }
    draw(image, color, shapes_overlay_);
  }

  void draw_groups(uint8_t* image, uint32_t color) noexcept
  {
    std::memset(groups_overlay_.data(), 0, sw * sh);
    for (const auto& group : groups_) {
      for (size_t i = 0, size = group.size(); i < size; i++) {
        cv::fillPoly(groups_overlay_image_, group[i], cv::Scalar(255), 0);
      }
    }
    draw(image, color, groups_overlay_);
  }

  void draw_targets(uint8_t* image, uint32_t color) noexcept
  {
    std::memset(targets_overlay_.data(), 0, sw * sh);
    cv::fillPoly(targets_overlay_image_, targets_, cv::Scalar(255), cv::LINE_AA);
    draw(image, color, targets_overlay_);
  }

  void draw_points(uint8_t* image, uint32_t color) noexcept
  {
    for (const auto& target : targets_) {
      for (size_t i = 0, size = target.size(); i < size; i++) {
        draw(image, color, target[i]);
      }
    }
  }

#if 0
  cv::drawContours(overlay_image_, polygons_, i, cv::Scalar(255), 1, cv::LINE_AA);
  cv::polylines(overlay_image_, polygons_[i], false, cv::Scalar(255), 1, cv::LINE_AA);
  cv::fillPoly(overlay_image_, polygons_[i], cv::Scalar(255), cv::LINE_AA);
#endif

  static void desaturate(uint8_t* image) noexcept;

private:
  static void draw(uint8_t* image, uint32_t color, const std::vector<uint8_t>& overlay) noexcept;
  static void draw(uint8_t* image, uint32_t color, const cv::Point& point) noexcept;

  struct timing {
    clock::time_point tp;
    const char* name{ nullptr };
  };

  std::vector<timing> timings_;

  std::vector<uint8_t> color_{ std::vector<uint8_t>(sw * sh) };
  cv::Mat color_image_{ sw, sh, CV_8UC1, color_.data(), sw };

  std::vector<uint8_t> color_mask_{ std::vector<uint8_t>(sw * sh) };
  cv::Mat color_mask_image_{ sw, sh, CV_8UC1, color_mask_.data(), sw };
  cv::Mat color_mask_erode_kernel_{ cv::getStructuringElement(cv::MORPH_RECT, { 2, 2 }) };
  cv::Mat color_mask_dilate_kernel_{ cv::getStructuringElement(cv::MORPH_RECT, { 6, 6 }) };

  std::vector<uint8_t> outlines_mask_{ std::vector<uint8_t>(sw * sh) };
  cv::Mat outlines_mask_image_{ sw, sh, CV_8UC1, outlines_mask_.data(), sw };

  std::vector<uint8_t> outlines_{ std::vector<uint8_t>(sw * sh) };
  cv::Mat outlines_image_{ sw, sh, CV_8UC1, outlines_.data(), sw };

  std::vector<uint8_t> regions_{ std::vector<uint8_t>(sw * sh) };
  cv::Mat regions_image_{ sw, sh, CV_8UC1, regions_.data(), sw };
  cv::Mat regions_close_kernel_{ cv::getStructuringElement(cv::MORPH_RECT, { 16, 16 }) };

  std::vector<polygon> contours_;
  std::vector<uint8_t> contours_overlay_{ std::vector<uint8_t>(sw * sh) };
  cv::Mat contours_overlay_image_{ sw, sh, CV_8UC1, contours_overlay_.data(), sw };

  std::vector<polygon> polygons_;
  std::vector<uint8_t> polygons_overlay_{ std::vector<uint8_t>(sw * sh) };
  cv::Mat polygons_overlay_image_{ sw, sh, CV_8UC1, polygons_overlay_.data(), sw };

  std::vector<polygon> hulls_;
  std::vector<uint8_t> hulls_overlay_{ std::vector<uint8_t>(sw * sh) };
  cv::Mat hulls_overlay_image_{ sw, sh, CV_8UC1, hulls_overlay_.data(), sw };

  std::vector<polygon> shapes_;
  std::vector<uint8_t> shapes_overlay_{ std::vector<uint8_t>(sw * sh) };
  cv::Mat shapes_overlay_image_{ sw, sh, CV_8UC1, shapes_overlay_.data(), sw };

  std::vector<std::vector<polygon>> groups_;
  std::vector<uint8_t> groups_overlay_{ std::vector<uint8_t>(sw * sh) };
  cv::Mat groups_overlay_image_{ sw, sh, CV_8UC1, groups_overlay_.data(), sw };

  std::vector<polygon> targets_;
  std::vector<uint8_t> targets_overlay_{ std::vector<uint8_t>(sw * sh) };
  cv::Mat targets_overlay_image_{ sw, sh, CV_8UC1, targets_overlay_.data(), sw };

  std::vector<cv::Vec4i> hierarchy_;
};

}  // namespace horus