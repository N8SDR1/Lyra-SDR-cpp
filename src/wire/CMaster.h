// Lyra-cpp — CMaster.h
//
// Ported from: openHPSDR Thetis (MI0BOT fork)
// Upstream: https://github.com/mi0bot/OpenHPSDR-Thetis
// Upstream mainline: https://github.com/ramdor/Thetis
// Source file: ChannelMaster/cmaster.h (whole file, verbatim)
// Source version: 2.10.3.13 (MI0BOT HL2 fork)
// Original copyright: (C) 2014-2019 Warren Pratt, NR0V
// License: GNU General Public License v2 or later
// Lyra-cpp is GPL v3+; redistribution complies with GPL terms
// (preserved copyright, documented modifications, complete source
// at https://github.com/N8SDR1/Lyra-SDR-cpp).
//
// ============================================================
// DIRECT PORT 2026-06-12 (P0.d — supersedes the 2026-06-09
// retrofit and its operator-rejected deviations)
// ============================================================
//
// This is the reference cmaster.h VERBATIM: the FULL `_cmaster`
// struct (cmaster.h:39-99) with the `cmaster, *CMASTER` twin
// typedef — radio-structure ints, the xcmaster surface, the four
// raw callback function pointers, the nested `_rcvr` / `_xmtr`
// substructs with every field including the PureSignal-relevant
// surface (out[3] / panalalloc / pgain / peer) — plus the
// reference declaration set and the AudioCODEC enum.  The
// retrofit's deviations are gone: no `CMasterState`/`CMasterXmtr`
// PascalCase tags, no std::function callback fields, no
// `enum class AudioCodecId`, no cmMAXstream=5/cmMAXxmtr=1
// constant overrides, no `pTxChannel` carve-out (P0.d's verbatim
// create_xmtr opens the WDSP TXA channel itself through the
// operator-approved wire/wdspcalls.h seam, which removes the
// runtime-DLL justification the carve-out rested on).
//
// Only Lyra-cpp packaging differences (NOT code changes):
//   * `lyra::wire` namespace (reference is global C).
//   * The reference include set maps per the cmcomm.h umbrella
//     note: cmbuffs.h/cmsetup.h/ilv.h/aamix.h -> the ported
//     wire/ headers; pipe.h/sync.h are unported (their types are
//     not referenced by this header); txgain.h/vox.h/analyzers.h
//     (TXGAIN/VOX/ANALYZERS) and the wdsp-side ANB/NOB/EER arrive
//     as the opaque verbatim twin typedefs in wire/cmcomm.h.
//   * The reference's literal `__declspec (dllexport)` is carried
//     as PORT (the documented cmcomm.h mapping).
//   * The include-guard idiom (`#ifndef _cmaster_h`) becomes
//     `#pragma once` (repo convention).
//
// Registration-ordering contract (reference posture, no null
// guards anywhere): callers register the Sendp* outbound pointers
// AFTER create_xmtr has populated pcm->xmtr[0].pilv / after the
// id-0 mixer exists — exactly as the reference netInterface
// orders it.
//
// See NOTICE.md and CREDITS.md (repo root) for full upstream
// attribution.

#pragma once

#include "wire/cmcomm.h"    // base surface + opaque ANB/NOB/EER/VOX/TXGAIN/ANALYZERS
#include "wire/CmBuffs.h"   // reference cmaster.h:29 `#include "cmbuffs.h"` — CMB
#include "wire/cmsetup.h"   // reference cmaster.h:30 `#include "cmsetup.h"` — cmMAX* sizing
#include "wire/ILV.h"       // reference cmaster.h:31 `#include "ilv.h"` — ILV
#include "wire/AAMix.h"     // reference cmaster.h:36 `#include "aamix.h"` — AAMIX

namespace lyra::wire {

// Reference cmaster.h:39-99 (verbatim):

typedef struct _cmaster
{
	// radio structure
	int cmSTREAM;													// total number of input streams to set up
	int cmRCVR;														// number of receivers to set up
	int cmXMTR;														// number of transmitters to set up
	int cmSubRCVR;													// number of sub-receivers per receiver to set up
	int cmNspc;														// number of TYPES of special units
	int cmSPC[cmMAXspc];											// number of special units of each type
	int cmMAXInbound[cmMAXstream];									// maximum number of samples in a call to Inbound()
	int cmMAXInRate;												// maximum sample rate of an input stream
	int cmMAXAudioRate;												// maximum channel audio output rate (incl. rcvr and tx monitor audio)
	int cmMAXTxOutRate;												// maximum transmitter channel output sample rate

	// xcmaster
	double *in[cmMAXstream];										// xcmaster() input buffer
	int xcm_inrate[cmMAXstream];									// sample rate of xcmaster() input stream
	int xcm_insize[cmMAXstream];									// samples per xcmaster() input buffer
	int audio_outrate;												// sample rate of audio output stream
	int audio_outsize;												// samples per audio output buffer
	CMB pcbuff[cmMAXstream];										// pointers to input ring buffer structs
	CMB pdbuff[cmMAXstream];
	CMB pebuff[cmMAXstream];
	CMB pfbuff[cmMAXstream];
	CRITICAL_SECTION update[cmMAXstream];
	int aamix_inrates[cmMAXrcvr * cmMAXSubRcvr + cmMAXxmtr];
	void (*OutboundRx)(int id, int nsamples, double* buff);			// pointer to Outbound function called by aamix with rx audio from the global mixer
	void (*OutboundTx)(int id, int nsamples, double* buff);			// pointer to Outbound function called by ilv with xmtr samples from the interleaver
	void (*OutboundTCIRxIQ)(int id, int nsamples, double* buff);	// pointer to callback with receiver IQ samples
	void (*InboundTCITxAudio)(int nsamples, double* buff);			// pointer to callback to fill TX audio input
	// #158 Stage 4 — VAC-in (PC soundcard / USB mic / digital app) TX audio.
	// Lyra-native, modeled on the TCI quartet above + the deferred asioIN
	// seam (the reference's PC-mic input lived in cmasio.c, unported).
	void (*InboundVacTxAudio)(int nsamples, double* buff);			// callback to fill TX audio input from VAC-in (xvacIN)
	// #50 native parametric-EQ rack stage — Lyra-native, NOT a reference
	// element.  Called per TX block on pcm->in[stream] (mic {I,Q} doubles)
	// just before fexchange0, so the EQ shapes the mic before WDSP TXA.
	void (*TxSpeechProcess)(int nsamples, double* buff);			// #88 in-place speech rack (Auto-AGC + De-esser), runs BEFORE the EQ
	void (*TxEqProcess)(int nsamples, double* buff);				// in-place mic EQ filter (no-op when unset / EQ bypassed)
	void (*TxCombinatorProcess)(int nsamples, double* buff);		// #51 in-place 5-band combinator, runs AFTER the EQ (no-op when unset / bypassed)
	void (*TxPlateProcess)(int nsamples, double* buff);				// #52 in-place plate reverb, runs AFTER the combinator (no-op when unset / bypassed)
	void (*TxBurstProcess)(int nsamples, double* buff);				// FM 1750 Hz access burst — mix after the rack, before the monitor tap / fexchange0
	void (*TxMonitorTap)(int nsamples, double* buff);				// #90 READ-ONLY TX-monitor tap on the post-rack mic (== fexchange0 input), captured for the operator monitor; MUST NOT write buff
	volatile long tx_rack_bypass;									// #50 — when set, skip the ENTIRE native rack (EQ + Speech + …); driven by mode (DIGU/DIGL → bypass so digital audio is never voice-shaped)
	volatile long tci_run;											// run TCI RX IQ/audio callbacks
	int	audioCodecId;
	ANALYZERS panalalloc;											// pointer to additional analyzer data structure

	// receivers
	struct _rcvr
	{
		int ch_outrate;												// rate at rcvr channel output = rcvr input to aamix
		int ch_outsize;												// size at rcvr channel output = rcvr input to aamix
		double* audio[cmMAXSubRcvr];								// audio buff, per subrx
		volatile long run_pan;										// run panadapter
		ANB panb;													// noiseblanker, per receiver
		NOB pnob;													// noiseblanker II, per receiver
	} rcvr[cmMAXrcvr];

	// transmitters
	struct _xmtr
	{
		int ch_outrate;												// xmtr output rate = tx channel output rate
		int ch_outsize;												// xmtr output size = tx channel output size
		double* out[3];												// output buff, per transmitter
		VOX pvox;													// vox, per transmitter
		void (__stdcall *pushvox)(int channel, int active);			// vox, per transmitter
		TXGAIN pgain;												// gain block, for Penelope power control & amp protect
		EER peer;													// eer block
		ILV pilv;													// interleave for EER
		AAMIX pavoxmix;												// anti-vox mixer
		volatile long use_tci_audio;								// use TCI TX audio instead of other TX sources
		volatile long use_vac_audio;								// #158 Stage 4 — use VAC-in TX audio (PC mic/digital) instead of other sources
	} xmtr[cmMAXxmtr];

} cmaster, *CMASTER;

// Reference cmaster.h:101-118 (verbatim):

extern CMASTER pcm;

extern PORT void xcmaster (int id);

extern void create_cmaster();

extern void destroy_cmaster();

extern PORT void SendpOutboundRx (void (*Outbound)(int id, int nsamples, double* buff));
extern PORT void SendpOutboundTx (void (*Outbound)(int id, int nsamples, double* buff));
extern PORT void SendpOutboundTCIRxIQ (void (*Outbound)(int id, int nsamples, double* buff));
extern PORT void SendpInboundTCITxAudio (void (*Inbound)(int nsamples, double* buff));
extern PORT void SetTCIRun (int active);
extern PORT void SetTXTCIAudio (int txid, int active);
// #158 Stage 4 — VAC-in TX-audio source (Lyra-native, mirrors the TCI pair).
extern PORT void SendpInboundVacTxAudio (void (*Inbound)(int nsamples, double* buff));
extern PORT void SetTXVacAudio (int txid, int active);
// #50 native parametric-EQ rack stage — register the in-place mic-EQ
// processor (called per TX block before fexchange0).  Pass nullptr to clear.
extern PORT void SendpTxEqProcessor (void (*Process)(int nsamples, double* buff));
// #88 — register the native TX speech pre-processor (Auto-AGC + De-esser),
// run BEFORE the EQ on the mic buffer.
extern PORT void SendpTxSpeechProcessor (void (*Process)(int nsamples, double* buff));
// #51 — register the native 5-band Combinator, run AFTER the EQ on the mic
// buffer (chain: Speech -> EQ -> Combinator).
extern PORT void SendpTxCombinatorProcessor (void (*Process)(int nsamples, double* buff));
// #52 — register the native Plate reverb, run AFTER the combinator on the
// mic buffer (chain: Speech -> EQ -> Combinator -> Plate).
extern PORT void SendpTxPlateProcessor (void (*Process)(int nsamples, double* buff));
extern PORT void SendpTxBurstProcessor (void (*Process)(int nsamples, double* buff));
// #90 — register the READ-ONLY TX-monitor tap, called per TX block on the
// post-rack mic (== the fexchange0 input) just before the modulator.  The cb
// only READS pcm->in (copies the mono I-lane into the monitor ring); it must
// never write the buffer.  Pass nullptr to clear.
extern PORT void SendpTxMonitorTap (void (*Tap)(int nsamples, double* buff));
// #50 — bypass the whole native TX rack (EQ + future Speech/Combinator/Plate).
// Set 1 for digital modes (DIGU/DIGL) so the mic DSP never touches FT8/JS8/etc.
extern PORT void SetTxRackBypass (int bypass);

// Reference cmaster.h:120-126 (verbatim):

enum AudioCODEC
{
	HERMES = 0,														// audio codec chip in radio hardware unit
	ASIO   = 1,														// asio sound device on host
	WASAPI = 2														// wasapi sound device on host (to be implemented)
};

}  // namespace lyra::wire
