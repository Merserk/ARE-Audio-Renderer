#pragma once
#include "win/interfaces.hpp"
#include <format>
namespace are::win::mpc {
inline constexpr wchar_t diagnostic_message[]=L"ARE.AudioRenderer.MPC.Diagnostic.v1";
struct Diagnostic {
    DWORD size{sizeof(Diagnostic)};
    DWORD signature{0x41524531};
    HRESULT result{VFW_E_NOT_FOUND};
    LiveStatus live{};
    UINT filters{};
    wchar_t filter_names[16][96]{};
};
inline std::wstring diagnostic_mapping(DWORD player,DWORD caller) {
    return std::format(L"Local\\ARE.AudioRenderer.MPC.Diagnostic.{}.{}",player,caller);
}
}
