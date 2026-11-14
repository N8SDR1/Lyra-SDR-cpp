# Remote voice session

Working note for the host proof, the microphone switch, and the transmit side of Lyra Remote. Locked 2026-10-15. Operator help lives in `docs/help/USER_GUIDE.md` (Settings → Remote) and in the Lyra-Remote package `HELP.md`. Those two stay in step. This file is the build order.

## Order

1. Lyra proves the password and issues a session token.
2. The same success takes the voice session: browser transmit audio, host rack and leveler aside, nothing written into Settings.
3. Then the page grows the microphone, EQ, and key controls.

A TCI connection by itself does not move the microphone. The receiver page, and a digital program on the radio computer, leave the desk profile alone.

## Live today

- **Settings → Remote** stores the password as a PBKDF2-SHA256 hash (`remote/password_salt`, `remote/password_hash`, `remote/password_iters`), the listen checkbox (`remote/listen_requires_password`), and the address lists (`remote/accept_ips`, `remote/refuse_ips`).
- Address lists are enforced in `TciServer::onNewConnection`. Empty accept means every reachable address may connect. Refuse wins over accept. This computer, including its own LAN addresses, is always allowed.
- A remote client may ask `LYRA_VOICE`. Lyra answers once with `lyra_voice:challenge,<nonce hex>,<salt base64>,<iters>`. The client returns `LYRA_VOICE_PROOF` as HMAC-SHA256 of that nonce, keyed by the stored password hash. The password stays off the wire. A match answers `lyra_voice:granted,<token hex>`. The token is 32 random bytes, memory only, bound to that socket.
- Five wrong proofs lock that address for 15 minutes. Backoff is 1, 2, 4, 8, 16 seconds. A correct proof clears the count. When the lock expires, the next ask starts a fresh count. While locked, the reply is `lyra_voice:locked,<seconds>` and there is no new challenge. The status bar, the TCI chip tooltip, and Settings → Remote name the locked address.
- `LYRA_VOICE_RELEASE` drops this socket's token. Disconnect drops it. `LYRA_VOICE_TOKEN` succeeds only when this socket already holds that exact token. A new socket presenting a captured token is denied and does not count as a try.
- Loopback is granted a token with no challenge. Other TCI commands, including `trx`, are unchanged. The listen checkbox does not drop a socket. With no password saved, `LYRA_VOICE` answers `lyra_voice:denied,no_password` and does not count a try.
- The page has a password field, a microphone list, three EQ sliders, MOX, and Tune. **Start voice** is the only control that sends `LYRA_VOICE`. A normal Connect does not start a voice session. The password is not stored in the page. **Password required to listen** is still not a hold on spectrum or audio.
- Granting a token starts the live session: browser transmit audio, rack bypass, leveler parked. Nothing is written into Prefs. `trx:0,true,tci` still calls `setMicSource("tci")` only when no voice session is held. While one is held, that write is skipped.

## What stays open

- Localhost (`127.0.0.1`) is not asked for a password, so WSJT-X and MSHV on the radio computer keep keying.
- A digital program on another computer is a remote. It is challenged only when it asks to start a voice session. A bare `trx:0,true` still keys.
- Do not require the token on every TCI command. Those programs send a bare `trx:0,true`.
- Listening stays up while an address is locked. A refused address never counts as a password try.
- With no password saved, remote transmit stays impossible. Do not turn that into a wire gate until the page can prove a phrase, or the working receiver page and a second-PC digital program break.
- DIGU, DIGL, and DRM already step the rack aside by mode. They do not take the voice token.
- FM already steps the rack aside by mode. A voice session on FM still moves transmit audio to that browser.
- USB, LSB, and AM stay those modes. The session sets rack bypass for its own span. It does not force a digital mode.

## Stage 1 — proof and token

Landed on the host. The page still has no password box, so this stage does not refuse a receiver command or a localhost key.

- Challenge once when a remote client asks to start a voice session. The password itself stays off the wire.
- A correct proof returns a long random token, memory only, bound to that socket. A wrong proof keys nothing and moves nothing.
- Five wrong proofs from one address lock that address for 15 minutes. Backoff 1, 2, 4, 8, 16 seconds. A correct proof clears the count. While locked, no new challenge. The radio shows which address is locked.
- Releasing the token, closing the socket, or a new connection starts a fresh challenge. A captured token cannot reconnect.
- **Password required to listen** stays a saved checkbox until the page can answer. Wiring it as "drop the socket" before that bricks listening.

## Stage 2 — voice session

Landed on the host. A receiver connection still does not start it. The page sends `LYRA_VOICE` only from **Start voice**.

Runs only after Stage 1 accepts that client and the token is held. Three live changes. Write nothing into Prefs or QSettings.

- Transmit audio comes from that socket only.
- `SetTxRackBypass(1)` for EQ, speech (auto-AGC and de-esser), combinator, and tube plating. USB, LSB, and AM stay those modes. FM is already bypassed.
- Park the host leveler for the same span. ALC stays on.

Key and unkey do not flip the rack. Unkey only releases transmit audio. Restore the previous microphone path, rack bypass, and leveler when the session ends and when the socket drops mid-key. Settings were never rewritten, so a crash comes up on the desk profile.

One voice owner at a time. A second remote does not take the microphone while the token is held.

## Stage 3 — page transmit

Landed on the page. The listen checkbox is still not a gate.

- One password field. Not in the URL, not in localStorage, not logged, not echoed in TCI status.
- Browser microphone via `enumerateDevices` and `getUserMedia`. Lyra never sees the device name.
- Web Audio EQ on the samples before they are packed into the transmit audio stream.
- Then MOX and Tune.

The later IP remote uses the same token and the same three live changes. Each client carries its own audio. A receive-only connection never takes the token.

## Help

Stage 3 is on the page. The guides say **Start voice** asks, and that **Password required to listen** is saved and does not hold the spectrum yet. In-app Help in a running Lyra still shows the older paragraph until the next build.

In-app Help reads `docs/help/USER_GUIDE.md` through the `lyra_help` resource. A running Lyra shows the new section after the next build.

## Bench

Dummy load. Do not retune the live radio to prove this.

- A refused address never connects.
- Localhost MSHV still keys with the desk or TCI digital path it already had.
- A wrong proof keys nothing and leaves Mic source, rack bypass, and the leveler untouched.
- A held voice session on USB sends browser audio, leaves the mode on USB, and steps the rack aside.
- Unkey keeps the session. Dropping the socket restores the desk profile.
- Settings → TX → Mic source still reads the desk choice after the session.
