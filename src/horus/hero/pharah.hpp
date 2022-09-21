#pragma once
#include "base.hpp"
#include <rock/client.hpp>
#include <chrono>

namespace horus::hero {

class pharah : public base {
public:
  static constexpr std::chrono::milliseconds jump_duration{ 1400 };
  static constexpr std::chrono::milliseconds blast_duration{ 500 };
  static constexpr std::chrono::milliseconds flight_duration{ 320 };
  static constexpr std::chrono::milliseconds fall_duration{ 430 };
  static constexpr std::chrono::milliseconds skip_delta{ 50 };
  static_assert(skip_delta < flight_duration);

  pharah(rock::client& client) noexcept : client_(client) {}

  pharah(pharah&& other) = delete;
  pharah(const pharah& other) = delete;
  pharah& operator=(pharah&& other) = delete;
  pharah& operator=(const pharah& other) = delete;

  ~pharah()
  {
    client_.mask(rock::button::middle, std::chrono::seconds(0));
  }

  hero::type type() const noexcept override
  {
    return hero::type::pharah;
  }

  status scan(
    std::uint8_t* data,
    const hid::keybd& keybd,
    const hid::mouse& mouse,
    clock::time_point frame) noexcept override
  {
    // Handle shift down event.
    bool invalid_jump_keys = false;
    if (keybd.shift && !shift_state_) {
      client_.mask(rock::button::middle, std::chrono::seconds(0));
      jump_flight_ = true;
      jump_flight_start_ = frame;
      invalid_jump_keys = keybd.space || mouse.right;
    }

    // Update shift down state.
    shift_state_ = keybd.shift;

    // Warn about invalid jump key combinations.
    if (invalid_jump_keys) {
      return status::beep;
    }

    // Handle jump flight state.
    if (jump_flight_ && !keybd.space && !mouse.right) {
      if (frame > jump_flight_start_ + jump_duration) {
        jump_flight_ = false;
        assisted_flight_ = true;
        assisted_flight_update_ = frame - flight_duration - fall_duration;
      }
      return status::none;
    }

    // Handle e down event.
    if (keybd.e && !e_state_) {
      client_.mask(rock::button::middle, std::chrono::seconds(0));
      blast_flight_ = true;
      blast_flight_start_ = frame;
    }

    // Update e down state.
    e_state_ = keybd.e;

    // Handle blast flight state.
    if (blast_flight_ && !keybd.space && !mouse.right) {
      if (frame > blast_flight_start_ + blast_duration) {
        blast_flight_ = false;
        assisted_flight_ = true;
        assisted_flight_update_ = frame - flight_duration - fall_duration;
      }
      return status::none;
    }

    if (keybd.space || mouse.right) {
      // Handle space or right mouse button down event.
      if (!manual_flight_) {
        client_.mask(rock::button::middle, std::chrono::seconds(1));
        manual_flight_ = true;
        manual_flight_start_ = frame;
        manual_flight_update_ = frame;
        return status::none;
      }

      // Handle space or right mouse button hold state.
      if (frame > manual_flight_update_ + std::chrono::milliseconds(500)) {
        client_.mask(rock::button::middle, std::chrono::seconds(1));
        manual_flight_update_ = frame;
      }
      return status::none;
    }

    // Handle space or right mouse button up event.
    if (manual_flight_) {
      const auto manual_flight_duration =
        std::chrono::duration_cast<std::chrono::milliseconds>(frame - manual_flight_start_);
      if (manual_flight_duration < flight_duration) {
        client_.mask(rock::button::middle, flight_duration - manual_flight_duration);
        assisted_flight_update_ = frame - manual_flight_duration;
      } else {
        client_.mask(rock::button::middle, std::chrono::seconds(0));
        assisted_flight_update_ = frame - flight_duration;
      }
      manual_flight_ = false;
      assisted_flight_ = true;
      return status::none;
    }

    if (assisted_flight_) {
      // Disable assisted flight on q, control, alt and windows keys.
      if (keybd.q || keybd.control || keybd.menu || keybd.win) {
        assisted_flight_ = false;
        client_.mask(rock::button::middle, std::chrono::seconds(0));
        return status::none;
      }

      // Updated assisted flight mask.
      if (frame > assisted_flight_update_ + flight_duration + fall_duration) {
        client_.mask(rock::button::middle, flight_duration);
        assisted_flight_update_ = frame;
        return status::none;
      }
    }

    return status::none;
  }

private:
  rock::client& client_;

  bool shift_state_{ false };
  bool jump_flight_{ false };
  clock::time_point jump_flight_start_{};

  bool e_state_{ false };
  bool blast_flight_{ false };
  clock::time_point blast_flight_start_{};

  bool manual_flight_{ false };
  clock::time_point manual_flight_start_{};
  clock::time_point manual_flight_update_{};

  bool assisted_flight_{ false };
  clock::time_point assisted_flight_update_{};
};

}  // namespace horus::hero