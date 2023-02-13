#include "eye.hpp"
#include "mulxp_hash.hpp"
#include <cuda_runtime.h>
#include <device_launch_parameters.h>
#include <opencv2/core/cuda/common.hpp>
#include <algorithm>
#include <numeric>
#include <cassert>

#define SHAPES_FILTER_FAST 0

namespace horus {
namespace {

using cv::cuda::device::divUp;

// Sets mask values to:
// - 0x02 if they have too many neighbors.
__global__ void mask_filter(uchar* data, size_t step)
{
  constexpr auto r = 3;
  constexpr auto d = r * 2 + 1;
  const auto x = blockIdx.x * blockDim.x + threadIdx.x;
  const auto y = blockIdx.y * blockDim.y + threadIdx.y;
  if (x < r || x >= eye::vw - r || y < r || y >= eye::vh - r) {
    return;
  }
  const auto di = data + y * step + x;
  const auto dv = *di;
  if (!dv) {
    return;
  }
  auto neighbors = -1;
  auto si = di - step * r - r;
  for (auto i = 0; i < d; i++) {
    for (auto j = 0; j < d; j++) {
      const auto sc = *si++;
      if (sc && ++neighbors > 9) {
        *di = 0x02;
        return;
      }
    }
    si += step - d;
  }
}

// Sets mask values to:
// - 0x00 if they were outside the filter radius.
__global__ void mask_shrink(uchar* data, size_t step)
{
  constexpr auto r = 3;
  const auto x = blockIdx.x * blockDim.x + threadIdx.x;
  const auto y = blockIdx.y * blockDim.y + threadIdx.y;
  if (x >= eye::vw || y >= eye::vh) {
    return;
  }
  const auto di = data + y * step + x;
  if (x < r || x >= eye::vw - r || y < r || y >= eye::vh - r) {
    *di = 0x00;
  }
}

// Sets mask values to:
// - 0x04 if they have a 0x02 neighbor.
__global__ void mask_dilate(uchar* data, size_t step)
{
  constexpr auto r = 3;
  constexpr auto d = r * 2 + 1;
  const auto x = blockIdx.x * blockDim.x + threadIdx.x;
  const auto y = blockIdx.y * blockDim.y + threadIdx.y;
  if (x < r || x >= eye::vw - r || y < r || y >= eye::vh - r) {
    return;
  }
  const auto di = data + y * step + x;
  const auto dv = *di;
  if (dv != 0x01) {
    return;
  }
  auto si = di - step * r - r;
  for (auto i = 0; i < d; i++) {
    for (auto j = 0; j < d; j++) {
      const auto sc = *si++;
      if (sc == 0x02) {
        *di = 0x04;
        return;
      }
    }
    si += step - d;
  }
}

// Sets 0x01 bit on filtered or dilated mask values that have a neighbor with the 0x01 bit set.
__global__ void mask_erode(uchar* data, size_t step)
{
  constexpr auto r = 1;
  constexpr auto d = r * 2 + 1;
  const auto x = blockIdx.x * blockDim.x + threadIdx.x;
  const auto y = blockIdx.y * blockDim.y + threadIdx.y;
  if (x < r || x >= eye::vw - r || y < r || y >= eye::vh - r) {
    return;
  }
  const auto di = data + y * step + x;
  const auto dv = *di;
  if (dv != 0x02 && dv != 0x04) {
    return;
  }
  auto si = di - step * r - r;
  for (auto i = 0; i < d; i++) {
    for (auto j = 0; j < d; j++) {
      const auto sc = *si++;
      if (sc & 0x01) {
        *di |= 0x01;
        return;
      }
    }
    si += step - d;
  }
}

// Draws mask values as 0x01 if the 0x01 bit is set.
__global__ void mask_scan(const uchar* data, size_t data_step, uchar* scan, size_t scan_step)
{
  const auto x = blockIdx.x * blockDim.x + threadIdx.x;
  const auto y = blockIdx.y * blockDim.y + threadIdx.y;
  if (x >= eye::vw || y >= eye::vh) {
    return;
  }
  scan[y * scan_step + x] = data[y * data_step + x] & 0x01 ? 0x01 : 0x00;
}

// Sets RGBA pixel value.
__device__ __forceinline__ void device_set(uchar* di, std::uint32_t color) noexcept
{
  *di++ = static_cast<uchar>(color >> 24 & 0xFF);
  *di++ = static_cast<uchar>(color >> 16 & 0xFF);
  *di++ = static_cast<uchar>(color >> 8 & 0xFF);
  *di = static_cast<uchar>(color & 0xFF);
}

// Draws mask values as separate colors.
__global__ void mask_draw(const uchar* data, size_t data_step, uchar* view, size_t view_step)
{
  const auto x = blockIdx.x * blockDim.x + threadIdx.x;
  const auto y = blockIdx.y * blockDim.y + threadIdx.y;
  if (x >= eye::vw || y >= eye::vh) {
    return;
  }
  // clang-format off
  auto di = view + y * view_step + x * 4;
  switch (data[y * data_step + x]) {
  case 0x00: device_set(di, 0x00000000); break;  // None   |       | Transparent
  case 0x01: device_set(di, 0x64DD17FF); break;  // Mask   |       | A700 Light Green
  case 0x02: device_set(di, 0xD50000FF); break;  // Filter |       | A700 Red
  case 0x03: device_set(di, 0xEEFF41FF); break;  // Filter | Erode | A200 Lime
  case 0x04: device_set(di, 0xFF9100FF); break;  // Dilate |       | A400 Orange
  case 0x05: device_set(di, 0xF4FF81FF); break;  // Dilate | Erode | A100 Lime
  default:   device_set(di, 0xE040FBFF); break;  // Error  |       | A200 Purple
  }
  // clang-format on
}

// Sets shapes values to:
// - 0x00 if they are border pixels.
// - 0x02 if they are near a 0x01 value.
__global__ void shapes_dilate(uchar* data, size_t step)
{
  constexpr auto r = 2;
  constexpr auto d = r * 2 + 1;
  const auto x = blockIdx.x * blockDim.x + threadIdx.x;
  const auto y = blockIdx.y * blockDim.y + threadIdx.y;
  if (x >= eye::vw || y >= eye::vh) {
    return;
  }
  const auto di = data + y * step + x;
  if (x < r || x >= eye::vw - r || y < r || y >= eye::vh - r) {
    *di = 0x00;
    return;
  }
  if (*di) {
    return;
  }
  auto si = di - r * step - r;
  for (auto i = 0; i < d; i++) {
    for (auto j = 0; j < d; j++) {
      if (*si++ == 0x01) {
        *di = 0x02;
        return;
      }
    }
    si += step - d;
  }
}

// Sets shapes values to:
// - 0x01 if they are not masked.
// - 0x05 if they are masked.
__global__ void shapes_mask(uchar* data, size_t step, const uchar* mask, size_t mask_step)
{
  const auto x = blockIdx.x * blockDim.x + threadIdx.x;
  const auto y = blockIdx.y * blockDim.y + threadIdx.y;
  if (x >= eye::vw || y >= eye::vh) {
    return;
  }
  const auto di = data + y * step + x;
  if (!*di) {
    return;
  }
  *di = mask[y * mask_step + x] ? 0x05 : 0x01;
}

// Sets shapes values to:
// - 0x02 if they are 0x01 and should be filtered.
// - 0x03 if they are 0x01 and are a filtered border.
#if SHAPES_FILTER_FAST
__global__ void shapes_filter(uchar* data, size_t step)
{
  constexpr auto r = 1;
  constexpr auto d = r * 2 + 1;
  const auto x = blockIdx.x * blockDim.x + threadIdx.x;
  const auto y = blockIdx.y * blockDim.y + threadIdx.y;
  if (x < r || x >= eye::vw - r || y < r || y >= eye::vh - r) {
    return;
  }
  const auto di = data + y * step + x;
  if (*di != 0x01) {
    return;
  }
  auto filter = false;
  auto border = false;
  auto si = di - r * step - r;
  for (auto i = 0; i < d; i++) {
    for (auto j = 0; j < d; j++) {
      const auto sc = *si++;
      filter |= !sc || (sc & 0x02) != 0x00;
      border |= sc > 0x02;
    }
    si += step - d;
  }
  if (filter) {
    *di = border ? 0x03 : 0x02;
  }
}
#else
__global__ void shapes_filter(uchar* data, size_t step)
{
  constexpr auto r = 1;
  constexpr auto d = r * 2 + 1;
  const auto x = blockIdx.x * blockDim.x + threadIdx.x;
  const auto y = blockIdx.y * blockDim.y + threadIdx.y;
  if (x < r || x >= eye::vw - r || y < r || y >= eye::vh - r) {
    return;
  }
  const auto di = data + y * step + x;
  if (*di != 0x01) {
    return;
  }
  auto filter = 0;
  auto border = 0;
  auto si = di - r * step - r;
  const auto sv = *si;
  for (auto i = 0; i < d; i++) {
    for (auto j = 0; j < d; j++) {
      const auto sc = *si++;
      if (filter < 2) {
        if (!sc || (sc & 0x02) != 0x00) {
          ++filter;
        } else {
          filter = 0;
        }
      }
      if (sc > 0x02) {
        ++border;
      }
    }
    si += step - d;
  }
  if (filter > 1 || (filter == 1 && (!sv || (sv & 0x02) != 0x00))) {
    *di = border > 1 ? 0x03 : 0x02;
  }
}
#endif

// Sets shapes values to:
// - 0x03 if they are 0x01 and have a 0x03 neighbor.
__global__ void shapes_close(uchar* data, size_t step)
{
  constexpr auto r = 1;
  constexpr auto d = r * 2 + 1;
  const auto x = blockIdx.x * blockDim.x + threadIdx.x;
  const auto y = blockIdx.y * blockDim.y + threadIdx.y;
  if (x < r || x >= eye::vw - r || y < r || y >= eye::vh - r) {
    return;
  }
  const auto di = data + y * step + x;
  if (*di != 0x01) {
    return;
  }
  auto si = di - r * step - r;
  for (auto i = 0; i < d; i++) {
    for (auto j = 0; j < d; j++) {
      if (*si++ == 0x03) {
        *di = 0x03;
        return;
      }
    }
    si += step - d;
  }
}

// Sets shapes values to:
// - 0x02 if they are 0x03 and have a 0x02 neighbor.
__global__ void shapes_erode(uchar* data, size_t step)
{
  constexpr auto r = 1;
  constexpr auto d = r * 2 + 1;
  const auto x = blockIdx.x * blockDim.x + threadIdx.x;
  const auto y = blockIdx.y * blockDim.y + threadIdx.y;
  if (x < r || x >= eye::vw - r || y < r || y >= eye::vh - r) {
    return;
  }
  const auto di = data + y * step + x;
  if (*di != 0x03) {
    return;
  }
  auto si = di - r * step - r;
  for (auto i = 0; i < d; i++) {
    for (auto j = 0; j < d; j++) {
      if (*si++ == 0x02) {
        *di = 0x02;
        return;
      }
    }
    si += step - d;
  }
}

// Draws shapes values as 0x01 if the 0x01 bit is set.
__global__ void shapes_scan(const uchar* data, size_t data_step, uchar* scan, size_t scan_step)
{
  const auto x = blockIdx.x * blockDim.x + threadIdx.x;
  const auto y = blockIdx.y * blockDim.y + threadIdx.y;
  if (x >= eye::vw || y >= eye::vh) {
    return;
  }
  scan[y * scan_step + x] = data[y * data_step + x] & 0x01 ? 0x01 : 0x00;
}

__global__ void shapes_draw(const uchar* data, size_t data_step, uchar* view, size_t view_step)
{
  const auto x = blockIdx.x * blockDim.x + threadIdx.x;
  const auto y = blockIdx.y * blockDim.y + threadIdx.y;
  if (x >= eye::vw || y >= eye::vh) {
    return;
  }
  // clang-format off
  auto di = view + y * view_step + x * 4;
  switch (data[y * data_step + x]) {
  case 0x00: device_set(di, 0x00000000); break;  // None     | Transparent
  case 0x01: device_set(di, 0x689F387F); break;  // Shape    | 700 Light Green
  case 0x02: device_set(di, 0xD32F2F7F); break;  // Filtered | 700 Red
  case 0x03: device_set(di, 0xFFD6007F); break;  // Border   | A700 Yellow
  case 0x05: device_set(di, 0x64DD17FF); break;  // Outline  | A700 Light Green
  default:   device_set(di, 0xE040FBFF); break;  // Error    | A200 Purple
  }
  // clang-format on
}

}  // namespace

const cv::Point eye::vc{ vw / 2, vh / 2 };

eye::eye()
{
  scan_.setTo(cv::Scalar(0));
  scan_hash_ = mulxp3_hash(scan_.data, scan_.step * scan_.rows, 0);
  freetype_ = cv::freetype::createFreeType2();
  freetype_->loadFontData("C:/OBS/horus/res/fonts/PixelOperatorMono.ttf", 0);
}

bool eye::scan(const cv::Mat& scan) noexcept
{
  // Verify scan type and size.
  assert(scan.type() == CV_8UC1);
  assert(scan.cols == sw);
  assert(scan.rows == sh);

  // Measure scan duration.
  const auto tp0 = clock::now();

  // Resize scan (120 μs).
  cv::resize(scan, scan_, { vw, vh }, 1.0 / vf, 1.0 / vf, cv::INTER_AREA);

  // Update hash (30 μs).
  const auto scan_hash = mulxp3_hash(scan_.data, scan_.step * scan_.rows, 0);
  if (scan_hash == scan_hash_) {
    return false;
  }
  scan_duration_ = clock::now() - tp0;
  scan_hash_ = scan_hash;

  hulls_ready_ = false;
  polygons_ready_ = false;
  return true;
}

const std::vector<eye::polygon>& eye::hulls() noexcept
{
  if (hulls_ready_) {
    return hulls_;
  }

  const auto tp0 = clock::now();
  mask_data_.upload(scan_);
#ifndef __INTELLISENSE__
  const dim3 block(16, 16);
  const dim3 grid(divUp(eye::vw, block.x), divUp(eye::vh, block.y));
  mask_filter<<<grid, block>>>(mask_data_.data, mask_data_.step);
  assert(cudaGetLastError() == cudaSuccess);
  mask_shrink<<<grid, block>>>(mask_data_.data, mask_data_.step);
  assert(cudaGetLastError() == cudaSuccess);
  mask_dilate<<<grid, block>>>(mask_data_.data, mask_data_.step);
  assert(cudaGetLastError() == cudaSuccess);
  for (auto i = 0; i < 6; i++) {
    mask_erode<<<grid, block>>>(mask_data_.data, mask_data_.step);
    assert(cudaGetLastError() == cudaSuccess);
  }
  mask_scan<<<grid, block>>>(mask_data_.data, mask_data_.step, mask_view_.data, mask_view_.step);
  assert(cudaGetLastError() == cudaSuccess);
#endif
  mask_view_.download(mask_);

  const auto tp1 = clock::now();
  mask_duration_ = tp1 - tp0;

  cv::findContours(mask_, contours_, hierarchy_, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

  const auto tp2 = clock::now();
  contours_duration_ = tp2 - tp1;

  hulls_.resize(contours_.size());
  for (std::size_t i = 0, size = contours_.size(); i < size; i++) {
    cv::convexHull(contours_[i], hulls_[i]);
  }

  const auto tp3 = clock::now();
  hulls_duration_ = tp3 - tp2;

  while (true) {
    const auto se = hulls_.end();
    for (auto si = hulls_.begin(); si != se; ++si) {
      const auto srect = cv::boundingRect(*si);
      if (srect.width < 3 || srect.height < 9) {
        hulls_.erase(si);
        goto repeat;
      }
      const auto sl = srect.x;
      const auto sr = srect.x + srect.width;
      const auto st = srect.y;
      const auto sb = srect.y + srect.height;
      for (auto di = std::next(si); di != se; ++di) {
        const auto drect = cv::boundingRect(*di);
        const auto dl = drect.x;
        const auto dr = drect.x + drect.width;
        const auto dt = drect.y;
        const auto db = drect.y + drect.height;
        if (sr < dl - 8 || sl > dr + 8 || sb < dt - 32 || st > db + 32) {
          continue;
        }
        si->reserve(si->size() + di->size());
        si->insert(si->end(), di->begin(), di->end());
        cv::convexHull(*si, *di);
        hulls_.erase(si);
        goto repeat;
      }
    }
    break;
  repeat:
    continue;
  }

  const auto tp4 = clock::now();
  groups_duration_ = tp4 - tp3;

  hulls_ready_ = true;
  return hulls_;
}

const std::vector<eye::polygon>& eye::polygons() noexcept
{
  if (polygons_ready_) {
    return polygons_;
  }
  if (!hulls_ready_) {
    hulls();
  }

  const auto tp0 = clock::now();
  shapes_.setTo(cv::Scalar(0));
  for (const auto& hull : hulls_) {
    const auto rect = cv::boundingRect(hull);
    cv::fillPoly(shapes_, hull, cv::Scalar(1), cv::LINE_4);
  }

  shapes_data_.upload(shapes_);
#ifndef __INTELLISENSE__
  const dim3 block(16, 16);
  const dim3 grid(divUp(eye::vw, block.x), divUp(eye::vh, block.y));
  shapes_dilate<<<grid, block>>>(shapes_data_.data, shapes_data_.step);
  assert(cudaGetLastError() == cudaSuccess);
  shapes_mask<<<grid, block>>>(shapes_data_.data, shapes_data_.step, mask_data_.data, mask_data_.step);
  assert(cudaGetLastError() == cudaSuccess);
  for (auto i = 0; i < 16; i++) {
    for (auto j = 0; j < 4; j++) {
      shapes_filter<<<grid, block>>>(shapes_data_.data, shapes_data_.step);
      assert(cudaGetLastError() == cudaSuccess);
    }
    shapes_close<<<grid, block>>>(shapes_data_.data, shapes_data_.step);
    assert(cudaGetLastError() == cudaSuccess);
  }
  shapes_erode<<<grid, block>>>(shapes_data_.data, shapes_data_.step);
  assert(cudaGetLastError() == cudaSuccess);
  shapes_scan<<<grid, block>>>(shapes_data_.data, shapes_data_.step, shapes_view_.data, shapes_view_.step);
  assert(cudaGetLastError() == cudaSuccess);
#endif
  shapes_view_.download(shapes_);

  const auto tp1 = clock::now();
  shapes_duration_ = tp1 - tp0;

  cv::findContours(shapes_, contours_, hierarchy_, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
  polygons_.resize(contours_.size());
  for (std::size_t i = 0, size = contours_.size(); i < size; i++) {
    cv::approxPolyDP(contours_[i], polygons_[i], 2.0, true);
  }

  const auto tp2 = clock::now();
  polygons_duration_ = tp2 - tp1;

  polygons_ready_ = true;
  return polygons_;
}

clock::duration eye::draw_scan(cv::Mat& overlay) noexcept
{
  assert(overlay.type() == CV_8UC4);
  assert(overlay.cols == vw);
  assert(overlay.rows == vh);

  overlay.setTo(scalar(0x64DD17FF), scan_);  // A700 Light Green

  return scan_duration_;
}

clock::duration eye::draw_mask(cv::Mat& overlay) noexcept
{
  assert(overlay.type() == CV_8UC4);
  assert(overlay.cols == vw);
  assert(overlay.rows == vh);

  if (!hulls_ready_) {
    hulls();
  }

#ifndef __INTELLISENSE__
  const dim3 block(16, 16);
  const dim3 grid(divUp(sw, block.x), divUp(sh, block.y));
  mask_draw<<<grid, block>>>(mask_data_.data, mask_data_.step, view_.data, view_.step);
  assert(cudaGetLastError() == cudaSuccess);
#endif
  view_.download(overlay);

  return mask_duration_;
}

clock::duration eye::draw_contours(cv::Mat& overlay) noexcept
{
  assert(overlay.type() == CV_8UC4);
  assert(overlay.cols == vw);
  assert(overlay.rows == vh);

  if (!hulls_ready_) {
    hulls();
  }

  overlay.setTo(scalar(0x64DD17FF), mask_);  // A700 Light Green

  return contours_duration_;
}

clock::duration eye::draw_groups(cv::Mat& overlay) noexcept
{
  draw_contours(overlay);

  for (const auto& hull : hulls_) {
    cv::rectangle(overlay, cv::boundingRect(hull), scalar(0x00B0FFFF), 1, cv::LINE_8);  // A400 Light Blue
  }

  return groups_duration_;
}

clock::duration eye::draw_hulls(cv::Mat& overlay) noexcept
{
  assert(overlay.type() == CV_8UC4);
  assert(overlay.cols == vw);
  assert(overlay.rows == vh);

  if (!hulls_ready_) {
    hulls();
  }

  cv::fillPoly(overlay, hulls_, scalar(0x64DD1760), cv::LINE_4);            // A700 Light Green
  cv::polylines(overlay, hulls_, true, scalar(0x64DD17FF), 1, cv::LINE_4);  // A700 Light Green

  return hulls_duration_;
}

clock::duration eye::draw_shapes(cv::Mat& overlay) noexcept
{
  assert(overlay.type() == CV_8UC4);
  assert(overlay.cols == vw);
  assert(overlay.rows == vh);

  if (!polygons_ready_) {
    polygons();
  }

#ifndef __INTELLISENSE__
  const dim3 block(16, 16);
  const dim3 grid(divUp(sw, block.x), divUp(sh, block.y));
  shapes_draw<<<grid, block>>>(shapes_data_.data, shapes_data_.step, view_.data, view_.step);
  assert(cudaGetLastError() == cudaSuccess);
#endif
  view_.download(overlay);

  return shapes_duration_;
}

clock::duration eye::draw_polygons(cv::Mat& overlay) noexcept
{
  assert(overlay.type() == CV_8UC4);
  assert(overlay.cols == vw);
  assert(overlay.rows == vh);

  if (!polygons_ready_) {
    polygons();
  }

  cv::fillPoly(overlay, polygons_, scalar(0x64DD1760), cv::LINE_4);            // A700 Light Green
  cv::polylines(overlay, polygons_, true, scalar(0x64DD17FF), 1, cv::LINE_4);  // A700 Light Green

  return polygons_duration_;
}

void eye::draw(
  cv::Mat& overlay,
  cv::Point position,
  const std::string& text,
  int height,
  std::uint32_t fg,
  std::uint32_t bg) noexcept
{
  const auto fgc = scalar(fg);
  const auto bgc = scalar(bg);
  if (freetype_ && !freetype_->empty()) {
    if (bg & 0xFF) {
      freetype_->putText(overlay, text, position, height, bgc, 2, cv::LINE_AA, false);
    }
    freetype_->putText(overlay, text, position, height, fgc, 1, cv::LINE_4, false);
    return;
  }
  const auto font = cv::FONT_HERSHEY_PLAIN;
  const auto scale = height / 16.0;
  if (bg & 0xFF) {
    cv::putText(overlay, text, position, font, scale, bgc, 2, cv::LINE_AA);
  }
  cv::putText(overlay, text, position, font, scale, fgc, 1, cv::LINE_4);
}

void eye::draw(cv::Mat& overlay, cv::Point point, std::uint32_t fg, std::uint32_t bg) noexcept
{
  const auto set = [](uchar* di, std::uint32_t color, unsigned count) noexcept {
    for (unsigned i = 0; i < count; i++) {
      *di++ = static_cast<uchar>(color >> 24 & 0xFF);
      *di++ = static_cast<uchar>(color >> 16 & 0xFF);
      *di++ = static_cast<uchar>(color >> 8 & 0xFF);
      *di++ = static_cast<uchar>(color & 0xFF);
    }
    return di;
  };

  if (point.x < 2 || point.x > eye::sw - 3 || point.y < 2 || point.y > eye::sh - 3) {
    return;
  }

  const auto x = static_cast<long>(point.x);
  const auto y = static_cast<long>(point.y);

  const size_t step = overlay.step;

  auto di = overlay.data + (y - 2) * step + (x - 2) * 4;  // dst iterator

  // Line 1.
  di = set(di, bg, 4);
  di = di + step - 5 * 4;

  // Line 2.
  di = set(di, bg, 2);
  di = set(di, fg, 2);
  di = set(di, bg, 2);
  di = di + step - 6 * 4;

  // Line 3.
  di = set(di, bg, 1);
  di = set(di, fg, 4);
  di = set(di, bg, 1);
  di = di + step - 6 * 4;

  // Line 4.
  di = set(di, bg, 1);
  di = set(di, fg, 4);
  di = set(di, bg, 1);
  di = di + step - 6 * 4;

  // Line 5.
  di = set(di, bg, 2);
  di = set(di, fg, 2);
  di = set(di, bg, 2);
  di = di + step - 5 * 4;

  // Line 6.
  set(di, bg, 4);
}

}  // namespace horus