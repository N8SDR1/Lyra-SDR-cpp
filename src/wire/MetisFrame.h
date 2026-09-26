// Lyra — MetisFrame wire-emit primitive (§6-B + §7 / §10.2 wire layer).
//
// TU-scope free functions + file-scope mutable state mirroring
// the reference's 2-layer wire-emit chain:
//
//   `int MetisWriteFrame(int endpoint, char* bufp)` at
//     `ChannelMaster/networkproto1.c:216-237` — builds the 8-byte
//     HPSDR header + memcpys the 1024-byte payload + bumps
//     `MetisOutBoundSeqNum`, then calls `sendPacket(...)`.
//   `int sendPacket(SOCKET sock, char* data, int length, int port)`
//     at `network.c:1382-1402` — constructs `sockaddr_in dest`
//     per-call from `MetisAddr` (file-scope IP) + `port` arg,
//     holds `prn->sndpkt` lock around dest setup + `sendto`,
//     calls `bandwidth_monitor_out(ret)` AFTER releasing lock,
//     logs `sendto failed` on `SOCKET_ERROR`.
//
// Lyra mirrors verbatim: `metis_write_frame` builds the datagram
// and delegates to a TU-scope `send_packet`-equivalent that
// constructs the dest sockaddr per call from the file-scope
// `g_metis_addr` + `prn->base_outbound_port` and holds
// `prn->sndpkt` around `sendto`.
//
// Both the EP2 send thread (`Ep2SendThread::run_loop` —
// `sendProtocol1Samples` mirror) AND the startup C&C priming
// pass (`force_candc_frames` — `ForceCandCFrames` mirror) call
// into this single shared primitive and share the single
// outbound sequence counter — matching the reference's pattern
// where every caller funnels through one `MetisWriteFrame` with
// one `MetisOutBoundSeqNum` global.
//
// 🔴 2026-06-06 TX-Agent-1 B14/B15/B16/B17 correction:
// the earlier Lyra-native single-layer collapse (inline `::sendto`
// in `metis_write_frame` with no `sndpkt` lock + no
// `bandwidth_monitor_out` + no error log + an opaque
// `g_metis_dest_addr` blob in place of the per-call sockaddr
// construct from `MetisAddr`) was a deviation from the reference
// 2-layer structure and removed per "do as reference, period."

#pragma once

#include <cstddef>
#include <cstdint>

namespace lyra::wire {

// ---- wire-bind setter ----
//
// Sets the TU-scope socket descriptor + the radio's IP address.
// Direct mirror of the reference's per-session initialization of
// the `listenSock` global (`network.c` file-scope) + the
// `MetisAddr` global at the discovery/StartMetis path.
//
// `radio_ip_be` is the radio's IPv4 address in network byte order
// (the same form stored in `sockaddr_in::sin_addr::s_addr`).
// The destination port is read from `prn->base_outbound_port` at
// each send call, not stored here — matching reference's
// `sendPacket(... prn->base_outbound_port)` pattern at
// `network.c:1382-1402`.
//
// Caller (the session-open path in `hl2_stream.cpp`) is responsible
// for calling this ONCE per session.  Reference does the equivalent
// init exactly once at StartMetis.
void metis_wire_bind(int           socket_fd,
                     std::uint32_t radio_ip_be);

// ---- metis_write_frame ----
//
// Composes the 8-byte HPSDR header (`0xEF 0xFE 0x01 endpoint` +
// 4-byte BE outbound sequence number), memcpys the 1024-byte
// payload, calls the TU-scope `send_packet`-equivalent (which
// holds `prn->sndpkt` around `sendto`), and post-increments
// `g_metis_out_seq_num`.  Returns the payload bytes sent
// (positive — should equal 1024 on success) or `-1` on a socket
// error.
//
// Direct mirror of `int MetisWriteFrame(int endpoint, char*
// bufp)` at `networkproto1.c:216-237`.  Reference quirks
// preserved verbatim per Rule 24:
//   - post-increment (first emit ships seq=0, second ships
//     seq=1, ...)
//   - 4-byte BE byte-swap of native uint32 into wire bytes
//     `[4..7]` (high byte first)
//   - 1024-byte payload size + 8-byte header = 1032 bytes
//     out the door
//
// Caller MUST have called `metis_wire_bind()` first.  Reference
// does NOT null-check inside `MetisWriteFrame` — would crash on
// a null `listenSock` / `prn`.  Lyra preserves the same contract.
int  metis_write_frame(int            endpoint,
                       const uint8_t* payload_1024);

// ---- diagnostic accessor ----
//
// Returns the current value of the TU-scope outbound sequence
// counter.  Diagnostic only — production callers should not
// branch on this value (the wire path increments it as a side
// effect of `metis_write_frame`).
std::uint32_t metis_out_seq_num();

// ---- TU-scope socket accessor ----
//
// Returns the bound socket fd, or -1 if `metis_wire_bind()` has
// not been called.  Direct mirror of reference's file-scope
// `listenSock` global — both `Ep6RecvThread` and `Ep2SendThread`
// consume it via this accessor.
int metis_socket_fd();

// Radio IPv4 (network byte order) from metis_wire_bind, or 0 if unset.
// EP6 recv drops datagrams whose source is not this address so leftover
// Protocol-2 IQ from another radio on the LAN cannot starve HL2 keepalive.
std::uint32_t metis_radio_ip_be();

}  // namespace lyra::wire
