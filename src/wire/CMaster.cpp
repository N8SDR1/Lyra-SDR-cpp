// Lyra-cpp — CMaster.cpp
//
// Ported from: openHPSDR Thetis (MI0BOT fork)
// Upstream: https://github.com/mi0bot/OpenHPSDR-Thetis
// Upstream mainline: https://github.com/ramdor/Thetis
// Source file: ChannelMaster/cmaster.c (whole file, verbatim)
// Source version: 2.10.3.13 (MI0BOT HL2 fork)
// Original copyright: (C) 2014-2019 Warren Pratt, NR0V
// License: GNU General Public License v2 or later
//
// ============================================================
// DIRECT PORT 2026-06-12 (P0.d — supersedes the 2026-06-09
// retrofit and its operator-rejected deviations)
// ============================================================
//
// Every LIVE line is the reference cmaster.c VERBATIM.  Reference
// lines whose subsystems are not yet ported are carried IN PLACE
// as commented reference text with a DEFERRED tag naming the
// blocking subsystem — so the mechanical diff against cmaster.c
// shows only whitespace + documented-deferred deltas, and each
// future subsystem port re-activates its lines without
// restructuring.  The retrofit's deviations are gone: no
// `pTxChannel` carve-out (create_xmtr opens the WDSP TXA channel
// itself — OpenChannel / XCreateAnalyzer / fexchange0 resolve
// through the operator-approved wire/wdspcalls.h seam, so the
// runtime-loaded-DLL justification for the carve-out no longer
// exists), no Lyra-native stype_for/txid_for helpers (the real
// cmsetup.c port supplies stype/txid/chid/inid), no per-xmtr_id
// create_xmtr(int) signature (the reference loops over
// pcm->cmXMTR internally), no omitted update[] critical section
// in xcmaster, no omitted TCI-override memset.
//
// Only Lyra-cpp packaging differences (NOT code changes):
//   * `lyra::wire` namespace (reference is global C).
//   * `#include "cmcomm.h"` becomes the explicit family includes
//     (see the umbrella-mapping note in wire/cmcomm.h) plus
//     wire/wdspcalls.h — the single operator-approved seam through
//     which the wdsp.dll entry points (OpenChannel / CloseChannel
//     / fexchange0 / XCreateAnalyzer / DestroyAnalyzer /
//     SetInputSamplerate / SetInputBuffsize / SetOutputSamplerate)
//     resolve; the reference statically links them.
//   * `(char *)""` cast at the XCreateAnalyzer call sites (C++
//     forbids the reference's string-literal-to-`char*`
//     conversion; the callee does not write through it).
//   * PORT carries the reference's `__declspec (dllexport)`.
//
// INVOCATION-SITE ACCOMMODATION (operator-acknowledged, forced by
// the runtime-loaded wdsp.dll): the reference's create_cmaster()
// calls create_rcvr()/create_xmtr() inline (cmaster.c:287-288) —
// but Lyra's create_cmaster() runs at app startup BEFORE wdsp.dll
// loads (the router it creates must exist before the EP6 sink
// registration).  create_xmtr()/destroy_xmtr() therefore keep
// their verbatim no-arg signatures but are invoked from main.cpp
// AFTER wdsp->load() + resolve_wdsp_calls() succeed (create) and
// from the matching aboutToQuit handler (destroy) — the same
// relative ordering the reference's create/destroy sequences
// produce.  The create_xmtr()/destroy_xmtr() lines inside
// create_cmaster()/destroy_cmaster() are carried as DEFERRED-
// callsite comments pointing here.
//
// DEFERRED SUBSYSTEMS (per Stage E.0 audit + the locked plan; each
// line marked where it occurs):
//   * create_rcvr/destroy_rcvr bodies — Lyra RX lives in
//     WdspEngine (operator-approved hybrid layout); porting the
//     bodies would double-open WDSP channel chid(0,0)=0.
//   * dexp/VOX + anti-vox mixer    — VOX is v0.2.3 scope.
//   * txgain (Penelope)            — reference run=0 on HL2; PS v0.3.
//   * sidetone                     — CW v0.2.2.
//
// eer was on this list through P1; RESTORED VERBATIM at P2.a
// (2026-06-12): create_eer(run=0)/destroy_eer/xeer/pSetEER* live
// via the wdspcalls seam (struct verbatim at wire/cmcomm.h).  HL2
// has no EER hardware — run stays 0, xeer is a no-op (in==out),
// but the object EXISTS so the `peer->run` derefs in the P2
// sendOutbound / P4 sendProtocol1Samples ports are valid, exactly
// as the reference ships it (cmaster.c:212-224).
//   * pipe / sync / cmasio / ivac / tci / znob / znobII /
//     analyzers(.c) / amix        — unported ChannelMaster units.
//   * the id-0 RX audio mixer      — WdspEngine constructs it
//     (openRx1) per the approved hybrid; creating it here too
//     would double-create paamix[0].
//
// See NOTICE.md and CREDITS.md (repo root) for full attribution.

#include "wire/CMaster.h"
#include "wire/Router.h"      // create_router/destroy_router (reference: via cmcomm.h)
#include "wire/wdspcalls.h"   // the operator-approved wdsp.dll linkage seam
#include "wire/TxGain.h"      // create_txgain/destroy_txgain/xtxgain (TX power gain block)

namespace lyra::wire {

// Reference cmaster.c:29-30 (verbatim):

cmaster cm  = {0};
CMASTER pcm = &cm;

// Reference cmaster.c:32-95 — create_rcvr.
//
// DEFERRED [WdspEngine hybrid — operator-approved]: Lyra-cpp's RX
// path lives in WdspEngine (src/wdsp_engine.cpp openRx1 == the
// per-RX OpenChannel/XCreateAnalyzer path below).  Porting this
// body would double-open WDSP channel chid(0,0)=0 and double-
// create analyzer disp 0.  Body carried verbatim as reference
// text; the function exists so create_cmaster()'s verbatim call
// keeps its shape.
//
//   void create_rcvr()
//   {
//   	int i, j, rc;
//   	for (i = 0; i < pcm->cmRCVR; i++)
//   	{
//   		int in_id = inid (0, i);
//   		// audio buffer, one per subreceiver
//   		for (j = 0; j < pcm->cmSubRCVR; j++)
//   			pcm->rcvr[i].audio[j] = (double *) malloc0 (getbuffsize (pcm->cmMAXAudioRate) * sizeof (complex));
//   		// noise blanker
//   		pcm->rcvr[i].panb = create_anb (0, pcm->xcm_insize[in_id],
//   			pcm->in[in_id], pcm->in[in_id], pcm->xcm_inrate[in_id],
//   			0.0001, 0.0001, 0.0001, 0.05, 30.0);
//   		// noise blanker II
//   		pcm->rcvr[i].pnob = create_nob (0, pcm->xcm_insize[in_id],
//   			pcm->in[in_id], pcm->in[in_id], pcm->xcm_inrate[in_id],
//   			0, 0.0001, 0.0001, 0.0001, 0.0001, 0.025, 0.05, 30.0);
//   		// dsp channel, one per subreceiver
//   		for (j = 0; j < pcm->cmSubRCVR; j++)
//   		{
//   			OpenChannel(chid (in_id, j), pcm->xcm_insize[in_id], 4096,
//   				pcm->xcm_inrate[in_id], 48000, pcm->rcvr[i].ch_outrate,
//   				0, 0, 0.010, 0.025, 0.000, 0.010, 1);
//   		}
//   		// displays
//   		XCreateAnalyzer(i, &rc, 262144, 1, 1, "");
//   	}
//   }
void create_rcvr()
{
}

// Reference cmaster.c:97-110 — destroy_rcvr.
//
// DEFERRED [WdspEngine hybrid] — see create_rcvr above.
//
//   void destroy_rcvr()
//   {
//   	int i, j;
//   	for (i = 0; i < pcm->cmRCVR; i++)
//   	{
//   		DestroyAnalyzer(i);
//   		for (j = 0; j < pcm->cmSubRCVR; j++)
//   			CloseChannel (chid (inid (0, i), j));
//   		destroy_nob (pcm->rcvr[i].pnob);
//   		destroy_anb (pcm->rcvr[i].panb);
//   		for (j = 0; j < pcm->cmSubRCVR; j++)
//   			_aligned_free (pcm->rcvr[i].audio[j]);
//   	}
//   }
void destroy_rcvr()
{
}

// Reference cmaster.c:112-253 (verbatim; DEFERRED lines carried
// in place as reference text):

// standard transmitter
void create_xmtr()
{
	int i, j, rc;
	// DEFERRED [dexp/VOX v0.2.3] — the anti-vox mixer inrates feed
	// the create_aamix(anti-vox) call below, deferred with it:
	//   int avoxmix_inrates[cmMAXrcvr * cmMAXSubRcvr];	// anti-vox mixer inrates
	//   for (i = 0; i < pcm->cmRCVR; i++)
	//   		for (j = 0; j < pcm->cmSubRCVR; j++)
	//   			avoxmix_inrates[pcm->cmSubRCVR * i + j] = pcm->rcvr[i].ch_outrate;
	for (i = 0; i < pcm->cmXMTR; i++)
	{
		int in_id = inid (1, i);

		// out[0] is transmitter output buffer
		// out[1] is needed for EER
		// out[2] is used for sidetone-out
		for (j = 0; j < 3; j++)
			pcm->xmtr[i].out[j] = (double *) malloc0 (getbuffsize (pcm->cmMAXTxOutRate) * sizeof (complex));

		// DEFERRED [dexp/VOX v0.2.3] — reference cmaster.c:130-156:
		//   create_dexp (i, 0, pcm->xcm_insize[in_id], pcm->in[in_id],
		//   	pcm->in[in_id], pcm->xcm_inrate[in_id], 0.01, 0.025,
		//   	0.100, 1.000, 4.000, 0.750, 0.050, 256, 0, 1000.0,
		//   	2000.0, 0, 1, 1, 0.060, pcm->xmtr[i].pushvox, 0,
		//   	pcm->audio_outsize, pcm->audio_outrate, 0.01, 0.01);
		// DEFERRED [dexp/VOX v0.2.3] — anti-vox mixer, reference
		// cmaster.c:157-175 (SendAntiVOXData lives in unported
		// vox.c):
		//   pcm->xmtr[i].pavoxmix = (AAMIX) create_aamix (-1, i,
		//   	pcm->audio_outsize, pcm->audio_outsize,
		//   	pcm->cmRCVR * pcm->cmSubRCVR, 0,
		//   	(1<<(pcm->cmRCVR*pcm->cmSubRCVR))-1, 1.0, 4096,
		//   	avoxmix_inrates, pcm->audio_outrate, SendAntiVOXData,
		//   	0.000, 0.000, 0.000, 0.000);

		// dsp channel
		OpenChannel(
			chid (in_id, 0),					// channel number
			pcm->xcm_insize[in_id],				// input buffer size
			4096,								// dsp buffer size
			pcm->xcm_inrate[in_id],				// input sample rate
			96000,								// dsp sample rate
			pcm->xmtr[i].ch_outrate,			// output sample rate
			1,									// channel type
			0,									// initial state
			0.000,								// tdelayup
			0.010,								// tslewup
			0.000,								// tdelaydown
			0.010,								// tslewdown
			1);									// block until output is available
		// display
		XCreateAnalyzer (
			in_id,
			&rc,
			262144,
			1,
			1,
			(char *)"");						// (char*) cast: C++ string-literal constness; callee does not write
		// Penelope gain (reference cmaster.c:200-210) — the TX-output
		// fixed-gain + amp-protect block.  Created run=0 / unity gain
		// (byte-identical to no-gain until the operator drives it via
		// SetTXFixedGain), matching the reference defaults exactly.
		pcm->xmtr[i].pgain = create_txgain(
			0,									// run fixed gain
			0,									// run protection gain for external amplifier
			pcm->xmtr[i].ch_outsize,			// size
			pcm->xmtr[i].out[0],				// in
			pcm->xmtr[i].out[0],				// out
			1.0,								// Igain (for fixed gain)
			1.0,								// Qgain (for fixed gain)
			0,									// adc_value
			50);								// ADC Supply = 5.0V
		// eer
		pcm->xmtr[i].peer = create_eer (
			0,									// run
			pcm->xmtr[i].ch_outsize,			// size
			pcm->xmtr[i].out[0],				// in
			pcm->xmtr[i].out[0],				// out
			pcm->xmtr[i].out[1],				// outM
			pcm->xmtr[i].ch_outrate,			// sample rate
			1.0,								// mgain
			1.0,								// pgain
			0,									// rundelays
			0.0,								// mdelay
			0.0,								// pdelay
			1);									// amiq
		// interleave (for eer)
		pcm->xmtr[i].pilv = create_ilv(
			0,									// run
			1,									// id to use in Outbound call
			pcm->xmtr[i].ch_outsize,			// input buffer size
			2,									// maximum number of inputs
			3,									// which streams to interleave, one bit per stream
			pcm->OutboundTx);					// function to call with Outbound data

		// DEFERRED [sidetone — CW v0.2.2] — reference
		// cmaster.c:235-251:
		//   create_sidetone(i, 1, 0, pcm->xmtr[i].ch_outrate,
		//   	pcm->xmtr[i].ch_outsize, pcm->xmtr[i].out[0],
		//   	pcm->xmtr[i].out[2], pcm->xmtr[i].out[0], 0, 400.0,
		//   	1.0, 1.0, 20, 0, 0.005);
		// keySidetone(0, 1);					// for testing only
	}
}

// Reference cmaster.c:255-271 (verbatim; DEFERRED lines carried
// in place as reference text):

void destroy_xmtr()
{
	int i, j;
	for (i = 0; i < pcm->cmXMTR; i++)
	{
		// DEFERRED [sidetone — CW v0.2.2]:
		//   destroy_sidetone(i);
		destroy_ilv (pcm->xmtr[i].pilv);
		destroy_eer (pcm->xmtr[i].peer);
		destroy_txgain (pcm->xmtr[i].pgain);
		DestroyAnalyzer (inid (1, i));
		CloseChannel (chid (inid (1, i), 0));
		// DEFERRED [dexp/VOX v0.2.3]:
		//   destroy_aamix ((void *)(pcm->xmtr[i].pavoxmix), -1);
		//   destroy_dexp (i);
		for (j = 0; j < 3; j++)
			_aligned_free (pcm->xmtr[i].out[j]);
	}
}

// Reference cmaster.c:273-320 (verbatim; DEFERRED lines carried
// in place as reference text):

void create_cmaster()
{
	int i;
	for (i = 0; i < pcm->cmSTREAM; i++)
	{
		InitializeCriticalSectionAndSpinCount(&pcm->update[i], 2500);	// 'update' critical section
		create_cmbuffs(													// input ring buffer
			i,															// stream number
			1,															// 'accept' data
			pcm->cmMAXInbound[i],										// maximum input size
			getbuffsize (pcm->cmMAXInRate),								// maximum output size
			pcm->xcm_insize[i]);										// ring outsize = xcmaster() insize
		pcm->in[i] = (double *) malloc0 (getbuffsize (pcm->cmMAXInRate) * sizeof (complex));// input buffer
	}
	create_rcvr();														// standard receiver
	// DEFERRED-CALLSITE [runtime-loaded wdsp.dll — see file
	// preamble]: main.cpp invokes create_xmtr() after wdsp->load()
	// + resolve_wdsp_calls() succeed (the OpenChannel /
	// XCreateAnalyzer calls inside need the resolved seam):
	//   create_xmtr();													// standard transmitter
	// DEFERRED [WdspEngine hybrid — operator-approved]: the id-0
	// RX audio mixer is constructed by WdspEngine::openRx1 (which
	// also registers the Outbound dispatcher); creating it here
	// too would double-create paamix[0].  Reference
	// cmaster.c:289-314:
	//   {																	// audio mixer
	//   	int active = 0;													// no inputs active initially
	//   	int what = (1 << (pcm->cmRCVR * pcm->cmSubRCVR + pcm->cmXMTR)) - 1;	// mix all
	//   	for (i = 0; i < pcm->cmRCVR; i++)
	//   		for (j = 0; j < pcm->cmSubRCVR; j++)
	//   			pcm->aamix_inrates[pcm->cmSubRCVR * i + j] = pcm->rcvr[i].ch_outrate;
	//   	for (i = 0; i < pcm->cmXMTR; i++)
	//   		pcm->aamix_inrates[pcm->cmRCVR * pcm->cmSubRCVR + i] = pcm->xmtr[i].ch_outrate;
	//   	create_aamix (0, 0, pcm->audio_outsize, pcm->audio_outsize,
	//   		pcm->cmRCVR * pcm->cmSubRCVR + pcm->cmXMTR, active, what,
	//   		1.0, 4096, pcm->aamix_inrates, pcm->audio_outrate,
	//   		pcm->OutboundRx, 0.000, 0.010, 0.000, 0.010);
	//   }
	// DEFERRED [cmasio unported — no Lyra ASIO]:
	//   create_cmasio();
	create_router(0);
	// DEFERRED [analyzers.c unported — Stage E.1 / PS v0.3]:
	//   pcm->panalalloc = (ANALYZERS)create_analyzer_alloc(32, 40);
	// alloc_analyzer(1, 0, 262144);
	// alloc_analyzer(1, 0, 16384);
}

// Reference cmaster.c:322-337 (verbatim; DEFERRED lines carried
// in place as reference text):

void destroy_cmaster()
{
	int i;
	// DEFERRED [analyzers.c unported — Stage E.1]:
	//   destroy_analyzer_alloc();
	destroy_router(0, 0);
	// DEFERRED [cmasio unported]:
	//   destroy_cmasio();
	// DEFERRED [WdspEngine hybrid]: the id-0 mixer is destroyed by
	// WdspEngine::closeRx1:
	//   destroy_aamix  (0, 0);
	// DEFERRED-CALLSITE [runtime-loaded wdsp.dll — see file
	// preamble]: main.cpp's aboutToQuit handler-1.5 invokes
	// destroy_xmtr() (gated on create_xmtr() having run) BEFORE
	// this function fires in handler-4 — the reference's relative
	// order (destroy_xmtr before the per-stream teardown) holds:
	//   destroy_xmtr();
	destroy_rcvr();
	for (i = 0; i < pcm->cmSTREAM; i++)
	{
		DeleteCriticalSection (&pcm->update[i]);
		destroy_cmbuffs (i);
		_aligned_free (pcm->in[i]);
	}
}

// Reference cmaster.c:339-405 (verbatim; DEFERRED lines carried
// in place as reference text):

PORT
void xcmaster (int stream)
{
	int error;
	EnterCriticalSection (&pcm->update[stream]);
	switch (stype (stream))
	{
#pragma warning(suppress: 4101)  // verbatim text wins: rx/j/k/disp serve the DEFERRED case-0 body below
	int rx, tx, j, k, disp;

	case 0:  // standard receiver
		// DEFERRED [WdspEngine hybrid — operator-approved]: the RX
		// dispatch lives in WdspEngine::feedIq (router-fed, not
		// cmbuffs-fed); no Inbound() producer feeds an RX stream,
		// so this case never executes.  Reference cmaster.c:347-373:
		//   rx = rxid (stream);
		//   xpipe (stream, 0, pcm->in);
		//   xanb (pcm->rcvr[rx].panb);																// nb
		//   xnob (pcm->rcvr[rx].pnob);																// nb2
		//   Spectrum0 (_InterlockedAnd (&pcm->rcvr[rx].run_pan, 0xffffffff), rx, 0, 0,				// panadapter
		//   	pcm->in[stream]);
		//
		//   for (j = 0; j < pcm->panalalloc->m_analyzers; j++)										// additional analyzers
		//   {
		//   	disp = pcm->panalalloc->disp[j];
		//   	if (disp >= 0 && pcm->panalalloc->stream[j] == stream)
		//   		Spectrum0 ( _InterlockedAnd (&pcm->panalalloc->run[j],   0xffffffff)
		//   			   && (!_InterlockedAnd (&pcm->panalalloc->stype[j], 0xffffffff)),
		//   			   disp, 0, 0, pcm->in[stream]);
		//   }
		//
		//   for (j = 0; j < pcm->cmSubRCVR; j++)
		//   	fexchange0 (chid (stream, j), pcm->in[stream], pcm->rcvr[rx].audio[j], &error);		// dsp
		//   xpipe (stream, 1, pcm->rcvr[rx].audio);
		//   for (j = 0; j < pcm->cmSubRCVR; j++)
		//   {
		//   	xMixAudio (0, 0, chid (stream, j), pcm->rcvr[rx].audio[j]);							// mix audio
		//   	for (k = 0; k < pcm->cmXMTR; k++)
		//   		xMixAudio (pcm->xmtr[k].pavoxmix, -1, chid (stream, j), pcm->rcvr[rx].audio[j]);// send audio to anti-vox mixer(s)
		//   }
		// if (rx == 0) WriteAudio(30.0, 48000, 64, pcm->rcvr[0].audio[0], 3);
		break;

	case 1:  // standard transmitter
		tx = txid (stream);
		// #158 Stage 4 — VAC-in (PC soundcard / USB mic / digital app) is the
		// Lyra-native realization of the reference's PC-mic line
		// (asioIN(pcm->in[stream]) — cmasio.c, unported).  Override the mic
		// buffer from the VAC-in ring (xvacIN) when this xmtr's use_vac_audio
		// is set, BEFORE the TCI override so the reference precedence holds
		// (the mic-source selector keeps exactly one of {EP6, VAC, TCI} live).
		if (_InterlockedAnd (&pcm->xmtr[tx].use_vac_audio, 1))
		{
			if (pcm->InboundVacTxAudio)
				(*pcm->InboundVacTxAudio)(pcm->xcm_insize[stream], pcm->in[stream]);
			else
				memset (pcm->in[stream], 0, pcm->xcm_insize[stream] * sizeof (complex));
		}
		if (_InterlockedAnd (&pcm->xmtr[tx].use_tci_audio, 1))									// from tci tx audio, service asio above so we still get other rx output, but override with tci if needed
		{
			if (pcm->InboundTCITxAudio)
				(*pcm->InboundTCITxAudio)(pcm->xcm_insize[stream], pcm->in[stream]);
			else
				memset (pcm->in[stream], 0, pcm->xcm_insize[stream] * sizeof (complex));
		}
		// DEFERRED [pipe unported — diagnostic tap]:
		//   xpipe (stream, 0, pcm->in);
		// DEFERRED [dexp/VOX v0.2.3]:
		//   xdexp (tx);																				// vox-dexp
		// Native mic-DSP rack (Lyra-native, pre-WDSP-TXA): speech pre-stages
		// (#88 Auto-AGC + De-esser) THEN the EQ (#50), in chain order, in
		// place on the mic buffer before the modulator.  The whole rack is
		// skipped when tx_rack_bypass is set (digital modes DIGU/DIGL) so the
		// mic DSP never touches digital audio; each hook is also a no-op when
		// its stage is unset / bypassed (the registered cb checks).
		if (!_InterlockedAnd (&pcm->tx_rack_bypass, 1)) {
			if (pcm->TxSpeechProcess)
				(*pcm->TxSpeechProcess)(pcm->xcm_insize[stream], pcm->in[stream]);
			if (pcm->TxEqProcess)
				(*pcm->TxEqProcess)(pcm->xcm_insize[stream], pcm->in[stream]);
			if (pcm->TxCombinatorProcess)
				(*pcm->TxCombinatorProcess)(pcm->xcm_insize[stream], pcm->in[stream]);
			if (pcm->TxPlateProcess)
				(*pcm->TxPlateProcess)(pcm->xcm_insize[stream], pcm->in[stream]);
		}
		// #90 TX-monitor tap — capture the post-rack mic (== the fexchange0
			// input, "what you sound like") for the operator monitor.  Sits
			// OUTSIDE the rack-bypass gate so digital/CW are captured too (just
			// unprocessed).  READ-ONLY on pcm->in; the cb must never write it.
			if (pcm->TxMonitorTap)
				(*pcm->TxMonitorTap)(pcm->xcm_insize[stream], pcm->in[stream]);
			fexchange0 (chid (stream, 0), pcm->in[stream], pcm->xmtr[tx].out[0], &error);			// dsp
		// WriteAudio(10.0, pcm->xmtr[tx].ch_outrate, pcm->xmtr[tx].ch_outsize, pcm->xmtr[tx].out[0], 3);
		// DEFERRED [sidetone — CW v0.2.2]:
		//   xsidetone(tx);
		// DEFERRED [pipe unported — diagnostic tap]:
		//   xpipe (stream, 1, pcm->xmtr[tx].out);
		// Spectrum0 (1, stream, 0, 0, pcm->xmtr[tx].out[0]);									// panadapter
		xMixAudio (0, 0, chid (stream, 0), pcm->xmtr[tx].out[2]);								// mix monitor audio
		xtxgain (pcm->xmtr[tx].pgain);															// Gain for Penelope & amp_protect
		xeer (pcm->xmtr[tx].peer);																// EER transmission
		xilv(pcm->xmtr[tx].pilv, pcm->xmtr[tx].out);											// interleave EER, call Outbound()
		break;

	case 2:  // special 0, stitched upper panadapter
		// DEFERRED [pipe unported — diagnostic tap]:
		//   xpipe (stream, 0, pcm->in);
		break;
	}
	LeaveCriticalSection (&pcm->update[stream]);
}

// Reference cmaster.c:407-412 (verbatim):

PORT
void SendpOutboundRx (void (*Outbound)(int id, int nsamples, double* buff))
{
	pcm->OutboundRx = Outbound;
	SetAAudioMixOutputPointer (0, 0, pcm->OutboundRx);
}

// Reference cmaster.c:414-419 (verbatim):

PORT
void SendpOutboundTx(void (*Outbound)(int id, int nsamples, double* buff))
{
	pcm->OutboundTx = Outbound;
	SetILVOutputPointer(0, pcm->OutboundTx);
}

// Reference cmaster.c:421-425 (verbatim):

PORT
void SendpOutboundTCIRxIQ (void (*Outbound)(int id, int nsamples, double* buff))
{
	pcm->OutboundTCIRxIQ = Outbound;
}

// Reference cmaster.c:428-432 (verbatim):

PORT
void SendpInboundTCITxAudio (void (*Inbound)(int nsamples, double* buff))
{
	pcm->InboundTCITxAudio = Inbound;
}

// Reference cmaster.c:434-437 (verbatim):

PORT
void SetTCIRun (int active)
{
	_InterlockedExchange (&pcm->tci_run, active);
}

// Reference cmaster.c:439-443 (verbatim):

PORT
void SetTXTCIAudio (int txid, int active)
{
	_InterlockedExchange (&pcm->xmtr[txid].use_tci_audio, active);
}

// #158 Stage 4 — VAC-in TX-audio source (Lyra-native, mirrors the TCI pair
// SendpInboundTCITxAudio / SetTXTCIAudio above).

PORT
void SendpInboundVacTxAudio (void (*Inbound)(int nsamples, double* buff))
{
	pcm->InboundVacTxAudio = Inbound;
}

PORT
void SetTXVacAudio (int txid, int active)
{
	_InterlockedExchange (&pcm->xmtr[txid].use_vac_audio, active);
}

// #50 native parametric-EQ rack stage (Lyra-native).  Register the in-place
// mic-EQ processor called per TX block on pcm->in[stream] before fexchange0.
PORT
void SendpTxEqProcessor (void (*Process)(int nsamples, double* buff))
{
	pcm->TxEqProcess = Process;
}

void SendpTxMonitorTap (void (*Tap)(int nsamples, double* buff))			// #90
{
	pcm->TxMonitorTap = Tap;
}

// #88 — register the native TX speech pre-processor (runs before the EQ).
PORT
void SendpTxSpeechProcessor (void (*Process)(int nsamples, double* buff))
{
	pcm->TxSpeechProcess = Process;
}

// #51 — register the native 5-band Combinator (runs after the EQ).
PORT
void SendpTxCombinatorProcessor (void (*Process)(int nsamples, double* buff))
{
	pcm->TxCombinatorProcess = Process;
}

// #52 — register the native Plate reverb (runs after the combinator).
PORT
void SendpTxPlateProcessor (void (*Process)(int nsamples, double* buff))
{
	pcm->TxPlateProcess = Process;
}

// #50 — gate the whole native TX rack on/off (digital-mode bypass).
PORT
void SetTxRackBypass (int bypass)
{
	_InterlockedExchange (&pcm->tx_rack_bypass, bypass);
}

// Reference cmaster.c:445-449 (verbatim):

PORT
void SetRunPanadapter (int id, int run)
{
	_InterlockedExchange (&pcm->rcvr[id].run_pan, run);
}

// Reference cmaster.c:451-507 (verbatim; DEFERRED lines carried
// in place as reference text — the znob/znobII, pipe-siphon,
// ivac, and dexp setters belong to unported units):

PORT
void SetXcmInrate (int in_id, int rate)	// 2014-12-18:  called for streams 0, 1, 3, 4 (RX).  Stream 2 (TX) called in CMCreateCMaster().
{
	int i, rx, tx, sp0;
	EnterCriticalSection (&pcm->update[in_id]);
	if (pcm->xcm_inrate[in_id] != rate)
	{
		pcm->xcm_inrate[in_id] = rate;
		pcm->xcm_insize[in_id] = getbuffsize (rate);
		SetCMRingOutsize(in_id, pcm->xcm_insize[in_id]);
		switch (stype (in_id))
		{
		case 0:  // receiver
			rx = rxid (in_id);
			// DEFERRED [znob/znobII unported]:
			//   SetRCVRANBBuffsize (0, rx, pcm->xcm_insize[in_id]);					// set anb input size
			//   SetRCVRANBSamplerate (0, rx, rate);									// set anb input rate
			//   SetRCVRNOBBuffsize (0, rx, pcm->xcm_insize[in_id]);					// set nob input size
			//   SetRCVRNOBSamplerate (0, rx, rate);									// set nob input rate
			// set display (currently in C#)
			for (i = 0; i < pcm->cmSubRCVR; i++)
			{
				SetInputSamplerate (chid (in_id, i), rate);						// dsp channel input rate
				SetInputBuffsize (chid (in_id, i), pcm->xcm_insize[in_id]);		// dsp channel input size
			}
			// PIPE - set wave player (leave in C# since player is there)
			// PIPE - set wave recorder (leave in C# since player is there)
			// DEFERRED [pipe unported]:
			//   if (rx == 0) SetSiphonInsize (rx, pcm->xcm_insize[in_id]);			// PIPE - set siphon for phase2 display, RX1 only
			// DEFERRED [ivac unported]:
			//   SetIVACiqSizeAndRate (rx, pcm->xcm_insize[in_id], pcm->xcm_inrate[in_id]);	// PIPE - set vacOUT size and rate for IQ data
			break;
		case 1:  // transmitter
			tx = txid (in_id);
			SetInputSamplerate (chid (in_id, 0), rate);							// dsp channel input rate
			SetInputBuffsize (chid (in_id, 0), pcm->xcm_insize[in_id]);			// dsp channel input size
			//SetTXAVoxSize (tx, pcm->xcm_insize[in_id]);							// VOX size
			// DEFERRED [dexp/VOX v0.2.3]:
			//   SetDEXPSize (tx, pcm->xcm_insize[in_id]);							// vox-dexp size
			//   SetDEXPRate (tx, rate);												// vox-dexp rate
			// PIPE - set wave player, rcvr0 (leave in C# since player is there)
			// PIPE - set wave player, rcvr1 (leave in C# since player is there)
			// PIPE - set wave recorder, rcvr0 (leave in C# since recorder is there)
			// PIPE - set wave recorder, rcvr1 (leave in C# since recorder is there)
			// DEFERRED [ivac unported]:
			//   SetIVACmicSize (0, pcm->xcm_insize[in_id]);							// PIPE - set vacIN0 input size
			//   SetIVACmicRate (0, rate);											// PIPE - set vacIN0 input rate
			//   SetIVACmicSize (1, pcm->xcm_insize[in_id]);							// PIPE - set vacIN1 input size
			//   SetIVACmicRate (1, rate);											// PIPE - set vacIN1 input rate
			break;
		case 2:	// special0 for stitched rx display
			sp0 = sp0id (in_id);
			// DEFERRED [znob/znobII unported]:
			//   SetRCVRANBBuffsize (2, sp0, pcm->xcm_insize[in_id]);				// PIPE - set anb input size
			//   SetRCVRANBSamplerate (2, sp0, rate);								// PIPE - set anb input rate
			//   SetRCVRNOBBuffsize (2, sp0, pcm->xcm_insize[in_id]);				// PIPE - set nob input size
			//   SetRCVRNOBSamplerate (2, sp0, rate);								// PIPE - set nob input rate
			break;
		}
	}
	LeaveCriticalSection (&pcm->update[in_id]);
}

// Reference cmaster.c:509-519 (verbatim):

PORT
void SetCMAudioOutrate (int in_id, int rate)		// 2014-11-24:  NOT called by console because this is fixed at 48K by the
{													//   protocol and that is the default rate being set at creation.
	EnterCriticalSection (&pcm->update[in_id]);
	pcm->audio_outrate = rate;
	pcm->audio_outsize = getbuffsize (rate);
	SetAAudioOutRate     (0, 0, pcm->audio_outrate);
	SetAAudioRingInsize  (0, 0, pcm->audio_outsize);
	SetAAudioRingOutsize (0, 0, pcm->audio_outsize);
	LeaveCriticalSection (&pcm->update[in_id]);
}

// Reference cmaster.c:521-547 (verbatim; DEFERRED lines carried
// in place as reference text):

PORT
void SetRcvrChannelOutrate (int rcvr_id, int rate, int state)	// 2014-12-18:  NOT called by console as values are set at creation and
{																//   not changed.
	int j;
	int in_id = inid (0, rcvr_id);
	int mix_in_id;
	EnterCriticalSection (&pcm->update[in_id]);
	pcm->rcvr[rcvr_id].ch_outrate = rate;
	pcm->rcvr[rcvr_id].ch_outsize = getbuffsize (rate);
	for (j = 0; j < pcm->cmSubRCVR; j++)
	{
		SetOutputSamplerate(chid(in_id, j), rate);							// set DSP Channel output rate (sets size too)
		mix_in_id = mixinid (in_id, j);										// set audio mixer
		SetAAudioMixState (0, 0, mix_in_id, 0);								//	.Make this stream INACTIVE in AAMixer
		SetAAudioStreamRate (0, 0, mix_in_id, rate);						//	.Set Mixer input rate (sets size too)
		SetAAudioMixState (0, 0, mix_in_id, state);							//	.Conditionally make this stream ACTIVE in AAMixer
	}
	// DEFERRED [ivac unported]:
	//   SetIVACaudioRate (rcvr_id, rate);
	//   SetIVACaudioSize (rcvr_id, pcm->rcvr[rcvr_id].ch_outsize);
	// DEFERRED [tci.c unported — Lyra TCI lives in tci_server]:
	//   SetTCIRxAudioRate (rcvr_id, rate);
	//   SetTCIRxAudioSize (rcvr_id, pcm->rcvr[rcvr_id].ch_outsize);
	// PIPE - set Scope, PSDR RX1 Only (leave in C# since scope is there)
	// PIPE - set wave recorder, rcvr0 (leave in C# since recorder is there)
	// PIPE - set wave recorder, rcvr1 (leave in C# since recorder is there)
	LeaveCriticalSection (&pcm->update[in_id]);
}

// Reference cmaster.c:549-580 (verbatim; DEFERRED lines carried
// in place as reference text):

PORT
void SetXmtrChannelOutrate (int xmtr_id, int rate, int state)	// 2014-11-24:  Called by console when TX rate is set
{
	int in_id = inid (1, xmtr_id);
	int mix_in_id = mixinid (inid (1, xmtr_id), 0);
	int size = getbuffsize (rate);
#pragma warning(suppress: 4101)  // verbatim text wins: i serves the DEFERRED SetTCITxMonitorRate loop below
	int i;
	EnterCriticalSection (&pcm->update[in_id]);
	pcm->xmtr[xmtr_id].ch_outrate = rate;									// channel out_rate
	pcm->xmtr[xmtr_id].ch_outsize = size;									// channel out_size
	SetOutputSamplerate(chid(in_id, 0), rate);								// set DSP Channel output rate (sets size too)
	// set TX display (currently in C#)
	// DEFERRED [sidetone — CW v0.2.2]:
	//   setSidetoneRate(xmtr_id, rate);
	//   setSidetoneSize(xmtr_id, size);
																			// set audio mixer
	SetAAudioMixState (0, 0, mix_in_id, 0);									//	.Make this stream INACTIVE in AAMixer
	SetAAudioStreamRate (0, 0, mix_in_id, rate);							//	.Set Mixer input rate (sets size too)
	SetAAudioMixState (0, 0, mix_in_id, state);								//	.Conditionally make this stream ACTIVE in AAMixer
																			//
	SetTXGainSize(pcm->xmtr[xmtr_id].pgain, size);							// set size for Penelope Gain Block
	pSetEERSamplerate(pcm->xmtr[xmtr_id].peer, rate);						// set rate for EER
	pSetEERSize(pcm->xmtr[xmtr_id].peer, size);								// set size for EER
	pSetILVInsize(pcm->xmtr[xmtr_id].pilv, size);							// set size for Interleave & output
	// PIPE - set Scope (leave in C# since scope is there)
	// DEFERRED [ivac unported]:
	//   SetIVACtxmonRate (0, rate);												// set vacOUT0 rate for tx monitor
	//   SetIVACtxmonSize (0, size);												// set vacOUT0 size for tx monitor
	//   SetIVACtxmonRate (1, rate);												// set vacOUT1 rate for tx monitor
	//   SetIVACtxmonSize (1, size);												// set vacOUT1 size for tx monitor
	// DEFERRED [tci.c unported]:
	//   for (i = 0; i < pcm->cmRCVR; i++)
	//   	SetTCITxMonitorRate (i, rate);
	// PIPE - set Wave Recorder (leave in C# since recorder is there)
	LeaveCriticalSection (&pcm->update[in_id]);
}

// Lyra-native (2026-09-06): narrow variant of SetXmtrChannelOutrate that
// changes ONLY the transmitter channel's DUC output rate + the output-stage
// block sizes (WDSP channel out rate, txgain, EER, interleaver).  It does
// NOT touch the RX-audio AAMixer: the full reference call re-rates AND
// toggles the TX-monitor mixer input's active state, which would clobber the
// operator's MON setting on this channel shared between P1 (48 kHz out) and
// P2 (192 kHz DUC out).  SetOutputSamplerate also re-points the compensating
// FIR (CFIR) to the new rate.  Used by the P2 DUC producer seam to raise the
// shared TXA channel to 192 kHz on P2 TX activate and restore 48 kHz on
// deactivate; P2 TX-monitor rate handling is a later (Stage-2+) item, and
// P1/HL2 never calls this so its 48 kHz path is unaffected.
void SetXmtrDucOutrate (int xmtr_id, int rate)
{
	int in_id = inid (1, xmtr_id);
	int size  = getbuffsize (rate);
	EnterCriticalSection (&pcm->update[in_id]);
	pcm->xmtr[xmtr_id].ch_outrate = rate;								// channel out_rate
	pcm->xmtr[xmtr_id].ch_outsize = size;								// channel out_size
	SetOutputSamplerate (chid (in_id, 0), rate);						// DSP out rate (+ CFIR rate, internal resamplers, reallocs)
	SetTXGainSize (pcm->xmtr[xmtr_id].pgain, size);						// Penelope gain block size
	pSetEERSamplerate (pcm->xmtr[xmtr_id].peer, rate);					// EER rate
	pSetEERSize (pcm->xmtr[xmtr_id].peer, size);						// EER size
	pSetILVInsize (pcm->xmtr[xmtr_id].pilv, size);						// interleave & Outbound size
	LeaveCriticalSection (&pcm->update[in_id]);
}

// Reference cmaster.c:582-588 (verbatim):

PORT
void SetAntiVOXSourceStates (int txid, int streams, int states)
{
	void* a = (void *)pcm->xmtr[txid].pavoxmix;
	SetAAudioMixStates (a, -1, streams, states);
}

// Reference cmaster.c:590-595 (verbatim):

PORT
void SetAntiVOXSourceWhat (int txid, int stream, int state)
{
	void* a = (void *)pcm->xmtr[txid].pavoxmix;
	SetAAudioMixWhat (a, -1, stream, state);
}

}  // namespace lyra::wire
