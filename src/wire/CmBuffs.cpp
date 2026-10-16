// Lyra-cpp — CmBuffs.cpp
//
// Ported from: openHPSDR Thetis (MI0BOT fork)
// Upstream: https://github.com/mi0bot/OpenHPSDR-Thetis
// Upstream mainline: https://github.com/ramdor/Thetis
// Source file: ChannelMaster/cmbuffs.c (whole file, verbatim)
// Source version: 2.10.3.13 (MI0BOT HL2 fork)
// Original copyright: (C) 2014 Warren Pratt, NR0V
// License: GNU General Public License v2 or later
//
// ============================================================
// DIRECT PORT 2026-06-12 (P0.d — supersedes the 2026-06-09
// retrofit and its operator-rejected deviations)
// ============================================================
//
// Every function body is the reference cmbuffs.c VERBATIM.  The
// retrofit's deviations are gone: no calloc/free substitution for
// malloc0/_aligned_free, no intptr_t round-trip casts, no
// destroy-time null guard, no Lyra-side co-allocation of
// pcm->in[id] (the reference allocates/frees that buffer in
// create_cmaster/destroy_cmaster — cmaster.c:285/:335 — and the
// P0.d CMaster.cpp port now does the same), no post-teardown
// clearing of the four pcm bank aliases.
//
// Only Lyra-cpp packaging differences (NOT code changes):
//   * `lyra::wire` namespace (reference is global C).
//   * `#include "cmcomm.h"` becomes the explicit family includes
//     (wire/cmcomm.h base surface arrives via wire/CmBuffs.h;
//     wire/CMaster.h supplies pcm + xcmaster — see the cmcomm.h
//     umbrella-mapping note for why the include set is explicit).
//   * Three compiler accommodations on otherwise-verbatim lines,
//     each marked inline:
//       - `#pragma warning(suppress: 4189)` on start_cmthread's
//         `handle` (kept by the reference for its commented-out
//         SetThreadPriority; same accommodation as AAMix.cpp).
//       - `#pragma warning(suppress: 4312)` / `(suppress: 4311)`
//         on the reference's `(void *)id` / `(int)pargs` thread-arg
//         casts (int<->pointer width mismatch warnings on x64; the
//         round-trip is value-preserving for any int).
//       - `(AVRT_PRIORITY) 2` cast in cm_main — C++ enum
//         strictness; the reference passes the bare literal 2 to
//         AvSetMmThreadPriority (= AVRT_PRIORITY_HIGH).  Same
//         accommodation as AAMix.cpp mix_main.
//
// See NOTICE.md and CREDITS.md (repo root) for full attribution.

#include "wire/CmBuffs.h"
#include "wire/CMaster.h"   // pcm + xcmaster (reference reaches both via cmcomm.h)

namespace lyra::wire {

// Reference cmbuffs.c:29-33 (verbatim):

void start_cmthread (int id)
{
#pragma warning(suppress: 4312)  // verbatim text wins: reference passes int as void* thread arg
#pragma warning(suppress: 4189)  // verbatim text wins: reference keeps `handle` for the commented SetThreadPriority
	// Width-safe int<->pointer round trip via intptr_t.  MSVC only
	// warns here (suppressed above); clang rejects the direct cast
	// outright on LP64, so the intermediate is required.
	HANDLE handle = (HANDLE) _beginthread(cm_main, 0, (void *)(intptr_t)id);
	//SetThreadPriority(handle, THREAD_PRIORITY_HIGHEST);
}

// Reference cmbuffs.c:35-58 (verbatim):

void create_cmbuffs (int id, int accept, int max_insize, int max_outsize, int outsize)
{
	CMB a = (CMB) malloc0 (sizeof(cmb));
	pcm->pcbuff[id] = pcm->pdbuff[id] = pcm->pebuff[id] = pcm->pfbuff[id] = a;
	a->id = id;
	a->accept = accept;
	a->run = 1;
	a->max_in_size = max_insize;
	a->max_outsize = max_outsize;
	a->r1_outsize = outsize;
	if (a->max_outsize > a->max_in_size)
		a->r1_size = a->max_outsize;
	else
		a->r1_size = a->max_in_size;
	a->r1_active_buffsize = CMB_MULT * a->r1_size;
	a->r1_baseptr = (double*) malloc0 (a->r1_active_buffsize * sizeof (complex));
	a->r1_inidx = 0;
	a->r1_outidx = 0;
	a->r1_unqueuedsamps = 0;
	a->Sem_BuffReady = CreateSemaphore(0, 0, 1000, 0);
	InitializeCriticalSectionAndSpinCount ( &a->csIN, 2500 );
	InitializeCriticalSectionAndSpinCount ( &a->csOUT,  2500 );
	start_cmthread (id);
}

// Reference cmbuffs.c:60-76 (verbatim):

void destroy_cmbuffs (int id)
{
	CMB a = pcm->pcbuff[id];
	InterlockedBitTestAndReset(&a->accept, 0);		// shut the Inbound() gate to prevent new infusions
	EnterCriticalSection (&a->csIN);				// wait until the current Inbound() infusion is finished
	EnterCriticalSection (&a->csOUT);				// block the CM thread before cmdata()
	Sleep (25);										// wait for the thread to arrive at the top of the cm_main() loop
	InterlockedBitTestAndReset(&a->run, 0);			// set a trap for the CM thread
	ReleaseSemaphore(a->Sem_BuffReady, 1, 0);		// be sure the CM thread can pass WaitForSingleObject in cm_main()									//
	LeaveCriticalSection (&a->csOUT);				// let the thread pass to the trap in cmdata()
	Sleep (2);										// wait for the CM thread to die
	DeleteCriticalSection (&a->csOUT);
	DeleteCriticalSection (&a->csIN);
	CloseHandle (a->Sem_BuffReady);
	_aligned_free (a->r1_baseptr);
	_aligned_free (a);
}

// Reference cmbuffs.c:78-86 (verbatim):

void flush_cmbuffs (int id)
{
	CMB a = pcm->pfbuff[id];
	memset (a->r1_baseptr, 0, a->r1_active_buffsize * sizeof (complex));
	a->r1_inidx = 0;
	a->r1_outidx = 0;
	a->r1_unqueuedsamps = 0;
	while (!WaitForSingleObject (a->Sem_BuffReady, 1)) ;
}

// Reference cmbuffs.c:88-121 (verbatim):

PORT
void Inbound (int id, int nsamples, double* in)
{
	int n;
	int first, second;
	CMB a = pcm->pebuff[id];

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

// Reference cmbuffs.c:123-149 (verbatim):

void cmdata (int id, double* out)
{
	int first, second;
	CMB a = pcm->pdbuff[id];
	EnterCriticalSection (&a->csOUT);
	if (!_InterlockedAnd (&a->run, 1))
	{
		LeaveCriticalSection (&a->csOUT);
		_endthread();
		return; //MW0LGE_21k5
	}
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
	LeaveCriticalSection (&a->csOUT);
}

// Reference cmbuffs.c:151-168 (verbatim):

void cm_main (void *pargs)
{
	DWORD taskIndex = 0;
	HANDLE hTask = AvSetMmThreadCharacteristics(TEXT("Pro Audio"), &taskIndex);
	if (hTask != 0) AvSetMmThreadPriority(hTask, (AVRT_PRIORITY) 2);  // verbatim `2`; cast = C++ enum strictness (AAMix.cpp precedent)
	else SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);

#pragma warning(suppress: 4311)  // verbatim text wins: reference truncates the void* thread arg back to int
#pragma warning(suppress: 4302)
	int id = (int)(intptr_t)pargs;   // see start_cmthread: width-safe round trip
	CMB a = pcm->pdbuff[id];

	while (_InterlockedAnd (&a->run, 1))
	{
		WaitForSingleObject(a->Sem_BuffReady,INFINITE);
		cmdata (id, pcm->in[id]);
		xcmaster(id);
	}
	_endthread();
}

// Reference cmbuffs.c:170-187 (verbatim):

void SetCMRingOutsize (int id, int size)
{
	CMB a = pcm->pcbuff[id];
	InterlockedBitTestAndReset(&a->accept, 0);		// shut the Inbound() gate to prevent new infusions
	EnterCriticalSection (&a->csIN);				// wait until the current Inbound() infusion is finished
	EnterCriticalSection (&a->csOUT);				// block the CM thread before cmdata()
	Sleep (25);										// wait for the thread to arrive at the top of the cm_main() loop
	InterlockedBitTestAndReset(&a->run, 0);			// set a trap for the CM thread
	ReleaseSemaphore(a->Sem_BuffReady, 1, 0);		// be sure the CM thread can pass WaitForSingleObject in cm_main()									//
	LeaveCriticalSection (&a->csOUT);				// let the thread pass to the trap in cmdata()
	Sleep (2);										// wait for the CM thread to die
	flush_cmbuffs(id);								// restore ring to pristine condition
	a->r1_outsize = size;							// set its new outsize
	InterlockedBitTestAndSet(&a->run,0);			// remove the CM thread trap
	start_cmthread(id);								// start the CM thread
	LeaveCriticalSection (&a->csIN);				// enable Inbound() processing
	InterlockedBitTestAndSet(&a->accept, 0);		// open the Inbound() gate
}

}  // namespace lyra::wire
