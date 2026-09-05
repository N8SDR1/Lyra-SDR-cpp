// Lyra-cpp — cmcomm.h
//
// Ported from: openHPSDR Thetis (MI0BOT fork)
// Upstream: https://github.com/mi0bot/OpenHPSDR-Thetis
// Upstream mainline: https://github.com/ramdor/Thetis
// Source files:
//   - ChannelMaster/cmcomm.h : the ChannelMaster family's common
//     header (every ChannelMaster .c includes it)
//   - wdsp/comm.h            : `complex` typedef + PORT macro
//   - wdsp/utilities.c:37-43 : malloc0 definition (NOT a DLL
//     export — defined above the "// Exported calls" marker, so
//     Lyra-cpp must carry its own verbatim copy)
// Source version: 2.10.3.13 (MI0BOT HL2 fork)
// Original copyright: (C) 2013-2019 Warren Pratt, NR0V
// License: GNU General Public License v2 or later
// Lyra-cpp is GPL v3+; redistribution complies with GPL terms
// (preserved copyright, documented modifications, complete source
// at https://github.com/N8SDR1/Lyra-SDR-cpp).
//
// DIRECT-PORT NOTE (operator rule, 2026-06-09): this header
// carries the reference declarations VERBATIM.  The only Lyra-cpp
// packaging differences are (a) the `lyra::wire` namespace (the
// reference is global C; some namespace is unavoidable in a C++
// executable and lyra::wire is where the ChannelMaster family
// lives) and (b) PORT expands empty (the reference builds
// ChannelMaster as a DLL and PORT is __declspec(dllexport);
// Lyra-cpp links the ported family into the executable, where
// dllexport is meaningless).  Both verified non-deviations by
// scratch/_typedef_fidelity_test.cpp.
//
// This header GROWS toward the full reference cmcomm.h surface as
// the direct port proceeds (P0.d adds the _cmaster struct + pcm
// umbrella here).  Until then it carries only the common-comm
// surface the ported translation units consume.

#pragma once

// Reference ChannelMaster/cmcomm.h:27-32 include set (Windows.h /
// process.h / intrin.h / math.h / time.h / avrt.h), plus the C
// stdlib headers the wdsp/comm.h surface supplies transitively.
//
// On non-Windows hosts src/platform/win32_compat.h supplies the same
// primitives (critical sections, semaphores, MMCSS, _beginthreadex) so
// the verbatim ChannelMaster port below needs no edits.
#if defined(_WIN32)
  #define WIN32_LEAN_AND_MEAN
  #define NOMINMAX
  #include <windows.h>

  #include <process.h>
  #include <intrin.h>
  #include <avrt.h>

  #pragma comment(lib, "avrt.lib")   // AvSetMmThreadCharacteristics/Priority
#else
  #include "platform/win32_compat.h"
#endif

#include <math.h>
#include <time.h>

#include <stdlib.h>
#include <string.h>

// Reference wdsp/comm.h: `#define PORT __declspec(dllexport)`.
// Lyra-cpp builds the ported family into the executable — expands
// empty so PORT-tagged reference functions port byte-for-byte.
#ifndef PORT
#define PORT
#endif

// Reference wdsp/comm.h:133 (verbatim):
//   #define PI 3.1415926535897932
#ifndef PI
#define PI								3.1415926535897932
#endif

namespace lyra::wire {

// Reference wdsp/comm.h:
//   typedef double complex[2];
// Coexists with std::complex (different name lookup scopes) —
// proven by scratch/_typedef_fidelity_test.cpp.
typedef double complex[2];

// Reference wdsp/utilities.c:37-43 (verbatim; defined in
// cmcomm.cpp).  Pairs with `_aligned_free` at every reference
// free site (e.g. ilv.c:53-54).
void *malloc0 (int size);

// ---------------------------------------------------------------
// P0.d — opaque twin typedefs for the deferred-subsystem types the
// reference `_cmaster` struct (cmaster.h:39-99) carries as pointer
// fields.  Each is a VERBATIM copy of the reference twin-typedef
// line with the struct BODY elided (the implementations are not
// yet ported; a pointer field only needs the incomplete type).
// The reference reaches these via its cmcomm.h include umbrella:
//
//   ANB       — wdsp/nob.h:30-63          `typedef struct _anb {...} anb, *ANB;`
//               (via ChannelMaster/znob.h wrappers; wdsp.dll-side impl)
//   NOB       — wdsp/nobII.h:30-79        `typedef struct _nob {...} nob, *NOB;`
//               (via ChannelMaster/znobII.h wrappers; wdsp.dll-side impl)
//   EER       — COMPLETED at P2.a (2026-06-12): full verbatim
//               struct below (wdsp/eer.h:30-49); impl stays
//               wdsp.dll-side, reached via the wdspcalls seam
//               (create_eer/destroy_eer/xeer/pSetEER*).  HL2 has
//               no EER hardware — the reference creates it run=0
//               (cmaster.c:212-224) so the object EXISTS and the
//               `peer->run` derefs in sendOutbound (network.c:1291)
//               + sendProtocol1Samples (networkproto1.c:1222) are
//               valid (always false on HL2).
//   DELAY     — wdsp/delay.h:32-59       `typedef struct _delay {...} delay, *DELAY;`
//               (opaque; the eer struct carries DELAY pointer
//               fields; delay impl is wdsp.dll-side)
//   VOX       — ChannelMaster/vox.h:30-42 `typedef struct _vox {...} vox, *VOX;`
//               (vox.c unported — VOX is v0.2.3 scope)
//   TXGAIN    — ChannelMaster/txgain.h:30-43
//               `typedef struct _txgain {...} txgain, *TXGAIN;`
//               (txgain.c unported — Penelope gain, reference run=0 on HL2)
//   ANALYZERS — ChannelMaster/analyzers.h:30-47
//               `typedef struct _analyzers {...} analyzers, *ANALYZERS;`
//               (analyzers.c unported — Stage E.1 / PS v0.3 surface)
//
// When a subsystem ports, its full verbatim header replaces the
// opaque line here (the typedef tags match the reference exactly,
// so completing the type later is source-compatible).
typedef struct _anb anb, *ANB;
typedef struct _nob nob, *NOB;
typedef struct _delay delay, *DELAY;
typedef struct _vox vox, *VOX;
typedef struct _txgain txgain, *TXGAIN;
typedef struct _analyzers analyzers, *ANALYZERS;

// Reference wdsp/eer.h:30-49 (verbatim — completed at P2.a; was the
// opaque `typedef struct _eer eer, *EER;` line above):

typedef struct _eer
{
	int run;
	int amiq;
	int size;
	double* in;
	double* out;
	double* outM;
	int rate;
	double mgain;
	double pgain;
	int rundelays;
	double mdelay;
	double pdelay;
	DELAY mdel;
	DELAY pdel;
	CRITICAL_SECTION cs_update;
	double *legacy;																										////////////  legacy interface - remove
	double *legacyM;																									////////////  legacy interface - remove
} eer, *EER;

}  // namespace lyra::wire

// ---------------------------------------------------------------
// P0.d — reference cmcomm.h include-umbrella mapping (cmcomm.h:34-53).
// The reference cmcomm.h includes the WHOLE ChannelMaster family and
// every .c includes only cmcomm.h.  Lyra-cpp CANNOT mirror that
// umbrella directly: the ported family headers each include
// wire/cmcomm.h for the base surface above (HANDLE / PORT / complex
// / malloc0), so an include-list here would be circular under
// #pragma once whenever a family header is the first include of a
// TU.  Instead each ported .cpp includes the specific family
// headers it consumes — a PACKAGING difference only.  The mapping:
//
//   aamix.h              -> wire/AAMix.h        (ported, P0.c)
//   amix.h               -> [unported]
//   analyzers.h          -> opaque ANALYZERS above
//   bandwidth_monitor.h  -> [unported]
//   cmasio.h             -> [unported — no Lyra ASIO]
//   cmaster.h            -> wire/CMaster.h      (ported, P0.d)
//   cmbuffs.h            -> wire/CmBuffs.h      (ported, P0.d)
//   cmsetup.h            -> wire/cmsetup.h      (ported, P0.d)
//   ilv.h                -> wire/ILV.h          (ported, P0.b)
//   ivac.h               -> [unported]
//   pipe.h               -> [unported]
//   tci.h                -> [unported — Lyra TCI lives in tci_server]
//   ring.h               -> [unported]
//   router.h             -> wire/Router.h       (ported, Phase B)
//   sidetone.h           -> [unported — CW v0.2.2]
//   sync.h               -> [unported]
//   txgain.h             -> opaque TXGAIN above
//   cmUtilities.h        -> [unported]
//   vox.h                -> opaque VOX above
//   znob.h / znobII.h    -> opaque ANB / NOB above
