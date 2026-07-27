// Lyra-cpp — ObBuffs.cpp
//
// Ported from: openHPSDR Thetis (MI0BOT fork)
// Upstream: https://github.com/mi0bot/OpenHPSDR-Thetis
// Upstream mainline: https://github.com/ramdor/Thetis
// Source file: ChannelMaster/obbuffs.c (whole file, verbatim)
// Source version: 2.10.3.13 (MI0BOT HL2 fork)
// Original copyright: (C) 2014 Warren Pratt, NR0V
// License: GNU General Public License v2 or later
//
// ============================================================
// DIRECT PORT 2026-06-12 (P1)
// ============================================================
//
// Every function body is the reference obbuffs.c VERBATIM,
// including the file-scope `_obpointers obp` four-alias bank and
// the reference's own 2014-era idioms where they differ from the
// cmbuffs sibling: calloc/free (NOT malloc0/_aligned_free), obdata
// WITHOUT the MW0LGE critical-section wrap, and the csOUT enter/leave
// pair at the top of the ob_main loop.  Do NOT "harmonize" these
// toward cmbuffs.c — they are what the reference ships in this TU.
// EXCEPTION: the reference's destroy-time `obp.pcbuff[0] == NULL`
// guard is REMOVED (not kept verbatim) — see destroy_obbuffs's own
// comment.  It was harmless on the reference (which never nulls the
// slot, so the check never fires) but became live-breaking once
// Lyra's slot-nulling deviation (also documented in destroy_obbuffs)
// was added: after ring 0 is destroyed, it silently skips destroying
// every other ring (bench/review finding 2026-07-21, N8SDR).
//
// Only Lyra-cpp packaging differences (NOT code changes):
//   * `lyra::wire` namespace (reference is global C).
//   * `#include "obbuffs.h"` becomes `#include "wire/ObBuffs.h"`.
//   * Compiler accommodations on otherwise-verbatim lines, each
//     marked inline (same set as CmBuffs.cpp):
//       - `#pragma warning(suppress: 4189)` on start_obthread's
//         `handle` (kept by the reference for its commented-out
//         SetThreadPriority).
//       - `#pragma warning(suppress: 4312)` / `(suppress: 4311)`
//         + `(suppress: 4302)` on the reference's `(void *)id` /
//         `(int)pargs` thread-arg casts (int<->pointer width
//         mismatch warnings on x64; the round-trip is
//         value-preserving for any int).
//       - `(AVRT_PRIORITY) 2` cast in ob_main — C++ enum
//         strictness; the reference passes the bare literal 2 to
//         AvSetMmThreadPriority (= AVRT_PRIORITY_HIGH).  Same
//         accommodation as CmBuffs.cpp cm_main / AAMix.cpp
//         mix_main.
//
// The ob_main `sendOutbound(id, a->out)` hand-off was DEFERRED at
// P1 and RESTORED VERBATIM at P2.c (2026-06-12) — sendOutbound is
// now ported at wire/Network.cpp (network.c:1237-1341; P1 branch
// live, ETH branch deferred).  Until P3 registers OutBound and a
// caller creates the rings, this whole TU remains dormant (no
// Lyra call site for create_obbuffs yet — the reference creates
// rings 0/1 in netInterface.c:1856-1857).
//
// See NOTICE.md and CREDITS.md (repo root) for full attribution.

#include "wire/ObBuffs.h"

namespace lyra::wire {

// Reference obbuffs.c:29-35 (verbatim):

struct _obpointers
{
	OBB pcbuff[numRings];
	OBB pdbuff[numRings];
	OBB pebuff[numRings];
	OBB pfbuff[numRings];
} obp;

// Reference obbuffs.c:37-41 (verbatim):

void start_obthread (int id)
{
#pragma warning(suppress: 4312)  // verbatim text wins: reference passes int as void* thread arg
#pragma warning(suppress: 4189)  // verbatim text wins: reference keeps `handle` for the commented SetThreadPriority
	HANDLE handle = (HANDLE) _beginthread(ob_main, 0, (void *)id);
	//SetThreadPriority(handle, THREAD_PRIORITY_HIGHEST);
}

// Reference obbuffs.c:43-66 (verbatim):

void create_obbuffs (int id, int accept, int max_insize, int outsize)
{
	OBB a = (OBB) calloc (1, sizeof(obb));
	obp.pcbuff[id] = obp.pdbuff[id] = obp.pebuff[id] = obp.pfbuff[id] = a;
	a->id = id;
	a->accept = accept;
	a->run = 1;
	a->max_in_size = max_insize;
	a->r1_outsize = outsize;
	if (a->r1_outsize > a->max_in_size)
		a->r1_size = a->r1_outsize;
	else
		a->r1_size = a->max_in_size;
	a->r1_active_buffsize = OBB_MULT * a->r1_size;
	a->r1_baseptr = (double*) calloc (a->r1_active_buffsize, sizeof (complex));
	a->r1_inidx = 0;
	a->r1_outidx = 0;
	a->r1_unqueuedsamps = 0;
	a->Sem_BuffReady = CreateSemaphore(0, 0, 1000, 0);
	InitializeCriticalSectionAndSpinCount ( &a->csIN, 2500 );
	InitializeCriticalSectionAndSpinCount ( &a->csOUT,  2500 );
	a->out = (double *) calloc (obMAXSIZE, sizeof (complex));
	start_obthread (id);
}

// Reference obbuffs.c:68-86 (verbatim):

void destroy_obbuffs (int id)
{
	OBB a = obp.pcbuff[id];
	// Lyra deviation (documented): the reference's [0]-only guard is
	// incompatible with the slot-nulling below — after ring 0 is
	// destroyed it early-returns on every subsequent id, leaking that
	// ring's semaphore, critical sections and thread.  The per-id
	// check above is the correct gate.  Do NOT restore the [0] check.
	if (a == NULL) return;
	InterlockedBitTestAndReset(&a->accept, 0);
	EnterCriticalSection (&a->csIN);
	EnterCriticalSection (&a->csOUT);
	Sleep (25);
	InterlockedBitTestAndReset(&a->run, 0);
	ReleaseSemaphore(a->Sem_BuffReady, 1, 0);
	LeaveCriticalSection (&a->csOUT);
	Sleep (2);
	DeleteCriticalSection (&a->csOUT);
	DeleteCriticalSection (&a->csIN);
	CloseHandle (a->Sem_BuffReady);
	free (a->out);
	free (a->r1_baseptr);
	free (a);
	// Lyra deviation (documented): null the slot aliases after free.
	// The reference never destroys rings mid-process so its OutBound
	// can't see a stale pointer — but Lyra destroys them at
	// HL2Stream::close(), and the Protocol 2 RX path then drives
	// dispatchAudioFrame → OutBound with NO P1 session: without this,
	// OutBound's null-guard passed a DANGLING pointer to freed memory
	// (use-after-free on every P2 audio frame after an HL2 open→close
	// in the same app run — bench 2026-07-19).
	obp.pcbuff[id] = obp.pdbuff[id] = obp.pebuff[id] = obp.pfbuff[id] = NULL;
}

// Reference obbuffs.c:88-96 (verbatim):

void flush_obbuffs (int id)
{
	OBB a = obp.pfbuff[id];
	memset (a->r1_baseptr, 0, a->r1_active_buffsize * sizeof (complex));
	a->r1_inidx = 0;
	a->r1_outidx = 0;
	a->r1_unqueuedsamps = 0;
	while (!WaitForSingleObject (a->Sem_BuffReady, 1)) ;
}

// Reference obbuffs.c:98-130 (verbatim):

PORT
void OutBound (int id, int nsamples, double* in)
{
	int n;
	int first, second;
	OBB a = obp.pebuff[id];
	// Lyra deviation (documented): null-guard on an uncreated ring.
	// The reference creates rings 0/1 unconditionally at boot
	// (netInterface.c:1856-1857) so this deref can't fault there —
	// but Lyra creates them at P1 stream open, and the Protocol 2
	// RX path (P2RxBridge → feedIq → dispatchAudioFrame) produces
	// audio frames with NO P1 session, which AV'd here on the first
	// P2 open (bench 2026-07-18, WER fault lyra.exe+0x6c590).  With
	// no ring there is no EP2 wire to feed — dropping is correct.
	if (a == NULL)
		return;
	if (_InterlockedAnd (&a->accept, 1))
	{
		EnterCriticalSection (&a->csIN);
		if (nsamples > (a->r1_active_buffsize - a->r1_inidx))
		{
			first = a->r1_active_buffsize - a->r1_inidx;
			second = nsamples - first;
		}
		else
		{
			first = nsamples;
			second = 0;
		}
		memcpy (a->r1_baseptr + 2 * a->r1_inidx, in,             first  * sizeof (complex));
		memcpy (a->r1_baseptr,                   in + 2 * first, second * sizeof (complex));

		if ((a->r1_unqueuedsamps += nsamples) >= a->r1_outsize)
		{
			n = a->r1_unqueuedsamps / a->r1_outsize;
			ReleaseSemaphore (a->Sem_BuffReady, n, 0);
			a->r1_unqueuedsamps -= n * a->r1_outsize;
		}
		if ((a->r1_inidx += nsamples) >= a->r1_active_buffsize)
			a->r1_inidx -= a->r1_active_buffsize;
		LeaveCriticalSection (&a->csIN);
	}
}

// Reference obbuffs.c:132-151 (verbatim):

void obdata (int id, double* out)
{
	int first, second;
	OBB a = obp.pdbuff[id];
	if (!_InterlockedAnd(&a->run, 1)) _endthread();
	if (a->r1_outsize > (a->r1_active_buffsize - a->r1_outidx))
	{
		first = a->r1_active_buffsize - a->r1_outidx;
		second = a->r1_outsize - first;
	}
	else
	{
		first = a->r1_outsize;
		second = 0;
	}
	memcpy (out,             a->r1_baseptr + 2 * a->r1_outidx, first  * sizeof (complex));
	memcpy (out + 2 * first, a->r1_baseptr,                    second * sizeof (complex));
	if ((a->r1_outidx += a->r1_outsize) >= a->r1_active_buffsize)
		a->r1_outidx -= a->r1_active_buffsize;
}

// Reference obbuffs.c:153-173 (verbatim except the DEFERRED
// sendOutbound hand-off, marked inline):

void ob_main (void *pargs)
{
	DWORD taskIndex = 0;
	HANDLE hTask = AvSetMmThreadCharacteristics(TEXT("Pro Audio"), &taskIndex);
	if (hTask != 0) AvSetMmThreadPriority(hTask, (AVRT_PRIORITY) 2);  // verbatim `2`; cast = C++ enum strictness (CmBuffs.cpp precedent)
	else SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);

#pragma warning(suppress: 4311)  // verbatim text wins: reference truncates the void* thread arg back to int
#pragma warning(suppress: 4302)
	int id = (int)pargs;
	OBB a = obp.pdbuff[id];

	while (_InterlockedAnd (&a->run, 1))
	{
		WaitForSingleObject(a->Sem_BuffReady,INFINITE);
		EnterCriticalSection (&a->csOUT);
		LeaveCriticalSection (&a->csOUT);
		obdata (id, a->out);
		sendOutbound(id, a->out);
		// if (id == 0) WriteAudio(15.0, 48000, 126, a->out, 3);
	}
	_endthread();
}

// Reference obbuffs.c:175-192 (verbatim):

void SetOBRingOutsize (int id, int size)
{
	OBB a = obp.pcbuff[id];
	InterlockedBitTestAndReset(&a->accept, 0);
	EnterCriticalSection (&a->csIN);
	EnterCriticalSection (&a->csOUT);
	Sleep (25);
	InterlockedBitTestAndReset(&a->run, 0);
	ReleaseSemaphore(a->Sem_BuffReady, 1, 0);
	LeaveCriticalSection (&a->csOUT);
	Sleep (2);
	flush_obbuffs(id);
	a->r1_outsize = size;
	InterlockedBitTestAndSet(&a->run,0);
	start_obthread(id);
	LeaveCriticalSection (&a->csIN);
	InterlockedBitTestAndSet(&a->accept, 0);
}

}  // namespace lyra::wire
