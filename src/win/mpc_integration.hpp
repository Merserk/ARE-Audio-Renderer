#pragma once
#include "win/common.hpp"

namespace are::win {
class Renderer;
void update_settings_renderer(Renderer* renderer, bool in_graph);
ULONG release_settings_renderer(Renderer* renderer, std::atomic<ULONG>& refs) noexcept;
ComPtr<IBaseFilter> settings_renderer();
}
