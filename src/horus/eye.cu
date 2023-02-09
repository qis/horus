#include "eye.hpp"
#include "mulxp_hash.hpp"
#include <cuda_runtime.h>
#include <device_launch_parameters.h>
#include <opencv2/core/cuda/common.hpp>
#include <algorithm>
#include <numeric>
#include <cassert>

namespace horus {
namespace {

using cv::cuda::device::divUp;

// Sets mask pixels to 0x02 if they have too many neighbors.
__global__ void mask_filter(uchar* data, size_t step, int r, int c)
{
  const auto d = r * 2 + 1;
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
      const auto sv = *si++;
      if (sv && ++neighbors > c) {
        *di = 0x02;
        return;
      }
    }
    si += step - d;
  }
}

// Sets mask pixels to 0x00 if they were outside the filter radius.
__global__ void mask_shrink(uchar* data, size_t step, int r)
{
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

// Sets mask pixels to 0x04 if they have a 0x02 neighbor.
__global__ void mask_dilate(uchar* data, size_t step, int r)
{
  const auto d = r * 2 + 1;
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
      const auto sv = *si++;
      if (sv == 0x02) {
        *di = 0x04;
        return;
      }
    }
    si += step - d;
  }
}

// Sets the 0x01 bit on filtered or dilated mask pixels that have neighbor with the 0x01 bit set.
__global__ void mask_erode(uchar* data, size_t step, int r)
{
  const auto d = r * 2 + 1;
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
      const auto sv = *si++;
      if (sv & 0x01) {
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

}  // namespace

const cv::Point eye::vc{ vw / 2, vh / 2 };

eye::eye()
{
  freetype_ = cv::freetype::createFreeType2();
  freetype_->loadFontData("C:/OBS/horus/res/fonts/PixelOperatorMono.ttf", 0);
}

bool eye::scan(const cv::Mat& scan) noexcept
{
  // Verify scan type and size.
  assert(scan.type() == CV_8UC1);
  assert(scan.cols == sw);
  assert(scan.rows == sh);

  // Resize scan (120 μs).
  cv::resize(scan, scan_, { vw, vh }, 1.0 / vf, 1.0 / vf, cv::INTER_AREA);

  // Update hash (30 μs).
  const auto hash = mulxp3_hash(scan_.data, scan_.step * scan_.rows, 0);
  if (hash == hash_) {
    return false;
  }
  hash_ = hash;
  targets_ready_ = false;
  return true;
}

const std::vector<eye::target>& eye::targets() noexcept
{
  if (targets_ready_) {
    return targets_;
  }
  const auto tp0 = clock::now();

  mask_data_.upload(scan_);

  const dim3 block(16, 16);
  const dim3 grid(divUp(eye::vw, block.x), divUp(eye::vh, block.y));
  mask_filter<<<grid, block>>>(mask_data_.data, mask_data_.step, 3, 9);
  assert(cudaGetLastError() == cudaSuccess);
  mask_shrink<<<grid, block>>>(mask_data_.data, mask_data_.step, 3);
  assert(cudaGetLastError() == cudaSuccess);
  mask_dilate<<<grid, block>>>(mask_data_.data, mask_data_.step, 3);
  assert(cudaGetLastError() == cudaSuccess);
  for (auto i = 0; i < 4; i++) {
    mask_erode<<<grid, block>>>(mask_data_.data, mask_data_.step, 1);
    assert(cudaGetLastError() == cudaSuccess);
  }
  mask_scan<<<grid, block>>>(mask_data_.data, mask_data_.step, mask_view_.data, mask_view_.step);
  assert(cudaGetLastError() == cudaSuccess);
  mask_view_.download(mask_);

  const auto tp1 = clock::now();
  mask_duration_ = tp1 - tp0;

  cv::findContours(mask_, targets_contours_, hierarchy_, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

  targets_.resize(targets_contours_.size());
  for (std::size_t i = 0, size = targets_contours_.size(); i < size; i++) {
    cv::convexHull(targets_contours_[i], targets_[i].hull);
    targets_[i].contours = { &targets_contours_[i] };
  }

  while (true) {
    for (auto si = targets_.begin(), se = targets_.end(); si != se; ++si) {
      const auto srect = cv::boundingRect(si->hull);
      const auto sl = srect.x;
      const auto sr = srect.x + srect.width;
      const auto st = srect.y;
      const auto sb = srect.y + srect.height;
      for (auto di = std::next(si); di != se; ++di) {
        const auto drect = cv::boundingRect(di->hull);
        const auto dl = drect.x;
        const auto dr = drect.x + drect.width;
        const auto dt = drect.y;
        const auto db = drect.y + drect.height;
        if (sr < dl - 8 || sl > dr + 8 || sb < dt - 32 || st > db + 32) {
          continue;
        }

        si->hull.reserve(si->hull.size() + di->hull.size());
        si->hull.insert(si->hull.end(), di->hull.begin(), di->hull.end());
        cv::convexHull(si->hull, di->hull);

        di->contours.reserve(di->contours.size() + si->contours.size());
        di->contours.insert(di->contours.end(), si->contours.begin(), si->contours.end());

        targets_.erase(si);
        goto joined;
      }
    }
    break;
  joined:
    continue;
  }

  targets_duration_ = clock::now() - tp1;
  targets_ready_ = true;
  return targets_;
}

clock::duration eye::draw_mask(cv::Mat& overlay) noexcept
{
  assert(overlay.type() == CV_8UC4);
  assert(overlay.cols == vw);
  assert(overlay.rows == vh);

  if (!targets_ready_) {
    targets();
  }

  const dim3 block(16, 16);
  const dim3 grid(divUp(sw, block.x), divUp(sh, block.y));
  mask_draw<<<grid, block>>>(mask_data_.data, mask_data_.step, view_.data, view_.step);
  assert(cudaGetLastError() == cudaSuccess);
  view_.download(overlay);

  return mask_duration_;
}

clock::duration eye::draw_targets(cv::Mat& overlay) noexcept
{
  assert(overlay.type() == CV_8UC4);
  assert(overlay.cols == vw);
  assert(overlay.rows == vh);

  if (!targets_ready_) {
    targets();
  }

  for (const auto& target : targets_) {
    cv::fillPoly(overlay, target.hull, scalar(0x64DD1760), cv::LINE_4);
    cv::polylines(overlay, target.hull, true, scalar(0x64DD17FF), 1, cv::LINE_4);
    cv::rectangle(overlay, cv::boundingRect(target.hull), scalar(0x00B0FFFF), 1, cv::LINE_8);
  }

  return targets_duration_;
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