#include "scan.hpp"
#include <opencv2/imgproc.hpp>
#include <tbb/parallel_for.h>

namespace horus::scan {
namespace {

// Returns true if the `si` src iterator pixel is magenta.
constexpr bool is_outline(const uint8_t* si) noexcept
{
  return si[0] > 0xA0 && si[1] < 0x60 && si[2] > 0xA0;
}

// Returns true if the `si` src iterator pixel is magenta and
// 1-4 adjacent pixels also return true for `depth` recursive calls.
constexpr bool is_outline(const uint8_t* si, unsigned depth) noexcept
{
  constexpr unsigned max = 4;       // max number of adjacent outline pixels (0-8)
  const uint8_t* pi = si - sw * 4;  // pixel above the src iterator
  const uint8_t* ni = si + sw * 4;  // pixel below the src iterator
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

// Converts RGBA pixel to GRAY without changing number of channels.
constexpr void rgba2gray(uint8_t* di) noexcept
{
  const auto v = di[0] * 0.299f + di[1] * 0.587f + di[2] * 0.114f;
  const auto l = static_cast<uint8_t>(v);
  di[0] = l;
  di[1] = l;
  di[2] = l;
}

// Sets `count` number of RGBA pixels and returns number of written bytes.
constexpr unsigned set(uint8_t* di, uint8_t r, uint8_t g, uint8_t b, unsigned count = 1) noexcept
{
  for (unsigned i = 0; i < count; i++) {
    di[0] = r;
    di[1] = g;
    di[2] = b;
    di += 4;
  }
  return count * 4;
}

}  // namespace

void filter(const uint8_t* src, uint8_t* dst, unsigned depth) noexcept
{
  static const auto dilate = cv::getStructuringElement(cv::MORPH_ELLIPSE, { 6, 6 });
  static const auto erode = cv::getStructuringElement(cv::MORPH_ELLIPSE, { 6, 6 });

  std::memset(dst, 0, sw * sh);
  const auto range = tbb::blocked_range<size_t>(depth + 1, sh - depth - 1, 64);
  tbb::parallel_for(range, [&](const tbb::blocked_range<size_t>& range) {
    const size_t rb = range.begin();
    const size_t re = range.end();
    auto si = src + rb * sw * 4;  // src iterator
    auto di = dst + rb * sw;      // dst iterator
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

  cv::Mat fi(sw, sh, CV_8UC1, dst, sw);
  cv::dilate(fi, fi, dilate);
  cv::erode(fi, fi, erode);
}

double find(
  uint8_t* src,
  std::vector<cv::Vec4i>& hierarchy,
  std::vector<std::vector<cv::Point>>& contours,
  std::vector<std::vector<cv::Point>>& polygons) noexcept
{
  cv::Mat si(sw, sh, CV_8UC1, src, sw);
  cv::findContours(si, contours, hierarchy, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

  // TODO: Merge countours that likely belong to a single target.
  // TODO: Merge countours that likely belong to a single target.

  polygons.resize(contours.size());
  for (size_t i = 0, size = contours.size(); i < size; i++) {
    cv::convexHull(cv::Mat(contours[i]), polygons[i]);
  }

  auto result = -1.0;
  const auto center = cv::Point2f(sw / 2.0f, sh / 2.0f);
  for (size_t i = 0, size = polygons.size(); i < size; i++) {
    if (auto distance = cv::pointPolygonTest(polygons[i], center, true); distance > 0.0) {
      result = distance;
      break;
    }
  }
  return result;
}

void draw(
  const std::vector<std::vector<cv::Point>>& contours,
  const std::vector<std::vector<cv::Point>>& polygons,
  uint8_t* overlay,
  uint8_t* dst,
  float alpha,
  bool cross,
  bool gray) noexcept
{
  // Create overlay image.
  std::memset(overlay, 0, sw * sh);
  cv::Mat oi(sw, sh, CV_8UC1, overlay, sw);
  for (size_t i = 0, size = polygons.size(); i < size; i++) {
    cv::drawContours(oi, polygons, i, cv::Scalar(255, 255, 255), -1, cv::LINE_AA);
  }

  // Draw overlay image.
  const auto range = tbb::blocked_range<size_t>(0, sh, 64);
  tbb::parallel_for(range, [&](const tbb::blocked_range<size_t>& range) {
    const size_t rb = range.begin();
    const size_t re = range.end();
    auto si = overlay + rb * sw;  // src iterator
    auto di = dst + rb * sw * 4;  // dst iterator
    for (auto y = rb; y < re; y++) {
      for (auto x = 0; x < sw; x++) {
        if (gray) {
          rgba2gray(di);
        }
        if (*si > 0) {
          const auto a = *si / 255.0f * alpha;
          di[0] = static_cast<uint8_t>(di[0] * (1.0f - a) + 0xA0 * a);
          di[1] = static_cast<uint8_t>(di[1] * (1.0f - a) + 0x1E * a);
          di[2] = static_cast<uint8_t>(di[2] * (1.0f - a) + 0x32 * a);
        }
        si += 1;
        di += 4;
      }
    }
  });

  // Draw contours.
  cv::Mat di(sw, sh, CV_8UC4, dst, sw * 4);
  for (size_t i = 0, size = contours.size(); i < size; i++) {
    cv::drawContours(di, contours, i, cv::Scalar(160, 30, 50, 100), 1, cv::LINE_4);
  }

  // Draw polygons.
  for (size_t i = 0, size = polygons.size(); i < size; i++) {
    cv::drawContours(di, polygons, i, cv::Scalar(240, 50, 70, 255), 1, cv::LINE_AA);
  }

  // Draw crosshair.
  if (cross) {
    auto di = dst + (sh / 2 - 2) * sw * 4 + (sw / 2 - 2) * 4;
    // Line 1.
    di += set(di, 0xFF, 0xFF, 0xFF, 4);
    di += sw * 4 - 5 * 4;

    // Line 2.
    di += set(di, 0xFF, 0xFF, 0xFF, 2);
    di += set(di, 0x14, 0x78, 0xB7, 2);
    di += set(di, 0xFF, 0xFF, 0xFF, 2);
    di += sw * 4 - 6 * 4;

    // Line 3.
    di += set(di, 0xFF, 0xFF, 0xFF);
    di += set(di, 0x14, 0x78, 0xB7, 4);
    di += set(di, 0xFF, 0xFF, 0xFF);
    di += sw * 4 - 6 * 4;

    // Line 4.
    di += set(di, 0xFF, 0xFF, 0xFF);
    di += set(di, 0x14, 0x78, 0xB7, 4);
    di += set(di, 0xFF, 0xFF, 0xFF);
    di += sw * 4 - 6 * 4;

    // Line 5.
    di += set(di, 0xFF, 0xFF, 0xFF, 2);
    di += set(di, 0x14, 0x78, 0xB7, 2);
    di += set(di, 0xFF, 0xFF, 0xFF, 2);
    di += sw * 4 - 5 * 4;

    // Line 6.
    di += set(di, 0xFF, 0xFF, 0xFF, 4);
  }
}

}  // namespace horus::scan