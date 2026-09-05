// Lyra — Winsock2 surface on POSIX hosts.
//
// hl2_stream.cpp deliberately uses raw sockets rather than QUdpSocket for
// the EP2/EP6 hot path (tight cadence, no event-loop hop).  It was
// written against Winsock2; this header supplies the same names on POSIX
// so the wire logic itself stays unchanged.
//
// The two APIs are close enough that the mapping is nearly one-to-one.
// The differences that actually matter:
//   * Winsock uses an opaque SOCKET with INVALID_SOCKET; POSIX uses an
//     int fd with -1.
//   * Winsock reports errors out-of-band via WSAGetLastError(); POSIX
//     uses errno.
//   * closesocket() vs close(), ioctlsocket() vs ioctl().
//   * Winsock's setsockopt takes const char*; POSIX takes const void*.
//     The ported call sites already pass char*, which converts silently.
//
// On Windows this header is empty and the real Winsock2 is used.

#pragma once

#if !defined(_WIN32)

#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

using SOCKET = int;

#define INVALID_SOCKET (-1)
#define SOCKET_ERROR   (-1)

// Winsock error codes the ported code names explicitly.  POSIX equivalents
// so error-handling branches keep working.
#ifndef WSAEWOULDBLOCK
#  define WSAEWOULDBLOCK  EWOULDBLOCK
#endif
#ifndef WSAEINTR
#  define WSAEINTR        EINTR
#endif
#ifndef WSAECONNRESET
#  define WSAECONNRESET   ECONNRESET
#endif
#ifndef WSAEMSGSIZE
#  define WSAEMSGSIZE     EMSGSIZE
#endif
#ifndef WSAETIMEDOUT
#  define WSAETIMEDOUT    ETIMEDOUT
#endif
#ifndef WSAEADDRINUSE
#  define WSAEADDRINUSE   EADDRINUSE
#endif

inline int closesocket(SOCKET s) { return ::close(s); }

inline int ioctlsocket(SOCKET s, long command, unsigned long *argument)
{
    return ::ioctl(s, static_cast<unsigned long>(command), argument);
}

// Winsock keeps its last error per-thread and out of band; errno is the
// direct POSIX analogue and is likewise thread-local.
inline int WSAGetLastError() { return errno; }
inline void WSASetLastError(int error) { errno = error; }

#endif  // !_WIN32
