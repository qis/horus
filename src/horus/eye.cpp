#include "eye.hpp"
#include <opencv2/imgcodecs.hpp>
#include <tbb/parallel_for.h>
#include <algorithm>
#include <filesystem>
#include <format>
#include <cassert>

#include <horus/log.hpp>

#define HORUS_DATA_DIR "C:/OBS/horus/res"

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

static_assert(eye::cursor_interpolation_capacity >= 4);
static_assert(eye::cursor_interpolation_capacity % 2 == 0);

static_assert(eye::cursor_interpolation_distance >= 2);
static_assert(eye::cursor_interpolation_distance % 2 == 0);

static_assert(eye::cursor_interpolation_position >= 0.05f);
static_assert(eye::cursor_interpolation_position <= 0.95f);

eye::eye()
{
  hierarchy_.reserve(1024);
  contours_.reserve(1024);
  polygons_.reserve(1024);

  hero_scan_ = cv::Mat(cv::Size(hw, hh), CV_8UC1);
  for (auto& e : hero_scans_) {
    e = cv::Mat(cv::Size(hw, hh), CV_8UC1);
  }

  if (auto hero = cv::imread(HORUS_DATA_DIR "/heroes/ana.png", cv::IMREAD_UNCHANGED); hero.size) {
    cv::cvtColor(hero, hero_scans_[static_cast<unsigned>(hero::ana)], cv::COLOR_BGRA2GRAY);
  }

  if (auto hero = cv::imread(HORUS_DATA_DIR "/heroes/ashe.png", cv::IMREAD_UNCHANGED); hero.size) {
    cv::cvtColor(hero, hero_scans_[static_cast<unsigned>(hero::ashe)], cv::COLOR_BGRA2GRAY);
  }

  if (auto hero = cv::imread(HORUS_DATA_DIR "/heroes/pharah.png", cv::IMREAD_UNCHANGED); hero.size) {
    cv::cvtColor(hero, hero_scans_[static_cast<unsigned>(hero::pharah)], cv::COLOR_BGRA2GRAY);
  }

  if (auto hero = cv::imread(HORUS_DATA_DIR "/heroes/reaper.png", cv::IMREAD_UNCHANGED); hero.size) {
    cv::cvtColor(hero, hero_scans_[static_cast<unsigned>(hero::reaper)], cv::COLOR_BGRA2GRAY);
  }
}

bool eye::scan(const uint8_t* image, float mx, float my) noexcept
{
  // Vertical iteration range.
  const auto range = tbb::blocked_range<size_t>(hh + 1, sh - 1, 64);

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

  // Remove single outline pixels and those who have too many outline pixels as neighbours.
  // TODO: Restore old version that uses two passes if this is not enough.
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

  // Close small gaps in outlines.
  cv::morphologyEx(outlines_image_, outlines_image_, cv::MORPH_CLOSE, close_kernel_);

  // Find countours.
  cv::findContours(outlines_image_, contours_, hierarchy_, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

  // Find polygons.
  polygons_.resize(contours_.size());
  for (size_t i = 0, size = contours_.size(); i < size; i++) {
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
  for (size_t i = 0, size = polygons_.size(); i < size; i++) {
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
          for (size_t i = 0, size = polygons_.size(); i < size; i++) {
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
    for (size_t i = 0, size = polygons_.size(); i < size; i++) {
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
  for (size_t i = 1, size = polygons_.size(); i < size; i++) {
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
  for (size_t i = 0, size = polygons_.size(); i < size; i++) {
    if (polygons_[i].size() > 2) {
      cv::drawContours(overlays_image_, polygons_, i, cv::Scalar(255), -1, cv::LINE_4);
    } else if (polygons_[i].size() == 2) {
      cv::line(overlays_image_, polygons_[i][0], polygons_[i][1], cv::Scalar(255), 3, cv::LINE_8);
    }
  }

  // Find new contours.
  cv::findContours(overlays_image_, contours_, hierarchy_, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

  // Create cursor interpolation.
  const auto x0 = sw / 2.0f;
  const auto y0 = sh / 2.0f;
  const auto x1 = x0 + mx;
  const auto y1 = y0 + my;
  if (const auto cd = std::max(std::abs(mx), std::abs(my)); cd < cursor_interpolation_distance / 2) {
    cursor_interpolation_size_ = 1;
    cursor_interpolation_[0] = { x0, y0 };
  } else if (cd < cursor_interpolation_distance) {
    cursor_interpolation_size_ = 1;
    cursor_interpolation_[0] = { x1, y1 };
  } else if (cd < cursor_interpolation_distance * 2) {
    cursor_interpolation_size_ = 2;
    cursor_interpolation_[0] = { std::lerp(x0, x1, 0.5f), std::lerp(y0, y1, 0.5f) };
    cursor_interpolation_[1] = { x1, y1 };
  } else {
    const auto cs = static_cast<size_t>(cd * (1.0f - cursor_interpolation_position));
    const auto ct = std::max(cs / cursor_interpolation_distance, size_t(1));
    cursor_interpolation_size_ = std::min(ct, cursor_interpolation_capacity);
    const auto s = (1.0f - cursor_interpolation_position) / cursor_interpolation_size_;
    auto t = cursor_interpolation_position;
    for (size_t i = 0; i < cursor_interpolation_size_ - 1; i++) {
      cursor_interpolation_[i] = { std::lerp(x0, x1, t), std::lerp(y0, y1, t) };
      t += s;
    }
    cursor_interpolation_[cursor_interpolation_size_ - 1] = { x1, y1 };
  }

  // Increase the cursor to hull distance based on mouse interpolation.
  constexpr auto mf = 0.2f;
  const auto md = std::sqrt(std::pow(mx, 2.0f) + std::pow(my, 2.0f));
  const auto dm = 1.0f + mf - (std::min(md, 64.0f)) / 64.0f * mf;

  // Check if the cursor is targeting an enemy.
  for (size_t i = 0, size = contours_.size(); i < size; i++) {
    cv::convexHull(cv::Mat(contours_[i]), hull_);
    for (size_t i = 0; i < cursor_interpolation_size_; i++) {
      auto& cursor_interpolation = cursor_interpolation_[i];
      const auto x = cursor_interpolation.x;
      const auto y = cursor_interpolation.y;
      if (x < 0 || x > sw || y < 0 || y > sh) {
        continue;
      }
      if (auto d = cv::pointPolygonTest(hull_, cursor_interpolation, true); d > 2.0) {
        const auto rect = cv::boundingRect(hull_);
        if (d > std::sqrt(rect.width * rect.height) / 8 * dm) {
          return true;
        }
      }
    }
  }
  return false;
}

std::pair<hero, double> eye::parse(uint8_t* image) noexcept
{
  constexpr auto norm_type = cv::NORM_INF;

  auto hero_class = hero::unknown;
  auto hero_error = std::numeric_limits<double>::max();

  auto src = cv::Mat(sw, sh, CV_8UC4, image, eye::sw * 4);
  cv::cvtColor(src(cv::Rect(0, 0, hw, hh)), hero_scan_, cv::COLOR_RGBA2GRAY);
  for (size_t i = 0; i < hero_scans_.size(); i++) {
    const auto error = cv::norm(hero_scan_, hero_scans_[i]);
    if (error < hero_error) {
      hero_class = static_cast<hero>(i);
      hero_error = error;
    }
  }

  return std::make_pair(hero_class, hero_error / (hw * hh));
}

void eye::draw(uint8_t* image, int64_t pf, int64_t os, int64_t ps, int64_t cs) noexcept
{
  // Find new polygons.
  if (contours_.size()) {
    polygons_.resize(contours_.size());
    for (size_t i = 0, size = contours_.size(); i < size; i++) {
      cv::convexHull(cv::Mat(contours_[i]), polygons_[i]);
    }
  }

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

  for (size_t i = 0; i < cursor_interpolation_size_; i++) {
    const auto sx = static_cast<long>(cursor_interpolation_[i].x);
    const auto sy = static_cast<long>(cursor_interpolation_[i].y);
    if (sx < 4 || sx > sw - 4 || sy < 4 || sy > sh - 4) {
      continue;
    }

    const overlay_color ooc(oc);
    const overlay_color oic(ic);

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