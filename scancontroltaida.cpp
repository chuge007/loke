#include "ScanControlTaiDa.h"

#include <QDebug>
#include <qwidget.h>
#include <QDebug>
#include <qwidget.h>
#include <QMessageBox>
#include <QTimer>
#include <QProgressDialog>
#include <QCoreApplication>
#include <QTcpSocket>
#include <QNetworkProxy>

#include "locke_log.h"


// 辅助函数：计算字节序列的校验和（简单求和）
quint8 ScanControlTaiDa::calculateChecksum(const QByteArray &data) {
    quint8 sum = 0;
    for (int i = 0; i < data.size();  ++i) {
        sum += static_cast<quint8>(data.at(i));
    }
    return sum;
}

void ScanControlTaiDa::runPosintion(int64_t angleControl, uint32_t maxSpeed){
    qCWarning(lcBus) << "位置控制: 台达控制器未实现，指令被忽略, 目标" << angleControl / 6000.0
                     << "mm 限速" << maxSpeed / 6000.0 << "mm/s";
}


void ScanControlTaiDa::processFrame(const QByteArray& frame)
{
    // 最小长度校验
    // CMD(5) + DATA(8) = 13 byte
    if (frame.size() < 13) {
        qCDebug(lcBus) << "[0x9C] 帧长错误:" << frame.size() << frame.toHex(' ').toUpper();
        return;
    }

    const uint8_t* buf = reinterpret_cast<const uint8_t*>(frame.constData());

    // ========== 帧头校验 ==========
    if (buf[0] != 0x3E || buf[1] != 0x9C) {
        qCDebug(lcBus) << "[0x9C] 帧头错误:" << frame.toHex(' ').toUpper();
        return;
    }

    uint8_t id   = buf[2];
    uint8_t len  = buf[3];
    uint8_t cmd_sum = buf[4];

    // CMD 校验
    uint8_t calc_cmd_sum = buf[0] + buf[1] + buf[2] + buf[3];
    if (cmd_sum != calc_cmd_sum) {
        qCDebug(lcBus) << "[0x9C] 命令校验错误:" << frame.toHex(' ').toUpper();
        return;
    }

    // DATA 起始索引
    int dataIndex = 5;

    // DATA 校验
    uint8_t data_sum = buf[dataIndex + 7];  // DATA_SUM
    uint8_t calc_data_sum = 0;
    for (int i = 0; i < 7; ++i) {
        calc_data_sum += buf[dataIndex + i];
    }

    if (data_sum != calc_data_sum) {
        qCDebug(lcBus) << "[0x9C] 数据校验错误:" << frame.toHex(' ').toUpper();
        return;
    }

    // ========== 数据解析 ==========
    int8_t temperature = static_cast<int8_t>(buf[dataIndex + 0]);

    int16_t iq_or_power =
        (int16_t)(buf[dataIndex + 2] << 8 | buf[dataIndex + 1]);

    int16_t speed =
        (int16_t)(buf[dataIndex + 4] << 8 | buf[dataIndex + 3]);

    uint16_t encoder =
        (uint16_t)(buf[dataIndex + 6] << 8 | buf[dataIndex + 5]);

    // ========== 输出 ==========
    qCDebug(lcPos) << "[0x9C] ID:" << id
             << "Temp:" << temperature
             << "IQ/Power:" << iq_or_power
             << "Speed:" << speed
             << "Encoder:" << encoder;

    // 👉 这里就是你要的编码器值
    // encoder = 0 ~ 65535 (取决于 14/15/16bit 编码器)

    //emit positionChange(QPointF);
}

ScanControlTaiDa::ScanControlTaiDa(QObject *parent) :
    ScanControlAbstract(parent)
{

}

ScanControlTaiDa::~ScanControlTaiDa()
{
    qCInfo(lcBus) << "台达控制器析构";
    userDisconnect = true;
    if (tcpSocket) {
        if (tcpSocket->state() != QAbstractSocket::UnconnectedState)
            tcpSocket->abort();
        delete tcpSocket;
        tcpSocket = nullptr;
    }
    if (timer) {
        timer->stop();
        delete timer;
        timer = nullptr;
    }
    delete settings;
    settings = nullptr;
}




void ScanControlTaiDa::connectFun()
{
    settings = new QSettings("./scan_setting.ini", QSettings::IniFormat);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0) && QT_CONFIG(textcodec)
    settings->setIniCodec("UTF-8");   //Qt5 INI 默认本地编码，中文会乱码
#endif

    timer = new QTimer(this);
    timer->setInterval(50);
    connect(timer, &QTimer::timeout, this, &ScanControlTaiDa::performTasks);


}



void ScanControlTaiDa::writeAxisStopStatus(int address)
{

}



void ScanControlTaiDa::readAxisEndState()
{

}




void ScanControlTaiDa::on_connectBtn_clicked()
{
    // 已连接时再点一次就是主动断开；未连接时复用同一个 socket，避免重复 new 造成信号堆积
    if (tcpSocket && tcpSocket->state() != QAbstractSocket::UnconnectedState) {
        userDisconnect = true;
        qCInfo(lcBus) << "操作员主动断开网关" << PlcIP << PlcPort
                      << "连接保持" << (connectedSince.isValid() ? connectedSince.elapsed() : -1) << "ms";
        tcpSocket->disconnectFromHost();
        return;
    }
    if (!tcpSocket)
        tcpSocket = new QTcpSocket(this);

    if (!socketSignalConnected) {
        socketSignalConnected = true;
        connect(tcpSocket, &QTcpSocket::connected, this, [this]{
            connectedSince.restart();
            warnedNotConnected = false;
            userDisconnect = false;
            qCInfo(lcBus) << "网关已连接" << PlcIP << PlcPort
                          << "对端" << tcpSocket->peerAddress().toString() << tcpSocket->peerPort();
            emit tcpStateChange(true);
        });
        connect(tcpSocket, &QTcpSocket::disconnected, this, [this]{
            const qint64 heldMs = connectedSince.isValid() ? connectedSince.elapsed() : -1;
            if (userDisconnect)
                qCInfo(lcBus) << "网关已断开(操作员主动断开)" << PlcIP << PlcPort
                              << "连接保持" << heldMs << "ms";
            else
                qCCritical(lcBus) << "网关连接意外断开" << PlcIP << PlcPort
                                  << "连接保持" << heldMs << "ms" << tcpSocket->errorString();
            userDisconnect = false;
            rxBuffer.clear();
            emit tcpStateChange(false);
        });
        connect(tcpSocket, &QTcpSocket::stateChanged, this,
                [](QAbstractSocket::SocketState state){
            qCInfo(lcBus) << "网关 socket 状态 ->" << lockeLog::socketStateName(state)
                          << "(" << static_cast<int>(state) << ")";
        });
        connect(tcpSocket,
                QOverload<QAbstractSocket::SocketError>::of(&QTcpSocket::error),
                this,
                [this](QAbstractSocket::SocketError err){
            qCWarning(lcBus) << "网关 socket 错误: 代码" << static_cast<int>(err)
                             << lockeLog::socketErrorName(err) << tcpSocket->errorString()
                             << "状态" << lockeLog::socketStateName(tcpSocket->state());
            emit tcpStateChange(false);
        });

        connect(tcpSocket, &QTcpSocket::readyRead, this, [this]() {
            const QByteArray chunk = tcpSocket->readAll();
            qCDebug(lcBus) << "RX" << chunk.size() << "字节" << chunk.toHex(' ').toUpper();
            rxBuffer.append(chunk);

            while (true) {
                int idx = rxBuffer.indexOf("\r\n");
                if (idx < 0) break;

                QByteArray frame = rxBuffer.left(idx);
                rxBuffer.remove(0, idx + 2);

                processFrame(frame);
            }
        });
    }

    qCInfo(lcBus) << "发起台达网关 TCP 连接" << PlcIP << PlcPort
                  << "当前状态" << lockeLog::socketStateName(tcpSocket->state());
    tcpSocket->setProxy(QNetworkProxy::NoProxy);
    tcpSocket->connectToHost(PlcIP, PlcPort);
}


void ScanControlTaiDa::on_setOriginBtn_clicked()
{
    if (!tcpSocket || tcpSocket->state() != QAbstractSocket::ConnectedState) {
        qCWarning(lcBus) << "设置零点失败: 网关未连接";
        return;
    }

    // 构造帧命令部分（同串口逻辑）
    QByteArray frame;
    frame.append(static_cast<char>(0x3E));      // 帧头
    frame.append(static_cast<char>(0x19));      // 命令
    frame.append(static_cast<char>(0x01));   // 电机ID
    frame.append(static_cast<char>(0x04));      // 数据长度
    quint8 cmdSum = calculateChecksum(frame);   // 校验和
    frame.append(static_cast<char>(cmdSum));

    const qint64 written = tcpSocket->write(frame);
    tcpSocket->flush();

    if (written != frame.size()) {
        qCWarning(lcBus) << "设置零点: 写 socket 失败, 应写" << frame.size() << "实际" << written
                         << "状态" << lockeLog::socketStateName(tcpSocket->state())
                         << "错误" << lockeLog::socketErrorName(tcpSocket->error())
                         << tcpSocket->errorString() << "hex:" << frame.toHex(' ').toUpper();
        return;
    }
    qCInfo(lcBus) << "TX 1号电机 当前位置写为零点(0x19):" << frame.toHex(' ').toUpper();
}



void ScanControlTaiDa::on_xAddBtn_pressed()
{
    if (!jogAllowed("X+ 按下")) return;
    updateCurPos = false;
    axisJog = AxisJog::XJogAddPressed;

}

void ScanControlTaiDa::on_xAddBtn_released()
{
    if (!jogAllowed("X+ 松开")) return;
    updateCurPos = false;
    axisJog = AxisJog::XJogAddReleased;

}

void ScanControlTaiDa::on_xSubBtn_pressed()
{
    if (!jogAllowed("X- 按下")) return;
    updateCurPos = false;
    axisJog = AxisJog::XJogSubPressed;
}

void ScanControlTaiDa::on_xSubBtn_released()
{
    if (!jogAllowed("X- 松开")) return;
    updateCurPos = false;
    axisJog = AxisJog::XJogSubReleased;
}

void ScanControlTaiDa::on_yAddBtn_pressed()
{
    if (!jogAllowed("Y+ 按下")) return;
    updateCurPos = false;
    axisJog = AxisJog::YJogAddPressed;
}

void ScanControlTaiDa::on_yAddBtn_released()
{
    if (!jogAllowed("Y+ 松开")) return;
    updateCurPos = false;
    axisJog = AxisJog::YJogAddReleased;
}

void ScanControlTaiDa::on_ySubBtn_pressed()
{
    if (!jogAllowed("Y- 按下")) return;
    updateCurPos = false;
    axisJog = AxisJog::YJogSubPressed;
}

void ScanControlTaiDa::on_ySubBtn_released()
{
    if (!jogAllowed("Y- 松开")) return;
    updateCurPos = false;
    axisJog = AxisJog::YJogSubReleased;
}

void ScanControlTaiDa::on_zAddBtn_pressed()
{
    if (!jogAllowed("Z+ 按下")) return;
    updateCurPos = false;
    axisJog = AxisJog::ZJogAddPressed;
}

void ScanControlTaiDa::on_zAddBtn_released()
{
    if (!jogAllowed("Z+ 松开")) return;
    updateCurPos = false;
    axisJog = AxisJog::ZJogAddReleased;
}

void ScanControlTaiDa::on_zSubBtn_pressed()
{
    if (!jogAllowed("Z- 按下")) return;
    updateCurPos = false;
    axisJog = AxisJog::ZJogSubPressed;
}

void ScanControlTaiDa::on_zSubBtn_released()
{
    if (!jogAllowed("Z- 松开")) return;
    updateCurPos = false;
    axisJog = AxisJog::ZJogSubReleased;
}

void ScanControlTaiDa::on_rAddBtn_pressed()
{
    if (!jogAllowed("R+ 按下")) return;
    updateCurPos = false;
    axisJog = AxisJog::RJogAddPressed;
}

void ScanControlTaiDa::on_rAddBtn_released()
{
    if (!jogAllowed("R+ 松开")) return;
    updateCurPos = false;
    axisJog = AxisJog::RJogAddReleased;
}

void ScanControlTaiDa::on_rSubBtn_pressed()
{
    if (!jogAllowed("R- 按下")) return;
    updateCurPos = false;
    axisJog = AxisJog::RJogSubPressed;
}

void ScanControlTaiDa::on_rSubBtn_released()
{
    if (!jogAllowed("R- 松开")) return;
    updateCurPos = false;
    axisJog = AxisJog::RJogSubReleased;
}


void ScanControlTaiDa::on_setLimitBtn_clicked()
{
    qCWarning(lcUi) << "设置机械极限位: 台达控制器未实现，忽略";
}


bool ScanControlTaiDa::jogAllowed(const char* action)
{
    if (!tcpSocket || tcpSocket->state() != QAbstractSocket::ConnectedState) {
        qCWarning(lcUi) << "点动" << action << "被忽略: 网关未连接";
        return false;
    }
    if ((tasks.count() != 0) || isEndScan || isAxisStop || isJogDone || isRunTarget) {
        qCWarning(lcUi) << "点动" << action << "被忽略: 控制器忙"
                        << "任务" << tasks.count() << "结束中" << isEndScan << "停止中" << isAxisStop
                        << "点动完成" << isJogDone << "执行目标点" << isRunTarget;
        return false;
    }
    qCInfo(lcUi) << "点动" << action;
    return true;
}





void ScanControlTaiDa::init()
{
    qCInfo(lcBus) << "台达控制器初始化";
    connectFun();

}




void ScanControlTaiDa::performTasks()
{

    updataCurrentPos();
    perfromJogTasks();
}



void ScanControlTaiDa::updataCurrentPos()
{

    if (!tcpSocket || tcpSocket->state() != QAbstractSocket::ConnectedState) {
        if (!warnedNotConnected) {
            warnedNotConnected = true;
            qCWarning(lcBus) << "位置查询暂停: 网关未连接";
        }
        return;
    }

    // 构造帧命令部分（同串口逻辑）
    QByteArray frame;
    frame.append(static_cast<char>(0x3E));      // 帧头
    frame.append(static_cast<char>(0x9C));      // 命令
    frame.append(static_cast<char>(0x01));   // 电机ID
    frame.append(static_cast<char>(0x04));      // 数据长度
    quint8 cmdSum = calculateChecksum(frame);   // 校验和
    frame.append(static_cast<char>(cmdSum));

    const qint64 written = tcpSocket->write(frame);
    tcpSocket->flush();

    if (written != frame.size()) {
        qCWarning(lcBus) << "位置查询: 写 socket 失败, 应写" << frame.size() << "实际" << written
                         << "状态" << lockeLog::socketStateName(tcpSocket->state())
                         << "错误" << lockeLog::socketErrorName(tcpSocket->error())
                         << tcpSocket->errorString() << "hex:" << frame.toHex(' ').toUpper();
        return;
    }
    qCDebug(lcBus) << "TX 1号电机 读编码器(0x9C):" << frame.toHex(' ').toUpper();

}



void ScanControlTaiDa::perfromJogTasks()
{


    if(axisJog != AxisJog::NotAxisJog && axisInch == AxisJog::NotAxisJog){
        switch (axisJog) {
            case XJogAddPressed:{
                sendCommandTcp(0x01, speed,0xA2);
            }break;
            case XJogAddReleased:{
               sendCommandTcp(0x01, 0,0xA2);
            }break;
            case XJogSubPressed:{
               sendCommandTcp(0x01, -speed,0xA2);
            }break;
            case XJogSubReleased:{
                sendCommandTcp(0x01, 0,0xA2);
            }break;
        }

        lastAxisJog=axisJog;
    }
}




void ScanControlTaiDa::sendCommandTcp(quint8 motorId, int32_t speedValue,int cmd) {

    if (!tcpSocket || tcpSocket->state() != QAbstractSocket::ConnectedState) {
        qCWarning(lcBus) << "速度指令未发送: 网关未连接, 电机" << motorId << "速度" << speedValue;
        return;
    }

    // 构造帧命令部分（同串口逻辑）
    QByteArray frame;
    frame.append(static_cast<char>(0x3E));      // 帧头
    frame.append(static_cast<char>(cmd));      // 命令
    frame.append(static_cast<char>(motorId));   // 电机ID
    frame.append(static_cast<char>(0x04));      // 数据长度
    quint8 cmdSum = calculateChecksum(frame);   // 校验和
    frame.append(static_cast<char>(cmdSum));

    // 数据部分
    QByteArray dataFrame;
    quint8* bytes = reinterpret_cast<quint8*>(&speedValue);
    dataFrame.append(static_cast<char>(bytes[0]));
    dataFrame.append(static_cast<char>(bytes[1]));
    dataFrame.append(static_cast<char>(bytes[2]));
    dataFrame.append(static_cast<char>(bytes[3]));
    quint8 dataSum = calculateChecksum(dataFrame);
    dataFrame.append(static_cast<char>(dataSum));

    // 合并帧并发送
    frame.append(dataFrame);
    const qint64 written = tcpSocket->write(frame);
    tcpSocket->flush();

    if (written != frame.size()) {
        qCWarning(lcBus) << "速度指令: 写 socket 失败, 电机" << motorId
                         << "状态" << lockeLog::socketStateName(tcpSocket->state())
                         << "错误" << lockeLog::socketErrorName(tcpSocket->error())
                         << tcpSocket->errorString() << "hex:" << frame.toHex(' ').toUpper();
        return;
    }
    qCInfo(lcBus) << "TX 电机" << motorId << "速度控制 cmd=0x" + QString::number(cmd, 16).toUpper()
                  << "速度值" << speedValue;
    qCDebug(lcBus) << "TX hex:" << frame.toHex(' ').toUpper();
}


void ScanControlTaiDa::on_start(){
    qCDebug(lcBus) << "开始: 台达控制器 on_start 未实现";
}
void ScanControlTaiDa::on_stop(){
    qCWarning(lcBus) << "暂停: 台达控制器 on_stop 未实现，电机不会停止";
}
void ScanControlTaiDa::on_end(){
    qCWarning(lcBus) << "结束: 台达控制器 on_end 未实现，电机不会停止";
}
void ScanControlTaiDa::on_backZero(){
    qCWarning(lcBus) << "回原: 台达控制器 on_backZero 未实现";
}


void ScanControlTaiDa::on_alarmReset(){
    qCWarning(lcUi) << "点击 复位(清除报警): 台达控制器未实现清除报警指令，忽略";
}
