// Second-receiver spectrum. Shown only on a two-converter radio while
// the second receiver is on. Trace, palette, dB range, glow, fill, and
// the band-plan strip all follow Settings → Visuals (same Prefs as the
// main pan), including 11m/CB channel ticks when that band is enabled.
// The scale is the full converter rate, centred on that
// receiver's converter, so a click lands on the right dial.

import QtQuick
import QtQuick.Controls
import LyraUI

Item {
    id: root

    // Wheel direction for tuning: vertical scroll, or a sideways scroll /
    // tilt when there is no vertical part.  Right (and up) tunes up; a
    // zero-movement event gives 0 and is ignored.
    function wheelDir(wheel) {
        var y = wheel.angleDelta.y, x = wheel.angleDelta.x
        if (y !== 0) return y > 0 ? 1 : -1
        if (x !== 0) return x < 0 ? 1 : -1
        return 0
    }
    implicitWidth: 600
    implicitHeight: 180
    readonly property int lyraMinWidth: 320
    readonly property int lyraMinHeight: 180

    property int ddcHz: Stream.rx2FreqHz
    property int vfoBHz: Stream.vfoBHz
    property bool splitOn: Stream.splitEnabled
    readonly property int spanHz: Math.max(1, WdspEngine.iqRateHz)
    readonly property int carrierHz: splitOn
                                      ? vfoBHz
                                      : (ddcHz + WdspEngine.markerOffsetHzRx2)
    property real effMin: -130
    property real effMax: -20

    Component.onCompleted: {
        ddcHz = Stream.rx2FreqHz
        vfoBHz = Stream.vfoBHz
        splitOn = Stream.splitEnabled
        effMin = pan.effDbMin
        effMax = pan.effDbMax
    }
    Connections {
        target: Stream
        function onRx2FreqChanged() { root.ddcHz = Stream.rx2FreqHz }
        function onVfoBHzChanged() { root.vfoBHz = Stream.vfoBHz }
        function onSplitEnabledChanged() { root.splitOn = Stream.splitEnabled }
    }
    Connections {
        target: pan
        function onEffRangeChanged() {
            root.effMin = pan.effDbMin
            root.effMax = pan.effDbMax
        }
    }
    Connections {
        target: Prefs
        function onPeakClearRequested() { pan.clearPeaks() }
    }

    function fmtHz(hz) {
        var n = Math.round(hz)
        var s = Math.abs(n).toString()
        var out = ""
        for (var i = 0; i < s.length; ++i) {
            if (i > 0 && ((s.length - i) % 3) === 0)
                out += "."
            out += s.charAt(i)
        }
        return (n < 0 ? "-" : "") + out
    }
    function xOfHz(hz, w) {
        return w * (0.5 + (hz - root.ddcHz) / root.spanHz)
    }
    function hzAtX(x, w) {
        return root.ddcHz + (x / Math.max(1, w) - 0.5) * root.spanHz
    }
    function snapCarrier(c) {
        if (Prefs.panRound100)
            c = Math.round(c / 100) * 100
        else if (Prefs.panScrollStepHz > 1)
            c = Math.round(c / Prefs.panScrollStepHz) * Prefs.panScrollStepHz
        return c
    }
    function tuneAt(x, w) {
        if (w <= 1)
            return
        var c = Math.round(snapCarrier(hzAtX(x, w)))
        if (Stream.splitEnabled)
            Stream.setVfoBHz(c)
        else
            Stream.setRx2FreqHz(c - WdspEngine.markerOffsetHzRx2)
    }
    function setCarrierHz(freqHz) {
        var c = Math.round(freqHz)
        if (Stream.splitEnabled)
            Stream.setVfoBHz(c)
        else
            Stream.setRx2FreqHz(c - WdspEngine.markerOffsetHzRx2)
    }
    function tuneLandmark(freqHz, mode) {
        if (mode && mode.length)
            Prefs.modeRx2 = mode
        setCarrierHz(freqHz)
        Status.show(qsTr("RX2 tuned to ")
                    + (freqHz / 1.0e6).toFixed(3) + " MHz "
                    + (mode || "") , 2500)
    }

    component DbLabels : Repeater {
        id: rep
        property bool rightSide: false
        property Item area
        property real dMax: 0
        property real dMin: 0
        model: 9
        delegate: Text {
            required property int index
            readonly property real frac: index / 8.0
            x: rep.rightSide ? (rep.area.width - width - 5) : 5
            y: Math.max(0, Math.min(rep.area.height - height,
                                    frac * rep.area.height - height / 2))
            text: Math.round(rep.dMax - frac * (rep.dMax - rep.dMin))
            color: "#8fa6ba"
            font.pixelSize: 12
            font.family: "Consolas"
            style: Text.Outline
            styleColor: "#80000000"
        }
    }

    SplitView {
        id: splitV
        anchors.fill: parent
        orientation: Qt.Vertical

        Item {
            id: specHost
            SplitView.fillHeight: true
            SplitView.minimumHeight: 90

            Panadapter {
                id: pan
                anchors.fill: parent
                receiver: 2
                engine: WdspEngine
                gridLevel: Prefs.gridLevel
                glassSheen: Prefs.glassSheen
                targetFps: Prefs.targetFps
                autoScale: Prefs.rx2DbAuto
                dbMin: Prefs.rx2DbMin
                dbMax: Prefs.rx2DbMax
                traceMode: Prefs.traceMode
                traceColor: Prefs.traceColor
                palette: Prefs.palette
                strengthColor: Prefs.strengthColor
                fillEnabled: Prefs.fillEnabled
                fillColor: Prefs.fillColor
                smoothing: Prefs.smoothing
                peakEnabled: Prefs.peakEnabled
                peakHoldSecs: Prefs.peakHoldSecs
                peakDecayDbps: Prefs.peakDecayDbps
                peakStyle: Prefs.peakStyle
                peakColor: Prefs.peakColor
                peakShowDb: Prefs.peakShowDb
                layer.enabled: Prefs.glow > 0
                layer.effect: ShaderEffect {
                    fragmentShader: "qrc:/shaders/glow.frag.qsb"
                    property real glowStrength: Prefs.glow / 100.0 * 1.6
                    property real glowThreshold: 0.12
                    property real glowSpread: 2.0
                    property size texSize: Qt.size(width, height)
                }
            }

            DbLabels { rightSide: false; area: specHost
                       dMax: root.effMax; dMin: root.effMin }
            DbLabels { rightSide: true;  area: specHost
                       dMax: root.effMax; dMin: root.effMin }

            Repeater {
                model: 11
                delegate: Text {
                    required property int index
                    readonly property real frac: index / 10.0
                    readonly property real fhz:
                        root.ddcHz + (frac - 0.5) * root.spanHz
                    text: (fhz / 1.0e6).toFixed(3)
                    color: "#8fa6ba"
                    font.pixelSize: 11
                    font.family: "Consolas"
                    style: Text.Outline
                    styleColor: "#80000000"
                    x: Math.max(0, Math.min(specHost.width - width,
                                            frac * specHost.width - width / 2))
                    y: specHost.height - height - 2
                }
            }

            Item {
                id: passband
                anchors.fill: parent
                z: 4
                function xOf(offHz) {
                    return specHost.width * (0.5 + offHz / root.spanHz)
                }
                readonly property real loX: xOf(WdspEngine.passbandLowHzRx2)
                readonly property real hiX: xOf(WdspEngine.passbandHighHzRx2)
                readonly property real carrierX:
                    specHost.width * (0.5 + (root.carrierHz - root.ddcHz)
                                      / root.spanHz)
                Rectangle {
                    x: Math.min(passband.loX, passband.hiX)
                    width: Math.abs(passband.hiX - passband.loX)
                    y: 0; height: specHost.height
                    color: "#34c759"
                    opacity: 0.16
                }
                Rectangle {
                    x: Math.round(passband.carrierX)
                    y: 0
                    width: 2
                    height: specHost.height
                    color: "#ffaa50"
                    opacity: 0.85
                }
                Repeater {
                    model: 2
                    delegate: Item {
                        required property int index
                        readonly property bool isLo: index === 0
                        x: (isLo ? passband.loX : passband.hiX) - width / 2
                        y: 0; width: 9; height: specHost.height
                        Rectangle {
                            anchors.horizontalCenter: parent.horizontalCenter
                            y: 0; width: 2; height: parent.height
                            color: "#8fe08f"; opacity: 0.85
                        }
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.SizeHorCursor
                            preventStealing: true
                            property double lastMs: 0
                            onPositionChanged: (m) => {
                                var nowMs = Date.now()
                                if (nowMs - lastMs < 33) return
                                lastMs = nowMs
                                var px = mapToItem(specHost, m.x, m.y).x
                                var off = (px / Math.max(1, specHost.width)
                                           - 0.5) * root.spanHz
                                var mode = Prefs.modeRx2
                                var isSsbOrDig = (mode === "USB" || mode === "LSB"
                                               || mode === "DIGU" || mode === "DIGL")
                                if (isSsbOrDig) {
                                    var isLsb = (mode === "LSB" || mode === "DIGL")
                                    var isInnerEdge = isLsb ? !isLo : isLo
                                    if (isInnerEdge) {
                                        Prefs.filterLow =
                                            Math.max(0, Math.min(500, Math.round(Math.abs(off))))
                                    } else {
                                        Prefs.rx2Bandwidth =
                                            WdspEngine.bandwidthForModeEdge(mode, off)
                                    }
                                } else {
                                    Prefs.rx2Bandwidth =
                                        WdspEngine.bandwidthForModeEdge(mode, off)
                                }
                            }
                        }
                    }
                }
            }

            Item {
                id: bandPlan
                anchors.fill: parent
                z: 6
                function xOf(hz) { return root.xOfHz(hz, specHost.width) }
                readonly property var segs:
                    (Prefs.bandPlanSegments && BandPlan.region !== "NONE")
                    ? BandPlan.segments(root.ddcHz, root.spanHz) : []
                readonly property var edges:
                    (Prefs.bandPlanEdges && BandPlan.region !== "NONE")
                    ? BandPlan.edges(root.ddcHz, root.spanHz) : []
                readonly property var classEdges:
                    (Prefs.bandPlanClassEdges && BandPlan.region === "US")
                    ? BandPlan.classEdges(root.ddcHz, root.spanHz) : []
                readonly property var marks:
                    (BandPlan.region !== "NONE"
                     && (Prefs.bandPlanLandmarks || Prefs.bandPlanBeacons))
                    ? BandPlan.landmarks(root.ddcHz, root.spanHz,
                                         Prefs.bandPlanLandmarks,
                                         Prefs.bandPlanBeacons) : []
                readonly property var cbChans:
                    (Prefs.cbBandEnabled && BandPlan.region.length)
                    ? BandPlan.cbChannels(root.ddcHz, root.spanHz) : []

                Repeater {
                    model: bandPlan.segs
                    delegate: Item {
                        id: segItem
                        required property var modelData
                        readonly property real x0: bandPlan.xOf(modelData.lo)
                        readonly property real x1: bandPlan.xOf(modelData.hi)
                        readonly property bool chan: modelData.channel === true
                        Rectangle {
                            x: Math.min(segItem.x0, segItem.x1)
                            y: 0
                            width: Math.max(0, Math.abs(segItem.x1 - segItem.x0))
                            height: 10
                            color: segItem.modelData.color
                            opacity: 0.82
                        }
                        Text {
                            visible: segItem.chan
                                     || Math.abs(segItem.x1 - segItem.x0) >= 24
                            x: Math.min(segItem.x0, segItem.x1) + 3
                            y: 0
                            text: segItem.modelData.label
                            color: "#f0f0f0"
                            font.pixelSize: segItem.chan ? 11 : 9
                            font.bold: true
                            style: Text.Outline
                            styleColor: "#a0000000"
                        }
                        MouseArea {
                            visible: segItem.chan
                            enabled: segItem.chan
                            x: Math.min(segItem.x0, segItem.x1)
                            y: 0
                            width: Math.max(Math.abs(segItem.x1 - segItem.x0), 24)
                            height: 12
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.tuneLandmark(segItem.modelData.qsy, "USB")
                            ToolTip.visible: containsMouse && Prefs.tooltipsEnabled
                            ToolTip.text: segItem.modelData.label
                        }
                    }
                }
                Repeater {
                    model: bandPlan.edges
                    delegate: Item {
                        id: edgeItem
                        required property var modelData
                        readonly property real ex: bandPlan.xOf(modelData.freq)
                        Repeater {
                            model: Math.max(1, Math.ceil(specHost.height / 10))
                            delegate: Rectangle {
                                required property int index
                                x: edgeItem.ex - 1
                                y: index * 10
                                width: 2; height: 5
                                color: "#ff5050"
                                opacity: 0.85
                            }
                        }
                        Text {
                            x: Math.max(0, Math.min(specHost.width - width,
                                                    edgeItem.ex + 2))
                            y: specHost.height - height - 16
                            text: edgeItem.modelData.name + qsTr(" EDGE")
                            color: "#ff8080"
                            font.pixelSize: 9
                            font.bold: true
                            style: Text.Outline
                            styleColor: "#cc000000"
                        }
                    }
                }
                Repeater {
                    model: bandPlan.classEdges
                    delegate: Item {
                        id: classEdgeItem
                        required property var modelData
                        readonly property real cx: bandPlan.xOf(modelData.freq)
                        Repeater {
                            model: Math.max(1, Math.ceil(specHost.height / 10))
                            delegate: Rectangle {
                                required property int index
                                x: classEdgeItem.cx - 0.5
                                y: index * 10
                                width: 1; height: 4
                                color: "#ffb000"
                                opacity: 0.7
                            }
                        }
                        Text {
                            x: Math.max(0, Math.min(specHost.width - width,
                                                    classEdgeItem.cx + 2))
                            y: specHost.height - height - 30
                            text: classEdgeItem.modelData.label
                            color: "#ffc65a"
                            font.pixelSize: 9
                            font.bold: true
                            style: Text.Outline
                            styleColor: "#cc000000"
                        }
                    }
                }
                Repeater {
                    model: bandPlan.marks
                    delegate: Item {
                        id: markItem
                        required property var modelData
                        readonly property real mx: bandPlan.xOf(modelData.freq)
                        readonly property color col:
                            modelData.beacon ? "#78dcff" : "#ffd700"
                        Rectangle {
                            x: markItem.mx - 0.5; y: 10; width: 1; height: 9
                            color: markItem.col; opacity: 0.5
                        }
                        Text {
                            x: markItem.mx - width / 2; y: 8
                            text: "▼"; color: markItem.col; font.pixelSize: 10
                        }
                        Text {
                            x: markItem.mx + 6; y: 10
                            text: markItem.modelData.label
                            color: markItem.col
                            font.pixelSize: 9; font.bold: true
                            style: Text.Outline; styleColor: "#cc000000"
                        }
                        MouseArea {
                            x: markItem.mx - 9; y: 6; width: 18; height: 18
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.tuneLandmark(markItem.modelData.freq,
                                                         markItem.modelData.mode)
                            ToolTip.visible: containsMouse && Prefs.tooltipsEnabled
                            ToolTip.text: markItem.modelData.label + " · "
                                          + markItem.modelData.mode
                        }
                    }
                }

                Repeater {
                    model: bandPlan.cbChans
                    delegate: Item {
                        id: cbItem
                        required property var modelData
                        readonly property real cx: bandPlan.xOf(modelData.freq)
                        readonly property string md:
                            (modelData.mode && modelData.mode.length)
                            ? modelData.mode : "AM"
                        Rectangle {
                            x: cbItem.cx - 0.5; y: 14; width: 1; height: 9
                            color: "#b0c4d4"; opacity: 0.6
                        }
                        Text {
                            x: cbItem.cx - width / 2; y: 9
                            text: cbItem.modelData.label
                            color: "#b0c4d4"
                            font.pixelSize: 11; font.bold: true
                            style: Text.Outline; styleColor: "#cc000000"
                        }
                        Text {
                            visible: cbItem.modelData.note !== ""
                            x: cbItem.cx - width / 2; y: 22
                            text: cbItem.modelData.note
                            color: "#ffcf6b"
                            font.pixelSize: 9; font.bold: true
                            style: Text.Outline; styleColor: "#cc000000"
                        }
                        MouseArea {
                            x: cbItem.cx - 11; y: 8; width: 22; height: 20
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                Prefs.modeRx2 = cbItem.md
                                root.setCarrierHz(Math.round(cbItem.modelData.freq))
                                Status.show(qsTr("RX2 tuned to CB ")
                                    + cbItem.modelData.label
                                    + " — "
                                    + (cbItem.modelData.freq / 1.0e6).toFixed(4)
                                    + " MHz " + cbItem.md, 2000)
                            }
                            ToolTip.visible: containsMouse && Prefs.tooltipsEnabled
                            ToolTip.text: "CB " + cbItem.modelData.label + "  "
                                + (cbItem.modelData.freq / 1.0e6).toFixed(3)
                                + " MHz " + cbItem.md
                                + (cbItem.modelData.note !== ""
                                   ? "\n" + cbItem.modelData.note : "")
                                + qsTr("\nClick to tune RX2")
                        }
                    }
                }
            }

            Text {
                z: 8
                anchors.left: parent.left
                anchors.top: parent.top
                anchors.margins: 12
                anchors.topMargin: 14
                color: "#d7e0ea"
                font.pixelSize: 12
                font.bold: true
                text: "RX2  " + root.fmtHz(root.carrierHz)
            }
            Text {
                z: 8
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: 6
                visible: Stream.psLiveArmed
                color: "#e0a030"
                font.pixelSize: 11
                text: "Feedback path has this converter"
            }
            Text {
                z: 8
                visible: Prefs.cursorReadout && specMouse.containsMouse
                         && specMouse.dbMode === ""
                text: root.fmtHz(root.hzAtX(specMouse.mouseX, specHost.width))
                color: "#cdd9e5"
                font.pixelSize: 12
                font.family: "Consolas"
                style: Text.Outline
                styleColor: "#cc000000"
                x: Math.max(2, Math.min(specHost.width - width - 2,
                                        specMouse.mouseX + 12))
                y: Math.max(16, Math.min(specHost.height - height - 16,
                                         specMouse.mouseY - 18))
            }

            MouseArea {
                id: specMouse
                anchors.fill: parent
                hoverEnabled: true
                acceptedButtons: Qt.LeftButton
                readonly property int zonePx: 50
                readonly property int dragThreshPx: 6
                property string dbMode: ""
                property real startY: 0
                property real startMin: 0
                property real startMax: 0
                property bool tuning: false
                property bool dragged: false
                property real startX: 0
                property int startCenter: 0
                function dbModeAt(mx, my) {
                    if (mx < specHost.width - zonePx) return ""
                    if (my < 24) return ""
                    var third = specHost.height / 3
                    if (my < third) return "max"
                    if (my > 2 * third) return "min"
                    return "pan"
                }
                cursorShape: dbModeAt(mouseX, mouseY) !== "" ? Qt.SizeVerCursor
                             : (tuning && dragged ? Qt.ClosedHandCursor
                                                  : Qt.CrossCursor)
                onPressed: (ev) => {
                    dbMode = dbModeAt(ev.x, ev.y)
                    if (dbMode !== "") {
                        if (Prefs.rx2DbAuto) {
                            Prefs.rx2DbMin = Math.round(pan.effDbMin)
                            Prefs.rx2DbMax = Math.round(pan.effDbMax)
                            Prefs.rx2DbAuto = false
                        }
                        startY = ev.y
                        startMin = Prefs.rx2DbMin
                        startMax = Prefs.rx2DbMax
                    } else {
                        tuning = true
                        dragged = false
                        startX = ev.x
                        startCenter = root.ddcHz
                    }
                }
                onReleased: (ev) => {
                    if (dbMode === "" && tuning && !dragged)
                        root.tuneAt(startX, width)
                    dbMode = ""
                    tuning = false
                    dragged = false
                }
                onPositionChanged: (ev) => {
                    if (dbMode !== "") {
                        var h = Math.max(1, specHost.height)
                        var span = startMax - startMin
                        var dbDelta = -(ev.y - startY) * (span / h)
                        var nMin = startMin
                        var nMax = startMax
                        if (dbMode === "max")      nMax = startMax + dbDelta
                        else if (dbMode === "min") nMin = startMin + dbDelta
                        else { nMin = startMin + dbDelta
                               nMax = startMax + dbDelta }
                        nMin = Math.max(-150, Math.min(-3, nMin))
                        nMax = Math.max(nMin + 3, Math.min(30, nMax))
                        Prefs.rx2DbMin = nMin
                        Prefs.rx2DbMax = nMax
                        return
                    }
                    if (!tuning) return
                    if (!dragged) {
                        if (Math.abs(ev.x - startX) < dragThreshPx) return
                        dragged = true
                    }
                    var hzPerPx = root.spanHz / Math.max(1, width)
                    var newDdc = startCenter - (ev.x - startX) * hzPerPx
                    if (Stream.splitEnabled)
                        Stream.setVfoBHz(Math.round(newDdc
                            + (root.vfoBHz - root.ddcHz)))
                    else
                        Stream.setRx2FreqHz(Math.round(newDdc))
                }
                onWheel: (wheel) => {
                    var dir = root.wheelDir(wheel)
                    if (dir !== 0)
                        root.setCarrierHz(root.carrierHz + dir * Prefs.panScrollStepHz)
                    wheel.accepted = true
                }
            }
        }

        Waterfall {
            id: wf
            visible: !Prefs.waterfallCollapsed
            SplitView.preferredHeight: 110
            SplitView.minimumHeight: 60
            receiver: 2
            engine: WdspEngine
            autoScale: Prefs.rx2DbAuto
            dbMin: Prefs.rx2DbMin
            dbMax: Prefs.rx2DbMax
            targetFps: Prefs.targetFps
            palette: Prefs.waterfallPalette
            strengthColor: Prefs.waterfallColor
            speed: Prefs.waterfallSpeed

            Rectangle {
                z: 4
                width: 2
                height: parent.height
                color: "#ffaa50"
                opacity: 0.85
                x: Math.round(root.xOfHz(root.carrierHz, wf.width))
                y: 0
            }

            MouseArea {
                id: wfMouse
                anchors.fill: parent
                acceptedButtons: Qt.LeftButton
                hoverEnabled: true
                readonly property int zonePx: 50
                property string dbMode: ""
                property real startY: 0
                property real startMin: 0
                property real startMax: 0
                property bool dragged: false
                property real downX: 0
                function dbModeAt(mx, my) {
                    if (mx < wf.width - zonePx) return ""
                    var third = wf.height / 3
                    if (my < third) return "max"
                    if (my > 2 * third) return "min"
                    return "pan"
                }
                cursorShape: dbModeAt(mouseX, mouseY) !== ""
                             ? Qt.SizeVerCursor : Qt.CrossCursor
                onPressed: (ev) => {
                    dragged = false
                    downX = ev.x
                    dbMode = dbModeAt(ev.x, ev.y)
                    if (dbMode !== "") {
                        if (Prefs.rx2DbAuto)
                            Prefs.rx2DbAuto = false
                        startY = ev.y
                        startMin = Prefs.rx2DbMin
                        startMax = Prefs.rx2DbMax
                    }
                }
                onPositionChanged: (ev) => {
                    if (dbMode !== "") {
                        var h = Math.max(1, wf.height)
                        var span = startMax - startMin
                        var dbDelta = -(ev.y - startY) * (span / h)
                        var nMin = startMin
                        var nMax = startMax
                        if (dbMode === "max")      nMax = startMax + dbDelta
                        else if (dbMode === "min") nMin = startMin + dbDelta
                        else { nMin = startMin + dbDelta
                               nMax = startMax + dbDelta }
                        nMin = Math.max(-150, Math.min(-3, nMin))
                        nMax = Math.max(nMin + 3, Math.min(30, nMax))
                        Prefs.rx2DbMin = nMin
                        Prefs.rx2DbMax = nMax
                        return
                    }
                    if (Math.abs(ev.x - downX) > 6)
                        dragged = true
                }
                onReleased: (ev) => {
                    if (dbMode === "" && !dragged)
                        root.tuneAt(ev.x, width)
                    dbMode = ""
                }
                onWheel: (wheel) => {
                    var dir = root.wheelDir(wheel)
                    if (dir !== 0)
                        root.setCarrierHz(root.carrierHz + dir * Prefs.panScrollStepHz)
                    wheel.accepted = true
                }
            }
        }
    }
}
