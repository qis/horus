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

bool neighboring(const eye::polygon& a, const eye::polygon& b, double distance) noexcept
{
  for (const auto& ap : a) {
    for (const auto& bp : b) {
      if (cv::norm(ap - bp) < distance) {
        return true;
      }
    }
  }
  return false;
}

bool intersecting(const cv::Point& a, const cv::Point& b, const cv::Point& c, const cv::Point& d) noexcept
{
  constexpr auto ccw = [](const cv::Point& a, const cv::Point& b, const cv::Point& c) noexcept {
    return (c.y - a.y) * (b.x - a.x) > (b.y - a.y) * (c.x - a.x);
  };
  return ccw(a, c, d) != ccw(b, c, d) && ccw(a, b, c) != ccw(a, b, d);
}

bool intersecting(const eye::polygon& a, const eye::polygon& b) noexcept
{
  const auto asize = a.size();
  const auto bsize = b.size();
  if (asize < 2 || bsize < 2) {
    return false;
  }
  for (size_t ai = 1; ai < asize; ai++) {
    const auto& a0 = a[ai - 1];
    const auto& a1 = a[ai];
    for (size_t bi = 1; bi < bsize; bi++) {
      const auto& b0 = b[bi - 1];
      const auto& b1 = b[bi];
      if (intersecting(a0, a1, b0, b1)) {
        return true;
      }
    }
  }
  return false;
}

}  // namespace

eye::eye() {}

size_t eye::scan(const uint8_t* image) noexcept
{
  // Block interval.
  constexpr size_t interval = 64;

  // Enemy outlines color.
  constexpr auto er = static_cast<uint8_t>(oc >> 16 & 0xFF);  // minimum red
  constexpr auto eg = static_cast<uint8_t>(oc >> 8 & 0xFF);   // maximum green
  constexpr auto eb = static_cast<uint8_t>(oc & 0xFF);        // minimum blue

  // Vertical scan iteration range without the user interface.
  const auto range = tbb::blocked_range<size_t>(uh + 1, sh - 1, interval);

  // Reset timings.
  timings_.clear();
  timings_.emplace_back(clock::now(), nullptr);

  // Copy pixels that fall into the outlines color range.
  std::memset(color_.data(), 0, sw * sh);
  tbb::parallel_for(range, [&](const tbb::blocked_range<size_t>& range) {
    const auto rb = range.begin();
    const auto re = range.end();
    auto si = image + rb * sw * 4;      // src iterator
    auto di = color_.data() + rb * sw;  // dst iterator
    for (auto y = rb; y < re; y++) {
      si += 4;
      di += 1;
      for (auto x = 1; x < sw - 1; x++) {
        di[0] = si[0] > er && si[1] < eg && si[2] > eb ? 0xFF : 0x00;
        si += 4;
        di += 1;
      }
      si += 4;
      di += 1;
    }
  });
  timings_.emplace_back(clock::now(), "color");

  // Create mask that filters large outline color regions.
  cv::erode(color_image_, color_mask_image_, color_mask_erode_kernel_);
  cv::dilate(color_mask_image_, color_mask_image_, color_mask_dilate_kernel_);
  timings_.emplace_back(clock::now(), "color mask");

  // Apply mask to get outlines.
  cv::bitwise_not(color_mask_image_, outlines_mask_image_);
  cv::bitwise_and(color_image_, outlines_mask_image_, outlines_image_);
  timings_.emplace_back(clock::now(), "outlines");

  // Create regions by closing small gaps in outlines and unsetting outline pixels in regular intervals.
  cv::morphologyEx(outlines_image_, regions_image_, cv::MORPH_CLOSE, regions_close_kernel_);
  for (size_t y = interval / 2; y < sh; y += interval) {
    std::fill_n(regions_.data() + y * sw, sw, 0x00);
  }
  for (size_t y = 0; y < sh; y++) {
    for (size_t x = interval / 2; x < sw; x += interval) {
      regions_[y * sw + x] = 0x00;
    }
  }
  timings_.emplace_back(clock::now(), "regions");

  // Find countours.
  cv::findContours(regions_image_, contours_, hierarchy_, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
  timings_.emplace_back(clock::now(), "contours");

  // Create polygons.
  polygons_.resize(contours_.size());
  for (size_t i = 0, size = contours_.size(); i < size; i++) {
    cv::approxPolyDP(contours_[i], polygons_[i], 3.0, false);
  }
  timings_.emplace_back(clock::now(), "polygons");

  // Join neighboring polygons into hulls.
  hulls_.clear();
  for (const auto& polygon : polygons_) {
    auto connected = false;
    for (auto& hull : hulls_) {
      if (neighboring(polygon, hull, polygon_connect_distance)) {
        hull.insert(hull.end(), polygon.begin(), polygon.end());
        connected = true;
        break;
      }
    }
    if (!connected) {
      hulls_.push_back(polygon);
    }
  }

  // Make sure the hulls are correct.
  for (size_t i = 0, size = hulls_.size(); i < size; i++) {
    const auto hull = std::move(hulls_[i]);
    cv::convexHull(cv::Mat(hull), hulls_[i]);
  }

  // Remove hulls with less, than three vertices.
  // clang-format off
  hulls_.erase(std::remove_if(hulls_.begin(), hulls_.end(), [](const auto& polygon) {
    return polygon.size() < 3;
  }), hulls_.end());
  // clang-format on

  timings_.emplace_back(clock::now(), "hulls");

  // Join neighboring and intersecting hulls into shapes.
  shapes_.clear();
  for (const auto& hull : hulls_) {
    auto connected = false;
    for (auto& shape : shapes_) {
      if (neighboring(hull, shape, polygon_connect_distance) || intersecting(hull, shape)) {
        shape.insert(shape.end(), hull.begin(), hull.end());
        connected = true;
        break;
      }
    }
    if (!connected) {
      shapes_.push_back(hull);
    }
  }

  // Make sure the shapes are correct.
  for (size_t i = 0, size = shapes_.size(); i < size; i++) {
    const auto shape = std::move(shapes_[i]);
    cv::convexHull(cv::Mat(shape), shapes_[i]);
  }

  // Remove shapes with less, than three vertices.
  // clang-format off
  shapes_.erase(std::remove_if(shapes_.begin(), shapes_.end(), [](const auto& polygon) {
    return polygon.size() < 3;
  }), shapes_.end());
  // clang-format on

  timings_.emplace_back(clock::now(), "shapes");

  // Group polygons based on which shape they belong to.
  groups_.clear();
  groups_.resize(shapes_.size());
  for (const auto& polygon : polygons_) {
    for (size_t i = 0, isize = shapes_.size(); i < isize; i++) {
      for (size_t j = 0, jsize = polygon.size(); j < jsize; j++) {
        if (cv::pointPolygonTest(shapes_[i], polygon[j], true) > -0.5) {
          groups_[i].push_back(polygon);
          i = isize;
          break;
        }
      }
    }
  }

  // Remove empty groups.
  // clang-format off
  groups_.erase(std::remove_if(groups_.begin(), groups_.end(), [](const auto& group) {
    return group.empty();
  }), groups_.end());
  // clang-format on

  timings_.emplace_back(clock::now(), "groups");

  // Create targets.
  std::vector<size_t> connected;

  // clang-format off
  constexpr auto next_point = [](
    size_t index, size_t size, bool forward
  ) noexcept -> size_t {
    assert(size);
    assert(index < size);
    if (forward) {
      index++;
      if (index == size) {
        return 0;
      }
      return index;
    }
    if (index == 0) {
      return size - 1;
    }
    return index - 1;
  };

  constexpr auto furthest_point = [](
    const polygon& segment, size_t size, size_t index
  ) noexcept -> std::pair<size_t, bool> {
    assert(size == segment.size());
    assert(size);
    assert(index < size);

    if (size == 1) {
      return { index, true };
    }

    if (size == 2) {
      return { next_point(index, size, true), true };
    }

    auto fp = index;
    auto fd = cv::norm(segment[fp] - segment[index]);

    auto bp = index;
    auto bd = cv::norm(segment[bp] - segment[index]);

    do {
      auto np = next_point(fp, size, true);
      fd += cv::norm(segment[fp] - segment[np]);
      fp = np;

      if (fp == bp) {
        break;
      }

      np = next_point(bp, size, false);
      bd += cv::norm(segment[bp] - segment[np]);
      bp = np;
    } while (fp != bp);
    
    if (fd > bd) {
      return { fp, true };
    }
    return { bp, false };
  };

  constexpr auto longest_path_forward = [](
    const polygon& segment, size_t size, size_t first, size_t last
  ) noexcept -> bool {
    assert(size == segment.size());
    assert(size);
    assert(first < size);
    assert(last < size);

    if (first == last) {
      return true;
    }

    double forward_distance = 0.0;
    for (auto i = first; i != last;) {
      const auto n = next_point(i, size, true);
      forward_distance += cv::norm(segment[n] - segment[i]);
      i = n;
    }

    double backward_distance = 0.0;
    for (auto i = first; i != last;) {
      const auto n = next_point(i, size, false);
      backward_distance += cv::norm(segment[n] - segment[i]);
      i = n;
    }

    return forward_distance > backward_distance;
  };

  const auto find_closest_segment = [&connected](
    const std::vector<polygon>& segments,
    const cv::Point& point,
    size_t skip
  ) noexcept -> std::tuple<size_t, size_t, double> {
    size_t segment_index = 0;
    size_t segment_point_index = 0;
    auto search_distance = std::numeric_limits<double>::max();
    for (size_t i = 1, isize = segments.size(); i < isize; i++) {
      if (i == skip) {
        continue;
      }
      if (const auto end = connected.end(); std::find(connected.begin(), end, i) != end) {
        continue;
      }
      for (size_t j = 0, jsize = segments[i].size(); j < jsize; j++) {
        const auto distance = cv::norm(segments[i][j] - point);
        if (distance < 128.0 && distance < search_distance) {
          search_distance = distance;
          segment_point_index = j;
          segment_index = i;
        }
      }
    }
    return { segment_index, segment_point_index, search_distance };
  };
  // clang-format on

  struct segment {
    size_t index{ 0 };
    size_t first{ 0 };
    size_t last{ 0 };
    size_t size{ 0 };
    bool forward{ true };
  };

  targets_.clear();
  targets_.resize(groups_.size());
  for (size_t i = 0, isize = groups_.size(); i < isize; i++) {
    const auto& segments = groups_[i];
    const auto segments_size = segments.size();
    auto& target = targets_[i];

    // If there is only one segment, add it as a target.
    if (segments_size < 2) {
      if (segments_size == 1) {
        target = segments[0];
      }
      continue;
    }

    // Reset list of connected segments.
    connected.clear();

    // First segment.
    segment sf;
    sf.size = segments[sf.index].size();

    // Last segment.
    segment sl;

    // Find first point of the first segment and last point of the last segment.
    auto search_distance = std::numeric_limits<double>::max();
    for (size_t pi = 0, psize = segments[0].size(); pi < psize; pi++) {
      const auto [segment_index, segment_point_index, distance] =
        find_closest_segment(segments, segments[0][pi], 0);
      if (distance < search_distance) {
        sf.first = pi;
        sl.index = segment_index;
        sl.last = segment_point_index;
        search_distance = distance;
      }
    }
    sl.size = segments[sl.index].size();

    // Set last point of the first segment in case there are only two segments.
    std::tie(sf.last, sf.forward) = furthest_point(segments[sf.index], sf.size, sf.first);

    // Set first point of the last segment in case there are only two segments.
    std::tie(sl.first, sl.forward) = furthest_point(segments[sl.index], sl.size, sl.last);

    // Prevent find_closest_segment from using the last segment.
    connected.push_back(sl.index);

    // Use first segment as the starting current segment.
    auto sc = sf;

    while (connected.size() < segments_size) {
      // Find next segment.
      segment sn;
      search_distance = std::numeric_limits<double>::max();
      for (size_t pi = 0, psize = segments[sc.index].size(); pi < psize; pi++) {
        // Skip first point.
        if (pi == sc.first) {
          continue;
        }

        // Find closest segment.
        const auto [segment_index, segment_point_index, distance] =
          find_closest_segment(segments, segments[sc.index][pi], sc.index);

        // Stop if there is no next segment.
        if (!segment_index) {
          break;
        }

        // Update last point and next segment.
        if (distance < search_distance) {
          sc.last = pi;
          sn.index = segment_index;
          sn.first = segment_point_index;
          search_distance = distance;
        }
      }

      // Use last segment to set the last point of the current segment if there is no next segment.
      if (!sn.index) {
        search_distance = std::numeric_limits<double>::max();
        for (size_t pi = 0, psize = segments[sc.index].size(); pi < psize; pi++) {
          const auto distance = cv::norm(segments[sl.index][sl.first] - segments[sc.index][pi]);
          if (distance < search_distance) {
            sc.last = pi;
            search_distance = distance;
          }
        }
      }

      // Connect current segment.
      const auto size = segments[sc.index].size();
      sc.forward = longest_path_forward(segments[sc.index], size, sc.first, sc.last);
      for (size_t pi = sc.first; pi != sc.last;) {
        target.push_back(segments[sc.index][pi]);
        pi = next_point(pi, size, sc.forward);
      }
      target.push_back(segments[sc.index][sc.last]);
      connected.push_back(sc.index);

      // Stop if there is no next segment.
      if (!sn.index) {
        break;
      }

      // Switch to the next segment.
      sc = sn;
    };

    // Connect last segment.
    const auto size = segments[sl.index].size();
    for (size_t pi = sl.first; pi != sl.last;) {
      target.push_back(segments[sl.index][pi]);
      pi = next_point(pi, size, sl.forward);
    }
  }

  // Remove targets with less, than three vertices.
  // clang-format off
  targets_.erase(std::remove_if(targets_.begin(), targets_.end(), [](const auto& polygon) {
    return polygon.size() < 3;
  }), targets_.end());
  // clang-format on

  timings_.emplace_back(clock::now(), "targets");

  return 0;
}

void eye::draw_stats(uint8_t* image, uint32_t color) noexcept
{
  using duration = std::chrono::duration<float, std::milli>;

  std::string text;
  auto tpos = cv::Point(10, eye::uh + 25);
  cv::Mat si(eye::sw, eye::sh, CV_8UC4, image, eye::sw * 4);

  size_t targets = targets_.size();
  size_t targets_points = 0;
  for (const auto& target : targets_) {
    targets_points += target.size();
  }
  text = std::format("{}/{} targets", targets, targets_points);
  cv::putText(si, text, tpos, cv::FONT_HERSHEY_PLAIN, 1.5, { 0, 0, 0, 255 }, 4, cv::LINE_AA);
  cv::putText(si, text, tpos, cv::FONT_HERSHEY_PLAIN, 1.5, { 0, 165, 231, 255 }, 2, cv::LINE_AA);
  tpos.y += 25;

  if (timings_.size() < 2) {
    return;
  }

  for (size_t i = 1, size = timings_.size(); i < size; i++) {
    const auto& t0 = timings_[i - 1];
    const auto& t1 = timings_[i];
    const auto ms = std::chrono::duration_cast<duration>(t1.tp - t0.tp).count();
    text = std::format("{:5.3f} {}", ms, t1.name ? t1.name : "");
    cv::putText(si, text, tpos, cv::FONT_HERSHEY_PLAIN, 1.5, { 0, 0, 0, 255 }, 4, cv::LINE_AA);
    cv::putText(si, text, tpos, cv::FONT_HERSHEY_PLAIN, 1.5, { 0, 165, 231, 255 }, 2, cv::LINE_AA);
    tpos.y += 25;
  }

  const auto& t0 = timings_[0];
  const auto& t1 = timings_[timings_.size() - 1];
  const auto ms = std::chrono::duration_cast<duration>(t1.tp - t0.tp).count();
  text = std::format("{:5.3f} total", ms);
  cv::putText(si, text, tpos, cv::FONT_HERSHEY_PLAIN, 1.5, { 0, 0, 0, 255 }, 4, cv::LINE_AA);
  cv::putText(si, text, tpos, cv::FONT_HERSHEY_PLAIN, 1.5, { 0, 165, 231, 255 }, 2, cv::LINE_AA);
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

void eye::draw(uint8_t* image, uint32_t color, const std::vector<uint8_t>& overlay) noexcept
{
  const auto oc = overlay_color(color);
  const auto range = tbb::blocked_range<size_t>(0, sh, 64);
  tbb::parallel_for(range, [&](const tbb::blocked_range<size_t>& range) {
    const size_t rb = range.begin();
    const size_t re = range.end();
    auto si = overlay.data() + rb * sw;  // src iterator
    auto di = image + rb * sw * 4;       // dst iterator
    for (auto y = rb; y < re; y++) {
      for (auto x = 0; x < sw; x++) {
        if (*si > 0) {
          oc.apply(di);
        }
        si += 1;
        di += 4;
      }
    }
  });
}

void eye::draw(uint8_t* image, uint32_t color, const cv::Point& point) noexcept
{
  constexpr auto set = [](uint8_t* di, const overlay_color& color, unsigned count) noexcept {
    for (unsigned i = 0; i < count; i++) {
      color.apply(di);
      di += 4;
    }
    return count * 4;
  };

  constexpr const overlay_color ooc(0x000000FF);
  const overlay_color oic(color);

  const auto x = static_cast<long>(point.x);
  const auto y = static_cast<long>(point.y);

  auto di = image + (y - 2) * sw * 4 + (x - 2) * 4;  // dst iterator

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

}  // namespace horus