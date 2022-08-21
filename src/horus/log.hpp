#pragma once
#include "config.hpp"
#include <format>
#include <memory>
#include <string>
#include <string_view>

namespace horus {

class HORUS_API logger {
public:
  logger() noexcept;
  logger(std::string filename, bool write_to_stdout = false) noexcept;

  static void log(std::string text) noexcept;

private:
  class impl;
  std::shared_ptr<impl> impl_;
};

inline void log(std::string text) noexcept
{
  logger::log(std::move(text));
}

template <class Arg, class... Args>
void log(std::string_view format, Arg&& arg, Args&&... args) noexcept
{
  try {
    logger::log(std::vformat(format, std::make_format_args(arg, args...)));
  }
  catch (const std::exception& e) {
    logger::log(std::string(e.what()));
  }
}

}  // namespace horus