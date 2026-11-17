#include "qrv/QrvPanel.h"

#include "qrv/QrvClient.h"
#include "qrv/QrvProtocol.h"

#include <QAbstractItemView>
#include <QBrush>
#include <QColor>
#include <QCursor>
#include <QEvent>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMouseEvent>
#include <QPushButton>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

namespace lyra::ui {
namespace {

constexpr int kRoleSpottable = Qt::UserRole;
constexpr int kRoleHz = Qt::UserRole + 1;
constexpr int kRoleMode = Qt::UserRole + 2;

} // namespace

QrvPanel::QrvPanel(lyra::qrv::QrvClient *client, QWidget *parent)
    : QWidget(parent), client_(client) {
    setObjectName(QStringLiteral("qrvPanel"));
    auto *v = new QVBoxLayout(this);
    v->setContentsMargins(10, 8, 10, 8);
    v->setSpacing(8);

    auto *row = new QHBoxLayout;
    qrvBtn_ = new QPushButton(this);
    qrvBtn_->setCheckable(true);
    qrvBtn_->setFixedWidth(72);
    qrvBtn_->setFocusPolicy(Qt::NoFocus);
    connect(qrvBtn_, &QPushButton::clicked, this, [this](bool on) {
        if (client_)
            client_->setPublishing(on);
        refresh();
    });
    status_ = new QLabel(this);
    status_->setStyleSheet(QStringLiteral("color:#cdd9e5;"));
    row->addWidget(qrvBtn_);
    row->addWidget(status_, 1);
    v->addLayout(row);

    list_ = new QTreeWidget(this);
    list_->setColumnCount(6);
    list_->setHeaderLabels({tr("Call"), tr("Band"), tr("Mode"), tr("Hz"),
                            tr("App"), tr("Note")});
    list_->setRootIsDecorated(false);
    list_->setAlternatingRowColors(true);
    list_->setSelectionMode(QAbstractItemView::SingleSelection);
    list_->setSelectionBehavior(QAbstractItemView::SelectRows);
    list_->setUniformRowHeights(true);
    list_->setMouseTracking(true);
    list_->viewport()->setMouseTracking(true);
    list_->viewport()->installEventFilter(this);
    list_->setToolTip(
        tr("Tune is only a Lyra row that shared frequency."));
    list_->header()->setStretchLastSection(true);
    list_->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    list_->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    list_->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    list_->header()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    list_->header()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    connect(list_, &QTreeWidget::itemDoubleClicked,
            this, &QrvPanel::onItemDoubleClicked);
    v->addWidget(list_, 1);

    error_ = new QLabel(this);
    error_->setWordWrap(true);
    error_->setStyleSheet(QStringLiteral("color:#e0736b;"));
    v->addWidget(error_);

    setMinimumSize(520, 280);
    setStyleSheet(QStringLiteral(
        "QWidget#qrvPanel{background:#101820;}"
        "QTreeWidget{background:#0c141c;color:#cdd9e5;border:1px solid #2a4a5a;}"
        "QHeaderView::section{background:#15202a;color:#8fa6ba;padding:4px;"
        "border:0px;border-right:1px solid #1b2a36;}"));

    if (client_) {
        connect(client_, &lyra::qrv::QrvClient::publishingChanged, this,
                &QrvPanel::refresh);
        connect(client_, &lyra::qrv::QrvClient::lastErrorChanged, this,
                &QrvPanel::refresh);
        connect(client_, &lyra::qrv::QrvClient::rosterChanged, this,
                &QrvPanel::refreshList);
        connect(client_, &lyra::qrv::QrvClient::optInChanged, this,
                &QrvPanel::refresh);
        connect(client_, &lyra::qrv::QrvClient::callsignChanged, this,
                &QrvPanel::refresh);
        connect(client_, &lyra::qrv::QrvClient::installKeyChanged, this,
                &QrvPanel::refresh);
    }
    refresh();
}

void QrvPanel::refresh() {
    if (!client_)
        return;
    const bool on = client_->publishing();
    {
        const QSignalBlocker b(qrvBtn_);
        qrvBtn_->setChecked(on);
    }
    qrvBtn_->setText(on ? tr("QRT") : tr("QRV"));
    qrvBtn_->setStyleSheet(on
        ? QStringLiteral("QPushButton{background:#1a3a2a;color:#3ecf6a;font-weight:700;}")
        : QStringLiteral("QPushButton{background:#1a2632;color:#cdd9e5;font-weight:700;}"));
    qrvBtn_->setEnabled(client_->optIn());
    qrvBtn_->setToolTip(
        client_->optIn()
            ? tr("Publish this station on the QRV board (POST heartbeat). "
                 "QRT removes you immediately.")
            : tr("Turn on QRV in Settings → Network first."));

    QString st;
    if (!client_->optIn())
        st = tr("QRV is off in Settings.");
    else if (on)
        st = tr("QRV as %1 — heartbeat ~30 s").arg(client_->callsign());
    else
        st = tr("Not publishing. Click QRV when the radio is running.");
    status_->setText(st);
    error_->setText(client_->lastError());
    error_->setVisible(!client_->lastError().isEmpty());
    refreshList();
}

void QrvPanel::refreshList() {
    if (!client_ || !list_)
        return;
    QString keepCall;
    QString keepApp;
    if (auto *cur = list_->currentItem()) {
        keepCall = cur->text(0);
        keepApp = cur->text(4);
    }
    list_->clear();
    const auto &entries = client_->roster().entries;
    const QString me = client_->callsign();
    QTreeWidgetItem *restore = nullptr;
    for (const lyra::qrv::RosterEntry &e : entries) {
        auto *item = new QTreeWidgetItem(list_);
        item->setText(0, e.call);
        item->setText(1, e.band);
        item->setText(2, e.mode);
        if (e.hasHz)
            item->setText(3, QString::number(e.hz));
        else
            item->setText(3, QStringLiteral("—"));
        item->setText(4, e.app);
        item->setText(5, e.note);
        const bool spot = lyra::qrv::isSpottableRow(e);
        item->setData(0, kRoleSpottable, spot);
        item->setData(0, kRoleHz, QVariant::fromValue(e.hz));
        item->setData(0, kRoleMode, e.mode);
        const QString tip = spot
            ? tr("Double-click to tune the focused receiver to %1 at %2 Hz %3")
                  .arg(e.call)
                  .arg(e.hz)
                  .arg(e.mode)
            : tr("Tune is only a Lyra row that shared frequency.");
        for (int c = 0; c < 6; ++c)
            item->setToolTip(c, tip);
        if (!me.isEmpty() && e.call.compare(me, Qt::CaseInsensitive) == 0
            && e.app == QLatin1String("Lyra")) {
            for (int c = 0; c < 6; ++c)
                item->setForeground(c, QBrush(QColor(QStringLiteral("#00e5ff"))));
        }
        if (!keepCall.isEmpty() && e.call == keepCall && e.app == keepApp)
            restore = item;
    }
    if (entries.isEmpty()) {
        auto *item = new QTreeWidgetItem(list_);
        item->setText(0, tr("(nobody QRV)"));
        item->setForeground(0, QBrush(QColor(QStringLiteral("#8fa6ba"))));
        item->setFlags(item->flags() & ~Qt::ItemIsSelectable);
        item->setData(0, kRoleSpottable, false);
        item->setToolTip(0, tr("Tune is only a Lyra row that shared frequency."));
    }
    if (restore)
        list_->setCurrentItem(restore);
}

void QrvPanel::onItemDoubleClicked(QTreeWidgetItem *item, int /*column*/) {
    if (!item || !item->data(0, kRoleSpottable).toBool())
        return;
    const qint64 hz = item->data(0, kRoleHz).toLongLong();
    if (hz <= 0)
        return;
    emit tuneSpotRequested(hz, item->data(0, kRoleMode).toString());
}

void QrvPanel::updateHoverCursor(const QPoint &pos) {
    auto *item = list_->itemAt(pos);
    const bool spot = item && item->data(0, kRoleSpottable).toBool();
    list_->viewport()->setCursor(spot ? Qt::PointingHandCursor : Qt::ArrowCursor);
}

bool QrvPanel::eventFilter(QObject *watched, QEvent *event) {
    if (list_ && watched == list_->viewport()) {
        if (event->type() == QEvent::MouseMove) {
            updateHoverCursor(static_cast<QMouseEvent *>(event)->pos());
        } else if (event->type() == QEvent::Leave) {
            list_->viewport()->unsetCursor();
        }
    }
    return QWidget::eventFilter(watched, event);
}

} // namespace lyra::ui
