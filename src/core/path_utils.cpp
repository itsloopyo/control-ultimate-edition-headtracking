#include "pch.h"
#include "path_utils.h"

namespace ControlHT::PathUtils {

static HMODULE GetSelfModule() {
    HMODULE h = nullptr;
    GetModuleHandleExA(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCSTR>(&GetSelfModule),
        &h);
    return h;
}

std::string GetModDirectory() {
    char buf[MAX_PATH];
    HMODULE self = GetSelfModule();
    DWORD len = GetModuleFileNameA(self, buf, MAX_PATH);
    if (len == 0 || len == MAX_PATH) return ".";

    std::string path(buf, len);
    size_t slash = path.find_last_of("\\/");
    if (slash == std::string::npos) return ".";
    return path.substr(0, slash);
}

std::wstring GetModPathW(const char* filename) {
    std::string narrow = GetModDirectory() + "\\" + filename;
    int len = MultiByteToWideChar(CP_ACP, 0, narrow.c_str(), -1, nullptr, 0);
    if (len <= 1) return {};
    std::wstring wide(static_cast<size_t>(len), L'\0');
    MultiByteToWideChar(CP_ACP, 0, narrow.c_str(), -1, &wide[0], len);
    wide.pop_back();
    return wide;
}

} // namespace ControlHT::PathUtils
