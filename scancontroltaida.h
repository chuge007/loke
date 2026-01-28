#ifndef ScanControlTaiDa_H
#define ScanControlTaiDa_H

#include "scancontrolabstract.h"
#include <QMutex>
class ScanControlTaiDa : public ScanControlAbstract
{
    Q_OBJECT

public:
    explicit ScanControlTaiDa(QObject *parent = nullptr);
    ~ScanControlTaiDa();

    void setModbusTcpIP(QString ip) { PlcIP = ip;}   //设置IP
    void setModbusPort(int port) { PlcPort = port;}   //设置端口

    QPointF virtualOrigin() const { return zeroPoint; }     //虚拟0点
    QPointF limitRegion() const { return limitPoint; }      //极限区域
    QPointF currentPosition() const { return currentPos;}   //当前位置

    float xLenght() const { return xScanLenght;}
    float yLenght() const { return yScanLenght;}
    float yStep()   const { return yScanStep;}

    void setXAxisVelocity(float vel);       //设置X轴速度
    void setYAxisVelocity(float vel);       //设置Y轴速度
    void setJogVelocity(float vel);         //设置点动速度

    void setAxisJogStep(int step);          //设置轴寸动步进

    bool isXCrossed();   //判断扫查区域的X轴是否越界
    bool isYCrossed();   //判断扫查区域的Y轴是否越界
    bool isJogCrossed(int &address, float &data);

    ScanModel currentScanModel() const{ return scanModel;}      //当前扫查模式
    QSettings* scanControlSetting() const{ return settings;}    //配置表句柄

    bool haveMachine() const { return isHaveMachine;}   //是否有设置机械原点
    bool haveLimit() const {return isHaveLimit;}        //是否有设置机械极限位

    void setManualModel(bool states);
    bool nextScan();

public slots:
    void init();
    void destroy();

    void on_connectBtn_clicked();

    void on_startScanBtn_clicked();     //开始扫查
    void on_stopScanBtn_clicked();      //暂停扫查
    void on_endScanBtn_clicked();       //结束扫描

    void on_setOriginBtn_clicked();     //设置起点

    void setXLenght(float lenght) { xScanLenght = lenght;}  //设置扫查X轴
    void setYLenght(float lenght) { yScanLenght = lenght;}  //设置扫查Y轴
    void setYStep(float step)     { yScanStep = step;}      //设置扫查步进

    void on_x_velocity_editingFinished(float val); // 设置X轴速度
    void on_y_velocity_editingFinished(float val); // 设置Y轴速度
    void on_jog_velocity_editingFinished(float val); // 设置点动速度

    //台达速度设置接口
    void on_backOrigin_velocity_editingFinished(float val) { Q_UNUSED(val)} //回原点速度
    void on_scan_velocity_editingFinished(float val) {Q_UNUSED(val)}       //扫查速度
    void on_addSub_velocity_editingFinished(float val) {Q_UNUSED(val)}     //点动加减速
    //-----End

    void on_jogStep_1_clicked();    //点动步进1
    void on_jogStep_5_clicked();    //点动步进5
    void on_jogStep_10_clicked();   //点动不经10

    //寸动
    void on_xAddBtn_clicked();  //X+
    void on_xSubBtn_clicked();  //X-
    void on_yAddBtn_clicked();  //Y+
    void on_ySubBtn_clicked();  //Y-
    void on_zAddBtn_clicked();  //X+
    void on_zSubBtn_clicked();  //X-
    void on_rAddBtn_clicked();  //Y+
    void on_rSubBtn_clicked();  //Y

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

    void on_alarmResetBtn_clicked();    //报警复位
    void on_setLimitBtn_clicked();      //设置机械极限位
    void on_setMachineBtn_clicked();    //设置机械极原点
    void on_singleScan_toggled(bool checked);   //单边模式

    void on_start();
    void on_stop();
    void on_end();
    void on_alarmReset();
    void on_backZero();
    bool sendPulseCommand(QModbusClient *modbusClient, QModbusDataUnit::RegisterType rGtype, quint16 address);

    void runTargetPosition(double x, double y);

private:
        QMutex mutex;
        AxisJog lastAxisJog = AxisJog::NotAxisJog;
private slots:
    void modbusState();
    void performTasks();

private:
    void initWidget();
    void connectFun();
    void readSetting();
    void writeSetting();
    void initStates();

    //float readModbusFloatData(int v1, int v2);
    float readModbusFloatData(quint16 lowWord, quint16 highWord);
    QPair<quint16, quint16> writeModbusFloatData(float value);
    void writeHoldingRegistersData(int address, quint16 size, float data);
    void readAxisRunStatus(int address, quint16 size);
    void writeAxisStopStatus(int address);
    void writeEndScanStatus();
    void writeBackZero();
    void readAxisEndState();
    void writeAxisVelocity(int address, quint16 size, float data);
    void readAxisVelocity(int address, quint16 size);
    void readAxisJogStatus(int address);
    void readAxisErrorID();
    void writeAxisReset();
    void writeAxisLimitPosition();
    void writeAxisMachineOrigin();
    void writeAxisJog(int address, bool data);
    void writeTargetPosition(double x, double y);

    void initTasks();               //初始化任务

    void creataTasksTable();        //创建自动扫查任务
    void perfromStartScanTasks();   //执行自动扫查任务
    void updataCurrentPos();        //更新当前点位置

    void perfromStopScanTasks();    //执行暂停
    void perfromEndScanTasks();     //结束扫查

    void perfromJogTasks();         //点动



private:
    QModbusTcpClient *modbusClient = nullptr;
    QString PlcIP;
    int PlcPort = 502;
    QSettings *settings = nullptr;
    QTimer *timer = nullptr;

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

    bool manState[2] = {false, false};  //手动状态位

    QQueue<QPair<QString, float>> tasks;    //任务列表

    ScanModel scanModel = ScanModel::NormalScan;    //扫查模式
    AxisJog axisInch = AxisJog::NotAxisJog;      //寸动
    AxisJog  axisJog = AxisJog::NotAxisJog;      //点动

    QPointF zeroPoint  = QPointF(0,0);       //虚拟0点
    QPointF limitPoint = QPointF(500, 400);  //极限位置
    QPointF currentPos = QPointF(0,0);

};

#endif // ScanControlTaiDa_H
