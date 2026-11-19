#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QDoubleValidator>
#include <QCheckBox>
#include <QElapsedTimer>
#include <QTimer>



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

    bool stopScan = true;   // 上电默认“未在扫查”，避免未初始化时被误读

    // ---- 事件驱动到位检测 ----
    // 不再用 50ms 轮询比较位置，改成：每次 updatePosition* 收到真实位置回包
    // 后立刻调用 checkArrival()，到位/超时就推进；arriveTimer 只是兜底超时器。
    bool firstPosReceived = false;
    bool arriveActive = false;
    Point2D arriveTarget = {0, 0};
    bool arriveCheckX = false;  // 是否检测X轴到位
    bool arriveCheckY = false;  // 是否检测Y轴到位
    QElapsedTimer arriveElapsed;
    int arriveResendCount = 0;
    float arriveLastDist = -1.0f;
    QElapsedTimer arriveStallSince;
    void checkArrival();

    // 扫查过程统计，扫查结束时输出一行汇总
    QElapsedTimer scanElapsed;
    int scanResendTotal = 0;      //卡滞重发次数
    int scanForcedTotal = 0;      //超时强制跳点次数(这些点可能漏扫)
    void enableManualControls();

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
    void startArriveCheck(Point2D target, bool checkX, bool checkY);

    std::vector<Point2D> generateBowScanPathDense(
            float xLength,
            float yLength,
            float yStep
        );


    QSettings *settings;

    float xlenght;
    float ylenght;
    float step;

    Point2D position;


private:
    void initWidget();
    void connectFun();
    void saveseting();
private slots:

    void scanEnd();

    void on_connectBtn_clicked();

    void updatePosition(float pos);
    void updatePosition2(float pos);
    void setMOrigin();

    void setStart();
    void on_backZero();

    void backOrigin_velocity();
    void jog_velocity();

private:
    Ui::MainWindow *ui;

    ScanControlAbstract *scanCtrl;

};

#endif // MAINWINDOW_H
