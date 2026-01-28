#ifndef SCANCONTROLABSTRACT_H
#define SCANCONTROLABSTRACT_H

#include <QObject>
#include <QModbusTcpClient>
#include <QModbusReply>
#include <QDoubleValidator>
#include <QPointF>
#include <QTimer>
#include <QQueue>
#include <QPair>
#include <QSettings>
#include <QTimer>
#include <QTime>

#include "modbusconfig.h"

enum ScanModel{
    NormalScan = 0, //正常模式
    SingleScan      //单边模式
};

enum AxisJog{       //寸动
    NotAxisJog = 0,
    XJogAdd,
    XJogSub,
    YJogAdd,
    YJogSub,
    ZJogAdd,
    ZJogSub,
    RJogAdd,
    RJogSub,

    XJogAddPressed ,
    XJogAddReleased,
    XJogSubPressed ,
    XJogSubReleased,
    YJogAddPressed ,
    YJogAddReleased,
    YJogSubPressed ,
    YJogSubReleased,
    ZJogAddPressed ,
    ZJogAddReleased,
    ZJogSubPressed ,
    ZJogSubReleased,
    RJogAddPressed ,
    RJogAddReleased,
    RJogSubPressed ,
    RJogSubReleased,
};

enum AxisState{
    XMoveDone = 0,
    YMoveDone
};

#define HuiChuan_IP "192.168.1.88"
#define TaiDa_IP    "192.168.1.5"

class ScanControlAbstract : public QObject
{
    Q_OBJECT

public:
    explicit ScanControlAbstract(QObject *parent = nullptr) : QObject(parent){}
    virtual ~ScanControlAbstract() {}

    virtual void setModbusTcpIP(QString ip)  = 0;   //设置IP
    virtual void setModbusPort(int port)  = 0;      //设置端口

    virtual QPointF virtualOrigin() const  = 0;     //虚拟0点
    virtual QPointF limitRegion() const  = 0;       //极限区域
    virtual QPointF currentPosition() const  = 0;   //当前位置

    virtual float xLenght() const  = 0;
    virtual float yLenght() const  = 0;
    virtual float yStep()   const  = 0;

    virtual void setXAxisVelocity(float vel) = 0;   //设置X轴速度
    virtual void setYAxisVelocity(float vel) = 0;   //设置Y轴速度
    virtual void setJogVelocity(float vel) = 0;     //设置点动速度

    virtual void setAxisJogStep(int step) = 0;      //设置轴寸动步进

    virtual bool isXCrossed() = 0;                  //判断扫查区域的X轴是否越界
    virtual bool isYCrossed() = 0;                  //判断扫查区域的Y轴是否越界
    virtual bool isJogCrossed(int &address, float &data) = 0; //点动判断是都越界

    virtual ScanModel currentScanModel() const = 0;       //当前扫查模式
    virtual QSettings* scanControlSetting() const = 0;    //配置表句柄

    virtual bool haveMachine() const = 0;           //是否有设置机械原点
    virtual bool haveLimit() const = 0;             //是否有设置机械极限位

    virtual void setManualModel(bool states) = 0;   //设置手动模式
    virtual bool nextScan() = 0;                    //扫查下一步

public slots:
    virtual void init() = 0;   //初始化设置
    virtual void destroy() = 0;  //销毁设置

    virtual void on_connectBtn_clicked() = 0;       //连接服务

    virtual void on_startScanBtn_clicked() = 0;     //开始扫查
    virtual void on_stopScanBtn_clicked() = 0;      //暂停扫查
    virtual void on_endScanBtn_clicked() = 0;       //结束扫描

    virtual void on_setOriginBtn_clicked() = 0;     //设置起点

    virtual void setXLenght(float lenght)  = 0;     //设置扫查X轴
    virtual void setYLenght(float lenght)  = 0;     //设置扫查Y轴
    virtual void setYStep(float step)      = 0;     //设置扫查步进

    virtual void on_x_velocity_editingFinished(float val) = 0;   // 设置X轴速度
    virtual void on_y_velocity_editingFinished(float val) = 0;   // 设置Y轴速度
    virtual void on_jog_velocity_editingFinished(float val) = 0; // 设置点动速度
    //台达速度设置接口
    virtual void on_backOrigin_velocity_editingFinished(float val) = 0; //回原点速度
    virtual void on_scan_velocity_editingFinished(float val) = 0;       //扫查速度
    virtual void on_addSub_velocity_editingFinished(float val) = 0;     //点动加减速
    //-----End

    virtual void on_jogStep_1_clicked() = 0;    //点动步进1
    virtual void on_jogStep_5_clicked() = 0;    //点动步进5
    virtual void on_jogStep_10_clicked() = 0;   //点动不经10

    //寸动
    virtual void on_xAddBtn_clicked() = 0;  //X+
    virtual void on_xSubBtn_clicked() = 0;  //X-
    virtual void on_yAddBtn_clicked() = 0;  //Y+
    virtual void on_ySubBtn_clicked() = 0;  //Y-
    virtual void on_zAddBtn_clicked() = 0;  //X+
    virtual void on_zSubBtn_clicked() = 0;  //X-
    virtual void on_rAddBtn_clicked() = 0;  //Y+
    virtual void on_rSubBtn_clicked() = 0;  //Y
    //点动
    virtual void on_xAddBtn_pressed() = 0;  //X+
    virtual void on_xAddBtn_released() = 0;
    virtual void on_xSubBtn_pressed() = 0;  //X-
    virtual void on_xSubBtn_released() = 0;
    virtual void on_yAddBtn_pressed() = 0;  //Y+
    virtual void on_yAddBtn_released() = 0;
    virtual void on_ySubBtn_pressed() = 0;  //Y-
    virtual void on_ySubBtn_released() = 0;
    virtual void on_zAddBtn_pressed() = 0;  //Z+
    virtual void on_zAddBtn_released() = 0;
    virtual void on_zSubBtn_pressed() = 0;  //Z-
    virtual void on_zSubBtn_released() = 0;
    virtual void on_rAddBtn_pressed() = 0;  //Z+
    virtual void on_rAddBtn_released() = 0;
    virtual void on_rSubBtn_pressed() = 0;  //Z-
    virtual void on_rSubBtn_released() = 0;

    virtual void on_alarmResetBtn_clicked() = 0;    //报警复位
    virtual void on_setLimitBtn_clicked() = 0;      //设置机械极限位
    virtual void on_setMachineBtn_clicked() = 0;    //设置机械极原点
    virtual void on_singleScan_toggled(bool checked) = 0;   //单边模式

    virtual void on_start()=0;
    virtual void on_stop()=0;
    virtual void on_end()=0;
    virtual void on_alarmReset()=0;
    virtual void on_backZero()=0;

    virtual void runTargetPosition(double x, double y) = 0; //运动到目标位置

    virtual void writeHoldingRegistersData(int address, quint16 size, float data)=0;
    virtual void writeAxisJog(int address, bool data)=0;
signals:
    void modbusStateChange(QModbusDevice::State);   //modbus连接状态
    void positionChange(QPointF);                   //轴位置变化
    void velocityChange(float x, float y, float jog);          //轴速度变化
    void axisError(int xErrorId, int yErrorId);     //轴报错
    void machineStart();    //机械开始按键
    void machineStop();     //机械暂停按键
    void scanRowNumChange(int num);     //扫查行数
    void scanRowEnd(AxisState);  //行扫查结束信号
    void scanTime(QString ms);  //扫查时间-单位毫秒

    //台达新增信号
    void scanScope(float xLenght, float yLenght, float yStep);
    //参数 1.点动速度 2.点动加减速 3.回原点速度 4.扫查速度
    void scanSetVelocity(float v1, float v2, float v3, float v4);


};

#endif // SCANCONTROLABSTRACT_H
