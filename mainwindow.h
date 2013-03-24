#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "ui_mainwindow.h"

#include <QSystemTrayIcon>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QSettings>
#include <QProcess>
#include <QTimer>

class ManageMiners;
class Graph;

class MainWindow : public QMainWindow, private Ui::MainWindow
{
    Q_OBJECT
    
public:
    explicit MainWindow(QWidget *parent = 0);
    
protected:
    void changeEvent(QEvent *e);
    void focusInEvent(QFocusEvent *);
    void focusOutEvent(QFocusEvent *);
    void closeEvent(QCloseEvent *);
    void keyPressEvent(QKeyEvent *);
    void mouseMoveEvent(QMouseEvent *);
    void mousePressEvent(QMouseEvent *);
    void mouseReleaseEvent(QMouseEvent *);

public slots:
    void requestBlockInfoUpdate();
    void requestPoolStatsUpdate();
    void requestAccountDataUpdate();

    void accountDataReply();
    void poolStatsReply();
    void blockInfoReply();

    void minersUpdated(QVariantMap, bool store=true);
    void graphDestroyed();
    void minerManagementDestroyed();
    void toggleWidget();
    void setWidget(bool);
    void showGraph();
    void showMinerManagement();
    void toggleVisible();
    void changeApiToken();
    void updateSelectedMiner(QAction* =0);
    void minerExited(int code, QProcess::ExitStatus);
    void minerStateChanged(QProcess::ProcessState);

    void stopMiner();
    void toggleMiner();
    void startMiner(QString name);

private slots:
    void finishTransform();

signals:
    void receivedPoolStatsData(QVariantMap data);
    void receivedAccountData(QVariantMap data);
    void receivedBlockInfoData(QVariantMap data);
    void invertChanged(bool);

private:
    QString apiKey;

    QTimer killMiner;
    QProcess* miner;
    QActionGroup* minerGroup;
    QAction* windowVisibilityAction;
    QAction* trayHashRate;
    QSystemTrayIcon* trayIcon;
    QNetworkAccessManager accessMan;
    QNetworkReply* accountDataRequest;
    QNetworkReply* poolStatsRequest;
    QNetworkReply* blockInfoRequest;
    QTimer updateAccountDataTimer;
    QTimer updateBlockInfoTimer;

    QPoint dragPoint;
    QSettings settings;
    bool widgetMode;

    ManageMiners* miners;
    Graph* graph;
};

#endif // MAINWINDOW_H
