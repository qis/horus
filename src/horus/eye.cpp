#include "eye.hpp"
#include <opencv2/imgproc.hpp>
#include <tbb/parallel_for.h>

namespace horus {
namespace {

// Returns true if the `si` src iterator pixel is magenta.
constexpr bool is_outline(const uint8_t* si) noexcept
{
  return si[0] > 0xA0 && si[1] < 0x60 && si[2] > 0xA0;
}

// Version: 0.1
// Returns true if the `si` src iterator pixel is magenta and
// 1-4 adjacent pixels also return true for `depth` recursive calls.
// Fast, but too many gaps in the outline prevent accurate polygon creation.
constexpr bool is_outline(const uint8_t* si, unsigned depth) noexcept
{
  constexpr unsigned max = 4;            // max number of adjacent outline pixels (0-8)
  const uint8_t* pi = si - eye::sw * 4;  // pixel above the src iterator
  const uint8_t* ni = si + eye::sw * 4;  // pixel below the src iterator
  if (!is_outline(si)) {
    return false;
  }
  unsigned counter = 0;
  unsigned adjacent_counter = 0;
  if (is_outline(pi - 4)) {
    if constexpr (max < 1 || max > 7) {
      return max > 7;
    } else {
      counter++;
      if (depth == 0 || is_outline(pi - 4, depth - 1)) {
        adjacent_counter++;
      }
    }
  }
  if (is_outline(pi)) {
    counter++;
    if (depth == 0 || is_outline(pi, depth - 1)) {
      adjacent_counter++;
    }
    if constexpr (max < 2) {
      if (counter > max) {
        return false;
      }
    }
  }
  if (is_outline(pi + 4)) {
    counter++;
    if (depth == 0 || is_outline(pi + 4, depth - 1)) {
      adjacent_counter++;
    }
    if constexpr (max < 3) {
      if (counter > max) {
        return false;
      }
    }
  }
  if (is_outline(si - 4)) {
    counter++;
    if (depth == 0 || is_outline(si - 4, depth - 1)) {
      adjacent_counter++;
    }
    if constexpr (max < 4) {
      if (counter > max) {
        return false;
      }
    }
  }
  if (is_outline(si + 4)) {
    counter++;
    if (depth == 0 || is_outline(si + 4, depth - 1)) {
      adjacent_counter++;
    }
    if constexpr (max < 5) {
      if (counter > max) {
        return false;
      }
    }
  }
  if (is_outline(ni - 4)) {
    counter++;
    if (depth == 0 || is_outline(ni - 4, depth - 1)) {
      adjacent_counter++;
    }
    if constexpr (max < 6) {
      if (counter > max) {
        return false;
      }
    }
  }
  if (is_outline(ni)) {
    counter++;
    if (depth == 0 || is_outline(ni, depth - 1)) {
      adjacent_counter++;
    }
    if constexpr (max < 7) {
      if (counter > max) {
        return false;
      }
    }
  }
  if (is_outline(ni + 4)) {
    counter++;
    if (depth == 0 || is_outline(ni + 4, depth - 1)) {
      adjacent_counter++;
    }
    if constexpr (max < 8) {
      if (counter > max) {
        return false;
      }
    }
  }
  return counter && counter < max + 1 && adjacent_counter && adjacent_counter < max + 1;
}

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

}  // namespace

eye::eye() :
  outlines_(sw * sh),
  outlines_image_(sw, sh, CV_8UC1, outlines_.data(), sw),
  overlays_(sw * sh),
  overlays_image_(sw, sh, CV_8UC1, overlays_.data(), sw),
  dilate_kernel_(cv::getStructuringElement(cv::MORPH_ELLIPSE, { 6, 6 })),
  erode_kernel_(cv::getStructuringElement(cv::MORPH_ELLIPSE, { 6, 6 }))
{
  hierarchy_.reserve(1024);
  contours_.reserve(1024);
  polygons_.reserve(1024);
}

double eye::scan(const uint8_t* image, unsigned depth) noexcept
{
  // Get outlines.
  std::memset(outlines_.data(), 0, sw * sh);
  const auto range = tbb::blocked_range<size_t>(depth + 1, sh - depth - 1, 64);
  tbb::parallel_for(range, [&](const tbb::blocked_range<size_t>& range) {
    const auto rb = range.begin();
    const auto re = range.end();
    auto si = image + rb * sw * 4;         // src iterator
    auto di = outlines_.data() + rb * sw;  // dst iterator
    for (auto y = rb; y < re; y++) {
      si += 4;
      di += 1;
      for (auto x = depth + 1; x < sw - depth - 1; x++) {
        if (is_outline(si, depth)) {
          di[0] = 0xFF;
        }
        si += 4;
        di += 1;
      }
      si += 4;
      di += 1;
    }
  });

  // Remove small gaps in outlines.
  //cv::dilate(outlines_image_, outlines_image_, dilate_kernel_);
  //cv::erode(outlines_image_, outlines_image_, erode_kernel_);

  // Find countours.
  cv::findContours(outlines_image_, contours_, hierarchy_, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

  // TODO: Merge countours that likely belong to a single target.
  // TODO: Merge countours that likely belong to a single target.

  // Create polygons.
  polygons_.resize(contours_.size());
  for (size_t i = 0, size = contours_.size(); i < size; i++) {
    cv::convexHull(cv::Mat(contours_[i]), polygons_[i]);
  }

  // Return distance between the center of image and the nearest containing polygon edge.
  const auto center = cv::Point2f(sw / 2.0f, sh / 2.0f);
  for (size_t i = 0, size = polygons_.size(); i < size; i++) {
    if (auto distance = cv::pointPolygonTest(polygons_[i], center, true); distance > 0.0) {
      return distance;
    }
  }
  return -1.0;
}

void eye::draw(uint8_t* image, int64_t pf, int64_t os, int64_t ps, int64_t cs) noexcept
{
  // Fill polygons.
  if (pf >= 0 && polygons_.size()) {
    std::memset(overlays_.data(), 0, sw * sh);
    for (size_t i = 0, size = polygons_.size(); i < size; i++) {
      cv::drawContours(overlays_image_, polygons_, i, cv::Scalar(255), -1, cv::LINE_AA);
    }
    draw(overlays_.data(), image, static_cast<uint32_t>(pf));
  }

  // Draw outlines.
  if (os >= 0) {
    draw(outlines_.data(), image, static_cast<uint32_t>(os));
  }

  // Draw polygons.
  if (ps >= 0 && polygons_.size()) {
    std::memset(overlays_.data(), 0, sw * sh);
    for (size_t i = 0, size = polygons_.size(); i < size; i++) {
      cv::drawContours(overlays_image_, polygons_, i, cv::Scalar(255), 1, cv::LINE_AA);
    }
    draw(overlays_.data(), image, static_cast<uint32_t>(ps));
  }

  // Draw contours.
  if (cs >= 0 && contours_.size()) {
    std::memset(overlays_.data(), 0, sw * sh);
    for (size_t i = 0, size = contours_.size(); i < size; i++) {
      cv::drawContours(overlays_image_, contours_, i, cv::Scalar(255), 1, cv::LINE_AA);
    }
    draw(overlays_.data(), image, static_cast<uint32_t>(cs));
  }
}

void eye::draw_reticle(uint8_t* image, uint32_t oc, uint32_t ic) noexcept
{
  constexpr auto set = [](uint8_t* di, const overlay_color& color, unsigned count) noexcept {
    for (unsigned i = 0; i < count; i++) {
      color.apply(di);
      di += 4;
    }
    return count * 4;
  };

  const overlay_color ooc(oc);
  const overlay_color oic(ic);

  auto di = image + (sh / 2 - 2) * sw * 4 + (sw / 2 - 2) * 4;  // dst iterator

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

void eye::desaturate(uint8_t* image) noexcept
{
  const auto range = tbb::blocked_range<size_t>(0, sh, 64);
  tbb::parallel_for(range, [&](const tbb::blocked_range<size_t>& range) {
    const auto rb = range.begin();
    const auto re = range.end();
    auto di = image + rb * sw * 4;  // dst iterator
    for (auto y = rb; y < re; y++) {
      for (auto x = 0; x < sw; x++) {
        const auto c = static_cast<uint8_t>(di[0] * 0.299f + di[1] * 0.587f + di[2] * 0.114f);
        di[0] = c;
        di[1] = c;
        di[2] = c;
        di += 4;
      }
    }
  });
}

void eye::draw(const uint8_t* overlays, uint8_t* image, uint32_t oc) noexcept
{
  const auto range = tbb::blocked_range<size_t>(0, sh, 64);
  const auto color = overlay_color(static_cast<uint32_t>(oc));
  tbb::parallel_for(range, [&](const tbb::blocked_range<size_t>& range) {
    const size_t rb = range.begin();
    const size_t re = range.end();
    auto si = overlays + rb * sw;   // src iterator
    auto di = image + rb * sw * 4;  // dst iterator
    for (auto y = rb; y < re; y++) {
      for (auto x = 0; x < sw; x++) {
        if (*si > 0) {
          color.apply(di, *si / 255.0f);
          //color.apply(di);
        }
        si += 1;
        di += 4;
      }
    }
  });
}

}  // namespace horus