#pragma once
#include "win/common.hpp"
namespace are::win {
HRESULT register_renderer() noexcept;
HRESULT unregister_renderer() noexcept;
}
