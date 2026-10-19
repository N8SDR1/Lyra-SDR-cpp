# Lyra Remote

A browser desk for the Lyra that is already running. It is a **separate package**. Lyra does not host the page, and the installer does not contain it.

| | |
|---|---|
| **Download the page** | [Lyra Remote releases](https://github.com/N8SDR1/Lyra-Remote/releases) |
| **How the page works** | [Lyra Remote wiki](https://github.com/N8SDR1/Lyra-Remote/wiki) |
| **Download Lyra** | [Lyra releases](https://github.com/N8SDR1/Lyra-SDR-cpp/releases/latest) |

On the radio computer, **Settings → Remote** is the password, who may connect, and the voice session. **Settings → Network** is the bind address and the TCI port (default `40001`). Leave the bind at `127.0.0.1` unless another computer on the house network should reach Lyra. Do not forward that port to the internet.

On the computer where you sit, open the page. Leave the radio box at `ws://127.0.0.1:40001` when that computer is also running Lyra. From another house computer, fill `ws://` plus the radio computer's house address and port.

**Start voice** proves the password once. Then **MOX** uses that browser's microphone and **Tune** sends a carrier from Lyra. A digital program on the radio computer is not asked for the password. End the voice session before that program transmits.

The full steps are in the [Lyra Remote wiki](https://github.com/N8SDR1/Lyra-Remote/wiki) and in the [User Guide → Settings → Remote](User-Guide#settings--remote). A second Lyra on a laptop is [Lyra to Lyra](User-Guide#lyra-to-lyra) in that guide.
