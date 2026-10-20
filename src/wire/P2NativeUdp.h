// Thread-safe UDP send on the already-bound Protocol 2 session socket.
//
// The session QUdpSocket owns bind + recv (Qt thread affinity). Host→radio
// datagrams — including DUC-IQ on port 1029 from a dedicated writer thread —
// go through sendto() on that same SOCKET, serialized like P1's sndpkt lock.
// Do not call QUdpSocket from the TX-IQ thread.

#pragma once

#include <QtGlobal>

#include <cstdint>
#include <mutex>

namespace lyra::wire {

class P2NativeUdp final {
public:
    void attach(qintptr socket, quint32 ipv4HostOrder) noexcept;
    void detach() noexcept;
    bool send(const void *data, int length, quint16 destPort) noexcept;

private:
    std::mutex mutex_;
    qintptr socket_ = -1;
    quint32 ipv4HostOrder_ = 0;
};

} // namespace lyra::wire
