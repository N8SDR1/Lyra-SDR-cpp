.pragma library

// Shared zone colours for Sonar / Cluster.  Same 0…1 as Arc.
var facePalette = ["#00e5ff", "#e8c477", "#ff4d4d"]

function ink(level, danger) {
    var d = (danger === undefined || danger === null) ? 0.5 : danger
    var n = Math.max(0, Math.min(1, level))
    if (n >= d + 0.16) return facePalette[2]
    if (n >= d) return facePalette[1]
    if (n >= d * 0.55) return "#5dff9a"
    return facePalette[0]
}
