// Lyra — in-app User Guide viewer.  See helpdialog.h.

#include "helpdialog.h"

#include <QCloseEvent>
#include <QDesktopServices>
#include <QFile>
#include <QFont>
#include <QHash>
#include <QHeaderView>
#include <QLabel>
#include <QPalette>
#include <QRegularExpression>
#include <QScrollBar>
#include <QSettings>
#include <QSplitter>
#include <QTextBrowser>
#include <QTextCursor>
#include <QTextStream>
#include <QTimer>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>

namespace lyra::ui {

namespace {

// ─────────────────────────────────────────────────────────────────────
// Topic mapping for per-panel "?" badges.
// ─────────────────────────────────────────────────────────────────────
const QHash<QString, QString> &topicHeads() {
    static const QHash<QString, QString> m = {
        {QStringLiteral("getting-started"),
         QStringLiteral("Getting around the window")},
        {QStringLiteral("panadapter"),
         QStringLiteral("The panadapter (spectrum display)")},
        {QStringLiteral("visuals"),   QStringLiteral("Settings → Visuals")},
        {QStringLiteral("tuning"),    QStringLiteral("Tuning panel")},
        {QStringLiteral("modes-filters"), QStringLiteral("Filters panel")},
        {QStringLiteral("audio"),     QStringLiteral("Audio panel")},
        {QStringLiteral("display"),   QStringLiteral("Display panel")},
        {QStringLiteral("meter"),     QStringLiteral("Meter panel")},
        {QStringLiteral("band"),      QStringLiteral("Band panel")},
        {QStringLiteral("propagation"),
         QStringLiteral("Solar / Propagation panel")},
        // TX front panel + the TX DSP-rack docks (each jumps to its own
        // guide subsection so "?" = fast "what does this do?").
        {QStringLiteral("tx"),        QStringLiteral("TX panel")},
        {QStringLiteral("ps"),        QStringLiteral("PureSignal")},
        {QStringLiteral("txeq"),      QStringLiteral("TX EQ — 10-band parametric")},
        {QStringLiteral("rxeq"),      QStringLiteral("RX EQ — receive parametric EQ")},
        {QStringLiteral("txspeech"),
         QStringLiteral("TX Speech — Noise Gate → Auto-AGC → De-esser")},
        {QStringLiteral("txcombinator"),
         QStringLiteral("TX Combinator — 5-band multiband compressor")},
        {QStringLiteral("txplate"),
         QStringLiteral("TX Plating — plate reverb (ESSB air)")},
        {QStringLiteral("vox"),
         QStringLiteral("Settings → VOX (voice-operated transmit)")},
        {QStringLiteral("cwconsole"),
         QStringLiteral("CW operating (paddle, keyboard, TCI)")},
        {QStringLiteral("cwdecoder"),
         QStringLiteral("Reading CW — the RX decoder")},
        {QStringLiteral("tuner"),     QStringLiteral("Tuner (manual ATU memory)")},
        {QStringLiteral("profiles"),
         QStringLiteral("Profiles (TX/RX chain presets)")},
        // Header "Options" toggle chips with no dock (right-click → help).
        {QStringLiteral("ctun"),      QStringLiteral("CTUN — centre-tune lock")},
        {QStringLiteral("wfid"),
         QStringLiteral("Waterfall ID (TX callsign courtesy ID)")},
        {QStringLiteral("freqcal"),
         QStringLiteral("Frequency calibration (WWV / time station)")},
    };
    return m;
}

// ─────────────────────────────────────────────────────────────────────
// Slug rule — matches the slugs hand-written in USER_GUIDE.md TOC
// bullets and body cross-references.  ASCII-only on purpose so that
// `cos² fade` → `cos-fade` (matches the hand-written link, NOT
// `cos²-fade` that QChar::isLetterOrNumber would produce).
//   - lowercase
//   - keep [a-z 0-9 -]
//   - each whitespace char → one hyphen (so "bottom — hl2" becomes
//     "bottom--hl2" because the em-dash gets dropped and its two
//     surrounding spaces each produce a hyphen)
//   - everything else (punctuation, symbols, non-ASCII letters)
//     dropped entirely
// ─────────────────────────────────────────────────────────────────────
QString slugify(const QString &heading) {
    QString out;
    out.reserve(heading.size());
    const QString lower = heading.toLower();
    for (const QChar c : lower) {
        const ushort u = c.unicode();
        if ((u >= 'a' && u <= 'z') ||
            (u >= '0' && u <= '9') ||
            u == '-') {
            out += c;
        } else if (c.isSpace()) {
            out += QLatin1Char('-');
        }
    }
    return out;
}

// Parsed-heading record used to populate the TOC tree.
struct Heading {
    int     level;
    QString text;
};

// Walk the raw markdown source and pull out every ATX heading
// (`#`-prefix), respecting fenced code blocks so a comment inside
// a ``` ... ``` example isn't mistaken for a heading.  Returns an
// ordered list preserving document order.
QList<Heading> parseHeadings(const QString &src) {
    QList<Heading> out;
    bool inFence = false;
    // Fences open/close with a line whose first non-whitespace run is
    // ``` or ~~~.  We don't need the full CommonMark spec — just the
    // common case used in USER_GUIDE.md.
    static const QRegularExpression fenceRe(
        QStringLiteral(R"(^[ \t]*(`{3,}|~{3,}))"));
    static const QRegularExpression headRe(
        QStringLiteral(R"(^(#{1,6})[ \t]+(.+?)[ \t]*$)"));
    const auto lines = src.split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        if (fenceRe.match(line).hasMatch()) {
            inFence = !inFence;
            continue;
        }
        if (inFence) continue;
        const auto m = headRe.match(line);
        if (m.hasMatch()) {
            out.push_back({ static_cast<int>(m.capturedLength(1)),
                            m.captured(2).trimmed() });
        }
    }
    return out;
}

// Strip an explicit-anchor span (`<a name="..."></a>`) that we may
// have injected at the front of a heading for the QTextBrowser to
// pick up — when used as a TOC label, the user sees the heading
// text alone, not the HTML noise.
QString stripAnchorHtml(const QString &headingText) {
    static const QRegularExpression anchorRe(
        QStringLiteral(R"(<a\s+name="[^"]*"\s*></a>)"),
        QRegularExpression::CaseInsensitiveOption);
    QString s = headingText;
    s.remove(anchorRe);
    return s.trimmed();
}

// Injecting `<a name="slug"></a>` into every heading is BELT (the
// in-document scrollToAnchor() pathway will work for any Qt build
// that does propagate the inline-HTML); SUSPENDERS is the find()
// based scroller invoked by the TOC tree click and the anchorClicked
// handler.  The injection is harmless either way (renders to a
// zero-width text fragment).
QString injectHeadingAnchors(const QString &src) {
    QString out;
    out.reserve(src.size() + 4096);
    static const QRegularExpression headingRe(
        QStringLiteral(R"(^(#{1,6})[ \t]+(.+?)[ \t]*$)"),
        QRegularExpression::MultilineOption);
    static const QRegularExpression hasAnchorRe(
        QStringLiteral(R"(<a\s+name=)"),
        QRegularExpression::CaseInsensitiveOption);
    qsizetype last = 0;
    auto it = headingRe.globalMatch(src);
    while (it.hasNext()) {
        const auto m = it.next();
        out.append(QStringView(src).mid(last, m.capturedStart() - last));
        const QString hashes      = m.captured(1);
        const QString headingText = m.captured(2);
        if (hasAnchorRe.match(headingText).hasMatch()) {
            out += m.captured(0);
        } else {
            const QString slug = slugify(headingText);
            out += hashes;
            out += QLatin1Char(' ');
            out += QStringLiteral("<a name=\"%1\"></a>").arg(slug);
            out += headingText;
        }
        last = m.capturedEnd();
    }
    out.append(QStringView(src).mid(last));
    return out;
}

// Remove the redundant "## Contents" block from the rendered markdown.
// The TOC tree on the left now serves the same purpose; leaving the
// markdown TOC in confuses find()-based navigation (heading text
// matches both the Contents bullet and the real heading) and wastes
// a screenful of scroll real estate at the top of the guide.
QString stripContentsBlock(const QString &src) {
    static const QRegularExpression startRe(
        QStringLiteral(R"(^##[ \t]+Contents[ \t]*$)"),
        QRegularExpression::MultilineOption);
    const auto startMatch = startRe.match(src);
    if (!startMatch.hasMatch())
        return src;
    const qsizetype start = startMatch.capturedStart();
    // The Contents block ends at the next `## ` heading or `---`
    // horizontal rule, whichever comes first.
    static const QRegularExpression endRe(
        QStringLiteral(R"(^(##[ \t]+|---[ \t]*$))"),
        QRegularExpression::MultilineOption);
    const auto endMatch = endRe.match(src, startMatch.capturedEnd());
    const qsizetype end = endMatch.hasMatch()
        ? endMatch.capturedStart() : src.size();
    QString out = src;
    out.remove(start, end - start);
    return out;
}

} // namespace

// ─────────────────────────────────────────────────────────────────────
// HelpDialog
// ─────────────────────────────────────────────────────────────────────
HelpDialog::HelpDialog(QWidget *parent) : QDialog(parent) {
    setWindowTitle(tr("Lyra — User Guide"));

    // QDialog defaults to a fixed window frame on most platforms; give
    // the operator a real resizable window with min/max/close in the
    // title bar so the help viewer behaves like any other application
    // window (drag the corner to resize, click the maximize icon to
    // go full-screen, click again to restore).
    setWindowFlags(windowFlags()
                   | Qt::WindowMinMaxButtonsHint
                   | Qt::WindowCloseButtonHint);
    setSizeGripEnabled(true);

    resize(1100, 760);   // default; overridden by restoreLayout() if set

    // ── Left pane: TOC tree (in a container with a header label) ───
    auto *tocPane   = new QWidget(this);
    auto *tocLayout = new QVBoxLayout(tocPane);
    tocLayout->setContentsMargins(8, 8, 4, 4);
    tocLayout->setSpacing(4);

    // Small label up top makes the click-affordance explicit — operator
    // feedback was "users might not realise the tree items are
    // clickable" since QTreeWidget rows don't look like hyperlinks
    // out of the box.  Link-colour styling on the rows themselves does
    // the rest of the heavy lifting (see QSS below).
    auto *tocHeader = new QLabel(
        tr("Contents — click any item to jump"), tocPane);
    {
        QFont hf = tocHeader->font();
        hf.setPointSize(9);
        hf.setItalic(true);
        tocHeader->setFont(hf);
        tocHeader->setStyleSheet(
            QStringLiteral("QLabel{color:#8fa6ba;padding:2px 4px;}"));
    }
    tocLayout->addWidget(tocHeader);

    toc_ = new QTreeWidget(tocPane);
    toc_->setHeaderHidden(true);
    toc_->setRootIsDecorated(true);        // show expand arrows
    toc_->setUniformRowHeights(true);
    toc_->setIndentation(16);
    toc_->setMinimumWidth(220);
    toc_->setMouseTracking(true);          // for hover row highlight
    // Pointing-hand cursor over rows so the mouse pointer itself reads
    // "clickable link" — same idiom every web browser uses on <a>.
    toc_->viewport()->setCursor(Qt::PointingHandCursor);
    // Link-styled rows: Lyra cyan text, light hover bg, slightly
    // darker selected bg + slightly brighter cyan when the row is
    // hovered.  Matches `a { color: #5ec8ff; }` used in the right-
    // pane QTextBrowser for in-document links — visually the TOC IS
    // a column of hyperlinks now.
    toc_->setStyleSheet(QStringLiteral(
        "QTreeWidget {"
        "  background-color: #0a0d12;"
        "  color: #5ec8ff;"                     // Lyra link cyan
        "  border: none;"
        "  outline: 0;"
        "}"
        "QTreeWidget::item {"
        "  padding: 3px 2px;"
        "}"
        "QTreeWidget::item:hover {"
        "  background-color: #102230;"          // subtle hover wash
        "  color: #8fd6ff;"                     // brighter cyan on hover
        "}"
        "QTreeWidget::item:selected {"
        "  background-color: #184a6a;"          // selected row chip
        "  color: #f0ffff;"
        "}"
        "QTreeWidget::item:selected:hover {"
        "  background-color: #1f5f87;"
        "  color: #ffffff;"
        "}"
        // Branch indicator arrows still need to read on the dark bg.
        "QTreeWidget::branch { background-color: #0a0d12; }"
    ));
    QFont tf = toc_->font();
    tf.setPointSize(10);
    toc_->setFont(tf);
    tocLayout->addWidget(toc_, /*stretch=*/1);
    connect(toc_, &QTreeWidget::itemActivated, this,
            [this](QTreeWidgetItem *it, int) {
        if (!it) return;
        const QString headText = it->data(0, Qt::UserRole).toString();
        if (!headText.isEmpty()) scrollToHeading(headText);
    });
    connect(toc_, &QTreeWidget::itemClicked, this,
            [this](QTreeWidgetItem *it, int) {
        if (!it) return;
        const QString headText = it->data(0, Qt::UserRole).toString();
        if (!headText.isEmpty()) scrollToHeading(headText);
    });

    // ── Right pane: rendered guide ──────────────────────────────────
    browser_ = new QTextBrowser(this);
    browser_->setOpenExternalLinks(true);
    browser_->setOpenLinks(false);        // we intercept anchor clicks
    {
        QFont gf = browser_->font();
        gf.setPointSize(12);
        browser_->setFont(gf);
    }
    {
        QPalette pal = browser_->palette();
        pal.setColor(QPalette::Base, QColor(0x0a, 0x0d, 0x12));
        pal.setColor(QPalette::Text, QColor(0xd8, 0xe4, 0xee));
        pal.setColor(QPalette::Link, QColor(0x5e, 0xc8, 0xff));   // cyan
        pal.setColor(QPalette::LinkVisited, QColor(0x8f, 0xd6, 0xff));
        browser_->setPalette(pal);
    }
    browser_->document()->setDefaultStyleSheet(
        QStringLiteral("a { color: #5ec8ff; }"
                       "h1, h2, h3 { color: #00e5ff; }"));
    // In-document body cross-reference links → route through the
    // slug→heading map + find()-based scroller.  External links open
    // in the system browser via QDesktopServices.
    connect(browser_, &QTextBrowser::anchorClicked, this,
            [this](const QUrl &u) {
        if (u.scheme().isEmpty() && u.path().isEmpty() &&
            !u.fragment().isEmpty()) {
            const QString slug = u.fragment();
            const QString head = slugToHeading_.value(slug);
            if (!head.isEmpty()) {
                scrollToHeading(head);
            } else {
                // Last-ditch fallback: try the QTextDocument anchor
                // path in case Qt did propagate the injected <a name>.
                browser_->scrollToAnchor(slug);
            }
        } else {
            QDesktopServices::openUrl(u);
        }
    });

    // ── Splitter ────────────────────────────────────────────────────
    split_ = new QSplitter(Qt::Horizontal, this);
    split_->setChildrenCollapsible(false);
    split_->addWidget(tocPane);       // header label + tree, wrapped
    split_->addWidget(browser_);
    split_->setStretchFactor(0, 0);   // TOC stays at preferred width
    split_->setStretchFactor(1, 1);   // browser expands
    split_->setSizes({ 280, 820 });   // default; restoreLayout overrides

    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->addWidget(split_);

    loadGuide();
    restoreLayout();
}

HelpDialog::~HelpDialog() = default;

void HelpDialog::closeEvent(QCloseEvent *e) {
    saveLayout();
    QDialog::closeEvent(e);
}

void HelpDialog::saveLayout() const {
    QSettings s;
    s.setValue(QStringLiteral("helpdialog/geometry"),     saveGeometry());
    s.setValue(QStringLiteral("helpdialog/splitterState"),
               split_ ? split_->saveState() : QByteArray());
}

void HelpDialog::restoreLayout() {
    QSettings s;
    const QByteArray g = s.value(
        QStringLiteral("helpdialog/geometry")).toByteArray();
    if (!g.isEmpty()) restoreGeometry(g);
    const QByteArray ss = s.value(
        QStringLiteral("helpdialog/splitterState")).toByteArray();
    if (!ss.isEmpty() && split_) split_->restoreState(ss);
}

void HelpDialog::loadGuide() {
    QFile f(QStringLiteral(":/help/USER_GUIDE.md"));
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        browser_->setPlainText(
            tr("User guide not found (:/help/USER_GUIDE.md)."));
        return;
    }
    QTextStream ts(&f);
    const QString raw = ts.readAll();

    // Build the TOC tree from the ORIGINAL raw source (so we catch
    // every heading) before we strip the Contents block.
    buildToc(raw);

    // Strip the redundant "## Contents" block (the tree on the left
    // serves the same role now), then inject heading anchors and hand
    // the result to QTextBrowser.
    const QString trimmed   = stripContentsBlock(raw);
    const QString anchored  = injectHeadingAnchors(trimmed);
    browser_->setMarkdown(anchored);
}

void HelpDialog::buildToc(const QString &markdownSrc) {
    if (!toc_) return;
    toc_->clear();
    slugToHeading_.clear();

    const QList<Heading> all = parseHeadings(markdownSrc);

    // Group structure:
    //   level 1 (the document title)    — skip; not navigable
    //   level 2 (## section)            — top-level tree row
    //   level 3 (### subsection)        — child of the preceding level-2
    //   level 4+                        — children of the most recent
    //                                     deeper ancestor (uncommon —
    //                                     none in the current doc)
    QTreeWidgetItem *currentH2 = nullptr;
    for (const Heading &h : all) {
        if (h.level <= 1) continue;            // skip the document title
        if (h.text.compare(QLatin1String("Contents"),
                           Qt::CaseInsensitive) == 0)
            continue;                          // duplicates the tree itself

        // Map slug → heading text so body cross-ref links resolve.
        slugToHeading_.insert(slugify(h.text), h.text);

        const QString label = stripAnchorHtml(h.text);
        auto *item = new QTreeWidgetItem({ label });
        item->setData(0, Qt::UserRole, h.text);   // exact text for find()
        item->setToolTip(0, label);

        if (h.level == 2 || !currentH2) {
            toc_->addTopLevelItem(item);
            currentH2 = item;
        } else {
            currentH2->addChild(item);
        }
    }
    // Expand top-level sections by default so all 26 H2 entries are
    // visible without a click; H3 children stay collapsed under the
    // expand arrow so the operator can drill in only where they want.
    toc_->expandToDepth(0);
}

void HelpDialog::scrollToHeading(const QString &headingText) {
    if (!browser_ || headingText.isEmpty()) return;
    // Defer one event-loop turn so first-open layout has finished —
    // an immediate scroll on a freshly-rendered document gets reset
    // by a late layout pass on some Qt builds.
    QTimer::singleShot(0, this, [this, headingText]() {
        browser_->moveCursor(QTextCursor::Start);
        if (!browser_->find(headingText)) return;
        QTextCursor c = browser_->textCursor();
        c.clearSelection();
        browser_->setTextCursor(c);
        browser_->ensureCursorVisible();
        // Nudge the heading up to near the top of the viewport
        // (ensureCursorVisible only guarantees somewhere visible).
        const int top = browser_->cursorRect().top();
        auto *vb = browser_->verticalScrollBar();
        vb->setValue(vb->value() + top - 8);
    });
}

void HelpDialog::showTopic(const QString &topic) {
    show();
    raise();
    activateWindow();
    const QString head = topicHeads().value(topic);
    if (!head.isEmpty()) scrollToHeading(head);
}

} // namespace lyra::ui
