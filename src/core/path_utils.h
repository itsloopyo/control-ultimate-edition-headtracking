#pragma once

#include <string>

namespace ControlHT::PathUtils {

// Directory containing the ASI module (game exe directory).
std::string GetModDirectory();

// Wide path for APIs that take wide strings (core logging::Open). Converts
// via CP_ACP so non-ASCII install paths stay intact.
std::wstring GetModPathW(const char* filename);

} // namespace ControlHT::PathUtils
