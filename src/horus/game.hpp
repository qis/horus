#pragma once
#include "config.hpp"
#include <boost/asio/awaitable.hpp>
#include <atomic>

namespace horus::game {

boost::asio::awaitable<void> monitor(std::atomic_bool& focus) noexcept;

}  // namespace horus::game