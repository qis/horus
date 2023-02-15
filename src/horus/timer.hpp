#pragma once
#include "config.hpp"
#include <horus/token.hpp>
#include <boost/asio/steady_timer.hpp>

namespace horus {

using timer = decltype(token::as_default_on(boost::asio::steady_timer({})));

}  // namespace horus