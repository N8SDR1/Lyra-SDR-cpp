// Lyra-cpp — Network.cpp
//
// Ported from: openHPSDR Thetis (MI0BOT fork)
// Upstream: https://github.com/mi0bot/OpenHPSDR-Thetis
// Upstream mainline: https://github.com/ramdor/Thetis
// Source file: ChannelMaster/network.c (PARTIAL — sendOutbound only,
//   network.c:1237-1341; the rest of network.c remains across the
//   Step-14 TUs pending the P4 reconciliation)
// Source version: 2.10.3.13 (MI0BOT HL2 fork)
// Original copyright: (C) 2015-2016 Doug Wigley (W5WC)
// License: GNU General Public License v2 or later
//
// ============================================================
// DIRECT PORT 2026-06-12 (P2.c)
// ============================================================
//
// `sendOutbound` is the consumer-side hand-off the obbuffs ob_main
// pump calls (wire/ObBuffs.cpp): it moves each drained ring buffer
// into the protocol writer's hand-off surface.  PROTOCOL_1 branch
// (network.c:1285-1340) is LIVE — the outLRbufp/outIQbufp memcpy +
// the hsendLRSem/hsendIQSem release + hobbuffsRun wait handshake
// that sendProtocol1Samples / WriteMainLoop_HL2 consume.  The ETH
// branch (network.c:1246-1283, Protocol 2) is carried as DEFERRED
// reference text — Protocol 2 / ANAN is v0.4 scope and its
// WriteUDPFrame/udpOUT surface is unported.
//
// Only Lyra-cpp packaging differences (NOT code changes):
//   * `lyra::wire` namespace (reference is global C).
//   * `#include "network.h"` becomes the explicit wire/ includes.
//   * `if (RadioProtocol == ETH)` becomes
//     `if (radioProtocol == RadioProtocol::ETH)` — the documented
//     RadioNet.h enum-class accommodation (the reference's C
//     `typedef enum {USB, ETH} RadioProtocol;` + shadow variable
//     `RadioProtocol RadioProtocol;` maps to the C++ enum class +
//     lower-case `radioProtocol` runtime variable; see
//     RadioNet.h:156).
//   * `#pragma warning(suppress: 4101)` on the `i`/`temp`/`itemp`
//     locals — used only by the DEFERRED ETH branch; verbatim text
//     wins (same accommodation class as CMaster.cpp).
//
// DORMANCY: nothing calls ob_main's pump until P3 registers
// OutBound + a caller creates the obbuffs rings; the semaphore
// quartet this branch releases/waits is created by P4's
// StartAudio-equivalent (netInterface.c:68-71) BEFORE the pump can
// deliver data — the same ordering the reference relies on.
//
// See NOTICE.md and CREDITS.md (repo root) for full attribution.

#include "wire/ObBuffs.h"    // sendOutbound decl (reference declares it in obbuffs.h)
#include "wire/RadioNet.h"   // prn + radioProtocol (reference: network.h)
#include "wire/CMaster.h"    // pcm (reference: via cmcomm.h)

namespace lyra::wire {

// Lyra deviation: the reference waits INFINITE here unconditionally.
// Skipped once HL2Stream::close() has set prn->obStopWait (see
// RadioNet.h) so the ob_main pumps can reach destroy_obbuffs' trap.
static void waitObbuffsRun(int i)
{
	if (!_InterlockedAnd(&prn->obStopWait, 1))
		WaitForSingleObject(prn->hobbuffsRun[i], INFINITE);
}

// Reference network.c:1237-1341 (verbatim; the ETH branch carried
// as DEFERRED reference text, marked inline):

void sendOutbound(int id, double* out)
{
#pragma warning(suppress: 4101)  // verbatim text wins: i/temp/itemp are used only by the DEFERRED ETH branch
	int i;
#pragma warning(suppress: 4101)
	short temp;
#pragma warning(suppress: 4101)
	int itemp;

	//// convert from complex to byte
	//// big-endian

	if (radioProtocol == RadioProtocol::ETH)  // reference: `if (RadioProtocol == ETH)` — documented enum-class accommodation
	{
		// DEFERRED [Protocol 2 / ANAN — v0.4 scope] — reference
		// network.c:1248-1283 (WriteUDPFrame / udpOUT / spp surface
		// unported):
		//   EnterCriticalSection(&prn->udpOUT);
		//   if (id == 1)
		//   {
		//   	// WriteAudio (15.0, 192000, prn->tx[0].spp, out, 3);
		//   	for (i = 0; i < 2 * prn->tx[0].spp; i++)
		//   	{
		//   		itemp = out[i] >= 0.0 ? (int)floor(out[i] * 8388607.0 + 0.5) :
		//   			(int)ceil(out[i] * 8388607.0 - 0.5);
		//   		prn->OutBufp[i * 3] = (char)((itemp >> 16) & 0xff);
		//   		prn->OutBufp[i * 3 + 1] = (char)((itemp >> 8) & 0xff);
		//   		prn->OutBufp[i * 3 + 2] = (char)(itemp & 0xff);
		//   	}
		//   	WriteUDPFrame(id, prn->OutBufp, prn->tx[0].spp * 6);
		//   }
		//   else
		//   {
		//   	if (prn->lr_audio_swap)
		//   	{
		//   		double swap;
		//   		for (i = 0; i < 2 * prn->audio[0].spp; i += 2)
		//   		{
		//   			swap       = out[i + 0];
		//   			out[i + 0] = out[i + 1];
		//   			out[i + 1] = swap;
		//   		}
		//   	}
		//   	for (i = 0; i < 2 * prn->audio[0].spp; i++)
		//   	{
		//   		temp = out[i] >= 0.0 ? (short)floor(out[i] * 32767.0 + 0.5) :
		//   			(short)ceil(out[i] * 32767.0 - 0.5);
		//   		prn->OutBufp[i * 2] = (char)((temp >> 8) & 0xff);
		//   		prn->OutBufp[i * 2 + 1] = (char)(temp & 0xff);
		//   	}
		//   	WriteUDPFrame(id, prn->OutBufp, prn->audio[0].spp * 4);
		//   }
		//   LeaveCriticalSection(&prn->udpOUT);
	}
	else	//  PROTOCOL_1
	{
		// note:  packet won't be sent until BOTH 'hsendIQSem' and 'hsendLRSem' are released
		// note:  'hobbuffsRun[0]' and 'hobbuffsRun[1]' are released AFTER we write the packet and can therefore
		//        refill 'the buffers'outIQbufp' and 'outLRbufp'
		// note:  there are 63 I-Q samples and 63 L-R samples per P1 frame => 126 of each per packet
		if (pcm->xmtr[0].peer->run)	// if eer/etr is running
		{
			if (id == 1)	// TX I-Q data arriving
			{
				static int ptr = 0;
				memcpy((void *)(prn->outIQbufp + 720), out, sizeof(complex) * 126);
				// de-interleave the two sample streams
#pragma warning(suppress: 4456)  // verbatim text wins: the reference's loop-scope i shadows its function-scope i (network.c:1239 vs :1298)
				for (int n = 0, i = ptr, j = 256 + ptr, k = 720; n < 63; n++, i+=2, j+=2, k+=4)
				{
					// final location of I-Q data
					prn->outIQbufp[i + 0] = prn->outIQbufp[k + 0];
					prn->outIQbufp[i + 1] = prn->outIQbufp[k + 1];
					// L-R data will be copied from here if needed
					prn->outIQbufp[j + 0] = prn->outIQbufp[k + 2];
					prn->outIQbufp[j + 1] = prn->outIQbufp[k + 3];
				}
				ptr = (ptr + 126) % 252;
				if (ptr == 0)
				{
					// release semaphore, indicating IQ data received and complete.
					ReleaseSemaphore(prn->hsendIQSem, 1, 0);
					// wait to refill 'outIQbuffp' until the packet is sent
					waitObbuffsRun(0);
				}
			}
			else			// RX L-R data arriving
			{
				memcpy(prn->outLRbufp, out, sizeof(complex) * 126);
				// release semaphore indicating L-R data ready
				ReleaseSemaphore(prn->hsendLRSem, 1, 0);
				// wait to refill 'outLRbufp' until the packet is sent
				waitObbuffsRun(1);
			}
		}
		else					// eer/etr is NOT running
		{
			if (id == 1)
			{
				memcpy(prn->outIQbufp, out, sizeof(complex) * 126);
				ReleaseSemaphore(prn->hsendIQSem, 1, 0);
				waitObbuffsRun(0);
			}
			else
			{
				memcpy(prn->outLRbufp, out, sizeof(complex) * 126);
				ReleaseSemaphore(prn->hsendLRSem, 1, 0);
				waitObbuffsRun(1);
			}
		}
	}
}

}  // namespace lyra::wire
