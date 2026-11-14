# Steinberg ASIO SDK 2.3.4 — Lyra copy

Source: Steinberg ASIO SDK **2.3.4, 15 October 2025** (the first release
dual-licensed proprietary / GPLv3 — see `changes.txt`).

## Licence

Lyra uses the SDK under the **GNU General Public License version 3**
option of `LICENSE.txt` ("…or alternatively under the terms of the
General Public License (GPL) Version 3").  Lyra is GPL-3.0-or-later, so a
Lyra build that includes ASIO support is distributed under GPLv3.  The
SDK's own licence text is kept verbatim in `LICENSE.txt` and
`common/LICENSE.txt`.

## What is included

Only the files an ASIO *host* (a program that uses ASIO drivers) needs,
all of which carry the dual-licence header:

- `common/asio.cpp`, `asio.h`, `asiosys.h`, `iasiodrv.h`
- `host/asiodrivers.cpp`, `asiodrivers.h`, `ginclude.h`
- `host/pc/asiolist.cpp`, `asiolist.h`
- `LICENSE.txt`, `common/LICENSE.txt`, `README.md`, `changes.txt`
- `logo/` — two of the official "ASIO Compatible" logo images, image data unaltered (files renamed to plain ASCII names for the Qt resource system).

Left out: the driver-side files (`combase.*`, `wxdebug.h`, `dllentry.cpp`,
`asiodrvr.*`, `register.cpp`, `debugmessage.cpp`, `driver/`), the host
sample, the PDFs and the old MSVC project files.  The *Steinberg ASIO
Usage Guidelines* PDF ships in the SDK download; read it there.

## Local changes

1. `host/pc/asiolist.cpp`: `delete lpdrv;` → `delete[] lpdrv;` (the
   struct is allocated with `new ASIODRVSTRUCT[1]`).  Same fix PortAudio's
   FindASIO applies to a generated `patched_asiolist.cpp`; done in place
   here so the build never writes into the source tree.

## Trademark

"ASIO" is a registered trademark of Steinberg Media Technologies GmbH.
Use of the name / logo is optional under GPLv3 but, where Lyra uses it,
it follows the Steinberg ASIO Usage Guidelines (SDK 2.3.4): the ASIO
Compatible logo in every dialog that enables / configures ASIO (ASIO is
opt-in in Lyra), ® on first use in product material, and the attribution
sentence above in credits and documentation.

## History

The previous copy (pre-GPL SDK v2.3, "(c) 1997-2019", no licence file)
lived under `third_party/portaudio/src/hostapi/asio/ASIOSDK/`, inherited
with PortAudio.  It was never compiled into Lyra and was removed when
this copy was added.  See `docs/architecture/asio_vac_plan.md`.
