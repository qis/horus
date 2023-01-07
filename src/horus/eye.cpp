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
        if (distance < search_distance) {
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

    // Set last point of the first segment in case there are only two segments.
    if (sf.first == 0) {
      assert(!segments[0].empty());
      sf.last = segments[0].size() - 1;
    } else {
      sf.last = sf.first - 1;
    }

    // Prevent find_closest_polygon from searching the last segment. 
    connected.push_back(sl.index);

    // Current segment.
    auto sc = sf;

    while (connected.size() + 1 < segments_size) {
      // Find next segment.
      segment sn;
      search_distance = std::numeric_limits<double>::max();
      for (size_t pi = 0, psize = segments[sc.index].size(); pi + 1 < psize; pi++) {
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
      
      // Connect current segment.
      const auto segment_size = segments[sc.index].size();
      assert(segment_size);
      for (size_t pi = sc.first; pi != sc.last;) {
        target.push_back(segments[sc.index][pi]);
        if (sc.first < sc.last) {
          pi++;
          if (pi >= segment_size) {
            pi = 0;
          }
        } else {
          if (pi == 0) {
            pi = segment_size;
          }
          pi--;
        }
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

    // Connect first segment if there are only two segments.
    assert(!target.empty());

    // Find first point of the last segment.
    search_distance = std::numeric_limits<double>::max();
    for (size_t pi = 0, psize = segments[sl.index].size(); pi + 1 < psize; pi++) {
      if (pi == sl.last) {
        continue;
      }
      const auto distance = cv::norm(segments[sl.index][pi] - target[target.size() - 1]);
      if (distance < search_distance) {
        search_distance = distance;
        sl.first = pi;
      }
    }

    // Connect last segment.
    const auto segment_size = segments[sl.index].size();
    assert(segment_size);
    for (size_t pi = sl.first; pi != sl.last;) {
      target.push_back(segments[sl.index][pi]);
      if (sl.first < sl.last) {
        pi++;
        if (pi >= segment_size) {
          pi = 0;
        }
      } else {
        if (pi == 0) {
          pi = segment_size;
        }
        pi--;
      }
    }
    target.push_back(segments[sl.index][sl.last]);
  }

  // Remove targets with less, than three vertices.
  // clang-format off
  targets_.erase(std::remove_if(targets_.begin(), targets_.end(), [](const auto& polygon) {
    return polygon.size() < 3;
  }), targets_.end());
  // clang-format on

  timings_.emplace_back(clock::now(), "targets");

#if 0
  // Creaate targets by connecting groups.
  targets_.clear();
  targets_.resize(groups_.size());
  std::vector<size_t> connected;
  for (size_t i = 0, size = groups_.size(); i < size; i++) {
    const auto& group = groups_[i];
    auto& target = targets_[i];

    const auto group_size = group.size();

    // If there is only one polygon in a group, add it as a target.
    if (group_size < 2) {
      if (group_size == 1) {
        target = group[0];
      }
      continue;
    }

    // First polygon.
    const auto& polygon_first = group[0];
    const auto polygon_first_size = polygon_first.size();

    // Last point of the first polygon.
    size_t polygon_first_end = 0;

    // Index of the next polygon.
    size_t polygon_next = 0;

    // Index of the first point in the next polygon.
    size_t polygon_next_begin = 0;

    // Used for the search of the closest adjacent polygon.
    auto search_distance = std::numeric_limits<double>::max();

    // Find polygon that is closest to one of the points of the first polygon.
    for (size_t pi = 0, psize = group[0].size(); pi < psize; pi++) {
      // Iterate over the other polygons in this group.
      for (size_t gi = 1; gi < group_size; gi++) {
        // Iterate over the points of the other polygon.
        for (size_t ai = 0, asize = group[gi].size(); ai < asize; ai++) {
          const auto distance = cv::norm(group[gi][ai] - group[0][pi]);
          if (distance < search_distance) {
            search_distance = distance;
            polygon_first_end = pi;
            polygon_next_begin = ai;
            polygon_next = gi;
          }
        }
      }
    }

    // Connect all polygons except first and last.
    connected.clear();
    connected.reserve(group_size);
    while (connected.size() < group_size - 2) {
      // Current polygon.
      const auto polygon_index = polygon_next;
      const auto& polygon = group[polygon_index];
      const auto polygon_size = polygon.size();

      // First point of the current polygon.
      const auto polygon_begin = polygon_next_begin;

      // Find last point of the current polygon.
      auto polygon_end = polygon_begin;
      search_distance = std::numeric_limits<double>::max();
      for (size_t pi = 0; pi < polygon_size; pi++) {
        // Skip first point of the current polygon.
        if (pi == polygon_begin) {
          continue;
        }

        // Iterate over the other polygons in this group.
        for (size_t gi = 1; gi < group_size; gi++) {
          // Skip current polygon.
          if (gi == polygon_index) {
            continue;
          }

          // Skip polygons that are already connected.
          if (std::find(connected.begin(), connected.end(), gi) != connected.end()) {
            continue;
          }

          // Iterate over the points of the other polygon.
          for (size_t ai = 0, asize = group[gi].size(); ai < asize; ai++) {
            const auto distance = cv::norm(group[gi][ai] - polygon[pi]);
            if (distance < search_distance) {
              search_distance = distance;
              polygon_end = pi;
              polygon_next_begin = ai;
              polygon_next = gi;
            }
          }
        }
      }

      // Connect current polygon.
      for (auto pi = polygon_begin; pi != polygon_end;) {
        target.push_back(polygon[pi]);
        if (polygon_begin < polygon_end) {
          if (pi == polygon_size - 1) {
            pi = 0;
          } else {
            pi++;
          }
        } else {
          if (pi == 0) {
            pi = polygon_size - 1;
          } else {
            pi--;
          }
        }
      }
      target.push_back(polygon[polygon_end]);
      connected.push_back(polygon_index);
    }

    // Target.
    assert(!targets_.empty());

    // Last polygon.
    const auto& polygon_last = group[group.size() - 1];
    const auto polygon_last_size = polygon_last.size();

    // Find first point of the last polygon.
    size_t polygon_last_begin = 0;
    search_distance = std::numeric_limits<double>::max();
    for (size_t ai = 0, asize = polygon_last.size(); ai < asize; ai++) {
      const auto distance = cv::norm(polygon_last[ai] - target[target.size() - 1]);
      if (distance < search_distance) {
        search_distance = distance;
        polygon_last_begin = ai;
      }
    }

    // Find first point of the first polygon and last point of the last polygon.
    auto polygon_first_begin = polygon_first_end;
    auto polygon_last_end = polygon_last_begin;
    search_distance = std::numeric_limits<double>::max();
    for (size_t fi = 0; fi < polygon_first_size; fi++) {
      for (size_t li = 0; li < polygon_last_size; li++) {
        const auto distance = cv::norm(polygon_last[li] - polygon_first[fi]);
        if (distance < search_distance) {
          search_distance = distance;
          polygon_first_begin = fi;
          polygon_last_end = li;
        }
      }
    }

    // Connect last polygon.
    for (auto pi = polygon_last_begin; pi != polygon_last_end;) {
      target.push_back(polygon_last[pi]);
      if (polygon_last_begin < polygon_last_end) {
        if (pi == polygon_last_size - 1) {
          pi = 0;
        } else {
          pi++;
        }
      } else {
        if (pi == 0) {
          pi = polygon_last_size - 1;
        } else {
          pi--;
        }
      }
    }
    target.push_back(polygon_last[polygon_last_end]);

    // Connect first polygon.
    for (auto pi = polygon_first_begin; pi != polygon_first_end;) {
      target.push_back(polygon_first[pi]);
      if (polygon_first_begin < polygon_first_end) {
        if (pi == polygon_first_size - 1) {
          pi = 0;
        } else {
          pi++;
        }
      } else {
        if (pi == 0) {
          pi = polygon_first_size - 1;
        } else {
          pi--;
        }
      }
    }
    target.push_back(polygon_first[polygon_first_end]);
  }

  // Remove targets with less, than three vertices.
  // clang-format off
  targets_.erase(std::remove_if(targets_.begin(), targets_.end(), [](const auto& polygon) {
    return polygon.size() < 3;
  }), targets_.end());
  // clang-format on

  timings_.emplace_back(clock::now(), "targets");
#endif

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

#if 0
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
        constexpr auto er = (ec >> 16 & 0xFF);  // minimum red
        constexpr auto eg = (ec >> 8 & 0xFF);   // maximum green
        constexpr auto eb = (ec & 0xFF);        // minimum blue

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
  std::optional<target> result;
  const cv::Point mouse(mx, my);
  centers_.resize(contours_.size());
  for (size_t i = 0, m = contours_.size(); i < m; i++) {
    const auto size = contours_[i].size();
    if (!size) {
      continue;
    }

    auto ax = 0;
    auto ay = 0;
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
      ay += point.y;
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
    centers_[i] = cv::Point(ax / size, ay / size);
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

void eye::draw_overlays(uint8_t* image, uint32_t ec) noexcept
{
  const auto range = tbb::blocked_range<size_t>(0, sh, 64);
  const auto color = overlay_color(static_cast<uint32_t>(ec));
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

#endif

}  // namespace horus