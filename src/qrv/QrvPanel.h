// Floating QRV roster (same dock class as PureSignal / Freq Cal).

#pragma once

#include <QWidget>

class QLabel;
class QPushButton;
class QTreeWidget;

namespace lyra::qrv {
class QrvClient;
}

namespace lyra::ui {

class QrvPanel : public QWidget {
    Q_OBJECT
public:
    explicit QrvPanel(lyra::qrv::QrvClient *client, QWidget *parent = nullptr);

private:
    void refresh();
    void refreshList();

    lyra::qrv::QrvClient *client_ = nullptr;
    QPushButton *qrvBtn_ = nullptr;
    QLabel *status_ = nullptr;
    QLabel *error_ = nullptr;
    QTreeWidget *list_ = nullptr;
};

} // namespace lyra::ui
