# Lyra-to-Lyra operate remote

Design note. Locked 2026-10-16 from the planning pass, revised the same day after review. **Hold.** No sockets, no UI, and no change to the browser desk until a later go-ahead. Windows is the first build. Linux is a separate hold and does not gate this plan.

The shack computer runs Lyra and keeps the HPSDR session. That Lyra is the host. A second Lyra, on the computer you are sitting at, is the desk. It operates the radio the host already has Open. It does not Discover, Add-by-IP, or send UDP to the radio.

The shack panel stays live while a desk is attached. The desk does not lock the host operator out. The host can drop the desk. The host unkey, TX inhibit, and TX timeout still win.

The browser package stays the house-network desk (`docs/wiki/Lyra-Remote.md`). This link is the off-site desk. One transmit owner at a time: a browser voice token and this link cannot both hold the microphone. The proof rules in `docs/architecture/remote_voice_session.md` still apply. The password stays off the wire. The token is memory-only. A new connection proves again.

```
radio on the shack LAN
        │  HPSDR UDP stays here
        ▼
   Host Lyra
        │  control, spectrum, and audio on three sockets
        ▼
     Tailscale
        │
        ▼
  Remote Lyra  ── mic on the audio socket, commands on control ──►  host
```

## Who needs the good internet

Design for the connection people actually buy, not for a thin pipe. A normal fiber or cable plan in this use is 100–200 Mbps. The product is aimed at that. Extra speed past the preferred number does not draw a better spectrum: once every local frame is on the wire, the picture is already the desk picture.

The direction that matters is not the same at both ends. The host sends the spectrum and the receiver audio, so the shack number is **upload**. The remote receives that stream, so the desk number is **download**. A cable plan advertised as 200 Mbps is usually the download. A shack whose upload is 15 Mbps does not qualify, even when the download looks large.

Capacity and delay are separate. A 3 ms reading to a nearby speed-test server is not the round trip to a desk in another state. A cross-country fiber path is often 50–80 ms and is still a solid radio link.

| | Host (shack) | Remote (desk) | Round trip |
|---|---|---|---|
| **Preferred** | 100 Mbps upload | 100 Mbps download | under 40 ms, the preferred feel |
| **Supported** | 25 Mbps upload | 25 Mbps download | under 100 ms |
| **Not supported** | under 25 Mbps upload | under 25 Mbps download | CW transmit from 150 ms; other transmit from 200 ms. Listening continues |

The 25 Mbps and 100 Mbps figures are what the guide tells you to buy. Lyra does not run a speed test and does not read the ISP plan. What Lyra measures is the keepalive round trip, and whether spectrum frames are arriving whole.

- **Preferred feel** is under 40 ms. Same-region paths land here.
- **Supported** is the 25 Mbps floor and a round trip under 100 ms. One panadapter at 60 Hz and the receiver audio are the designed link on that path. The 120 Hz rate and RX2 are allowed when the frames are keeping up, including on a long-haul path under 100 ms.
- **From 150 ms up to 200 ms,** listening stays up. CW transmit stays off. Other transmit stays available under 200 ms.
- **At 200 ms and above,** listening stays up and transmit stays off. When 60 Hz frames are not arriving whole, the session says the path cannot hold the picture. A silent link unkeys in about 300 ms.
- **Under 25 Mbps** in the carrying direction is outside the design. Say that in the guide. A phone tether and ordinary cell service are not a supported connection. There is no cellphone mode. Lyra catches them by round trip and by frames that do not keep up, not by a speed test.

A link that was fine and then falls apart still unkeys. That safety step is not a promise that a weak circuit can operate.

Raw IQ stays off this link. The host keeps running WDSP. The remote paints from the full 4096-point spectrum cache (`kAnPixels`), not from the zoomed crop the shack window uses. Each point is an int16 in tenths of a dB, so the frame stays 4096 × 2 bytes. Center and span travel as numbers. The desk zooms on its own screen. The waterfall is those lines. It is not a second picture.

The network path reads the cache the shack display already updated. It does not call `GetPixels`. The panadapter and the waterfall already share that cache, and a fresh read clears the ready flag. A reader on the network thread would steal frames from the shack display.

## Getting past the router

No port forward. Lyra does not listen on the public internet, and the guide does not ask anyone to open a port on the house router. A forwarded port is how remote-SDR setups get lost: the shack is often behind CGNAT, the public address changes, and the radio's UDP must never be exposed. This link does not use that.

The documented path is **Tailscale** on both PCs. Both computers connect outward. Tailscale gives them a private name and address. The desk Lyra connects to the shack Lyra by that name. Neither machine accepts a connection from the open internet. When the two can see each other directly, the audio and spectrum go machine to machine. When a direct path is not available, Tailscale carries the same encrypted traffic through its relay. The operator does not choose that. Lyra still requires the Remote password, so a computer that is only on the tailnet cannot transmit.

Tailscale's Personal plan is free for this use: up to 6 users and unlimited computers on one account. Two of your own PCs cost nothing. Sign in with a personal address such as Gmail. An address on your own domain starts an Enterprise trial instead of the free plan. The paid Standard and Premium plans are for teams that have outgrown six users. They are not required here.

The guide teaches Tailscale only. Someone who already runs another mesh that gives both PCs a private address can paste that name. Lyra refuses a public IP, a blank name, and any listen address on the open internet. It does not refuse a private mesh just because the name is not a Tailscale name.

The TCI accept and refuse lists are not applied to this link. Those lists are house addresses. A Tailscale address is in a different range, so a correct house rule would reject the desk. This listener accepts the private mesh name only. The Remote password still gates transmit.

A Lyra-operated relay is not the plan. It would also avoid a port forward, and it would put every session's audio on a server we have to run. Tailscale already solves the reachability. The usual path stays direct.

### Fields in Lyra

Both ends show a hostname on **Settings → Remote**, in the **Lyra to Lyra** group. The same app has both controls. The shack uses **Allow desk**. The desk uses **Attach to host**. The control port is **40111**, bound to the Tailscale address only.

- **Shack.** A hostname field filled from Tailscale, with Copy. You do not invent this name. Next to it, **Allow desk**. That is the host side of attach. Until it is on, Lyra does not accept a desk. While a desk is on, the same field shows that desk's name, and **Drop desk** is there. There is no **Attach to host** on the shack. This Lyra is the host.
- **Desk.** A hostname field you paste or type into, and **Attach to host**. After it connects, the field stays so you can see it matches the shack. Detach is the way off.

The two fields hold the same name: the shack's private name. The desk does not have its own name to give the shack. The shack does not type the desk's name to be found.

### Steps the guide will give

These are the whole setup. Nothing on the router.

1. On the shack PC and on the desk PC, install Tailscale. Sign in with the same account, using a personal email. Leave it running.
2. Both computers show in the Tailscale machines list. That is the check that they can reach each other.
3. On the shack, Lyra stays Open on the radio. The hostname field shows the Tailscale name. Copy it. Turn **Allow desk** on. Lyra listens on that private name, not on the public internet.
4. On the desk, paste that name into the hostname field and choose **Attach to host**. Do not type a public IP, and do not type the radio's address. The name in the two fields should match.
5. Listening can start. **Start voice** still proves the Remote password. The password stays off the wire. The token is memory-only, same rules as the browser desk.
6. If Tailscale shows the path as relayed rather than direct, check the round trip. Relayed is still encrypted. CW transmit needs under 150 ms. Other transmit needs under 200 ms. Listening continues above that. Under 100 ms, the session is supported. Under 40 ms is the preferred feel. A silent link unkeys in about 300 ms. The desk shows the worst ping for about 3 seconds, then the latest ping, with RX, CW, and Voice lamps for that latest ping.

Do not port-forward. Do not turn on Tailscale Funnel. Funnel would publish the shack onto the public internet. This design uses the private tailnet only.

## How the link stays reliable

Three sockets run inside that private address. Tailscale does encryption and NAT. Lyra's own password proof is what allows transmit.

- **Control.** Reliable and small. Keepalive, frequency, mode, MOX, Tune, meters, session state. This is the only path that carries unkey.
- **Spectrum.** The 4096-point frames. A late frame is dropped. It must not sit in front of audio or an unkey.
- **Audio.** Receiver audio toward the desk, and the desk microphone back toward the host. Spectrum traffic is not on this socket.

Keepalive on the control socket about every 100 ms. If the host misses three while transmitting, it unkeys by about 300 ms and does not wait for TCP or Tailscale to notice. The host operator's own unkey, TX inhibit, and TX timeout always win. The host can drop the desk. A reconnect may listen again. The transmit token does not survive the drop. Switching or stopping the radio on the host drops it too.

Never drop the keepalive to save bandwidth. Never add a tier whose purpose is to make a cellphone usable. Never run a speed test to decide the tier.

- **Frames keeping up,** round trip under 100 ms: receiver audio about 128–192 kbps, spectrum at 60 Hz, full 4096 points. When the frames still keep up, the rate may follow the Spec slider to 120 Hz, and RX2's spectrum plus RX2's audio are allowed. The stream stays under about 20 Mbps.
- **25 Mbps floor, frames keeping up, round trip under 100 ms:** one spectrum at 60 Hz and one receiver audio. The second picture and the second audio are not promised if the frames are only just holding 60 Hz.
- **Frames falling behind:** the session says the path cannot hold the picture. If the control socket goes late or silent while transmitting, forced receive still happens. A silent link unkeys in about 300 ms.
- **Round trip from 150 ms up to 200 ms:** listening stays up. CW transmit stays off. Other transmit stays available under 200 ms.
- **Round trip 200 ms and above:** listening stays up. Transmit stays off.

## What "operate" includes

In scope, because you need them to hear, tune, and key safely:

- Session lamps on both sides: attached, keyed, kicked, link too poor. The shack controls stay usable. The host can drop the desk.
- Receiver audio, mono, compressed, with a short jitter buffer on the remote (start near 80 ms, grow under loss, cap so it cannot become seconds).
- VFO, band, mode, filter width, RIT, Split frequency. Drive changes stay inside the host's existing caps and PA rules.
- Spectrum and waterfall from the full 4096-point cache. The desk zooms locally. Click-tune sends a frequency, not a mouse pixel. At 60 Hz this is the supported picture. Up to 120 Hz when frames keep up.
- S-meter, forward power, SWR.
- MOX and Tune, after the password proof.
- The receive controls you touch while operating: volume, AGC, NR on or off, squelch.
- One radio: the one the host has Open. The host's other radios can show as names. The remote cannot Open, stop, or Discover a radio.
- RX2's spectrum and RX2's audio when the frames are keeping up. They are not part of the 60 Hz minimum picture.

Out of scope until a later plan. They are not required to operate, or they are unsafe or too heavy on this path:

- IQ, recordings, and captured-profile capture.
- PureSignal calibration, Diversity, and TX-rack curve editing. The host's saved TX profile stays in force. A voice session may still step the rack aside the way the browser voice session already does, and those changes are still not written into Settings.
- Paddle and QSK CW. Internet delay is too long. Keyboard CW can be its own later slice because the host keyer would send the characters.
- Gateware, protocol choice, and any remote write that Opens, stops, or retunes the radio's wire session except through the host's normal tune and key paths.

## Bandwidth sketch

4096 points × 16-bit is about 4 Mbps at 60 Hz and about 8 Mbps at 120 Hz. Receiver audio for this slice is 48 kHz mono int16 PCM on port 40112, about 768 kbps, on its own socket. The spectrum is the full 4096-point cache as int16 tenths of a dB on port 40113, about 4 Mbps at 60 Hz, with center and span in the frame. Frequency, mode, filter width, RIT, Split, AGC, NR, squelch, and drive ride the control socket. The host sends S-meter, forward power, and SWR about five times a second on that same socket. The desk shows those meters and does not send them back. Opus is not in the tree; a codec can replace the PCM payload later without moving the socket. Meters and the other operate controls are noise next to the spectrum. Two spectra at 120 Hz, plus a second receiver audio, land near 16 Mbps. That is why 100 Mbps is preferred and 25 Mbps is the floor for a single 60 Hz picture: the pipe stays several times larger than the stream.

- Host to desk, when frames keep up: that spectrum plus audio, under about 20 Mbps even with RX2.
- Desk to host: microphone about 128 kbps on the audio socket, plus commands on the control socket. The desk's download is the requirement. Its upload is small on a fiber or cable plan and is still part of a real connection, not a phone uplink.
- A circuit under 25 Mbps in the carrying direction is outside the design. Cell tether included.

## Where it will live

The host side sits next to `src/tci_server.cpp` as its own link, not as more TCI commands. TCI stays the digital-app and browser-desk socket, including its accept and refuse lists. Those lists are not consulted here. Spectrum frames are quantized from the cache `WdspEngine` already keeps (`kAnPixels` = 4096 in `src/wdsp_engine.cpp`). The sender does not call `GetPixels`. The shack window has the hostname field and **Allow desk**. The desk window has the hostname field and **Attach to host**. Tailscale stays a Windows service Lyra does not start or stop. The host tells the desk which radio is open, Protocol 1 or Protocol 2, and the rig name. The desk front end follows that radio: LNA on a Hermes Lite, ATT on Protocol 2. The band row's ANT and RX chips follow a Protocol 2 radio that has an antenna ladder, and the desk can switch them. SUB and the second receiver frequency follow the shack, so the second frequency display appears when the shack has it on. NNR, transmit filter width, and the transmit low cutoff ride the control socket with the other operate controls. Rig → Remote opens this page. A desk profile saved on the laptop holds the filters and the transmit rack. Recall applies that set on the shack for the session and does not write the shack profile. End voice, Detach, and Drop desk put the shack filters and rack back. PureSignal and Amp View on the desk show the correction calculated on the shack. That mode is instead of Open on a local radio. Password storage stays in `src/prefs.h` (`remotePwHash_` and the address lists). Operator steps: [Lyra to Lyra](../help/USER_GUIDE.md#lyra-to-lyra).

## Build order

Still a hold on coding until a go-ahead. The order below is the plan. Each slice keeps the rules above. A later slice does not relax them to make a weak link look connected.

1. **Attach, and refuse a bad path.** The shack shows its hostname and listens only after **Allow desk**. The desk pastes that name and uses **Attach to host**. A public IP, a blank name, or a listen on the open internet is refused. Control socket, keepalive, session lamps. Lyra measures round trip from the keepalive. One latest ping decides. CW transmit needs under 150 ms. Other transmit needs under 200 ms. Above that, listening stays up and the key stays off. A silent link unkeys in about 300 ms. The desk shows that ping as a peak held for about 3 seconds, plus RX, CW, and Voice lamps that follow the latest ping. No port-forward control anywhere in the UI. No speed test. The TCI address lists are not applied.
2. **Receiver audio.** Audio socket, separate from control and from spectrum. Compressed mono about 128–192 kbps. Jitter buffer starts near 80 ms, grows under loss, and is capped. A stall on this socket does not delay an unkey on the control socket.
3. **One spectrum at 60 Hz.** Full 4096-point cache, int16 tenths of a dB, read from the cache the display already updated. No `GetPixels` on this path. Waterfall is those lines. Center and span go with the frame so the desk zooms locally. This is the supported picture: about 4 Mbps plus the audio. Click-tune sends a frequency.
4. **Operate controls.** VFO, band, mode, filter width, RIT, Split, volume, AGC, NR on or off, squelch, drive inside the host caps, S-meter, forward power, SWR. The shack panel stays live. The remote cannot Open, stop, or Discover a radio. The host can drop the desk.
5. **Transmit.** MOX and Tune only after the Remote password proof. One owner. A browser voice token and this link cannot both hold the microphone. Mic audio returns on the audio socket into the transmitter. CW transmit needs a round trip under 150 ms. Other transmit needs under 200 ms. Above that, listening stays up and the key stays off. The desk shows a short peak-held ping and RX, CW, and Voice lamps for the latest ping. Three missed keepalives while transmitting unkey in about 300 ms. Host unkey, TX inhibit, and TX timeout win. Dropping the link, dropping the desk, or stopping the radio drops the token. This is the current local build.
6. **120 Hz and RX2.** When 60 Hz frames are arriving whole and the round trip is under 100 ms, the spectrum may follow the Spec slider up to 120 Hz, and RX2's spectrum plus RX2's audio are allowed. The stream stays under about 20 Mbps. If frames fall behind, those two turn off and the session says the path cannot hold that picture.

## Acceptance against the minimums

A slice is done only if all of these are true on a dummy load:

- On a path at or above **25 Mbps** in the carrying direction, with round trip under **100 ms**, one 60 Hz spectrum and the audio arrive whole.
- On a path at the preferred **100 Mbps**, with frames keeping up, 60–120 Hz and RX2's spectrum plus RX2's audio stay under about **20 Mbps**. A round trip under **40 ms** is the preferred feel, not a gate that hides 120 Hz from a long-haul path under 100 ms.
- CW transmit needs a round trip under **150 ms**. Other transmit needs under **200 ms**. Above that, listening stays up and the key stays off. A silent link unkeys in about **300 ms**. The desk shows a short peak-held ping and RX, CW, and Voice lamps for the latest ping. 60 Hz frames that are not arriving whole are told the path cannot hold the picture.
- The guide states the 25 Mbps floor. Lyra does not run a speed test to enforce it.
- A public IP cannot attach. A private mesh name can. The TCI accept and refuse lists do not drop this session.
- No router port is opened. The guide's six Tailscale steps are sufficient, including the personal-email signup.
- Killing the desk process, the host dropping the desk, or pulling the path returns the host to receive without a transmit token left behind. The shack controls never go dead while a desk is attached.
