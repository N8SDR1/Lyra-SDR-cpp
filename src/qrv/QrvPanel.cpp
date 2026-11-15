#include "qrv/QrvPanel.h"

#include "qrv/QrvClient.h"

#include <QAbstractItemView>
#include <QBrush>
#include <QColor>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace lyra::ui {

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
    list_->setSelectionMode(QAbstractItemView::NoSelection);
    list_->header()->setStretchLastSection(true);
    list_->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    list_->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    list_->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    list_->header()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    list_->header()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
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
    list_->clear();
    const auto &entries = client_->roster().entries;
    const QString me = client_->callsign();
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
        if (!me.isEmpty() && e.call.compare(me, Qt::CaseInsensitive) == 0
            && e.app == QLatin1String("Lyra")) {
            for (int c = 0; c < 6; ++c)
                item->setForeground(c, QBrush(QColor(QStringLiteral("#00e5ff"))));
        }
    }
    if (entries.isEmpty()) {
        auto *item = new QTreeWidgetItem(list_);
        item->setText(0, tr("(nobody QRV)"));
        item->setForeground(0, QBrush(QColor(QStringLiteral("#8fa6ba"))));
    }
}

} // namespace lyra::ui
