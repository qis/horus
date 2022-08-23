#include "eye.hpp"
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <tbb/parallel_for.h>
#include <algorithm>
#include <format>
#include <cassert>

#define HORUS_AMMO_DIR "C:/OBS/horus/res/ammo"

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

eye::eye() :
  outlines_buffer_(sw * sh),
  outlines_(sw * sh),
  outlines_image_(sw, sh, CV_8UC1, outlines_.data(), sw),
  overlays_(sw * sh),
  overlays_image_(sw, sh, CV_8UC1, overlays_.data(), sw),
  close_kernel_(cv::getStructuringElement(cv::MORPH_RECT, { 12, 12 })),
  merge_kernel_(cv::getStructuringElement(cv::MORPH_RECT, { 24, 24 }))
{
  hierarchy_.reserve(1024);
  contours_.reserve(1024);
  polygons_.reserve(1024);

  ammo_scan_ = cv::Mat(cv::Size(aw, ah), CV_8UC1);

  for (size_t i = 0; i < ammo_scans_.size(); i++) {
    ammo_scans_[i] = cv::Mat(cv::Size(aw, ah), CV_8UC1);
    ammo_masks_[i] = cv::Mat(cv::Size(aw, ah), CV_8UC1);
    auto scan = cv::imread(std::format(HORUS_AMMO_DIR "/{:02d}.png", i), cv::IMREAD_UNCHANGED);
    assert(scan.cols == aw);
    assert(scan.rows == ah);
    assert(scan.channels() == 4);
    cv::cvtColor(scan, ammo_scans_[i], cv::COLOR_BGRA2GRAY);
    cv::threshold(ammo_scans_[i], ammo_masks_[i], 0x33, 0xFF, cv::THRESH_BINARY);
  }
}

bool eye::scan(const uint8_t* image) noexcept
{
  // Prepare outlines and outlines buffer.
  std::memset(outlines_.data(), 0, sw * sh);
  std::memset(outlines_buffer_.data(), 0, sw * sh);
  const auto range = tbb::blocked_range<size_t>(ah + 1, sh - 1, 64);

  // Get outlines.
  tbb::parallel_for(range, [&](const tbb::blocked_range<size_t>& range) {
    const auto rb = range.begin();
    const auto re = range.end();
    auto si = image + rb * sw * 4;                // src iterator
    auto di = outlines_buffer_.data() + rb * sw;  // dst iterator
    for (auto y = rb; y < re; y++) {
      si += 4;
      di += 1;
      for (auto x = 1; x < sw - 1; x++) {
        constexpr auto mr = (oc >> 16 & 0xFF);  // minimum red
        constexpr auto mg = (oc >> 8 & 0xFF);   // maximum green
        constexpr auto mb = (oc & 0xFF);        // minimum blue
        di[0] = si[0] > mr && si[1] < mg && si[2] > mb ? 0x01 : 0x00;
        si += 4;
        di += 1;
      }
      si += 4;
      di += 1;
    }
  });

  // Remove outline pixels that have too many or no sorrounding outline pixels.
  tbb::parallel_for(range, [&](const tbb::blocked_range<size_t>& range) {
    const auto rb = range.begin();
    const auto re = range.end();
    auto si = outlines_buffer_.data() + rb * sw;  // src iterator
    auto pi = si - sw;                            // pixel above the dst iterator
    auto ni = si + sw;                            // pixel below the dst iterator
    auto di = outlines_.data() + rb * sw;         // dst iterator
    for (auto y = rb; y < re; y++) {
      for (auto x = 1; x < sw - 0; x++) {
        if (si[1]) {
          const auto count = pi[0] + pi[1] + pi[2] + si[0] + si[1] + si[2] + ni[0] + ni[1] + ni[2];
          if (count > 1 && count < 6) {
            di[1] = 0x01;
          }
        }
        si += 1;
        pi += 1;
        ni += 1;
        di += 1;
      }
      si += 2;
      pi += 2;
      ni += 2;
      di += 2;
    }
  });

  // Remove single outline pixels.
  std::memcpy(outlines_buffer_.data(), outlines_.data(), sw * sh);
  tbb::parallel_for(range, [&](const tbb::blocked_range<size_t>& range) {
    const auto rb = range.begin();
    const auto re = range.end();
    auto si = outlines_buffer_.data() + rb * sw;  // src iterator
    auto pi = si - sw;                            // pixel above the dst iterator
    auto ni = si + sw;                            // pixel below the dst iterator
    auto di = outlines_.data() + rb * sw;         // dst iterator
    for (auto y = rb; y < re; y++) {
      for (auto x = 1; x < sw - 0; x++) {
        if (si[1] && pi[0] + pi[1] + pi[2] + si[0] + si[1] + si[2] + ni[0] + ni[1] + ni[2] == 1) {
          di[1] = 0x00;
        }
        si += 1;
        pi += 1;
        ni += 1;
        di += 1;
      }
      si += 2;
      pi += 2;
      ni += 2;
      di += 2;
    }
  });

  // Remove small gaps in outlines.
  cv::morphologyEx(outlines_image_, outlines_image_, cv::MORPH_CLOSE, close_kernel_);

  // Find countours.
  cv::findContours(outlines_image_, contours_, hierarchy_, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

  // Create polygons.
  polygons_.resize(contours_.size());
  for (size_t i = 0, size = contours_.size(); i < size; i++) {
    cv::convexHull(cv::Mat(contours_[i]), polygons_[i]);
  }

  // Render polygons that have a large surface.
  // Make sure that all polygons have a good aspect ratio and area size.
  std::memset(outlines_.data(), 0, sw * sh);
  for (size_t i = 0, size = polygons_.size(); i < size; i++) {
    const auto rect = cv::boundingRect(polygons_[i]);
    if (rect.width < rect.height * 3 && cv::contourArea(polygons_[i]) > 480.0) {
      cv::drawContours(outlines_image_, polygons_, i, cv::Scalar(255), 1, cv::LINE_4);
    }
  }

  // Remove large gaps in outlines.
  cv::morphologyEx(outlines_image_, outlines_image_, cv::MORPH_CLOSE, merge_kernel_);

  // Find countours.
  cv::findContours(outlines_image_, contours_, hierarchy_, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

  // Create polygons.
  polygons_.resize(contours_.size());
  for (size_t i = 0, size = contours_.size(); i < size; i++) {
    cv::convexHull(cv::Mat(contours_[i]), polygons_[i]);
  }

  // Check if the center of the image is targeting an enemy.
  const auto center = cv::Point2f(sw / 2.0f, sh / 2.0f);
  for (size_t i = 0, size = polygons_.size(); i < size; i++) {
    if (auto distance = cv::pointPolygonTest(polygons_[i], center, true); distance > 2.0) {
      const auto rect = cv::boundingRect(polygons_[i]);
      const auto x = sw / 2;
      const auto l = rect.x + rect.width / 5;
      const auto r = rect.x + rect.width - rect.width / 5;
      if (l < x && x < r) {
        return true;
      }
    }
  }
  return false;
}

ammo eye::ammo(uint8_t* image) noexcept
{
  auto count = unsigned(0);
  auto error = std::numeric_limits<double>::max();

  auto src = cv::Mat(sw, sh, CV_8UC4, image, eye::sw * 4);
  cv::cvtColor(src(cv::Rect(0, 0, aw, ah)), ammo_scan_, cv::COLOR_RGBA2GRAY);

  for (uint8_t i = 0, max = static_cast<uint8_t>(ammo_scans_.size()); i < max; i++) {
    constexpr auto type = cv::NORM_INF;
    if (const auto e = cv::norm(ammo_scan_, ammo_scans_[i], type, ammo_masks_[i]); e < error) {
      count = i;
      error = e;
    }
  }

  return { count, static_cast<unsigned>(error / aw * ah) };
}

void eye::draw(uint8_t* image, int64_t pf, int64_t os, int64_t ps, int64_t cs) noexcept
{
  // Fill polygons.
  if (pf >= 0 && polygons_.size()) {
    std::memset(overlays_.data(), 0, sw * sh);
    for (size_t i = 0, size = polygons_.size(); i < size; i++) {
      cv::drawContours(overlays_image_, polygons_, i, cv::Scalar(255), -1, cv::LINE_AA);
    }
    draw_overlays(image, static_cast<uint32_t>(pf));
  }

  // Draw outlines.
  if (os >= 0) {
    draw_outlines(image, static_cast<uint32_t>(os));
  }

  // Draw polygons.
  if (ps >= 0 && polygons_.size()) {
    std::memset(overlays_.data(), 0, sw * sh);
    for (size_t i = 0, size = polygons_.size(); i < size; i++) {
      cv::drawContours(overlays_image_, polygons_, i, cv::Scalar(255), 1, cv::LINE_AA);
    }
    draw_overlays(image, static_cast<uint32_t>(ps));
  }

  // Draw contours.
  if (cs >= 0 && contours_.size()) {
    std::memset(overlays_.data(), 0, sw * sh);
    for (size_t i = 0, size = contours_.size(); i < size; i++) {
      cv::drawContours(overlays_image_, contours_, i, cv::Scalar(255), 1, cv::LINE_AA);
    }
    draw_overlays(image, static_cast<uint32_t>(cs));
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
        di += rgba2gray(di);
        ;
      }
    }
  });
}

void eye::draw_outlines(uint8_t* image, uint32_t oc) noexcept
{
  const auto range = tbb::blocked_range<size_t>(0, sh, 64);
  const auto color = overlay_color(static_cast<uint32_t>(oc));
  tbb::parallel_for(range, [&](const tbb::blocked_range<size_t>& range) {
    const size_t rb = range.begin();
    const size_t re = range.end();
    auto si = outlines_.data() + rb * sw;  // src iterator
    auto di = image + rb * sw * 4;         // dst iterator
    for (auto y = rb; y < re; y++) {
      for (auto x = 0; x < sw; x++) {
        if (*si > 0) {
          color.apply(di);
        }
        si += 1;
        di += 4;
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
          //color.apply(di);
        }
        si += 1;
        di += 4;
      }
    }
  });
}

}  // namespace horus