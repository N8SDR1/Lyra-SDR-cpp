.pragma library
// Lyra — VFO style registry (Tuning panel).  One entry per style:
//   id    — saved in Prefs.vfoStyle; NEVER rename an existing id
//   name  — shown in the right-click "VFO style" menu
//   group — Modern / Classic / Ham shack (menu headings, Phase 2+)
//   file  — the style's QML file, next to this one
// Only implemented styles are listed.  resolve() maps an empty or unknown id
// (first run, or a removed style) to defaultId.
// scratch/test_vfo_styles.cpp loads every entry and checks the contract.

var defaultId = "amber"

var styles = [
    { id: "amber", name: "Amber LED", group: "Classic", file: "VfoStyleAmber.qml" }
]

function byId(id) {
    for (var i = 0; i < styles.length; ++i)
        if (styles[i].id === id) return styles[i]
    return null
}

function resolve(id) {
    return byId(id) || byId(defaultId)
}
