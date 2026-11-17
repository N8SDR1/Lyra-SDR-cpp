// Floating QRV roster (same dock class as PureSignal / Freq Cal).

#pragma once

#include <QString>
#include <QWidget>
#include <QtGlobal>

class QEvent;
class QLabel;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;

namespace lyra::qrv {
class QrvClient;
}

namespace lyra::ui {

class QrvPanel : public QWidget {
    Q_OBJECT
public:
    explicit QrvPanel(lyra::qrv::QrvClient *client, QWidget *parent = nullptr);

signals:
    // Double-click on a Lyra+Hz row. Carrier Hz + posted Lyra mode (unmapped).
    void tuneSpotRequested(qint64 hz, const QString &mode);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void refresh();
    void refreshList();
    void onItemDoubleClicked(QTreeWidgetItem *item, int column);
    void updateHoverCursor(const QPoint &pos);

    lyra::qrv::QrvClient *client_ = nullptr;
    QPushButton *qrvBtn_ = nullptr;
    QLabel *status_ = nullptr;
    QLabel *error_ = nullptr;
    QTreeWidget *list_ = nullptr;
};

} // namespace lyra::ui
