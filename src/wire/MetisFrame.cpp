// Lyra — MetisFrame wire-emit primitive implementation.
// See MetisFrame.h for the §6-B + §7 reference-parity contract.

#include "wire/MetisFrame.h"
#include "wire/RadioNet.h"  // prn->sndpkt + prn->base_outbound_port

#include <array>
#include <cstring>
#include <cstdio>
#include <mutex>

#if defined(_WIN32)
  #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
  #endif
  #ifndef NOMINMAX
    #define NOMINMAX
  #endif
  #include <winsock2.h>
  using socket_send_len_t  = int;
  using socket_send_size_t = int;
#else
  #include <sys/socket.h>
  #include <arpa/inet.h>
  #include <unistd.h>
  using socket_send_len_t  = ssize_t;
  using socket_send_size_t = size_t;
#endif

namespace lyra::wire {

// ---- TU-scope file-scope mutable state ----
//
// Direct mirror of the reference's:
//   `unsigned int MetisOutBoundSeqNum;`     (`networkproto1.c:30`)
//   `extern SOCKET listenSock;`             (file-scope, network.c)
//   `IN_ADDR MetisAddr;` (or equivalent)    (file-scope, network.c)
//
// `prn->base_outbound_port` (held in the prn struct) is read at
// each send call — NOT cached here, matching reference's
// `sendPacket(... prn->base_outbound_port)` pattern.
namespace {
std::uint32_t g_metis_out_seq_num{0};
int           g_metis_socket_fd  {-1};
std::uint32_t g_metis_addr_be    {0};   // radio IPv4 in network byte order

// EP2 outbound datagram: 8-byte HPSDR header + 1024-byte payload
// = 1032 bytes.  Direct mirror of reference's
// `unsigned char framebuf[1032];` (`networkproto1.c:218-220`).
constexpr std::size_t kHpsdrHeaderBytes = 8;
constexpr std::size_t kPayloadBytes     = 1024;
constexpr std::size_t kDatagramBytes    = kHpsdrHeaderBytes + kPayloadBytes;

// ---- send_packet (TU-scope; reference `sendPacket` mirror) ----
//
// Reference body verbatim from `network.c:1382-1402`:
//
//   int sendPacket(SOCKET sock, char* data, int length, int port) {
//     int ret;
//     struct sockaddr_in dest = { 0 };
//     EnterCriticalSection(&prn->sndpkt);
//     dest.sin_port = htons((u_short)port);
//     dest.sin_family = AF_INET;
//     dest.sin_addr.s_addr = MetisAddr;
//     ret = sendto(sock, data, length, 0, (SOCKADDR*)&dest, sizeof(dest));
//     LeaveCriticalSection(&prn->sndpkt);
//     if (ret > 0) bandwidth_monitor_out(ret);
//     if (ret == SOCKET_ERROR) {
//       wprintf(L"sendto failed with error:%d\n", WSAGetLastError());
//     }
//     return ret;
//   }
//
// `length` is the FULL datagram length (header + payload = 1032).
int send_packet(int sock,
                const std::uint8_t* data,
                int length,
                int port) {
    socket_send_len_t ret = 0;
    {
        std::lock_guard<std::mutex> lk(prn->sndpkt);
        sockaddr_in dest{};
        dest.sin_family      = AF_INET;
        dest.sin_port        = htons(static_cast<std::uint16_t>(port));
        dest.sin_addr.s_addr = g_metis_addr_be;
        ret = ::sendto(sock,
                       reinterpret_cast<const char*>(data),
                       static_cast<socket_send_size_t>(length),
                       0,
                       reinterpret_cast<const sockaddr*>(&dest),
                       static_cast<int>(sizeof(dest)));
    }
    // FIXME (Task #114): `bandwidth_monitor_out(ret)` per
    // `network.c:1394` — Lyra has no bandwidth-meter subsystem
    // yet.  Call site stub preserved at the reference-faithful
    // location (outside the `sndpkt` lock).
    // if (ret > 0) bandwidth_monitor_out(static_cast<int>(ret));

#if defined(_WIN32)
    if (ret == SOCKET_ERROR) {
        // Reference: `wprintf(L"sendto failed with error:%d\n",
        // WSAGetLastError());` at `network.c:1396-1399`.
        std::fprintf(stderr,
                     "sendto failed with error:%d\n",
                     WSAGetLastError());
    }
#else
    if (ret < 0) {
        std::fprintf(stderr, "sendto failed: %s\n", std::strerror(errno));
    }
#endif
    return static_cast<int>(ret);
}
}  // namespace

// ---- metis_wire_bind ----

void metis_wire_bind(int socket_fd, std::uint32_t radio_ip_be) {
    g_metis_socket_fd = socket_fd;
    g_metis_addr_be   = radio_ip_be;
}

// ---- metis_write_frame ----
//
// Reference-verbatim port of `int MetisWriteFrame(int endpoint,
// char* bufp)` at `networkproto1.c:216-237`.

int metis_write_frame(int            endpoint,
                      const uint8_t* payload_1024) {
    // Caller contract:  `metis_wire_bind()` MUST have been called
    // first.  Reference does NOT null-check inside MetisWriteFrame
    // — would crash on a null `listenSock` / `prn`.  Lyra
    // preserves the same contract.

    std::array<uint8_t, kDatagramBytes> framebuf{};

    // 4-byte HPSDR sync prefix (`:223-226`).
    framebuf[0] = 0xEF;
    framebuf[1] = 0xFE;
    framebuf[2] = 0x01;
    framebuf[3] = static_cast<uint8_t>(endpoint);

    // 4-byte outbound sequence number, BE pack (`:221, 227-231`).
    // Reference uses `unsigned char* p = (unsigned char*)
    // &MetisOutBoundSeqNum; framebuf[4]=p[3]; framebuf[5]=p[2];
    // framebuf[6]=p[1]; framebuf[7]=p[0];` — manual byte-swap
    // relying on little-endian host layout.  Lyra uses explicit
    // shifts (endian-portable, byte-identical wire result).
    // Post-increment ordering (read → write bytes → increment)
    // preserved verbatim per Rule 24.
    const std::uint32_t seq = g_metis_out_seq_num;
    framebuf[4] = static_cast<uint8_t>((seq >> 24) & 0xff);
    framebuf[5] = static_cast<uint8_t>((seq >> 16) & 0xff);
    framebuf[6] = static_cast<uint8_t>((seq >>  8) & 0xff);
    framebuf[7] = static_cast<uint8_t>( seq        & 0xff);
    ++g_metis_out_seq_num;  // matches reference `++MetisOutBoundSeqNum;` :231

    // Payload copy (`:232`).
    std::memcpy(framebuf.data() + kHpsdrHeaderBytes,
                payload_1024,
                kPayloadBytes);

    // sendto via `send_packet` (`:234` → `sendPacket(listenSock,
    // &outpacket, 1032, prn->base_outbound_port);`).  send_packet
    // constructs sockaddr_in per call from `g_metis_addr_be` +
    // port, holds `prn->sndpkt`, logs `sendto failed` on error.
    const int result = send_packet(g_metis_socket_fd,
                                   framebuf.data(),
                                   static_cast<int>(kDatagramBytes),
                                   prn->base_outbound_port);

    if (result < 0) {
        return -1;
    }
    // Return payload bytes only (matches reference `result -= 8;`
    // at `:235`).
    return result - static_cast<int>(kHpsdrHeaderBytes);
}

// ---- diagnostic accessor ----

std::uint32_t metis_out_seq_num() {
    return g_metis_out_seq_num;
}

// ---- TU-scope socket accessor ----

int metis_socket_fd() {
    return g_metis_socket_fd;
}

std::uint32_t metis_radio_ip_be() {
    return g_metis_addr_be;
}

}  // namespace lyra::wire
