#pragma once

namespace anubis {

class mouse {
public:
  mouse() noexcept;
  mouse(mouse&& other) = delete;
  mouse(const mouse& other) = delete;
  mouse& operator=(mouse&& other) = delete;
  mouse& operator=(const mouse& other) = delete;
  ~mouse();
};

}  // namespace anubis