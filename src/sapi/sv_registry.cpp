#include "sv_registry.hpp"

namespace SoftVoice {
namespace registry {

void key::set(const std::wstring& name, const std::wstring& value)
{
    const auto size =
        static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t));
    if (RegSetValueExW(handle_, name.c_str(), 0, REG_SZ,
                       reinterpret_cast<const BYTE*>(value.c_str()),
                       size) != ERROR_SUCCESS) {
        throw error("Unable to write a value to the registry");
    }
}

}  // namespace registry
}  // namespace SoftVoice
