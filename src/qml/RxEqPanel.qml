// Lyra — RX parametric-EQ dock panel (#59).
//
// The RX twin of the TX EQ.  It IS an EqPanel — the whole EESDR3-style view
// (typed nodes, tile row, analyzer, Accumulate / Before-After-Mod, the
// bandwidth-tracking x-range #168) is reused verbatim — only the four
// per-side bindings differ:
//   • eq          → the RxEq model (post-RXA receive audio), not the TX mic.
//   • bwHz        → the RX bandwidth, so the graph x-range tracks the RX
//                   passband actually in use (the RX mirror of TX #168).
//   • bypassModes → just the digital data modes; the RX EQ stays useful in
//                   CW (unlike the TX rack, which is moot there), so it isn't
//                   auto-dimmed in CWU/CWL — only DIGU/DIGL force-bypass it.
//   • autoRangeSpectrum → the analyzer scale follows the receive level (the
//                   post-RXA audio is AGC'd to ~0 dBFS then AF-gained, so it
//                   can sit above 0 dBFS and flat-top a fixed 0 dBFS scale).
//
// The manual ON/OFF (RxEq.bypass) and the engine wiring
// (WdspEngine::setRxEqEngine, gated rxEqEngine && !bypass && mode∉{DIGU,DIGL})
// live on the C++ side; this wrapper is purely the view binding.

import QtQuick

EqPanel {
    eq: RxEq
    bwHz: Prefs.rxBandwidth
    bypassModes: ["DIGU", "DIGL"]
    panelTitle: qsTr("RX EQ")
    autoRangeSpectrum: true
}
