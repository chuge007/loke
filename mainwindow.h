#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QDoubleValidator>
#include <QCheckBox>
#include <QElapsedTimer>
#include <QTimer>



#include "scancontroltaida.h"
#include "scancontrolhuichuan.h"

struct Point2D {
    float x;
    float y;
};

namespace Ui {
class MainWindow;
}

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

    int currentPathIndex = 0;
    std::vector<Point2D> path;
    QTimer* arriveTimer = nullptr;

    bool stopScan;

    // ---- 事件驱动到位检测 ----
    // 不再用 50ms 轮询比较位置，改成：每次 updatePosition* 收到真实位置回包
    // 后立刻调用 checkArrival()，到位/超时就推进；arriveTimer 只是兜底超时器。
    bool firstPosReceived = false;
    bool arriveActive = false;
    Point2D arriveTarget = {0, 0};
    QElapsedTimer arriveElapsed;
    int arriveResendCount = 0;
    float arriveLastDist = -1.0f;
    QElapsedTimer arriveStallSince;
    void checkArrival();

    bool freeMotionActive = false;
    bool freeMotionAxisXEnabled = false;
    bool freeMotionAxisYEnabled = false;
    float freeMotionRangeX = 0.0f;
    float freeMotionRangeY = 0.0f;
    float freeMotionTargetX = 0.0f;
    float freeMotionTargetY = 0.0f;

    uint32_t axisCommandSpeed(bool isXAxisCommand) const;
    Point2D pathPointToPhysicalTargets(const Point2D& point) const;
    void sendXAxisUiTarget(double targetUiX, uint32_t maxSpeed);
    void sendYAxisUiTarget(double targetUiY, uint32_t maxSpeed);
    void startFreeMotion();
    void stopFreeMotion();
    void checkFreeMotionX();
    void checkFreeMotionY();
    void sendNextPoint();
    void startArriveCheck(Point2D target);

    std::vector<Point2D> generateBowScanPathDense(
            float xLength,
            float yLength,
            float yStep
        );


    QSettings *settings;
    int origin;
    int start;
    int end;


    int xo;
    int yo;

    int xs;
    int ys;

    int xe;
    int ye;

    int sweep;


    float xlenght;
    float ylenght;
    float step;

    Point2D position;


private:
    void initWidget();
    void connectFun();
    void updateWight();
    void saveseting();
private slots:

    void scanEnd();

    void on_connectBtn_clicked();

    void updatePosition(float pos);
    void updatePosition2(float pos);
    void setMOrigin();

    void setOrigin();
    void setStart();
    void setEnd();
    void on_backZero();

    void backOrigin_velocity();
    void jog_velocity();
    void originSpeed();
    void startSpeed();
    void endSpeed();

    void regin();


    void setBtn();
private:
    Ui::MainWindow *ui;

    ScanControlAbstract *scanCtrl;
    ScanControlAbstract *scanCtrl2;

    ScanControlHuiChuan *scanCtrlHunChuan;
    ScanControlHuiChuan *scanCtrlTaiDa;

};

#endif // MAINWINDOW_H
