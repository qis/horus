#include "eye.hpp"
#include <horus/log.hpp>
#include <opencv2/imgcodecs.hpp>
#include <tbb/parallel_for.h>
#include <algorithm>
#include <filesystem>
#include <format>
#include <numeric>
#include <cassert>

namespace horus {
namespace {

class overlay_color {
public:
  constexpr overlay_color(uint8_t r, uint8_t g, uint8_t b, uint8_t a) noexcept :
    a_(a / 255.0f), r_(r * a_), g_(g * a_), b_(b * a_), m_(1.0f - a_)
  {}

  constexpr overlay_color(uint32_t color) noexcept :
    overlay_color(
      static_cast<uint8_t>(color >> 24 & 0xFF),
      static_cast<uint8_t>(color >> 16 & 0xFF),
      static_cast<uint8_t>(color >> 8 & 0xFF),
      static_cast<uint8_t>(color & 0xFF))
  {}

  constexpr void apply(uint8_t* di) const noexcept
  {
    di[0] = static_cast<uint8_t>(di[0] * m_ + r_);
    di[1] = static_cast<uint8_t>(di[1] * m_ + g_);
    di[2] = static_cast<uint8_t>(di[2] * m_ + b_);
  }

  constexpr void apply(uint8_t* di, float alpha) const noexcept
  {
    const auto m = m_ + a_ * (1.0f - alpha);
    di[0] = static_cast<uint8_t>(di[0] * m + r_ * alpha);
    di[1] = static_cast<uint8_t>(di[1] * m + g_ * alpha);
    di[2] = static_cast<uint8_t>(di[2] * m + b_ * alpha);
  }

private:
  const float a_;
  const float r_;
  const float g_;
  const float b_;
  const float m_;
};

constexpr unsigned rgba2gray(uint8_t* di) noexcept
{
  const auto c = static_cast<uint8_t>(di[0] * 0.299f + di[1] * 0.587f + di[2] * 0.114f);
  di[0] = c;
  di[1] = c;
  di[2] = c;
  return 4;
}

}  // namespace

eye::eye()
{
  hierarchy_.reserve(1024);
  contours_.reserve(1024);
  polygons_.reserve(1024);
}

std::optional<eye::target> eye::scan(const uint8_t* image, int32_t mx, int32_t my) noexcept
{
  // Vertical iteration range.
  const auto range = tbb::blocked_range<size_t>(1, sh - 1, 64);

  // Draw outlines.
  std::memset(outlines_.data(), 0, sw * sh);
  tbb::parallel_for(range, [&](const tbb::blocked_range<size_t>& range) {
    const auto rb = range.begin();
    const auto re = range.end();
    auto si = image + rb * sw * 4;         // src iterator
    auto di = outlines_.data() + rb * sw;  // dst iterator
    for (auto y = rb; y < re; y++) {
      si += 4;
      di += 1;
      for (auto x = 1; x < sw - 1; x++) {
        constexpr auto er = (oc >> 16 & 0xFF);  // minimum red
        constexpr auto eg = (oc >> 8 & 0xFF);   // maximum green
        constexpr auto eb = (oc & 0xFF);        // minimum blue

        di[0] = si[0] > er && si[1] < eg && si[2] > eb ? 0x01 : 0x00;

        si += 4;
        di += 1;
      }
      si += 4;
      di += 1;
    }
  });

  // Remove single outline pixels and those who have too many outline pixels as neighbours.
  tbb::parallel_for(range, [&](const tbb::blocked_range<size_t>& range) {
    const auto rb = range.begin();
    const auto re = range.end();
    auto si = outlines_.data() + rb * sw;  // src iterator
    auto pi = si - sw;                     // pixel above the src iterator
    auto ni = si + sw;                     // pixel below the src iterator
    for (auto y = rb; y < re; y++) {
      for (auto x = 1; x < sw - 0; x++) {
        if (si[1]) {
          const auto count = pi[0] + pi[1] + pi[2] + si[0] + si[1] + si[2] + ni[0] + ni[1] + ni[2];
          if (count == 1 || count > 6) {
            si[1] = 0x00;
          }
        }
        si += 1;
        pi += 1;
        ni += 1;
      }
      si += 2;
      pi += 2;
      ni += 2;
    }
  });

  // Ignore frames with too many outline pixels.
  if (std::reduce(std::begin(outlines_), std::end(outlines_), uint32_t(0)) > sw * sh / 64) {
    return std::nullopt;
  }

  // Close small gaps in outlines.
  cv::morphologyEx(outlines_image_, outlines_image_, cv::MORPH_CLOSE, close_kernel_);

  // Find countours.
  cv::findContours(outlines_image_, contours_, hierarchy_, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

  // Find polygons.
  polygons_.resize(contours_.size());
  for (size_t i = 0, m = contours_.size(); i < m; i++) {
    cv::convexHull(cv::Mat(contours_[i]), polygons_[i]);
  }

  // Remove polygons with small areas.
  // clang-format off
  polygons_.erase(std::remove_if(polygons_.begin(), polygons_.end(), [](const auto& points) {
    return cv::contourArea(points) < minimum_contour_area;
  }), polygons_.end());
  // clang-format on

  // Draw polygons.
  std::memset(overlays_.data(), 0, sw * sh);
  for (size_t i = 0, m = polygons_.size(); i < m; i++) {
    cv::drawContours(overlays_image_, polygons_, i, cv::Scalar(255), -1, cv::LINE_4);
  }

  // Count overlay pixels inside polygons.
  polygons_fill_count_.assign(polygons_.size(), 0);
  tbb::parallel_for(range, [&](const tbb::blocked_range<size_t>& range) {
    const auto rb = range.begin();
    const auto re = range.end();
    auto si = outlines_.data() + rb * sw;  // src iterator
    for (auto y = rb; y < re; y++) {
      for (auto x = 0; x < sw; x++) {
        if (si[0]) {
          for (size_t i = 0, m = polygons_.size(); i < m; i++) {
            if (cv::pointPolygonTest(polygons_[i], cv::Point(x, y), false) > 0) {
              polygons_fill_count_[i]++;
            }
          }
        }
        si += 1;
      }
    }
  });

  // Remove polygons with too many overlay pixels.
  if (maximum_outline_ratio < 1.0) {
    for (size_t i = 0, m = polygons_.size(); i < m; i++) {
      if (polygons_fill_count_[i] > cv::contourArea(polygons_[i]) * maximum_outline_ratio) {
        polygons_[i].clear();
      }
    }
    // clang-format off
    polygons_.erase(std::remove_if(polygons_.begin(), polygons_.end(), [](const auto& points) {
      return points.empty();
    }), polygons_.end());
    // clang-format on
  }

  // Connect polygons, which have points close to each other.
  for (size_t i = 1, m = polygons_.size(); i < m; i++) {
    for (const auto& p0 : polygons_[i - 1]) {
      for (const auto& p1 : polygons_[i]) {
        const auto distance = std::sqrt(std::pow(p1.x - p0.x, 2) + std::pow(p1.y - p0.y, 2));
        if (distance < polygon_connect_distance) {
          using std::min, std::max;
          const auto x0 = min(max(0, p0.x < p1.x ? p0.x - 2 : p0.x + 2), static_cast<int>(sw));
          const auto y0 = min(max(0, p0.y < p1.y ? p0.y - 2 : p0.y + 2), static_cast<int>(sh));
          const auto x1 = min(max(0, p1.x < p0.x ? p1.x - 2 : p1.x + 2), static_cast<int>(sw));
          const auto y1 = min(max(0, p1.y < p0.y ? p1.y - 2 : p1.y + 2), static_cast<int>(sh));
          polygons_.push_back({ { x0, y0 }, { x1, y1 } });
          break;
        }
      }
    }
  }

  // Draw remaining polygons and new connections.
  std::memset(overlays_.data(), 0, sw * sh);
  for (size_t i = 0, m = polygons_.size(); i < m; i++) {
    if (polygons_[i].size() > 2) {
      cv::drawContours(overlays_image_, polygons_, i, cv::Scalar(255), -1, cv::LINE_4);
    } else if (polygons_[i].size() == 2) {
      cv::line(overlays_image_, polygons_[i][0], polygons_[i][1], cv::Scalar(255), 3, cv::LINE_8);
    }
  }

  // Remove protrusions.
  //cv::morphologyEx(overlays_image_, overlays_image_, cv::MORPH_OPEN, cv::Mat::ones(64, 32, CV_8UC1));

  // Erode new contours.
  cv::erode(overlays_image_, overlays_image_, erode_kernel_);

  // Find new contours.
  cv::findContours(overlays_image_, contours_, hierarchy_, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

  // Find contour centers and determine closest target.
  const cv::Point mouse(sw / 2 + mx, sh / 2 + my);
  std::optional<target> result;
  centers_.resize(contours_.size());
  for (size_t i = 0, m = contours_.size(); i < m; i++) {
    const auto size = contours_[i].size();
    if (!size) {
      continue;
    }

    auto ax = 0;
    auto al = static_cast<int>(sw);
    auto at = static_cast<int>(sh);
    auto ar = 0;
    auto ab = 0;
    for (const auto& point : contours_[i]) {
      ax += point.x;
      if (point.x < al) {
        al = point.x;
      } else if (point.x > ar) {
        ar = point.x;
      }
      if (point.y < at) {
        at = point.y;
      } else if (point.y > ab) {
        ab = point.y;
      }
    }
    if (ar < al) {
      ar = al;
    }
    if (ab < at) {
      ab = at;
    }
    const auto aw = ar - al;
    const auto ah = ab - at;
    centers_[i] = cv::Point(ax / size, at + ah / 16);
    if (const auto d = cv::norm(mouse - centers_[i]); !result || d < result->distance) {
      result.emplace(centers_[i], d, aw, ah);
    }
  }

  return result;
}

void eye::draw(uint8_t* image, int64_t pf, int64_t ps, int64_t cc) noexcept
{
  // Find new polygons.
  if (contours_.size()) {
    polygons_.resize(contours_.size());
    for (size_t i = 0, m = contours_.size(); i < m; i++) {
      cv::convexHull(cv::Mat(contours_[i]), polygons_[i]);
    }
  }

  // Fill polygons.
  if (pf >= 0 && polygons_.size()) {
    std::memset(overlays_.data(), 0, sw * sh);
    for (size_t i = 0, m = polygons_.size(); i < m; i++) {
      cv::drawContours(overlays_image_, polygons_, i, cv::Scalar(255), -1, cv::LINE_AA);
    }
    draw_overlays(image, static_cast<uint32_t>(pf));
  }

  // Draw polygons.
  if (ps >= 0 && polygons_.size()) {
    std::memset(overlays_.data(), 0, sw * sh);
    for (size_t i = 0, m = polygons_.size(); i < m; i++) {
      cv::drawContours(overlays_image_, polygons_, i, cv::Scalar(255), 1, cv::LINE_AA);
    }
    draw_overlays(image, static_cast<uint32_t>(ps));
  }

  // Draw centers.
  constexpr auto set = [](uint8_t* di, const overlay_color& color, unsigned count) noexcept {
    for (unsigned i = 0; i < count; i++) {
      color.apply(di);
      di += 4;
    }
    return count * 4;
  };

  if (cc >= 0 && centers_.size() == contours_.size()) {
    for (size_t i = 0, m = contours_.size(); i < m; i++) {
      if (!contours_[i].size()) {
        continue;
      }
      const auto sx = static_cast<long>(centers_[i].x);
      const auto sy = static_cast<long>(centers_[i].y);

      constexpr const overlay_color ooc(0x000000FF);
      const overlay_color oic(cc);

      auto di = image + (sy - 2) * sw * 4 + (sx - 2) * 4;  // dst iterator

      // Line 1.
      di += set(di, ooc, 4);
      di += sw * 4 - 5 * 4;

      // Line 2.
      di += set(di, ooc, 2);
      di += set(di, oic, 2);
      di += set(di, ooc, 2);
      di += sw * 4 - 6 * 4;

      // Line 3.
      di += set(di, ooc, 1);
      di += set(di, oic, 4);
      di += set(di, ooc, 1);
      di += sw * 4 - 6 * 4;

      // Line 4.
      di += set(di, ooc, 1);
      di += set(di, oic, 4);
      di += set(di, ooc, 1);
      di += sw * 4 - 6 * 4;

      // Line 5.
      di += set(di, ooc, 2);
      di += set(di, oic, 2);
      di += set(di, ooc, 2);
      di += sw * 4 - 5 * 4;

      // Line 6.
      set(di, ooc, 4);
    }
  }
}

void eye::desaturate(uint8_t* image) noexcept
{
  const auto range = tbb::blocked_range<size_t>(0, sh, 64);
  tbb::parallel_for(range, [&](const tbb::blocked_range<size_t>& range) {
    const auto rb = range.begin();
    const auto re = range.end();
    auto di = image + rb * sw * 4;  // dst iterator
    for (auto y = rb; y < re; y++) {
      for (auto x = 0; x < sw; x++) {
        di += rgba2gray(di);
        ;
      }
    }
  });
}

void eye::draw_overlays(uint8_t* image, uint32_t oc) noexcept
{
  const auto range = tbb::blocked_range<size_t>(0, sh, 64);
  const auto color = overlay_color(static_cast<uint32_t>(oc));
  tbb::parallel_for(range, [&](const tbb::blocked_range<size_t>& range) {
    const size_t rb = range.begin();
    const size_t re = range.end();
    auto si = overlays_.data() + rb * sw;  // src iterator
    auto di = image + rb * sw * 4;         // dst iterator
    for (auto y = rb; y < re; y++) {
      for (auto x = 0; x < sw; x++) {
        if (*si > 0) {
          color.apply(di, *si / 255.0f);
        }
        si += 1;
        di += 4;
      }
    }
  });
}

}  // namespace horus