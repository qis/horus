#pragma once
#include "config.hpp"
#include <boost/asio/as_tuple.hpp>
#include <boost/asio/use_awaitable.hpp>

namespace horus {

using token = decltype(boost::asio::as_tuple(boost::asio::use_awaitable));

}  // namespace horus