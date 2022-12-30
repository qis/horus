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

void ammo2mask(uint8_t* si, uint8_t* di) noexcept
{
  int min = 0xFF;
  int max = 0x00;
  for (uint32_t y = 0; y < eye::ah; y++) {
    for (uint32_t x = 0; x < eye::aw; x++) {
      *di = static_cast<uint8_t>(si[0] * 0.299f + si[1] * 0.587f + si[2] * 0.114f);
      if (*di < min) {
        min = *di;
      }
      if (*di > max) {
        max = *di;
      }
      di++;
      si += 4;
    }
    si += (eye::sw - eye::aw) * 4;
  }
  di -= eye::aw * eye::ah;
  const auto threshold = max > min ? static_cast<uint8_t>(min + (max - min) * 0.8f) : uint8_t(0xFF);
  for (uint32_t i = 0; i < eye::aw * eye::ah; i++) {
    *di = *di > threshold ? 0x01 : 0x00;
    di++;
  }
}

}  // namespace

eye::eye()
{
  hierarchy_.reserve(1024);
  contours_.reserve(1024);
  polygons_.reserve(1024);

  std::vector<uint8_t> ammo_data;
  ammo_data.resize(sw * sh * 4);
  std::fill(ammo_data.begin(), ammo_data.end(), 0x00);
  cv::Mat ammo_scan(sw, sh, CV_8UC4, ammo_data.data(), sw * 4);

  for (std::size_t i = 0; i < ammo_masks_.size(); i++) {
    auto bgra = cv::imread(std::format(HORUS_RES "/ammo/{:01d}.png", i), cv::IMREAD_UNCHANGED);
    assert(bgra.cols == static_cast<int>(aw));
    assert(bgra.rows == static_cast<int>(ah));
    assert(bgra.channels() == 4);

    cv::Mat rgba(cv::Size(aw, ah), CV_8UC4);
    cv::cvtColor(bgra, rgba, cv::COLOR_BGRA2RGBA);

    rgba.copyTo(ammo_scan(cv::Rect(0, 0, aw, ah)));
    ammo2mask(ammo_data.data(), ammo_masks_[i].data());
  }

  selections_.push_back({ 0x402F1D, 308, 831, { 1, 0 }, "Ashe" });
  selections_.push_back({ 0x402F1C, 361, 831, { 1, 0 }, "Bastion" });
  selections_.push_back({ 0xE3E1BD, 429, 831, { 1, 1 }, "Cassidy" });
  selections_.push_back({ 0x402E1E, 500, 831, { 1, 2 }, "Echo" });
  selections_.push_back({ 0x402E1D, 566, 831, { 1, 3 }, "Genji" });
  selections_.push_back({ 0x402F1C, 633, 831, { 1, 4 }, "Hanzo" });
  selections_.push_back({ 0x402F1C, 716, 831, { 1, 5 }, "Junkrat" });
  selections_.push_back({ 0x402F1D, 772, 831, { 1, 6 }, "Mei" });
  selections_.push_back({ 0x402F1D, 813, 831, { 1, 7 }, "Pharah" });
  selections_.push_back({ 0x402F1C, 332, 895, { 0, 0 }, "Reaper" });
  selections_.push_back({ 0x402E1D, 390, 895, { 0, 1 }, "Sojourn" });
  selections_.push_back({ 0x402F1D, 454, 895, { 0, 2 }, "Soldier: 76" });
  selections_.push_back({ 0x402E1D, 521, 895, { 0, 3 }, "Sombra" });
  selections_.push_back({ 0x402F1D, 622, 895, { 0, 4 }, "Symmetra" });
  selections_.push_back({ 0x402F1C, 650, 895, { 0, 5 }, "Torbjörn" });
  selections_.push_back({ 0x402E1E, 715, 895, { 0, 6 }, "Tracer" });
  selections_.push_back({ 0x402F1C, 776, 895, { 0, 7 }, "Widowmaker" });
}

bool eye::scan(const uint8_t* image, int32_t mx, int32_t my) noexcept
{
  // Vertical iteration range.
  const auto range = tbb::blocked_range<size_t>(ah + 1, sh - 1, 64);

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
    return false;
  }

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

  // Remove protrusions.
  //cv::morphologyEx(overlays_image_, overlays_image_, cv::MORPH_OPEN, cv::Mat::ones(64, 32, CV_8UC1));

  // Erode new contours.
  cv::erode(overlays_image_, overlays_image_, erode_kernel_);

  // Find new contours.
  cv::findContours(overlays_image_, contours_, hierarchy_, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

  // Create cursor interpolation.
  constexpr auto x0 = sw / 2.0f;
  constexpr auto y0 = sh / 2.0f;

  const auto dx = mx * cm;
  const auto dy = my * cm;

  cursor_interpolation_[0] = { x0 + dx, y0 + dy };
  cursor_interpolation_[1] = { x0 + dx * 0.96f, y0 + dy * 0.96f };
  cursor_interpolation_[2] = { x0 + dx * 0.90f, y0 + dy * 0.90f };
  cursor_interpolation_[3] = { x0 + dx * 0.82f, y0 + dy * 0.82f };
  cursor_interpolation_[4] = { x0 + dx * 0.72f, y0 + dy * 0.72f };
  cursor_interpolation_[5] = { x0 + dx * 0.60f, y0 + dy * 0.60f };
  cursor_interpolation_[6] = { x0 + dx * 0.46f, y0 + dy * 0.46f };

  // Check if the cursor is targeting an enemy.
  bool target = false;
  for (size_t i = 0, max = contours_.size(); i < max; i++) {
    for (const auto& point : contours_[i]) {
    }

    // Create convex hull.
    cv::convexHull(cv::Mat(contours_[i]), hull_);

    // Get bounding rect for the hull.
    const auto rect = cv::boundingRect(hull_);

    // Find max left and right edges based on the top 20% of points and raise hull to include the head.
    auto ml = static_cast<float>(dw);
    auto mr = 0.0f;
    for (auto& point : hull_) {
      if (point.y < rect.y + rect.height * 0.2f) {
        if (point.x < ml) {
          ml = point.x;
        }
        if (point.x > mr) {
          mr = point.x;
        }
      }
      point.y -= rect.height / 9;
    }

    // Modify hull.
    if (mr >= ml) {
      // Max left and right difference.
      auto md = mr - ml;

      // Make sure the max left and right difference is at least 10% of the model width.
      if (md < rect.width * 0.1f) {
        ml -= (rect.width * 0.1f - md) / 2.0f;
        mr += (rect.width * 0.1f - md) / 2.0f;
        md = mr - ml;
      }

      // Make sure the max left and right difference is at most 10/30% of the model width.
#if HERO_WIDOWMAKER
      constexpr auto max_width = 0.1f;
#else
      constexpr auto max_width = 0.3f;
#endif
      if (md > rect.width * max_width) {
        ml += (md - rect.width * max_width) / 2.0f;
        mr -= (md - rect.width * max_width) / 2.0f;
        md = mr - ml;
      }

      // Make sure the max left and right difference is at least 12 pixels wide.
      if (md < 12.0f) {
        ml -= (12.0f - md) / 2.0f;
        mr += (12.0f - md) / 2.0f;
        hull_.resize(4);
        hull_[0].x = ml;
        hull_[0].y = rect.y - rect.height / 9;
        hull_[1].x = ml;
        hull_[1].y = rect.y - rect.height / 9 + rect.height / 2;
        hull_[2].x = mr;
        hull_[2].y = rect.y - rect.height / 9;
        hull_[3].x = mr;
        hull_[3].y = rect.y - rect.height / 9 + rect.height / 2;
      } else {
        for (auto& point : hull_) {
          if (point.x < ml) {
            point.x = ml;
          } else if (point.x > mr) {
            point.x = mr;
          }
        }
      }
    }

    // Check if target is acquired.
    for (const auto& cip : cursor_interpolation_) {
      if (cip.x < 0 || cip.x > sw || cip.y < 0 || cip.y > sh) {
        continue;
      }
      if (cv::pointPolygonTest(hull_, cip, false) > 0.0) {
        if (!DRAW_OVERLAY) {
          return true;
        }
        target = true;
        break;
      }
    }

    // Store hull as contour for the draw call.
    contours_[i] = std::move(hull_);
  }
  return target;
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

  for (const auto& cip : cursor_interpolation_) {
    const auto sx = static_cast<long>(cip.x);
    const auto sy = static_cast<long>(cip.y);

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

std::pair<int, float> eye::ammo(uint8_t* image) noexcept
{
  auto count = -1;
  auto error = 1.0f;
  ammo2mask(image, ammo_mask_.data());
  for (int i = 0; i < ammo_masks_.size(); i++) {
    uint32_t mismatches = 0;
    auto si = ammo_mask_.data();
    auto ci = ammo_masks_[i].data();
    for (auto j = 0; j < aw * ah; j++) {
      if (*si != *ci) {
        mismatches++;
      }
      si++;
      ci++;
    }
    if (const auto e = static_cast<float>(mismatches) / (aw * ah); e < error) {
      count = i;
      error = e;
    }
  }
#if DRAW_OVERLAY && 0
  for (uint32_t y = 0; y < ah; y++) {
    for (uint32_t x = 0; x < aw; x++) {
      if (ammo_mask_[y * aw + x]) {
        image[(y * sw + x) * 4 + 0] = 0xFF;
        image[(y * sw + x) * 4 + 1] = 0xFF;
        image[(y * sw + x) * 4 + 2] = 0xFF;
      } else {
        image[(y * sw + x) * 4 + 0] = 0x00;
        image[(y * sw + x) * 4 + 1] = 0x00;
        image[(y * sw + x) * 4 + 2] = 0x00;
      }
    }
  }
#endif
  return { count, error };
}

std::optional<std::pair<uint16_t, uint16_t>> eye::hero(uint8_t* image) noexcept
{
  //if (!selections_.empty()) {
  //  static size_t i = 0;
  //  if (i >= selections_.size()) {
  //    i = 0;
  //  }
  //  const auto& selection = selections_[i++];
  //  const auto p = image + selection.y * sw * 4 + selection.x * 4;
  //  log("0x{:02X}{:02X}{:02X} {}", p[0], p[1], p[2], selection.name);
  //}

  std::optional<std::pair<uint16_t, uint16_t>> result;
  for (const auto& selection : selections_) {
    const auto r = static_cast<uint8_t>(selection.c >> 16 & 0xFF);
    const auto g = static_cast<uint8_t>(selection.c >> 8 & 0xFF);
    const auto b = static_cast<uint8_t>(selection.c & 0xFF);
    const auto p = image + selection.y * sw * 4 + selection.x * 4;
    if (r == p[0] && g == p[1] && b == p[2]) {
      if (result) {
        return std::nullopt;
      }
      result = selection.arrows;
    }
  }
  return result;
}

std::optional<cv::Point> eye::find() noexcept
{
  auto cx = 0;
  auto cy = 0;
  for (const auto& contour : contours_) {
    const auto count = contour.size();
    if (!count) {
      continue;
    }
    auto ax = 0;
    auto ay = 0;
    for (const auto& point : contour) {
      ax += point.x;
      ay += point.y;
    }
    ax /= count;
    ay /= count;

    constexpr auto cw = static_cast<int>(sw / 2);
    constexpr auto ch = static_cast<int>(sh / 2);
    if (std::abs(ax - cw) < std::abs(cx - cw)) {
      cx = ax;
      cy = ay - cv::boundingRect(contour).height / 4;
    }
  }
  if (cx) {
    return cv::Point(cx, cy);
  }
  return std::nullopt;
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