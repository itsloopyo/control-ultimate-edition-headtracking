// Proves the receiver reclaims the tracker port on its own.
//
// The scenario is a player who launches Control while a previous game (or a
// second head tracking mod) still holds UDP 4242, then quits it. Nothing in
// the mod re-runs Start(), so the whole recovery is the receiver's supervisor
// thread; if it ever regresses, tracking stays dead for the session with no
// symptom other than "head tracking does nothing".

#include <winsock2.h>
#include <ws2tcpip.h>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <thread>

#include <cameraunlock/protocol/opentrack_packet.h>
#include <cameraunlock/protocol/udp_receiver.h>

#pragma comment(lib, "ws2_32.lib")

namespace {

// Not 4242: a real OpenTrack instance on the dev machine would occupy the
// default port and the test would measure that instead of what it hooks up.
constexpr uint16_t kTestPort = 45871;

// Retry interval plus one supervisor tick plus slack for a loaded CI runner.
constexpr int kMaxReclaimMs = 2000;

int g_failures = 0;

void Check(bool condition, const char* what) {
    std::printf("%s %s\n", condition ? "[ ok ]" : "[FAIL]", what);
    if (!condition) ++g_failures;
}

int64_t NowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

SOCKET OpenSquatter(uint16_t port) {
    SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == INVALID_SOCKET) return INVALID_SOCKET;

    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;
    if (bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
        closesocket(s);
        return INVALID_SOCKET;
    }
    return s;
}

void SendPose(SOCKET s, uint16_t port, double yaw, double pitch, double roll) {
    double packet[6] = {0.0, 0.0, 0.0, yaw, pitch, roll};
    static_assert(sizeof(packet) == cameraunlock::OpenTrackPacket::kMinPacketSize,
                  "hand-built pose must stay the shape the receiver parses");

    sockaddr_in dest = {};
    dest.sin_family = AF_INET;
    dest.sin_port = htons(port);
    inet_pton(AF_INET, "127.0.0.1", &dest.sin_addr);

    sendto(s, reinterpret_cast<const char*>(packet), sizeof(packet), 0,
           reinterpret_cast<sockaddr*>(&dest), sizeof(dest));
}

}  // namespace

int main() {
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        std::printf("[FAIL] WSAStartup\n");
        return 1;
    }

    SOCKET squatter = OpenSquatter(kTestPort);
    Check(squatter != INVALID_SOCKET, "test can occupy the port first");
    if (squatter == INVALID_SOCKET) return 1;

    cameraunlock::UdpReceiver receiver;
    receiver.SetLog([](const std::string& msg) { std::printf("       UDP: %s\n", msg.c_str()); });

    // The port is held, so the bind must FAIL rather than succeed alongside the
    // other holder. A success here means SO_REUSEADDR crept back into
    // UdpSocket::Open and the mod would sit bound and permanently deaf.
    Check(!receiver.Start(kTestPort), "Start reports failure while the port is held");
    Check(receiver.IsRetrying(), "receiver is retrying");
    Check(!receiver.IsRunning(), "receive thread is not running");

    // The previous game exits.
    closesocket(squatter);
    const int64_t releasedAt = NowMs();

    while (!receiver.IsRunning() && NowMs() - releasedAt < kMaxReclaimMs) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    const int64_t reclaimMs = NowMs() - releasedAt;

    Check(receiver.IsRunning(), "receiver bound the port once it was freed");
    Check(!receiver.IsRetrying(), "retry state cleared after binding");
    Check(!receiver.IsFailed(), "failure state cleared after binding");
    std::printf("       reclaimed in %lldms (budget %dms)\n",
                static_cast<long long>(reclaimMs), kMaxReclaimMs);
    Check(reclaimMs < kMaxReclaimMs, "reclaim happened promptly");

    // Bound is not the same as receiving: the tracker has been sending the
    // whole time, so poses must flow the moment the socket is ours.
    SOCKET sender = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    Check(sender != INVALID_SOCKET, "test can open a sender socket");

    const int64_t sendStart = NowMs();
    float yaw = 0.0f, pitch = 0.0f, roll = 0.0f;
    bool gotPose = false;
    while (NowMs() - sendStart < 1000) {
        SendPose(sender, kTestPort, 12.0, -3.0, 1.5);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        if (receiver.IsReceiving() && receiver.GetRotation(yaw, pitch, roll)) {
            gotPose = true;
            break;
        }
    }

    Check(gotPose, "tracker packets reach the reclaimed socket");
    Check(gotPose && yaw > 11.5f && yaw < 12.5f, "yaw parsed from the reclaimed socket");

    closesocket(sender);
    receiver.Stop();
    WSACleanup();

    if (g_failures == 0) {
        std::printf("\nAll checks passed\n");
        return 0;
    }
    std::printf("\n%d check(s) FAILED\n", g_failures);
    return 1;
}
