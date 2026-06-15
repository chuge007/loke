#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QDoubleValidator>
#include <QCheckBox>
#include <QElapsedTimer>



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
     bool firstPosReceived = false;     // 至少收到一次真实位置回包
     bool arriveActive = false;         // 当前是否在等待到位
     Point2D arriveTarget = {0, 0};     // 当前目标点(逻辑扫查/步进坐标)
     QElapsedTimer arriveElapsed;       // 单点到位超时计时
     int  arriveResendCount = 0;        // 防呆：当前点已重发次数(粘帧丢命令时救命)
     float arriveLastDist = -1.0f;      // 上次兜底 tick 时的距离，用于侦测卡死
     QElapsedTimer arriveStallSince;    // 距离不再缩短的起始时刻
     void checkArrival();





    uint32_t axisCommandSpeed(bool isXAxisCommand) const;
    Point2D pathPointToPhysicalTargets(const Point2D& point) const;
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
