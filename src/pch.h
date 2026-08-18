#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <atomic>
#include <mutex>
#include <thread>
#include <string>
#include <vector>
#include <cstdint>
#include <cmath>
#include <chrono>
#include <fstream>
#include <sstream>
#include <iomanip>

#include "core/constants.h"

namespace ControlHT {
    class Mod;
    struct Config;
}
