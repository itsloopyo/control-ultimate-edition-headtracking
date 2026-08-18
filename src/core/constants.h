#pragma once

namespace ControlHT {

constexpr const char* CONTROLHT_VERSION = "0.0.0";

constexpr const char* CONFIG_FILENAME = "HeadTracking.ini";
constexpr const char* LOG_FILENAME = "HeadTracking.log";

// UDP listen port (OpenTrack standard) and the valid bind range.
constexpr int DEFAULT_UDP_PORT = 4242;
// Below 1024 is privileged and could only fail to bind, so the boundary check
// rejects it rather than letting the receiver retry forever.
constexpr int MIN_UDP_PORT = 1024;
constexpr int MAX_UDP_PORT = 65535;

} // namespace ControlHT
