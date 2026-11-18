#ifndef ScanControlHuiChuan_H
#define ScanControlHuiChuan_H

#include "scancontrolabstract.h"
#include <QMutex>
#include <QTcpSocket>
#include <QElapsedTimer>

class ScanControlHuiChuan : public ScanControlAbstract
{
    Q_OBJECT

public:
    explicit ScanControlHuiChuan(QObject *parent = nullptr);
    ~ScanControlHuiChuan();

    void setModbusTcpIP(QString ip) { PlcIP = ip;}   //设置IP
    void setModbusPort(int port) { PlcPort = port;}   //设置端口


public slots:
    void init();

    void on_connectBtn_clicked();

    void on_setOriginBtn_clicked();     //设置起点

    //点动
    void on_xAddBtn_pressed();  //X+
    void on_xAddBtn_released();
    void on_xSubBtn_pressed();  //X-
    void on_xSubBtn_released();
    void on_yAddBtn_pressed();  //Y+
    void on_yAddBtn_released();
    void on_ySubBtn_pressed();  //Y-
    void on_ySubBtn_released();
    void on_zAddBtn_pressed();  //Y+
    void on_zAddBtn_released();
    void on_zSubBtn_pressed();  //Y-
    void on_zSubBtn_released();
    void on_rAddBtn_pressed();  //Z+
    void on_rAddBtn_released();
    void on_rSubBtn_pressed();  //Z-
    void on_rSubBtn_released() ;

    void runPosintion(int64_t angleControl, uint32_t maxSpeed);

    void on_setLimitBtn_clicked();      //设置机械极限位

    void on_start();
    void on_stop();
    void on_end();
    void on_alarmReset();
    void on_backZero();
    void releaseMotors();          //断使能：0x80 电机关闭，电机可手推
    void ensureMotorRunning(quint8 motorId);   //0x88：电机关闭状态下发运动指令前恢复运行

    void processFrame(const QByteArray& frame);
    void processFrame92(const QByteArray& frame);
    void parseAngleByChecksum();   //容错解析：不依赖0x3E帧头，用DATA_SUM锚定角度
    void dispatchAxisMm(uint8_t axisId, double mm, bool strictAxis);  //带跳变过滤的位置派发
    quint8 calculateChecksum(const QByteArray &data);


private:
        QMutex mutex;
        AxisJog lastAxisJog = AxisJog::NotAxisJog;

private slots:
    void performTasks();
    void tryAutoReconnect();          //意外断线后自动重连




private:


    void writeAxisStopStatus(int address);
    void writeBackZero();
    void readAxisEndState();
    void releaseMotorsDirect(const char* reason, bool waitForSend = false);   //链路异常时绕开队列直接写 0x80

    void updataCurrentPos();        //更新当前点位置
    void perfromJogTasks();         //点动
    bool jogAllowed(const char* action);   //点动前置检查，被拒时写日志说明原因

    // 所有发往网关的帧统一走这里排队，由 performTasks 串行发出，
    // 避免控制指令和 0x92 位置查询在半双工总线上撞帧导致位置回包丢失。
    void enqueueFrame(const QByteArray& frame, bool urgent = false);
    bool writeFrame(const QByteArray& frame);
    void resetBusState();
    void reportStats();             //周期输出通讯健康统计



private:
    QTcpSocket *tcpSocket = nullptr;
    QString PlcIP;
    int PlcPort = 502;
    QSettings *settings = nullptr;
    QTimer *timer = nullptr;
    QByteArray rxBuffer;

    QQueue<QByteArray> txQueue;     //待发送帧队列(控制指令优先于位置查询)
    QElapsedTimer busIdle;          //距上一次发帧/收到位置回包的时间
    QElapsedTimer posReplyWait;     //当前位置查询已等待时间
    QElapsedTimer sinceMotionCmd;   //距最近一次运动指令(0xA4/0xA2)发出的时间
    quint8 activeAxisId = 0;        //最近一次运动指令的轴：2=X, 1=Y, 0=未知
    int rejectCountX = 0;           //X 连续跳变拒绝次数
    int rejectCountY = 0;           //Y 连续跳变拒绝次数

    // 单轴通讯统计：窗口计数在每次 reportStats 后清零
    struct AxisStats {
        int queries = 0;            //发出的位置查询
        int replies = 0;            //按时收到的回包
        int timeouts = 0;           //超时次数
        int late = 0;               //迟到/多余回包
        int rejects = 0;            //被跳变/量程过滤丢掉的值
        int timeoutStreak = 0;      //连续超时(不随窗口清零)
        QElapsedTimer sinceUpdate;  //距上一次被接受的位置
    };
    AxisStats statX;
    AxisStats statY;
    AxisStats& axisStat(uint8_t axisId) { return axisId == 2 ? statX : statY; }
    QElapsedTimer statWindow;       //统计窗口计时
    QElapsedTimer connectedSince;   //本次连接建立时刻，断线时用于计算连接保持时长
    int statCmdFrames = 0;          //窗口内发出的控制指令帧
    int statWriteFails = 0;         //窗口内写 socket 失败
    int statQueueDrops = 0;         //窗口内队列溢出丢弃
    int statQueuePeak = 0;          //窗口内队列峰值
    int statJunkBytes = 0;          //窗口内丢弃的非位置字节(含指令应答)
    // 位置回包解析层级命中数：L1=完整帧，L2/L3=被吃掉帧头/截断的残帧。
    // 网关真正透明时 L2/L3 恒为 0，这两项是网关配置是否正确的直接证据。
    int statParseL1 = 0;
    int statParseL2 = 0;
    int statParseL3 = 0;
    bool userDisconnect = false;    //是否为操作员主动断开
    bool handlingSocketError = false;   //socket 错误处理中，防止嵌套错误递归
    bool motorOffX = true;          //X 轴处于 0x80 电机关闭状态（上电未知，先按关闭处理）
    bool motorOffY = true;          //Y 轴处于 0x80 电机关闭状态
    QTimer *reconnectTimer = nullptr;   //意外断线后的重连定时器
    QTimer *connectTimeoutTimer = nullptr;  //单次连接超时；复用定时器，避免旧回调误伤新连接
    int reconnectAttempts = 0;          //连续自动重连次数
    int lastWifiResetAttempt = -1;      //上次因重连失败而重置 Wi-Fi 的次数
    void scheduleReconnect(const char* reason);



    void sendCommandTcp(quint8 motorId, int speedValue,int cmd);

    float xScanLenght = 0;
    float yScanLenght = 0;
    float yScanStep = 0;
    float xVelocity = 20;
    float yVelocity = 20;
    float jogVelocity = 20;
    int jopStep = 1;
    double keepTime = 0;

    bool isInit = true;         //链接服务初始化
    bool isStartScan = false;   //开始扫查标志位
    bool isPerform = false;     //任务执行标志位
    bool isStopScan = false;    //暂停扫查标志位
    bool isKeepScan = false;    //继续扫查标志位
    bool isAxisStop = false;    //轴停止状态
    bool updateCurPos = false;  //更新轴位置
    bool isEndScan = false;     //结束扫查标志位
    bool isJogDone = false;     //点动完成标志位

    bool isRunTarget = false;   //执行目标点

    bool isHaveMachine = true;  //判断是否有机械原点
    bool isHaveLimit = true;    //判断是否有机械极限

    bool isSocketSignalConnected = false;  //socket信号是否已连接(防止重复connect)

    uint8_t lastQueriedAxisId = 2;  //最近一次查询的轴ID(用于容错解析时判断x/y)
    bool waitingPosReply = false;   //是否有位置查询在等待回包
    bool queryXNext = true;         //下次优先查询X轴
    int missedPosReplyCount = 0;    //位置查询超时计数
    bool hasLastX = false;          //是否已收到过有效x角度
    bool hasLastY = false;          //是否已收到过有效y角度
    double lastXmm = 0;             //上次有效x位置(mm)，用于跳变过滤
    double lastYmm = 0;             //上次有效y位置(mm)



    QQueue<QPair<QString, float>> tasks;    //任务列表

    AxisJog axisInch = AxisJog::NotAxisJog;      //寸动
    AxisJog  axisJog = AxisJog::NotAxisJog;      //点动

    QPointF zeroPoint  = QPointF(0,0);       //虚拟0点
    QPointF limitPoint = QPointF(500, 400);  //极限位置
    QPointF currentPos = QPointF(0,0);

};

#endif // ScanControlHuiChuan_H
