.pragma library
// Shared danger-zone colouring for the later meter faces (style 3+).
//
// `danger` is Meter.normAtS9 — the source's danger point on the 0..1 scale
// (S9 on the S-meter, rated power on PWR, 2:1 on SWR, …).  Zones:
//   0  below 55 % of the danger point   cool
//   1  up to the danger point           normal
//   2  past the danger point            warn  (amber)
//   3  danger + 0.16 and above          over  (red)
// Faces pick a palette; the thresholds live only here.  Also the glass
// faces' warning lamp and fixed digit cells (bottom of the file).

var kOverStep = 0.16
var kNormalFrac = 0.55

function zone(v, danger) {
    if (v >= danger + kOverStep) return 3
    if (v >= danger) return 2
    if (v >= danger * kNormalFrac) return 1
    return 0
}

// The single-hue faces (Iris, Sonar, Drum, Phosphor) and Cluster.
var facePalette = ["#3ad7ff", "#3dff9a", "#ff9a3c", "#ff4d4d"]

function ink(v, danger) {
    return facePalette[zone(v, danger)]
}

// Glass faces (VFD) keep their tube colour and carry a separate red
// "OVER" lamp, like a rig's HI-SWR LED: it lights in the over zone, where
// the other faces turn red.
var lampOff = "#2a0c0c"
var lampOn = "#ff3020"

function lampLit(v, danger) {
    return zone(v, danger) >= 3
}

function lamp(v, danger) {
    return lampLit(v, danger) ? lampOn : lampOff
}

// Glass faces draw one character per digit cell, and the cell count is
// fixed per meter source so the tubes do not resize as the reading's
// length changes ("S9" → "S9+20", "9.5 W" → "12 W").  Counts are the
// longest normal reading for each MeterModel::Source; an unexpectedly
// longer one still gets a cell per character.
var kCellsBySource = { 0: 5, 1: 6, 2: 6, 3: 6, 4: 6, 5: 7 }
var kCellsDefault = 8   // the dB sources: "-30.0 dB"

function readoutCells(source, text) {
    var n = kCellsBySource[source] !== undefined ? kCellsBySource[source] : kCellsDefault
    return Math.max(n, text.length)
}

// Pad `text` with blanks to `cells`.  The S-meter reads left to right from
// a fixed "S"; numeric readings sit right-aligned like a panel meter.
function padCells(text, cells, leftAlign) {
    var pad = ""
    for (var i = text.length; i < cells; ++i) pad += " "
    return leftAlign ? text + pad : pad + text
}
