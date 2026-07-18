# Lyra — Hermes Lite 2 / 2+ SDR transceiver, native C++ rebuild.
#
# Architecture (operator-locked 2026-05-20):
#   * C++23 (no Python, no GIL, anywhere)
#   * Qt 6 (Qt Quick / QML, RHI for graphics — Vulkan/Metal/OpenGL)
#   * Native UDP + OS threads for the wire path
#   * WDSP linked directly (lands in a later commit; Step 1 = window + discovery only)
#   * GPL v3+ (matches the existing Python Lyra tree + WDSP)
#
# Step 1 scope (this skeleton):
#   * Builds clean with MSVC v143 + Qt 6.11.1 MSVC 2022 64-bit binding
#   * Opens a Qt Quick window
#   * C++ UDP discovery thread broadcasts HPSDR Protocol 1 discovery
#     packets on every local NIC and surfaces any HL2 / HL2+ replies
#     in the window — proving the wire path can talk to the radio
#     from C++ with NO Python / NO GIL in the loop.

cmake_minimum_required(VERSION 3.21)

project(Lyra
    VERSION 0.20.0
    DESCRIPTION "Lyra — Hermes Lite 2/2+ SDR transceiver (C++23 / Qt6 rebuild)"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)

# Qt 6 — bring in only the modules Step 1 needs.  More land later
# (Multimedia for audio output, Quick3D for the panadapter, etc.).
# Widgets + QuickWidgets: the app shell is a QMainWindow + QDockWidget
# dock framework (movable/floatable/snappable panels + save/restore
# layout, matching the proven Python-Lyra UX), and the Vulkan
# panadapter/waterfall scene-graph content is embedded INSIDE docks via
# QQuickWidget.  All native C++ — no Python, no GIL anywhere.
find_package(Qt6 6.11 REQUIRED COMPONENTS Core Gui Quick Network Multimedia Widgets QuickWidgets ShaderTools WebSockets SerialPort)

# Vulkan SDK -- optional at compile time (Qt RHI dynamically loads
# the Vulkan runtime via the loader at app launch), but having the
# headers installed silences the Qt-side WrapVulkanHeaders warning
# every configure pass.  Operator-side install steps:
#   1. https://vulkan.lunarg.com/sdk/home#windows -> SDK Installer
#   2. Default install location; restart shell so VULKAN_SDK env
#      var is set.
# If the SDK is absent we silently skip -- Lyra still runs (Qt RHI
# falls back to D3D12 -> OpenGL transparently per FEATURES.md §0).
find_package(Vulkan QUIET)

# --- PortAudio (#158 device-layer rebuild, DL-0) ----------------------
# Vendored PortAudio 19.7.0 SOURCE (third_party/portaudio) — the SAME
# version the reference bundles — static-linked into lyra.exe.  This is
# the full-duplex host-audio engine for the IVAC device layer: ONE
# duplex stream + one callback doing both directions on one clock
# (replacing the two independent Qt streams).  Static (PA_BUILD_SHARED_LIBS
# OFF) so there is no extra DLL to ship; tests/examples off.  MIT-licensed
# (third_party/portaudio/LICENSE.txt; notice retained in NOTICE.md/
# CREDITS.md — distinct from the WDSP no-attribution posture).
set(PA_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(PA_BUILD_TESTS       OFF CACHE BOOL "" FORCE)
set(PA_BUILD_EXAMPLES    OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/portaudio EXCLUDE_FROM_ALL)

# --- ONNX Runtime (DeepFist neural CW decoder) ------------------------
# Prebuilt CPU ONNX Runtime (MIT, Microsoft) vendored under
# third_party/onnxruntime.  Only the API headers + license are committed;
# the ~11 MB DLL + import lib live in lib/ (git-ignored, repopulated per
# third_party/onnxruntime/README.md).  Imported as a SHARED lib: link the
# import lib, ship onnxruntime.dll next to lyra.exe.  Used by
# src/dsp/deepfist/ to run the DeepFist CNN+CTC CW model (DeepFist is MIT,
# © Brent Crier — permissive inside Lyra's GPLv3).
set(ORT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/onnxruntime)
add_library(onnxruntime SHARED IMPORTED)
set_target_properties(onnxruntime PROPERTIES
    IMPORTED_IMPLIB               ${ORT_DIR}/lib/onnxruntime.lib
    IMPORTED_LOCATION             ${ORT_DIR}/lib/onnxruntime.dll
    INTERFACE_INCLUDE_DIRECTORIES ${ORT_DIR}/include)

qt_standard_project_setup(REQUIRES 6.5)

qt_add_executable(lyra
    src/main.cpp
    src/hl2_discovery.h
    src/hl2_discovery.cpp
    src/hl2_stream.h
    src/hl2_stream.cpp
    src/wdsp_native.h
    src/wdsp_native.cpp
    src/wdsp_engine.h
    src/wdsp_engine.cpp
    src/mic_source.h
    src/mic_source.cpp
    # TX-rip Phase 1 step 1c-a — wire/ empty skeletons.  Populated
    # in Phase 2 per docs/TX_ARCHITECTURAL_MAPPING.md §3 / §5.4 / §8.
    src/wire/RadioNet.h
    src/wire/RadioNet.cpp
    src/wire/CMaster.h
    src/wire/CMaster.cpp
    # P0.d — cmbuffs.{h,c} DIRECT PORT (verbatim: `cmb, *CMB` twin
    # typedef, malloc0/_aligned_free, the CMB ring + cm_main pump
    # thread at the central producer/consumer hand-off between the
    # Inbound() producer and the cmdata consumer → xcmaster →
    # fexchange0 + xilv → wire).  Supersedes the Phase-C retrofit
    # and its operator-rejected deviations.
    src/wire/CmBuffs.h
    src/wire/CmBuffs.cpp
    # P1 — obbuffs.{h,c} DIRECT PORT (verbatim: `obb, *OBB` twin
    # typedef, file-scope obp four-alias bank, calloc/free 2014-era
    # idioms kept as the reference ships them).  The TX-OUT seam:
    # OutBound producer → ring → ob_main pump → sendOutbound
    # (DEFERRED to P2).  Dormant until P3 registers OutBound and a
    # caller creates the rings.
    src/wire/ObBuffs.h
    src/wire/ObBuffs.cpp
    # P2.c — network.c PARTIAL DIRECT PORT (sendOutbound verbatim,
    # network.c:1237-1341): the consumer-side hand-off ob_main calls
    # — P1-branch outLRbufp/outIQbufp memcpy + semaphore handshake
    # LIVE; ETH branch DEFERRED (Protocol 2/ANAN = v0.4).  Dormant
    # until P3/P4.
    src/wire/Network.cpp
    # P4.a — networkproto1.c PARTIAL DIRECT PORT
    # (sendProtocol1Samples verbatim, networkproto1.c:1204-1267):
    # the EP2 writer thread.  DORMANT until P4.b starts it.
    src/wire/NetworkProto1.cpp
    # P0.d — cmsetup.{h,c} DIRECT PORT (verbatim: the five cmMAX*
    # sizing macros, the stream/channel-id helpers rxid/txid/sp0id/
    # stype/chid/inid/mixinid/getbuffsize, SetRadioStructure +
    # set_cmdefault_rates configuration surface).
    src/wire/cmsetup.h
    src/wire/cmsetup.cpp
    # P0.a — WDSP call table: the single operator-approved linkage
    # seam for the ChannelMaster direct port (function pointers
    # carrying the WDSP exports' exact names, resolved once from the
    # loaded wdsp.dll; includes the full PureSignal entry-point
    # family).  See src/wire/wdspcalls.h preamble for the seam
    # rationale + per-entry signature citations.
    src/wire/wdspcalls.h
    src/wire/wdspcalls.cpp
    # P0.b — ChannelMaster family common header (reference cmcomm.h
    # surface: complex typedef + PORT + malloc0 ported verbatim from
    # wdsp/utilities.c:37-43, which is NOT a DLL export).  Grows to
    # the full reference cmcomm.h umbrella in P0.d.
    src/wire/cmcomm.h
    src/wire/cmcomm.cpp
    # P0.b — ilv.{h,c} DIRECT PORT (verbatim: `ilv, *ILV` twin
    # typedef, raw Outbound fn ptr, pcm->xmtr[].pilv setters,
    # malloc0/_aligned_free).  Supersedes the retired src/wdsp/ILV
    # retrofit and its operator-rejected deviations.
    src/wire/ILV.h
    src/wire/ILV.cpp
    # txgain.{h,c} DIRECT PORT (NR0V/ChannelMaster) — the TX-output
    # fixed-gain + amp-protect block for PS-safe HL2 power control.
    src/wire/TxGain.cpp
    # P0.c — aamix.{h,c} DIRECT PORT (verbatim: `aamix, *AAMIX` twin
    # typedef, raw Outbound fn ptr, paamix[] bank private to the .cpp,
    # RESAMPLE public-ABI typedef at wire/resample.h).  Supersedes the
    # retired src/wdsp/AAMix retrofit + src/wdsp/Resample.h.
    src/wire/resample.h
    src/wire/AAMix.h
    src/wire/AAMix.cpp
    # #158 Stage 1 — ivac.{h,c} DIRECT PORT (verbatim: `ivac, *IVAC`
    # twin typedef, file-static pvac[] bank in the .cpp, the two
    # rmatchV rings via the wire/wdspcalls.h seam + the ported AAMix
    # instance, full SetIVAC* surface).  Wire-INERT: the engine +
    # setters compile but nothing calls them yet (no Qt audio, no
    # radio wiring — Stage 2+).  PortAudio device I/O + GetIVACControlFlag
    # deferred; see src/wire/Ivac.h preamble + IVAC_PORT_PLAN.md.
    src/wire/Ivac.h
    src/wire/Ivac.cpp
    src/wire/RbpFilter.h
    src/wire/RbpFilter.cpp
    src/wire/FrameComposer.h
    src/wire/FrameComposer.cpp
    src/wire/Ep6RecvThread.h
    src/wire/Ep6RecvThread.cpp
    src/wire/Router.h
    src/wire/Router.cpp
    src/wire/MetisFrame.h
    src/wire/MetisFrame.cpp
    src/wire/ForceCandC.h
    src/wire/ForceCandC.cpp
    # TX-rip Phase 1 step 1c-b — wdsp/ empty skeletons.  Populated
    # in Phase 2 per docs/TX_ARCHITECTURAL_MAPPING.md §6.
    # (src/wdsp/TxChannel.{h,cpp} RETIRED 2026-06-12 — the P0.d
    # verbatim create_xmtr in src/wire/CMaster.cpp opens the WDSP
    # TXA channel itself through the wire/wdspcalls.h seam; the
    # runtime-DLL RAII carve-out the class existed for is gone.)
    src/wdsp/TxaCffi.h
    src/wdsp/ChannelLifecycle.h
    # (Stage B src/wdsp/AAMix.{h,cpp} + src/wdsp/Resample.h RETIRED
    # 2026-06-11 — replaced by the P0.c verbatim direct port at
    # src/wire/AAMix.{h,cpp} + src/wire/resample.h above.)
    # (Stage C src/wdsp/ILV.{h,cpp} RETIRED 2026-06-11 — replaced by
    # the P0.b verbatim direct port at src/wire/ILV.{h,cpp} above.)
    # TX-rip Phase 1 step 1c-c — dsp/ empty skeletons.  Populated
    # in Phase 2 per docs/TX_ARCHITECTURAL_MAPPING.md §3.
    src/dsp/StreamThread.h
    src/dsp/StreamThread.cpp
    src/dsp/StreamRing.h
    src/dsp/StreamRegistry.h
    src/dsp/StreamRegistry.cpp
    # #50 / #59 — native parametric EQ (RBJ biquad cascade; Lyra-native,
    # not a WDSP port).  Pre-WDSP-TXA mic rack stage (TX) + post-RXA (RX).
    src/dsp/ParamEq.h
    src/dsp/ParamEq.cpp
    # #50 Stage-2b — live mic-spectrum analyzer behind the EQ curve.
    src/dsp/EqAnalyzer.h
    src/dsp/EqAnalyzer.cpp
    # #88 — native TX speech pre-processor (Auto-AGC leveller + De-esser),
    # a pre-EQ rack stage.
    src/dsp/SpeechProcessor.h
    src/dsp/SpeechProcessor.cpp
    # #51 — native 5-band Combinator (multiband comp + SBC).  Wire-inert
    # until the model/panel/wire stages land.
    src/dsp/Combinator.h
    src/dsp/Combinator.cpp
    # #52 — native Plate reverb (Schroeder-Moorer).  Wire-inert until the
    # model/panel/wire stages land.
    src/dsp/Plate.h
    src/dsp/Plate.cpp
    # #175 — TX Waterfall callsign Auto-ID raster generator (increment 1).
    # Pure text→audio; wire-inert until the keyed-burst orchestration lands.
    src/dsp/WaterfallId.h
    src/dsp/WaterfallId.cpp
    # #173 CW-5a — native real-time CW decoder (IQ + AFC + matched filter +
    # adaptive floor + Bayesian dit/dah/Farnsworth classifier).  CW-mode-gated
    # RX tap; Lyra-native DSP, no Qt in the core.
    src/dsp/CwDecoder.h
    src/dsp/CwDecoder.cpp
    src/dsp/cw_fldigi/gfft.h
    src/dsp/cw_fldigi/fldigi_cw.h
    src/dsp/cw_fldigi/fldigi_cw.cpp
    src/dsp/FreqCalMeasure.h
    src/dsp/FreqCalMeasure.cpp
    # DeepFist neural CW decoder (second, selectable CW engine)
    src/dsp/deepfist/DeepFistResampler.h
    src/dsp/deepfist/DeepFistResampler.cpp
    src/dsp/deepfist/DeepFistSpectrogram.h
    src/dsp/deepfist/DeepFistSpectrogram.cpp
    src/dsp/deepfist/DeepFistConditioner.h
    src/dsp/deepfist/DeepFistConditioner.cpp
    src/dsp/deepfist/DeepFistCtc.h
    src/dsp/deepfist/DeepFistCtc.cpp
    src/dsp/deepfist/DeepFistScp.h
    src/dsp/deepfist/DeepFistScp.cpp
    src/dsp/deepfist/DeepFistRescore.h
    src/dsp/deepfist/DeepFistRescore.cpp
    src/dsp/deepfist/DeepFistModel.h
    src/dsp/deepfist/DeepFistModel.cpp
    src/dsp/deepfist/NeuralCwDecoder.h
    src/dsp/deepfist/NeuralCwDecoder.cpp
    src/dsp/ZeroBeat.h
    src/dsp/ZeroBeat.cpp
    # (Stage 7.1 TxIqRing RETIRED 2026-06-09 — wrong-architecture
    # post-TXA placement per audit Agent F; replaced by the Thetis-
    # faithful pre-TXA CMB ring + cm_main pump thread at src/wire/
    # CmBuffs.{h,cpp} — see Phase C commit message.  Files deleted
    # from tree.)
    # TX-rip Phase 1 step 1c-d — tx/ empty skeletons.  Populated
    # in Phase 2 per docs/TX_ARCHITECTURAL_MAPPING.md §5.
    src/tx/PttFsm.h
    src/tx/PttFsm.cpp
    src/tx/KeydownSequence.h
    src/tx/KeydownSequence.cpp
    src/tx/KeyupSequence.h
    src/tx/KeyupSequence.cpp
    src/tx/AttOnTxPolicy.h
    src/tx/AttOnTxPolicy.cpp
    src/tx/TrSequencing.h
    src/tx/MoxEdgeFade.h
    src/tx/MoxEdgeFade.cpp
    # #105 CW-3a — host CW morse engine + keyer pump (FSM wiring follows).
    src/tx/CwMorse.h
    src/tx/CwMorse.cpp
    src/tx/CwKeyer.h
    src/tx/CwKeyer.cpp
    # #91 VOX — pure voice-operated-TX decision gate (threshold + open-
    # delay + hang + anti-VOX); HL2Stream owns the taps + PttSource::Vox.
    src/tx/VoxDetector.h
    src/tx/VoxDetector.cpp
    src/tx/ClipRecorderPlayer.h
    src/tx/ClipRecorderPlayer.cpp
    src/tx/WavIo.h
    src/tx/WavIo.cpp
    src/tx/ClipBank.h
    src/tx/ClipBank.cpp
    src/tx/VoiceKeyer.h
    src/tx/VoiceKeyer.cpp
    src/tx/ClipRecorder.h
    src/tx/ClipRecorder.cpp
    # Session Recorder (#201) — RX/TX audio + timed pan/waterfall snapshots.
    # Headless engine (RT-safe ring + writer thread) + streaming WAV writer;
    # UI panel/status-chip + Settings tab + offline MP4 export land on top.
    src/recorder/WavStreamWriter.h
    src/recorder/WavStreamWriter.cpp
    src/recorder/RecorderEngine.h
    src/recorder/RecorderEngine.cpp
    src/recorder/SessionConverter.h
    src/recorder/SessionConverter.cpp
    # TX-rip Phase 1 step 1c-e — tci/ empty skeletons (4 NEW
    # components; TciServer.{h,cpp} stays at top level and is
    # rewritten in-place in Phase 2 per §10.2 / §7).  Populated
    # in Phase 2 per docs/TX_ARCHITECTURAL_MAPPING.md §7.
    src/tci/TciListener.h
    src/tci/TciListener.cpp
    src/tci/TciTxSupervisorThread.h
    src/tci/TciTxSupervisorThread.cpp
    src/tci/TciTxBridge.h
    src/tci/TciTxBridge.cpp
    src/tci/TciTxStereoMode.h
    # COM-port features (#171-adjacent) — serial PTT input + Kenwood CAT
    # serial server.  See docs/architecture/com_port_design.md.
    src/cat/SerialPtt.h
    src/cat/SerialPtt.cpp
    src/cat/SerialCwKey.h
    src/cat/SerialCwKey.cpp
    src/cat/CatServer.h
    src/cat/CatServer.cpp
    # TX/RX operator Profiles (Stage 0 — model/store/manager; UI wires
    # in next).  See docs/architecture/PROFILE_MODEL_STAGE0_DESIGN.md.
    src/profile/Profile.h
    src/profile/Profile.cpp
    src/profile/ProfileBindings.h
    src/profile/ProfileStore.h
    src/profile/ProfileStore.cpp
    src/profile/ProfileManager.h
    src/profile/ProfileManager.cpp
    src/profile/CompanionLauncher.h
    src/profile/CompanionLauncher.cpp
    src/profileui.h
    src/profileui.cpp
    # #158 DL-5 — src/ivac_audio.{h,cpp} (the Qt two-stream device layer)
    # RETIRED: the device layer is the reference's single full-duplex
    # PortAudio stream in wire/Ivac (DL-2); enumeration is PortAudio (DL-3).
    # TX-rip Phase 1 step 1c-f — ps/ empty skeletons (Rule 10:
    # present from v0.2 but empty/inert; v0.3 PureSignal fills).
    # Populated in Phase 2 per docs/TX_ARCHITECTURAL_MAPPING.md §9.
    src/ps/CalcCffi.h
    src/ps/IqcCffi.h
    src/ps/PsCalcThread.h
    src/ps/PsCalcThread.cpp
    src/ps/PsFsm.h
    src/ps/PsFsm.cpp
    src/ps/IqcLifecycle.h
    src/ps/IqcLifecycle.cpp
    src/iqstft.h
    src/iqstft.cpp
    src/capturedprofile.h
    src/capturedprofile.cpp
    src/noisereducer.h
    src/noisereducer.cpp
    src/panadapter.h
    src/panadapter.cpp
    src/waterfall.h
    src/waterfall.cpp
    src/freqdisplay.h
    src/freqdisplay.cpp
    src/autoscale.h
    src/mainwindow.h
    src/mainwindow.cpp
    src/dockdragcontroller.h
    src/dockdragcontroller.cpp
    src/panelrack.h
    src/panelrack.cpp
    src/prefs.h
    src/prefs.cpp
    src/settingsdialog.h
    src/settingsdialog.cpp
    src/WaterfallIdController.h
    src/WaterfallIdController.cpp
    src/CwMacroModel.h
    src/CwMacroModel.cpp
    src/palettes.h
    src/palettes.cpp
    src/bands.h
    src/bands.cpp
    src/oc/OcControl.h
    src/oc/OcControl.cpp
    src/bandplan.h
    src/bandplan.cpp
    src/statusbus.h
    src/timesync.h
    src/timesync.cpp
    src/bandmemory.h
    src/bandmemory.cpp
    src/backup.h
    src/backup.cpp
    src/genslots.h
    src/genslots.cpp
    src/time_stations.h
    src/time_stations.cpp
    src/memorystore.h
    src/memorystore.cpp
    src/eibistore.h
    src/eibistore.cpp
    src/tci_server.h
    src/tci_server.cpp
    src/spotstore.h
    src/spotstore.cpp
    src/spothole_feeder.h
    src/spothole_feeder.cpp
    src/dxcluster_feeder.h
    src/dxcluster_feeder.cpp
    src/dxcc.h
    src/dxcc.cpp
    src/metermodel.h
    src/metermodel.cpp
    # Tuner panel — native manual-ATU tuning memory (Tuner Reminder, built in).
    src/tunermemory.h
    src/tunermemory.cpp
    # #50 / #59 — parametric-EQ model (bridges dsp/ParamEq to EqPanel.qml).
    src/eqmodel.h
    src/eqmodel.cpp
    # #88 — speech-rack model (bridges dsp/SpeechProcessor to SpeechPanel.qml).
    src/speechmodel.h
    src/speechmodel.cpp
    # #51 — combinator model (bridges dsp/Combinator to CombinatorPanel.qml).
    src/combinatormodel.h
    src/combinatormodel.cpp
    # #52 — plate model (bridges dsp/Plate to PlatePanel.qml).
    src/platemodel.h
    src/platemodel.cpp
    src/usb_bcd.h
    src/usb_bcd.cpp
    src/help.h
    src/help.cpp
    src/helpdialog.h
    src/helpdialog.cpp
    src/logbuffer.h
    src/logbuffer.cpp
    src/logdialog.h
    src/logdialog.cpp
    src/updatechecker.h
    src/updatechecker.cpp
    src/grid.h
    src/grid.cpp
    src/wxservice.h
    src/wxservice.cpp
    src/wxindicator.h
    src/wxindicator.cpp
    src/solarservice.h
    src/solarservice.cpp
    src/solarpanel.h
    src/solarpanel.cpp
    src/ncdxffollow.h
    src/ncdxffollow.cpp
)

# QML module — keeps Main.qml inside the binary as a Qt resource so
# we don't have to ship loose .qml files alongside the .exe.
qt_add_qml_module(lyra
    URI Lyra
    VERSION 1.0
    QML_FILES
        src/qml/LyraSlider.qml
        src/qml/LyraSpinBox.qml
        src/qml/LyraComboBox.qml
        src/qml/PanadapterPanel.qml
        src/qml/TuningPanel.qml
        src/qml/AudioPanel.qml
        src/qml/TxPanel.qml
        src/qml/EqPanel.qml
        src/qml/RxEqPanel.qml
        src/qml/SpeechPanel.qml
        src/qml/CwConsolePanel.qml
        src/qml/CwDecoderPanel.qml
        src/qml/CalibrationPanel.qml
        src/qml/VoiceKeyerPanel.qml
        src/qml/RecorderPanel.qml
        src/qml/TrashCan.qml
        src/qml/CombinatorPanel.qml
        src/qml/PlatePanel.qml
        src/qml/ProfilePanel.qml
        src/qml/DisplayPanel.qml
        src/qml/BandPanel.qml
        src/qml/ModeFilterPanel.qml
        src/qml/MeterPanel.qml
        src/qml/TunerPanel.qml
        src/qml/HorizonArc.qml
        src/qml/PlasmaBar.qml
        src/qml/VerticalLadder.qml
    RESOURCES
        src/assets/watermarks/lyra-watermark.jpg
        src/assets/logo/lyra-icon-128.png
        src/assets/logo/lyra-icon-256.png
)

# In-app User Guide: bundle the markdown so the HelpDialog can load it
# at ":/help/USER_GUIDE.md" (rendered in a QTextBrowser).  This is the
# user-facing help the per-panel "?" badges open.
qt_add_resources(lyra "lyra_help"
    PREFIX "/help"
    BASE "${CMAKE_CURRENT_SOURCE_DIR}/docs/help"
    FILES
        docs/help/USER_GUIDE.md
)

# DXCC prefix→ISO table (pre-baked from cty.dat) for labelling TCI spots
# with their country, e.g. "US WE7CAT".  Loaded at ":/data/dxcc_prefix.csv".
qt_add_resources(lyra "lyra_data"
    PREFIX "/data"
    BASE "${CMAKE_CURRENT_SOURCE_DIR}/data"
    FILES
        data/dxcc_prefix.csv
)

# Panadapter shaders: qsb-compiles GLSL → SPIR-V/MSL/HLSL/GLSL and
# embeds the .qsb in the binary at ":/shaders/<name>.qsb".  QML uses
# them as layer.effect ShaderEffects (glow/glass).  Cross-platform by
# construction (one GLSL source, all RHI backends).
qt_add_shaders(lyra "lyra_panadapter_shaders"
    PREFIX "/shaders"
    BASE "${CMAKE_CURRENT_SOURCE_DIR}/src/shaders"
    FILES
        src/shaders/glow.frag
        src/shaders/watermark.frag
)

target_link_libraries(lyra PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Quick
    Qt6::Network
    Qt6::Multimedia
    Qt6::Widgets
    Qt6::QuickWidgets
    Qt6::WebSockets
    Qt6::SerialPort
    # #158 DL-0 — vendored static PortAudio (full-duplex IVAC device
    # layer).  Carries its own PUBLIC include dir (portaudio.h) + the
    # Windows host-API system libs (winmm/ole32/uuid/setupapi) as
    # transitive static-link deps.
    portaudio
    # DeepFist neural CW decoder — ONNX Runtime (prebuilt CPU, MIT).
    onnxruntime
)

# TX-rip Phase 1 step 1c — put `src/` on the include path so the
# new namespace subdirs (src/wire/, src/wdsp/, src/dsp/, src/tx/,
# src/tci/, src/ps/) are reachable as `#include "wire/X.h"` etc.
# from anywhere in the tree.  Same-dir relative includes inside
# each subdir continue to work because CMake auto-adds the
# current source file's directory.
target_include_directories(lyra PRIVATE src)

# Single source of truth for the app version + release repo: the
# project(... VERSION ...) above flows to code as LYRA_VERSION, and the
# GitHub repo coordinates as LYRA_REPO_OWNER/LYRA_REPO_NAME.  The About
# dialog and the update checker both read these (no hardcoded strings).
target_compile_definitions(lyra PRIVATE
    LYRA_VERSION="${PROJECT_VERSION}"
    LYRA_REPO_OWNER="N8SDR1"
    LYRA_REPO_NAME="Lyra-SDR-cpp"
)

# Embed the application icon into lyra.exe (Explorer / taskbar / shortcut
# icon) via a Windows resource script.
if(WIN32)
    target_sources(lyra PRIVATE src/lyra.rc)
endif()

# Step 2b: HL2Stream uses raw WinSock2 (sendto/recvfrom from
# dedicated OS threads -- one shared native socket) and Win32
# CreateWaitableTimerExW (HIGH_RESOLUTION 380 Hz EP2 keepalive
# cadence).  Link ws2_32 for WinSock2.  CreateWaitableTimerExW is
# in kernel32 which the MSVC toolchain links by default.
if(WIN32)
    # ws2_32:   WinSock2.
    # winmm:    timeBeginPeriod (1 ms scheduler tick for the EP2
    #           keepalive cadence when no audio is flowing).
    # iphlpapi: GetUdpStatisticsEx — Task #48 diagnostic, snapshot
    #           system-wide UDP RX counters at stream start/close
    #           so we can tell whether EP6 seq-error drops happen
    #           in the kernel UDP buffer or upstream (NIC ring).
    target_link_libraries(lyra PRIVATE ws2_32 winmm iphlpapi)
endif()

# Step 3a: bundle the WDSP DSP engine DLLs (`wdsp.dll` +
# `libfftw3-3.dll` + `libfftw3f-3.dll` + `rnnoise.dll` +
# `specbleach.dll`) into a `_native/` subdirectory next to
# `lyra.exe` at build time.  WdspNative::load() resolves them
# from that path via AddDllDirectory + LoadLibraryExW.
#
# Provenance: DLLs originate from the upstream WDSP source
# (Warren Pratt NR0V, GPL v3+ -- link-compatible with Lyra's
# GPL v3+ license).  Bundled as binaries because the Python
# tree's _native/ already has the matching set (MSVC-built,
# x64) and re-building from source is an orthogonal task --
# CLAUDE.md §14 documents the toolchain.
# `copy_directory` auto-creates the destination directory + copies
# all files in one step.  Doing per-file `copy_if_different` calls
# fails on first build because the `_native/` dest dir doesn't
# exist yet (CMake's per-file copy doesn't create parent dirs).
add_custom_command(TARGET lyra POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "$<TARGET_FILE_DIR:lyra>/_native"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/_native"
        "$<TARGET_FILE_DIR:lyra>/_native"
    COMMENT "Bundling WDSP DLLs into _native/"
    VERBATIM)

# DeepFist neural CW decoder — ship onnxruntime.dll next to lyra.exe (a
# load-time import, so it must sit beside the exe, not in _native/) and copy
# the trained model into models/ next to the exe.  The model is git-ignored;
# if it isn't present locally the copy is skipped and the neural engine simply
# reports "model not found" at runtime (classic decoder unaffected).
if(WIN32)
    add_custom_command(TARGET lyra POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${ORT_DIR}/lib/onnxruntime.dll" "$<TARGET_FILE_DIR:lyra>"
        COMMENT "Bundling onnxruntime.dll next to lyra.exe"
        VERBATIM)
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/models/deepfist.onnx")
    add_custom_command(TARGET lyra POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:lyra>/models"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/models/deepfist.onnx"
            "$<TARGET_FILE_DIR:lyra>/models/"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/models/deepfist.onnx.json"
            "$<TARGET_FILE_DIR:lyra>/models/"
        COMMENT "Bundling DeepFist model into models/"
        VERBATIM)
else()
    message(WARNING "DeepFist model not found at models/deepfist.onnx — the "
                    "neural CW decoder will be disabled at runtime. See models/README.md.")
endif()
# MASTER.SCP callsign DB for the CTC-lattice rescorer (optional; rescorer is a
# no-op without it, so no warning when absent).
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/models/MASTER.SCP")
    add_custom_command(TARGET lyra POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:lyra>/models"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/models/MASTER.SCP"
            "$<TARGET_FILE_DIR:lyra>/models/"
        COMMENT "Bundling MASTER.SCP into models/"
        VERBATIM)
endif()

# Auto-deploy the Qt runtime DLLs next to lyra.exe after every build.
# Without this, adding/changing Qt modules (e.g. Widgets +
# QuickWidgets for the dock shell) leaves the .exe unable to find a
# fresh DLL at launch -> the Windows "code execution cannot proceed
# because Qt6XXX.dll was not found" dialog + a black window.  Running
# windeployqt on POST_BUILD keeps the build dir self-contained and
# launchable by double-click.  windeployqt skips DLLs already present,
# so incremental builds stay fast.  Derive its path from Qt6::qmake
# so it tracks whatever Qt kit CMake configured against.
if(WIN32)
    get_target_property(_qmake_exe Qt6::qmake IMPORTED_LOCATION)
    get_filename_component(_qt_bin_dir "${_qmake_exe}" DIRECTORY)
    find_program(WINDEPLOYQT_EXE windeployqt HINTS "${_qt_bin_dir}")
    if(WINDEPLOYQT_EXE)
        add_custom_command(TARGET lyra POST_BUILD
            COMMAND "${WINDEPLOYQT_EXE}"
                --no-translations --no-compiler-runtime
                --qmldir "${CMAKE_CURRENT_SOURCE_DIR}/src/qml"
                "$<TARGET_FILE:lyra>"
            COMMENT "windeployqt: bundling Qt runtime DLLs next to lyra.exe"
            VERBATIM)
    endif()
endif()

# MSVC: enable /utf-8 (sane source encoding) and the standards-
# conformant preprocessor (/Zc:preprocessor) — both required for
# clean C++23 + Qt headers on MSVC v143/v144.
if(MSVC)
    target_compile_options(lyra PRIVATE /utf-8 /Zc:preprocessor /W4 /permissive-)
    # GUI-subsystem build: no console window pops up alongside the app on
    # release/full-build installs.  Qt diagnostic output (QML errors,
    # qWarning, qFatal) is captured by LogBuffer into the in-app Log
    # viewer (Help → Show Log…) + a rolling app-data log file instead of
    # the parent console — turn on Settings → Hardware → "verbose debug
    # logging" to capture the full detail.
    set_target_properties(lyra PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

# Install / deploy hint (used later by windeployqt to bundle Qt DLLs
# next to the .exe so the binary runs outside Qt Creator / VS).
install(TARGETS lyra
    BUNDLE  DESTINATION .
    RUNTIME DESTINATION .
)

# --- Unit tests -----------------------------------------------------
# Behavioural test for the Stage-0 profile engine (Profile / ProfileStore
# / ProfileManager).  EXCLUDE_FROM_ALL so the normal `lyra` build is
# untouched; built+run on demand via _build_test_profile.bat (which does
# `cmake --build build --target test_profile`).  Qt Core only — the
# profile units need QObject/QSettings/QJson + moc, so the Qt-free
# scratch-bat pattern won't fit; CMAKE_AUTOMOC (set above) handles the
# ProfileManager Q_OBJECT.
add_executable(test_profile EXCLUDE_FROM_ALL
    scratch/test_profile.cpp
    src/profile/Profile.h
    src/profile/Profile.cpp
    src/profile/ProfileBindings.h
    src/profile/ProfileStore.h
    src/profile/ProfileStore.cpp
    src/profile/ProfileManager.h
    src/profile/ProfileManager.cpp
)
target_include_directories(test_profile PRIVATE src)
target_link_libraries(test_profile PRIVATE Qt6::Core)
if(MSVC)
    target_compile_options(test_profile PRIVATE /utf-8 /Zc:preprocessor)
endif()

# #199 OC Control Stage 1 — pure compute() + change-gate unit test.
# EXCLUDE_FROM_ALL; `cmake --build build --target test_oc_control`.  Links
# OcControl (Q_OBJECT, AUTOMOC) + bands.cpp (amateurBands() band count) +
# Qt6::Core.  Wire-inert — no HL2Stream, no event loop.
add_executable(test_oc_control EXCLUDE_FROM_ALL
    scratch/test_oc_control.cpp
    src/oc/OcControl.h
    src/oc/OcControl.cpp
    src/bands.h
    src/bands.cpp
)
target_include_directories(test_oc_control PRIVATE src)
target_link_libraries(test_oc_control PRIVATE Qt6::Core)
if(MSVC)
    target_compile_options(test_oc_control PRIVATE /utf-8 /Zc:preprocessor)
endif()

# #158 Stage 1 — IVAC direct-port engine unit test (verbatim
# ChannelMaster/ivac.c engine + setters; wire-INERT).  EXCLUDE_FROM_ALL
# so the normal `lyra` build is untouched; built+run on demand via
# _build_test_ivac.bat (`cmake --build build --target test_ivac`).  The
# test defines its own cm/pcm globals (so CMaster.cpp is NOT linked) and
# LoadLibrary's wdsp.dll + resolve_wdsp_calls() before create_ivac, so
# it links the same wire/ TUs the engine needs (Ivac + AAMix + cmcomm +
# cmsetup + wdspcalls).  Qt Core only — wire/wdspcalls.cpp uses qWarning.
add_executable(test_ivac EXCLUDE_FROM_ALL
    scratch/test_ivac.cpp
    src/wire/Ivac.h
    src/wire/Ivac.cpp
    src/wire/AAMix.h
    src/wire/AAMix.cpp
    src/wire/cmcomm.h
    src/wire/cmcomm.cpp
    src/wire/cmsetup.h
    src/wire/cmsetup.cpp
    src/wire/wdspcalls.h
    src/wire/wdspcalls.cpp
)
target_include_directories(test_ivac PRIVATE src)
target_link_libraries(test_ivac PRIVATE Qt6::Core)
if(MSVC)
    target_compile_options(test_ivac PRIVATE /utf-8 /Zc:preprocessor)
endif()

# #89 Stage A — voice-keyer injector core unit test (ClipRecorderPlayer).
# Qt-free / DSP-free by design; EXCLUDE_FROM_ALL so the normal `lyra` build is
# untouched.  Build + run:  cmake --build build --target test_clip_player
add_executable(test_clip_player EXCLUDE_FROM_ALL
    scratch/test_clip_player.cpp
    src/tx/ClipRecorderPlayer.h
    src/tx/ClipRecorderPlayer.cpp
)
target_include_directories(test_clip_player PRIVATE src)
if(MSVC)
    target_compile_options(test_clip_player PRIVATE /utf-8 /Zc:preprocessor)
endif()

# #89 Stage B — WAV I/O unit test (WavIo).  Qt-free; EXCLUDE_FROM_ALL.
# Build + run:  cmake --build build --target test_wav_io
add_executable(test_wav_io EXCLUDE_FROM_ALL
    scratch/test_wav_io.cpp
    src/tx/WavIo.h
    src/tx/WavIo.cpp
)
target_include_directories(test_wav_io PRIVATE src)
if(MSVC)
    target_compile_options(test_wav_io PRIVATE /utf-8 /Zc:preprocessor)
endif()

# Session Recorder (#201) Stage 1 — engine + streaming WAV writer unit test.
# Qt Core only (QSettings isolated to a temp INI); EXCLUDE_FROM_ALL.
# Build + run:  cmake --build build --target test_recorder
add_executable(test_recorder EXCLUDE_FROM_ALL
    scratch/test_recorder.cpp
    src/recorder/WavStreamWriter.h
    src/recorder/WavStreamWriter.cpp
    src/recorder/RecorderEngine.h
    src/recorder/RecorderEngine.cpp
)
target_include_directories(test_recorder PRIVATE src)
target_link_libraries(test_recorder PRIVATE Qt6::Core)
set_target_properties(test_recorder PROPERTIES AUTOMOC ON)   # RecorderEngine Q_OBJECT
if(MSVC)
    target_compile_options(test_recorder PRIVATE /utf-8 /Zc:preprocessor)
endif()

# #89 Stage C — recorder capture-core unit test (ClipRecorder).  Qt-free;
# EXCLUDE_FROM_ALL.  Build + run:  cmake --build build --target test_clip_recorder
add_executable(test_clip_recorder EXCLUDE_FROM_ALL
    scratch/test_clip_recorder.cpp
    src/tx/ClipRecorder.h
    src/tx/ClipRecorder.cpp
)
target_include_directories(test_clip_recorder PRIVATE src)
if(MSVC)
    target_compile_options(test_clip_recorder PRIVATE /utf-8 /Zc:preprocessor)
endif()

# #89 Stage B (B3) — clip bank + persistence unit test (ClipBank).  Qt-based
# (QSettings + QStandardPaths, isolated via test mode); EXCLUDE_FROM_ALL.
# Build + run:  cmake --build build --target test_clip_bank
add_executable(test_clip_bank EXCLUDE_FROM_ALL
    scratch/test_clip_bank.cpp
    src/tx/ClipBank.h
    src/tx/ClipBank.cpp
    src/tx/WavIo.h
    src/tx/WavIo.cpp
)
target_include_directories(test_clip_bank PRIVATE src)
target_link_libraries(test_clip_bank PRIVATE Qt6::Core)
set_target_properties(test_clip_bank PROPERTIES AUTOMOC ON)
if(MSVC)
    target_compile_options(test_clip_bank PRIVATE /utf-8 /Zc:preprocessor)
endif()

# #158 DL-5 — the test_ivac_audio target retired with src/ivac_audio.{h,cpp}
# (the Qt two-stream device layer it exercised).  The device layer is now the
# reference's single full-duplex PortAudio stream (wire/Ivac, DL-1/DL-2); the
# DL-0 scratch/test_portaudio + the operator HL2 bench cover it.

# #158 DL-0 — PortAudio link/init smoke for the device-layer rebuild.
# EXCLUDE_FROM_ALL; built on demand via
# `cmake --build build --target test_portaudio`.  Proves the vendored
# static PortAudio (third_party/portaudio) compiles + links + its header
# resolves + it initialises on this host, BEFORE any IVAC device-I/O port
# (DL-1).  No Qt, no Lyra TUs — just the PortAudio surface.
add_executable(test_portaudio EXCLUDE_FROM_ALL scratch/test_portaudio.cpp)
target_link_libraries(test_portaudio PRIVATE portaudio)
if(MSVC)
    target_compile_options(test_portaudio PRIVATE /utf-8 /Zc:preprocessor)
endif()

# #50 / #59 — native parametric EQ engine unit test (Lyra-native RBJ
# biquad cascade; wire-INERT, no Qt — pure C++).  EXCLUDE_FROM_ALL; built
# on demand via `cmake --build build --target test_parameq`.  Verifies the
# cookbook math + that magnitudeDb() matches what process() applies.
add_executable(test_parameq EXCLUDE_FROM_ALL
    scratch/test_parameq.cpp
    src/dsp/ParamEq.h
    src/dsp/ParamEq.cpp
)
target_include_directories(test_parameq PRIVATE src)
if(MSVC)
    target_compile_options(test_parameq PRIVATE /utf-8 /Zc:preprocessor)
endif()

# #88 — native speech pre-processor unit test (Auto-AGC + De-esser).
# EXCLUDE_FROM_ALL; `cmake --build build --target test_speech`.  No Qt.
add_executable(test_speech EXCLUDE_FROM_ALL
    scratch/test_speech.cpp
    src/dsp/SpeechProcessor.h
    src/dsp/SpeechProcessor.cpp
)
target_include_directories(test_speech PRIVATE src)
if(MSVC)
    target_compile_options(test_speech PRIVATE /utf-8 /Zc:preprocessor)
endif()

# #51 — native Combinator unit test (crossover reconstruction + comp + SBC).
# EXCLUDE_FROM_ALL; `cmake --build build --target test_combinator`.  No Qt.
add_executable(test_combinator EXCLUDE_FROM_ALL
    scratch/test_combinator.cpp
    src/dsp/Combinator.h
    src/dsp/Combinator.cpp
)
target_include_directories(test_combinator PRIVATE src)
if(MSVC)
    target_compile_options(test_combinator PRIVATE /utf-8 /Zc:preprocessor)
endif()

# #52 — native Plate reverb unit test (decay tail + bypass + MIX).
# EXCLUDE_FROM_ALL; `cmake --build build --target test_plate`.  No Qt.
add_executable(test_plate EXCLUDE_FROM_ALL
    scratch/test_plate.cpp
    src/dsp/Plate.h
    src/dsp/Plate.cpp
)
target_include_directories(test_plate PRIVATE src)
if(MSVC)
    target_compile_options(test_plate PRIVATE /utf-8 /Zc:preprocessor)
endif()

# #173 CW-5a — native CW decoder unit test (synthetic CW → decode, reset,
# AFC lock).  EXCLUDE_FROM_ALL; `cmake --build build --target test_cw_decoder`.
add_executable(test_cw_decoder EXCLUDE_FROM_ALL
    scratch/test_cw_decoder.cpp
    src/dsp/CwDecoder.h
    src/dsp/CwDecoder.cpp
    src/dsp/cw_fldigi/gfft.h
    src/dsp/cw_fldigi/fldigi_cw.h
    src/dsp/cw_fldigi/fldigi_cw.cpp
)
target_include_directories(test_cw_decoder PRIVATE src)
if(MSVC)
    target_compile_options(test_cw_decoder PRIVATE /utf-8 /Zc:preprocessor)
endif()

# #49 Stage 1 — TX DSP rack save/load round-trip (the four model states the
# TX profile bundle captures).  Qt Core (the models are QObject).
add_executable(test_profile_rack EXCLUDE_FROM_ALL
    scratch/test_profile_rack.cpp
    src/eqmodel.h          src/eqmodel.cpp
    src/dsp/ParamEq.h      src/dsp/ParamEq.cpp
    src/dsp/EqAnalyzer.h   src/dsp/EqAnalyzer.cpp
    src/speechmodel.h      src/speechmodel.cpp
    src/dsp/SpeechProcessor.h src/dsp/SpeechProcessor.cpp
    src/combinatormodel.h  src/combinatormodel.cpp
    src/dsp/Combinator.h   src/dsp/Combinator.cpp
    src/platemodel.h       src/platemodel.cpp
    src/dsp/Plate.h        src/dsp/Plate.cpp
)
target_include_directories(test_profile_rack PRIVATE src)
target_link_libraries(test_profile_rack PRIVATE Qt6::Core)
set_target_properties(test_profile_rack PROPERTIES AUTOMOC ON)
if(MSVC)
    target_compile_options(test_profile_rack PRIVATE /utf-8 /Zc:preprocessor)
endif()

# DeepFist neural CW decoder — headless WAV decode verification harness.
# Proves the C++ front-end + ONNX decode matches the Python/Rust reference on
# known clips BEFORE any UI wiring.  Qt-free (the DeepFist DSP is Qt-free);
# links only ONNX Runtime.  EXCLUDE_FROM_ALL; build + run:
#   cmake --build build --target test_neural_cw
#   build/test_neural_cw <modelDir> <clip.wav> [expected text] ...
add_executable(test_neural_cw EXCLUDE_FROM_ALL
    scratch/test_neural_cw.cpp
    src/dsp/deepfist/DeepFistResampler.h
    src/dsp/deepfist/DeepFistResampler.cpp
    src/dsp/deepfist/DeepFistSpectrogram.h
    src/dsp/deepfist/DeepFistSpectrogram.cpp
    src/dsp/deepfist/DeepFistConditioner.h
    src/dsp/deepfist/DeepFistConditioner.cpp
    src/dsp/deepfist/DeepFistCtc.h
    src/dsp/deepfist/DeepFistCtc.cpp
    src/dsp/deepfist/DeepFistScp.h
    src/dsp/deepfist/DeepFistScp.cpp
    src/dsp/deepfist/DeepFistRescore.h
    src/dsp/deepfist/DeepFistRescore.cpp
    src/dsp/deepfist/DeepFistModel.h
    src/dsp/deepfist/DeepFistModel.cpp
    src/dsp/deepfist/NeuralCwDecoder.h
    src/dsp/deepfist/NeuralCwDecoder.cpp
)
target_include_directories(test_neural_cw PRIVATE src)
target_link_libraries(test_neural_cw PRIVATE onnxruntime)
if(MSVC)
    target_compile_options(test_neural_cw PRIVATE /utf-8 /Zc:preprocessor)
endif()
if(WIN32)
    add_custom_command(TARGET test_neural_cw POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${ORT_DIR}/lib/onnxruntime.dll" "$<TARGET_FILE_DIR:test_neural_cw>"
        VERBATIM)
endif()

# DeepFist — real-time STREAMING harness: plays a 48 kHz CW WAV through the
# actual NeuralCwDecoder (worker thread + frame-timed commit) and prints
# committed characters as they stream.  Deterministic test of the live decode
# path without a radio/GUI.  Build + run:
#   cmake --build build --target test_neural_cw_stream
#   build/test_neural_cw_stream <modelDir> <clip48k.wav>
add_executable(test_neural_cw_stream EXCLUDE_FROM_ALL
    scratch/test_neural_cw_stream.cpp
    src/dsp/deepfist/DeepFistResampler.cpp
    src/dsp/deepfist/DeepFistSpectrogram.cpp
    src/dsp/deepfist/DeepFistCtc.cpp
    src/dsp/deepfist/DeepFistModel.cpp
    src/dsp/deepfist/NeuralCwDecoder.cpp
)
target_include_directories(test_neural_cw_stream PRIVATE src)
target_link_libraries(test_neural_cw_stream PRIVATE onnxruntime)
if(MSVC)
    target_compile_options(test_neural_cw_stream PRIVATE /utf-8 /Zc:preprocessor)
endif()
if(WIN32)
    add_custom_command(TARGET test_neural_cw_stream POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${ORT_DIR}/lib/onnxruntime.dll" "$<TARGET_FILE_DIR:test_neural_cw_stream>"
        VERBATIM)
endif()

# DeepFist — CTC-lattice rescorer unit test (ctcNll vs PyTorch + candidate flip).
# Qt-free, no ONNX Runtime.  Build + run:
#   cmake --build build --target test_rescore
add_executable(test_rescore EXCLUDE_FROM_ALL
    scratch/test_rescore.cpp
    src/dsp/deepfist/DeepFistRescore.h
    src/dsp/deepfist/DeepFistRescore.cpp
    src/dsp/deepfist/DeepFistScp.h
    src/dsp/deepfist/DeepFistScp.cpp
)
target_include_directories(test_rescore PRIVATE src)
if(MSVC)
    target_compile_options(test_rescore PRIVATE /utf-8 /Zc:preprocessor)
endif()
