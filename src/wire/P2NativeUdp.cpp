#include "P2NativeUdp.h"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
using socket_send_len_t = int;
using socket_send_size_t = int;
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
using socket_send_len_t = ssize_t;
using socket_send_size_t = size_t;
#endif

namespace lyra::wire {

void P2NativeUdp::attach(qintptr socket, quint32 ipv4HostOrder) noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    socket_ = socket;
    ipv4HostOrder_ = ipv4HostOrder;
}

void P2NativeUdp::detach() noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    socket_ = -1;
    ipv4HostOrder_ = 0;
}

bool P2NativeUdp::send(const void *data, int length, quint16 destPort) noexcept {
    if (!data || length <= 0)
        return false;

    std::lock_guard<std::mutex> lock(mutex_);
    if (socket_ < 0 || ipv4HostOrder_ == 0)
        return false;

    sockaddr_in dest{};
    dest.sin_family = AF_INET;
    dest.sin_port = htons(destPort);
    dest.sin_addr.s_addr = htonl(ipv4HostOrder_);

#if defined(_WIN32)
    const SOCKET fd = static_cast<SOCKET>(socket_);
#else
    const int fd = static_cast<int>(socket_);
#endif
    const socket_send_len_t n = ::sendto(
        fd, static_cast<const char *>(data),
        static_cast<socket_send_size_t>(length), 0,
        reinterpret_cast<const sockaddr *>(&dest),
        static_cast<int>(sizeof(dest)));
#if defined(_WIN32)
    return n != SOCKET_ERROR && n == length;
#else
    return n == static_cast<socket_send_len_t>(length);
#endif
}

} // namespace lyra::wire
