#pragma once
#include "config.hpp"
#include <opencv2/core/cuda.hpp>
#include <opencv2/cudafilters.hpp>
#include <opencv2/freetype.hpp>
#include <opencv2/imgproc.hpp>
#include <array>
#include <string>
#include <vector>
#include <cstdint>

namespace horus {

class eye {
public:
  // Display size.
  static constexpr uint32_t dw{ 2560 };
  static constexpr uint32_t dh{ 1080 };

  // Game size and offset to display.
  static constexpr uint32_t gw{ 1920 };
  static constexpr uint32_t gh{ 1080 };
  static constexpr uint32_t gx{ (dw - gw) / 2 };
  static constexpr uint32_t gy{ (dh - gh) / 2 };

  // Scan size and offset to display.
  static constexpr uint32_t sw{ 1024 };
  static constexpr uint32_t sh{ 1024 };
  static constexpr uint32_t sx{ (dw - sw) / 2 };
  static constexpr uint32_t sy{ (dh - sh) / 2 };

  // View plane factor and size.
  static constexpr uint32_t vf{ 2 };
  static constexpr uint32_t vw{ sw / vf };
  static constexpr uint32_t vh{ sh / vf };
  static const cv::Point vc;

  // Expected frames per second.
  static constexpr float fps = 120.0f;

  // Set of points representing a polygon.
  using polygon = std::vector<cv::Point>;

  eye();
  eye(eye&& other) = delete;
  eye(const eye& other) = delete;
  eye& operator=(eye&& other) = delete;
  eye& operator=(const eye& other) = delete;
  ~eye() = default;

  /// Creates scan (150 μs).
  ///
  /// @param scan @ref eye::sw x @ref eye::sh image with bytes set to 0x00 or 0x01.
  ///
  /// @return Returns true if the hulls have changed.
  ///
  bool scan(const cv::Mat& scan) noexcept;

  /// Creates hulls (600 μs).
  ///
  /// Uses scan provided by the previous @ref eye::scan call.
  ///
  /// @return Returns generated hulls on a @ref eye::vw x @ref eye::vh plane.
  ///
  const std::vector<polygon>& hulls() noexcept;

  /// Creates polygons (??? μs).
  ///
  /// Uses scan provided by the previous @ref eye::scan call.
  ///
  /// @return Returns generated polygons on a @ref eye::vw x @ref eye::vh plane.
  ///
  const std::vector<polygon>& polygons() noexcept;

  clock::duration draw_scan(cv::Mat& overlay) noexcept;
  clock::duration draw_mask(cv::Mat& overlay) noexcept;
  clock::duration draw_contours(cv::Mat& overlay) noexcept;
  clock::duration draw_groups(cv::Mat& overlay) noexcept;
  clock::duration draw_hulls(cv::Mat& overlay) noexcept;
  clock::duration draw_shapes(cv::Mat& overlay) noexcept;
  clock::duration draw_polygons(cv::Mat& overlay) noexcept;

  void draw(
    cv::Mat& overlay,
    cv::Point position,
    const std::string& text,
    int height = 16,
    std::uint32_t fg = 0x00B0FFFF,
    std::uint32_t bg = 0x000000A0) noexcept;

  static void draw(
    cv::Mat& overlay,
    cv::Point point,
    std::uint32_t fg = 0x00B0FFFF,
    std::uint32_t bg = 0x000000FF) noexcept;

  static __forceinline cv::Scalar scalar(std::uint32_t color) noexcept
  {
    return {
      static_cast<double>(color >> 24 & 0xFF),
      static_cast<double>(color >> 16 & 0xFF),
      static_cast<double>(color >> 8 & 0xFF),
      static_cast<double>(color & 0xFF),
    };
  }

private:
  static cv::Mat kernel(int shape, int x, int y)
  {
    return cv::getStructuringElement(shape, cv::Point(x, y));
  }

  cv::Mat scan_{ vw, vh, CV_8UC1 };
  std::uint64_t scan_hash_{ 0x00 };

  cv::Mat mask_{ vw, vh, CV_8UC1 };
  cv::cuda::GpuMat mask_data_{ vw, vh, CV_8UC1 };
  cv::cuda::GpuMat mask_view_{ vw, vh, CV_8UC1 };

  std::vector<polygon> contours_;
  std::vector<cv::Vec4i> hierarchy_;

  std::vector<polygon> hulls_;
  bool hulls_ready_{ false };

  cv::Mat shapes_{ vw, vh, CV_8UC1 };
  cv::cuda::GpuMat shapes_data_{ vw, vh, CV_8UC1 };
  cv::cuda::GpuMat shapes_view_{ vw, vh, CV_8UC1 };

  std::vector<polygon> polygons_;
  bool polygons_ready_{ false };

  clock::duration scan_duration_{};
  clock::duration mask_duration_{};
  clock::duration contours_duration_{};
  clock::duration groups_duration_{};
  clock::duration hulls_duration_{};
  clock::duration shapes_duration_{};
  clock::duration polygons_duration_{};

  cv::cuda::GpuMat view_{ vw, vh, CV_8UC4 };
  cv::Ptr<cv::freetype::FreeType2> freetype_;
};

}  // namespace horus