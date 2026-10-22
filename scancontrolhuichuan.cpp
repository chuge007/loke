#include "ScanControlHuiChuan.h"
#include "locke_log.h"

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
#include <QElapsedTimer>

namespace {
// 调度节拍：10ms 检查一次，真正的发送节奏由下面的间隔控制
constexpr int kTickMs          = 10;
// 两帧之间至少留出的总线空闲时间，给网关转发和电机应答留余量
constexpr int kBusGapMs        = 30;
// 位置查询等待回包的超时；超时后放弃本次，换另一轴继续查
constexpr int kPosReplyTimeout = 200;
// 跳变过滤连续拒绝这么多次后，认为是真实位置变化，强制接受
constexpr int kMaxJumpRejects  = 3;
// 发送队列上限，防止断线期间无限堆积
constexpr int kMaxTxQueue      = 32;
// 通讯统计输出周期；窗口内有异常时立即按 WARN 输出
constexpr int kStatIntervalMs  = 10000;
// 某轴超过这么久没有被接受的位置，视为位置停更
constexpr int kStaleMs         = 2000;
// 连续超时达到这个次数开始告警
constexpr int kTimeoutWarnStreak = 3;

// 把发出的帧翻译成可读文字，日志里直接看出发了什么、给哪个轴、目标多少
QString describeFrame(const QByteArray& f)
{
    if (f.size() < 5) return QString("短帧 %1").arg(QString(f.toHex(' ').toUpper()));
    const quint8 cmd = quint8(f[1]);
    const quint8 id  = quint8(f[2]);
    const QString axis = (id == 2) ? "X" : (id == 1) ? "Y" : QString("ID%1").arg(id);
    auto le = [&f](int from, int bytes) {
        qint64 v = 0;
        for (int i = 0; i < bytes; ++i) v |= qint64(quint8(f[from + i])) << (8 * i);
        return v;
    };
    QString text;
    switch (cmd) {
    case 0xA4:
        if (f.size() >= 18)
            text = QString("位置控制 电机坐标=%1mm 限速=%2mm/s")
                   .arg(le(5, 8) / 6000.0, 0, 'f', 2)
                   .arg(quint32(le(13, 4)) / 6000.0, 0, 'f', 2);
        else text = "位置控制(帧长异常)";
        break;
    case 0xA2:
        if (f.size() >= 10)
            text = QString("速度控制 %1mm/s").arg(qint32(le(5, 4)) / 6000.0, 0, 'f', 2);
        else text = "速度控制(帧长异常)";
        break;
    case 0x81: text = "停止"; break;
    case 0x19: text = "当前位置写为零点"; break;
    case 0x76: text = "电机重启"; break;
    case 0x9B: text = "清除报警"; break;
    case 0x92: text = "读位置"; break;
    default:   text = QString("cmd=0x%1").arg(cmd, 2, 16, QChar('0')); break;
    }
    return QString("%1轴 %2").arg(axis, text);
}
}


// 辅助函数：计算字节序列的校验和（简单求和）
quint8 ScanControlHuiChuan::calculateChecksum(const QByteArray &data) {
    quint8 sum = 0;
    for (int i = 0; i < data.size();  ++i) {
        sum += static_cast<quint8>(data.at(i));
    }
    return sum;
}


//void ScanControlHuiChuan::processFrame(const QByteArray& frame)
//{
//    // 最小长度校验
//    // CMD(5) + DATA(8) = 13 byte
//    if (frame.size() < 13) {
//        qDebug() << "[FRAME] length error:" << frame.size();
//        return;
//    }

//    const uint8_t* buf = reinterpret_cast<const uint8_t*>(frame.constData());

//    // ========== 帧头校验 ==========
//    if (buf[0] != 0x3E || buf[1] != 0x90) {
//        qDebug() << "[FRAME] header error";
//        return;
//    }

//    uint8_t id   = buf[2];
//    uint8_t len  = buf[3];
//    uint8_t cmd_sum = buf[4];

//    // CMD 校验
//    uint8_t calc_cmd_sum = buf[0] + buf[1] + buf[2] + buf[3];
//    if (cmd_sum != calc_cmd_sum) {
//        qDebug() << "[FRAME] CMD checksum error";
//        return;
//    }

//    // DATA 起始索引
//    int dataIndex = 5;

//    // DATA 校验
//    uint8_t data_sum = buf[dataIndex + 7];  // DATA_SUM
//    uint8_t calc_data_sum = 0;
//    for (int i = 0; i < 7; ++i) {
//        calc_data_sum += buf[dataIndex + i];
//    }

//    if (data_sum != calc_data_sum) {
//        qDebug() << "[FRAME] DATA checksum error";
//        return;
//    }

//    // ========== 数据解析 ==========
//    int8_t temperature = static_cast<int8_t>(buf[dataIndex + 0]);

//    int16_t iq_or_power =
//        (int16_t)(buf[dataIndex + 2] << 8 | buf[dataIndex + 1]);

//    int16_t speed =
//        (int16_t)(buf[dataIndex + 4] << 8 | buf[dataIndex + 3]);

//    uint16_t encoder =
//        (uint16_t)(buf[dataIndex + 6] << 8 | buf[dataIndex + 5]);

//    // ========== 输出 ==========
//    qDebug() << "ID:" << id
//             << "Temp:" << temperature
//             << "IQ/Power:" << iq_or_power
//             << "Speed:" << speed
//             << "Encoder:" << encoder;

//    // 👉 这里就是你要的编码器值
//    // encoder = 0 ~ 65535 (取决于 14/15/16bit 编码器)

//    emit positionChange(encoder);
//}





void ScanControlHuiChuan::processFrame(const QByteArray& frame)
{
    // ===== 长度校验 =====
    // CMD(5) + DATA(7) = 12 byte
    if (frame.size() < 12) {
        qDebug() << "[0x90] length error:" << frame.size();
        return;
    }

    const uint8_t* buf = reinterpret_cast<const uint8_t*>(frame.constData());

    // ===== 帧头校验 =====
    if (buf[0] != 0x3E || buf[1] != 0x90) {
        qDebug() << "[0x90] header error";
        return;
    }

    uint8_t id      = buf[2];
    uint8_t len     = buf[3];   // 应为 0x06
    uint8_t cmd_sum = buf[4];

    // ===== CMD 校验 =====
    uint8_t calc_cmd_sum = buf[0] + buf[1] + buf[2] + buf[3];
    if (cmd_sum != calc_cmd_sum) {
        qDebug() << "[0x90] CMD checksum error";
        return;
    }

    int dataIndex = 5;

    // ===== DATA 校验 =====
    uint8_t data_sum = buf[dataIndex + 6];  // DATA_SUM
    uint8_t calc_data_sum = 0;
    for (int i = 0; i < 6; ++i) {
        calc_data_sum += buf[dataIndex + i];
    }

    if (data_sum != calc_data_sum) {
        qDebug() << "[0x90] DATA checksum error";
        return;
    }

    // ===== 数据解析 =====
    uint16_t encoder =
            (uint16_t)(buf[dataIndex + 1] << 8 | buf[dataIndex + 0]);

    uint16_t encoderRaw =
            (uint16_t)(buf[dataIndex + 3] << 8 | buf[dataIndex + 2]);

    uint16_t encoderOffset =
            (uint16_t)(buf[dataIndex + 5] << 8 | buf[dataIndex + 4]);

    // ===== 输出 =====
    qDebug() << "[0x90] ID:" << id
             << "Encoder:" << encoder
             << "Raw:" << encoderRaw
             << "Offset:" << encoderOffset;

    // ===== 信号输出 =====
    //emit positionChange(encoder);          // 实际位置
    //emit encoderRawChange(encoderRaw);     // 原始编码器
    //emit encoderOffsetChange(encoderOffset); // 零偏
}


void ScanControlHuiChuan::processFrame92(const QByteArray& frame)
{
    qDebug() << "[0x92] processFrame92 收到帧, size:" << frame.size()
             << "hex:" << frame.toHex(' ');

    // ===== 长度校验 =====
    // CMD(5) + DATA(9) = 14 byte
    if (frame.size() < 14) {
        qDebug() << "[0x92] length error:" << frame.size();
        return;
    }


    const uint8_t* buf = reinterpret_cast<const uint8_t*>(frame.constData());

    // ===== 帧头 + 命令校验 =====
    if (buf[0] != 0x3E || buf[1] != 0x92) {
        qDebug() << "[0x92] header error";
        return;
    }

    uint8_t id      = buf[2];
    uint8_t len     = buf[3];   // 必须是 0x08
    uint8_t cmd_sum = buf[4];

    if (len != 0x08) {
        qDebug() << "[0x92] LEN error: 期望 0x08, 实际:" << len;
        return;
    }


    // ===== CMD 校验 =====
    uint8_t calc_cmd_sum = buf[0] + buf[1] + buf[2] + buf[3];
    if (cmd_sum != calc_cmd_sum) {
        qDebug() << "[0x92] CMD checksum error";
        return;
    }

    int dataIndex = 5;

    // ===== DATA 校验 =====
    uint8_t data_sum = buf[dataIndex + 8];  // DATA_SUM
    uint8_t calc_data_sum = 0;
    for (int i = 0; i < 8; ++i) {
        calc_data_sum += buf[dataIndex + i];
    }

    if (data_sum != calc_data_sum) {
        qDebug() << "[0x92] DATA checksum error";
        return;
    }

    // ===== 解析 motorAngle (int64_t, little-endian) =====
    int64_t motorAngle = 0;
    for (int i = 0; i < 8; ++i) {
        motorAngle |= (int64_t(buf[dataIndex + i]) << (8 * i));
    }

    // 单位换算：0.01° / LSB
    double angleDeg = motorAngle * 0.01;

    // ===== 输出 =====
    //    qDebug() << "[0x92] ID:" << id
    //             << "motorAngle(raw):" << motorAngle
    //             << "angle(deg):" << angleDeg
    //             <<"mm"<<angleDeg/60.0;

    qDebug() << "[0x92] 解析成功 ID:" << id
             << "motorAngle(raw):" << motorAngle
             << "angle(deg):" << angleDeg
             << "mm:" << angleDeg / 60.0;

    // ===== 信号 =====
    if(id==2){
        qDebug() << "[0x92] emit positionChangex(x):" << angleDeg / 60.0;
        emit positionChangex(angleDeg/60.0);     // 实际工程角度（°）
    }else {
        qDebug() << "[0x92] emit positionChangey(y):" << angleDeg / 60.0;
        emit positionChangey(angleDeg/60.0);
    }



}


// ------ 位置回包派发 + 跳变过滤 ------
// axisId: 1 = Y, 2 = X (协议固定)
// mm:     已换算成 mm 的角度值
// strictAxis = true ：来自带轴号的完整帧(13/14 字节)，自证身份，可信；
//                     此时只做超量程和"突变到不合理大值"过滤，0 是合法值不丢。
// strictAxis = false：来自 9 字节滑窗兜底，仅按 lastQueriedAxisId 路由，
//                     再叠加"0 噪声"过滤（持有非零位置时突然蹦 0 多半是 ACK 误锚定）。
void ScanControlHuiChuan::dispatchAxisMm(uint8_t axisId, double mm, bool strictAxis)
{
    AxisStats& stat = axisStat(axisId);
    const char* axisName = (axisId == 2) ? "X" : "Y";

    // 只有回包轴号与本次查询的轴一致，才算本次查询结束；
    // 迟到的上一轴回包不能提前释放总线，否则会和真正的回包撞帧。
    if (waitingPosReply && axisId == lastQueriedAxisId) {
        waitingPosReply = false;
        missedPosReplyCount = 0;
        busIdle.start();
        stat.replies++;
        if (stat.timeoutStreak >= 3) {
            qCInfo(lcBus) << axisName << "轴位置回包恢复，此前连续超时" << stat.timeoutStreak << "次";
        }
        stat.timeoutStreak = 0;
    } else {
        stat.late++;
        qCDebug(lcBus) << axisName << "轴迟到/多余回包, 当前查询轴 =" << lastQueriedAxisId;
    }

    // 超量程：设备实际行程远小于 1000 mm，多半是解析错位
    if (qAbs(mm) > 1000.0) {
        stat.rejects++;
        qCWarning(lcPos) << axisName << "轴位置超量程被丢弃:" << mm << "mm";
        return;
    }

    double& lastMm   = (axisId == 2) ? lastXmm : lastYmm;
    bool&   hasLast  = (axisId == 2) ? hasLastX : hasLastY;
    int&    rejCount = (axisId == 2) ? rejectCountX : rejectCountY;

    // 跳变过滤：strict 帧放宽到 500mm；滑窗兜底维持 100mm。
    // 连续被拒 kMaxJumpRejects 次视为真实变化，强制接受，避免该轴永久不刷新。
    const double jumpLimit = strictAxis ? 500.0 : 100.0;
    if (hasLast && qAbs(mm - lastMm) > jumpLimit) {
        if (++rejCount < kMaxJumpRejects) {
            stat.rejects++;
            qCWarning(lcPos) << axisName << "轴位置跳变被拒:" << mm << "上次:" << lastMm
                             << "第" << rejCount << "次" << "strict:" << strictAxis;
            return;
        }
        qCWarning(lcPos) << axisName << "轴连续" << rejCount << "次跳变，按真实位置接受:"
                         << mm << "上次:" << lastMm;
    }
    // "0 噪声"过滤：仅在不可信的滑窗匹配时启用
    if (!strictAxis && hasLast && qAbs(lastMm) > 2.0 && qAbs(mm) < 0.001) {
        stat.rejects++;
        qCDebug(lcPos) << axisName << "轴滑窗匹配到疑似假 0，丢弃, 上次:" << lastMm;
        return;
    }

    rejCount = 0;
    lastMm = mm;
    hasLast = true;
    stat.sinceUpdate.start();
    if (axisId == 2) emit positionChangex(mm);
    else             emit positionChangey(mm);
}


// 容错解析：分三层定位 0x92 位置回包
//   L1 完整帧 3E 92 ID 08 SUM <8 angle> <sum>           (14B, 轴号最可信)
//   L2 吃掉头 92 ID 08 SUM <8 angle> <sum>              (13B, 仍可信)
//   L3 吃掉更多 N + 08 IDSUM <8 angle> <sum>            (>=11B 锚点)
//      0xDA = 3E+92+02+08  -> X轴；0xD9 = 3E+92+01+08 -> Y轴
//      只要碎片里出现 "08 DA" 或 "08 D9" + 8字节 + 校验匹配，就能直接拿到轴号。
//   L4 都没命中再用 9字节滑窗 + lastQueriedAxisId 兜底（最不可信）
void ScanControlHuiChuan::parseAngleByChecksum()
{
    // 在指定位置尝试一种已知模式，命中就 dispatch 并返回应消费的字节数；0=未匹配
    auto tryAt = [this](int idx) -> int {
        const int n = rxBuffer.size();
        const uint8_t* base = reinterpret_cast<const uint8_t*>(rxBuffer.constData());

        // L1: 14 字节完整帧
        if (idx + 14 <= n) {
            const uint8_t* q = base + idx;
            if (q[0]==0x3E && q[1]==0x92 && (q[2]==0x01||q[2]==0x02) && q[3]==0x08
                && uint8_t(q[0]+q[1]+q[2]+q[3]) == q[4]) {
                uint8_t s=0; for (int k=0;k<8;++k) s += q[5+k];
                if (s == q[13]) {
                    int64_t a=0; for (int k=0;k<8;++k) a |= (int64_t(q[5+k])<<(8*k));
                    dispatchAxisMm(q[2], (a*0.01)/60.0, true);
                    return 14;
                }
            }
        }
        // L2: 13 字节，被吃掉 0x3E
        if (idx + 13 <= n) {
            const uint8_t* q = base + idx;
            if (q[0]==0x92 && (q[1]==0x01||q[1]==0x02) && q[2]==0x08) {
                uint8_t s=0; for (int k=0;k<8;++k) s += q[4+k];
                if (s == q[12]) {
                    int64_t a=0; for (int k=0;k<8;++k) a |= (int64_t(q[4+k])<<(8*k));
                    dispatchAxisMm(q[1], (a*0.01)/60.0, true);
                    return 13;
                }
            }
        }
        // L3: 以 "08 DA/D9" 做锚点，吃掉 1~多 字节都能救回来
        //     模式: <08> <IDSUM> <8 angle> <sum>  共 11 字节
        if (idx + 11 <= n) {
            const uint8_t* q = base + idx;
            if (q[0]==0x08 && (q[1]==0xDA || q[1]==0xD9)) {
                uint8_t s=0; for (int k=0;k<8;++k) s += q[2+k];
                if (s == q[10]) {
                    uint8_t axisId = (q[1]==0xDA) ? 2 : 1;
                    int64_t a=0; for (int k=0;k<8;++k) a |= (int64_t(q[2+k])<<(8*k));
                    dispatchAxisMm(axisId, (a*0.01)/60.0, true);
                    return 11;
                }
            }
        }
        return 0;
    };

    // 第一轮：从头扫，命中任一层就消费，从头再来
    int i = 0;
    while (i + 11 <= rxBuffer.size()) {
        int consumed = tryAt(i);
        if (consumed > 0) {
            if (i > 0) {
                statJunkBytes += i;
                qCDebug(lcBus) << "跳过非位置字节" << i << ":" << rxBuffer.left(i).toHex(' ').toUpper();
            }
            rxBuffer.remove(0, i + consumed);
            i = 0;
            continue;
        }
        ++i;
    }

    // 第二轮（已禁用）：9 字节滑窗兜底太容易把另一轴的位置错认成本轴，
    // 触发 jump reject 后该轴位置就不再刷新，到位检测会卡到 15s 超时。
    // L1/L2/L3 三层带轴号锚定已经足够覆盖网关吃头/粘包的情况，
    // 这里如果再走滑窗，弊远大于利 —— 直接关掉。


    // 防止缓冲无限增长：保留尾部最多 32 字节(可能是半截帧)
    if (rxBuffer.size() > 256) {
        statJunkBytes += rxBuffer.size() - 32;
        qCWarning(lcBus) << "接收缓冲溢出，丢弃" << rxBuffer.size() - 32 << "字节未识别数据";
        rxBuffer.remove(0, rxBuffer.size() - 32);
    }
}




ScanControlHuiChuan::ScanControlHuiChuan(QObject *parent) :
    ScanControlAbstract(parent)
{
    init();
}


ScanControlHuiChuan::~ScanControlHuiChuan()
{

    delete tcpSocket;
    if (settings) {

        delete settings;
        settings = nullptr;
    }

    if (timer) {
        timer->stop();
        delete timer;
        timer = nullptr;
    }
}




void ScanControlHuiChuan::writeAxisStopStatus(int address)
{

}



void ScanControlHuiChuan::readAxisEndState()
{

}



void ScanControlHuiChuan::on_connectBtn_clicked()
{


    // 只连接一次 socket 信号，防止每次点击"连接"都重复 connect 导致回调多次触发
    if (!isSocketSignalConnected) {
        isSocketSignalConnected = true;

        // 连接成功
        connect(tcpSocket, &QTcpSocket::connected, this, [this]{
            qCInfo(lcBus) << "网关已连接" << PlcIP << PlcPort;
            userDisconnect = false;
            resetBusState();
            emit tcpStateChange(true);
        });

        // 断开连接
        connect(tcpSocket, &QTcpSocket::disconnected, this, [this]{
            if (userDisconnect) {
                qCInfo(lcBus) << "网关已断开(操作员主动断开)";
            } else {
                qCCritical(lcBus) << "网关连接意外断开" << PlcIP << PlcPort
                                  << "未发出指令" << txQueue.size() << "条被丢弃";
            }
            userDisconnect = false;
            resetBusState();
            emit tcpStateChange(false);
        });

        // ❗连接失败 / 网络错误（关键）
        connect(tcpSocket,
                QOverload<QAbstractSocket::SocketError>::of(&QTcpSocket::error),
                this,
                [this](QAbstractSocket::SocketError err){
            qCWarning(lcBus) << "网关 socket 错误:" << err << tcpSocket->errorString();
            resetBusState();
            emit tcpStateChange(false);
        });

        // 收到数据（只连接一次）
        // 不再因为 "现在没在等位置回包" 就 clear() 缓冲：
        // 实测 0x92 回包延迟有时 80~150ms，等到回包到达时 waitingPosReply 可能已经被
        // 上层超时复位，再 clear 就把回包丢了。这里只解析有协议自证身份的位置帧
        // (parseAngleByChecksum 内部按 92 ID 08 / 08 DA(D9) 锚点)，其它字节做溢出保护。
        connect(tcpSocket, &QTcpSocket::readyRead, this, [this]() {
            rxBuffer.append(tcpSocket->readAll());
            parseAngleByChecksum();
            // 解析完后还很大说明全是非位置帧的 ACK，丢掉防膨胀
            if (rxBuffer.size() > 256) {
                rxBuffer.remove(0, rxBuffer.size() - 32);
            }
        });


    }



    qCInfo(lcBus) << "连接按钮: 当前状态" << tcpSocket->state() << "目标" << PlcIP << PlcPort;

    // 状态切换
    if(tcpSocket->state() == QAbstractSocket::ConnectedState){
        userDisconnect = true;
        tcpSocket->disconnectFromHost();
    }else {
        tcpSocket->setProxy(QNetworkProxy::NoProxy);
        tcpSocket->connectToHost(PlcIP, PlcPort);

        if (tcpSocket->waitForConnected(3000)) { // 等待最多3秒
            emit tcpStateChange(true);
        } else {
            qCCritical(lcBus) << "网关连接失败(3s 超时)" << PlcIP << PlcPort << tcpSocket->errorString()
                              << "请检查 IP/端口、网线和网关电源";
            emit tcpStateChange(false);
        }
    }

}


void ScanControlHuiChuan::on_setOriginBtn_clicked()
{
    if (!tcpSocket || tcpSocket->state() != QAbstractSocket::ConnectedState) {
        qCWarning(lcBus) << "设置零点失败: 网关未连接";
        return;
    }

    qCInfo(lcBus) << "开始设零序列: 设零前位置 X =" << lastXmm << "Y =" << lastYmm << "(电机坐标 mm)";

    // 设零期间暂停位置轮询，避免 0x19/0x76 序列被 0x92 查询打断。
    // 不强行清 waitingPosReply：在途的位置回包仍要等它回来，命令排队在其后发出。
    pushsend = true;
    queryXNext = true;

    // ===== 0x19：保存当前位置为零点（写ROM）=====
    // 注意：MWD/MyActuator 协议中 0x19 把当前位置作为零点写入ROM，
    // 实际生效需要电机重启（0x76）。下方在两个轴 0x19 发送完后再各发一条 0x76。

    auto buildAndSend = [this](quint8 cmd, quint8 motorId){
        QByteArray frame;
        frame.append(static_cast<char>(0x3E));   // 帧头
        frame.append(static_cast<char>(cmd));    // 命令
        frame.append(static_cast<char>(motorId));// 电机ID
        frame.append(static_cast<char>(0x00));   // 数据长度
        frame.append(static_cast<char>(calculateChecksum(frame)));
        enqueueFrame(frame);
    };

    // 先把缓存清掉，避免设零后位置显示因缓存延迟感觉"没变"
    hasLastX = false; hasLastY = false;
    lastXmm  = 0;     lastYmm  = 0;
    // UI 立即归零，给操作员一个明确反馈
    emit positionChangex(0);
    emit positionChangey(0);

    // 四条命令依次入队，由调度器按 kBusGapMs 间隔串行发出，不会粘帧
    buildAndSend(0x19, 0x01);
    buildAndSend(0x19, 0x02);
    // 0x76 重启 -> 让 0x19 写入的零点立即生效
    buildAndSend(0x76, 0x01);
    buildAndSend(0x76, 0x02);

    // 给电机重启留时间后再恢复轮询；重启后位置可能大幅变化，放开跳变过滤
    QTimer::singleShot(400, this, [this]() {
        hasLastX = false;
        hasLastY = false;
        pushsend = false;
        qCInfo(lcBus) << "设零序列结束，恢复位置轮询";
    });
}



void ScanControlHuiChuan::on_xAddBtn_pressed()
{
    if (!tcpSocket || tcpSocket->state() != QAbstractSocket::ConnectedState)
        return;

    if((tasks.count() != 0)  || isEndScan || isAxisStop || isJogDone || isRunTarget) return;
    updateCurPos = false;
    axisJog = AxisJog::XJogAddPressed;

}

void ScanControlHuiChuan::on_xAddBtn_released()
{
    if (!tcpSocket || tcpSocket->state() != QAbstractSocket::ConnectedState)
        return;

    if((tasks.count() != 0)  || isEndScan || isAxisStop || isJogDone || isRunTarget) return;
    updateCurPos = false;
    axisJog = AxisJog::XJogAddReleased;

}

void ScanControlHuiChuan::on_xSubBtn_pressed()
{
    if (!tcpSocket || tcpSocket->state() != QAbstractSocket::ConnectedState)
        return;

    if((tasks.count() != 0)  || isEndScan || isAxisStop || isJogDone || isRunTarget) return;
    updateCurPos = false;
    axisJog = AxisJog::XJogSubPressed;
}

void ScanControlHuiChuan::on_xSubBtn_released()
{
    if (!tcpSocket || tcpSocket->state() != QAbstractSocket::ConnectedState)
        return;

    if((tasks.count() != 0)  || isEndScan || isAxisStop || isJogDone || isRunTarget) return;
    updateCurPos = false;
    axisJog = AxisJog::XJogSubReleased;
}

void ScanControlHuiChuan::on_yAddBtn_pressed()
{
    if (!tcpSocket || tcpSocket->state() != QAbstractSocket::ConnectedState)
        return;

    if((tasks.count() != 0)  || isEndScan || isAxisStop || isJogDone || isRunTarget) return;
    updateCurPos = false;
    axisJog = AxisJog::YJogAddPressed;
}

void ScanControlHuiChuan::on_yAddBtn_released()
{
    if (!tcpSocket || tcpSocket->state() != QAbstractSocket::ConnectedState)
        return;

    if((tasks.count() != 0)  || isEndScan || isAxisStop || isJogDone || isRunTarget) return;
    updateCurPos = false;
    axisJog = AxisJog::YJogAddReleased;
}

void ScanControlHuiChuan::on_ySubBtn_pressed()
{
    if (!tcpSocket || tcpSocket->state() != QAbstractSocket::ConnectedState)
        return;

    if((tasks.count() != 0)  || isEndScan || isAxisStop || isJogDone || isRunTarget) return;
    updateCurPos = false;
    axisJog = AxisJog::YJogSubPressed;
}

void ScanControlHuiChuan::on_ySubBtn_released()
{
    if (!tcpSocket || tcpSocket->state() != QAbstractSocket::ConnectedState)
        return;

    if((tasks.count() != 0)  || isEndScan || isAxisStop || isJogDone || isRunTarget) return;
    updateCurPos = false;
    axisJog = AxisJog::YJogSubReleased;
}

void ScanControlHuiChuan::on_zAddBtn_pressed()
{
    if (!tcpSocket || tcpSocket->state() != QAbstractSocket::ConnectedState)
        return;

    if((tasks.count() != 0)  || isEndScan || isAxisStop || isJogDone || isRunTarget) return;
    updateCurPos = false;
    axisJog = AxisJog::ZJogAddPressed;
}

void ScanControlHuiChuan::on_zAddBtn_released()
{
    if (!tcpSocket || tcpSocket->state() != QAbstractSocket::ConnectedState)
        return;

    if((tasks.count() != 0)  || isEndScan || isAxisStop || isJogDone || isRunTarget) return;
    updateCurPos = false;
    axisJog = AxisJog::ZJogAddReleased;
}

void ScanControlHuiChuan::on_zSubBtn_pressed()
{
    if (!tcpSocket || tcpSocket->state() != QAbstractSocket::ConnectedState)
        return;

    if((tasks.count() != 0)  || isEndScan || isAxisStop || isJogDone || isRunTarget) return;
    updateCurPos = false;
    axisJog = AxisJog::ZJogSubPressed;
}

void ScanControlHuiChuan::on_zSubBtn_released()
{
    if (!tcpSocket || tcpSocket->state() != QAbstractSocket::ConnectedState)
        return;

    if((tasks.count() != 0)  || isEndScan || isAxisStop || isJogDone || isRunTarget) return;
    updateCurPos = false;
    axisJog = AxisJog::ZJogSubReleased;
}

void ScanControlHuiChuan::on_rAddBtn_pressed()
{
    if (!tcpSocket || tcpSocket->state() != QAbstractSocket::ConnectedState)
        return;

    if((tasks.count() != 0)  || isEndScan || isAxisStop || isJogDone || isRunTarget) return;
    updateCurPos = false;
    axisJog = AxisJog::RJogAddPressed;
}

void ScanControlHuiChuan::on_rAddBtn_released()
{
    if (!tcpSocket || tcpSocket->state() != QAbstractSocket::ConnectedState)
        return;

    if((tasks.count() != 0)  || isEndScan || isAxisStop || isJogDone || isRunTarget) return;
    updateCurPos = false;
    axisJog = AxisJog::RJogAddReleased;
}

void ScanControlHuiChuan::on_rSubBtn_pressed()
{
    if (!tcpSocket || tcpSocket->state() != QAbstractSocket::ConnectedState)
        return;

    if((tasks.count() != 0)  || isEndScan || isAxisStop || isJogDone || isRunTarget) return;
    updateCurPos = false;
    axisJog = AxisJog::RJogSubPressed;
}

void ScanControlHuiChuan::on_rSubBtn_released()
{
    if (!tcpSocket || tcpSocket->state() != QAbstractSocket::ConnectedState)
        return;

    if((tasks.count() != 0)  || isEndScan || isAxisStop || isJogDone || isRunTarget) return;
    updateCurPos = false;
    axisJog = AxisJog::RJogSubReleased;
}


void ScanControlHuiChuan::on_setLimitBtn_clicked()
{

}





void ScanControlHuiChuan::init()
{
    settings = new QSettings("./scan_setting.ini", QSettings::IniFormat);

    timer = new QTimer(this);
    // 调度节拍 10ms：每个节拍最多发一帧。
    // 发送规则：上一帧发出/回包到达后至少空闲 kBusGapMs；
    // 有位置查询在途时什么都不发，直到回包或超时，保证总线上同一时刻只有一问一答。
    timer->setInterval(kTickMs);
    busIdle.start();
    posReplyWait.start();
    statWindow.start();



    connect(timer, &QTimer::timeout, this, &ScanControlHuiChuan::performTasks);

    timer->start();
    tcpSocket=new QTcpSocket(this);

}



void ScanControlHuiChuan::performTasks()
{
    if (!tcpSocket || tcpSocket->state() != QAbstractSocket::ConnectedState) {
        return;
    }

    if (statWindow.elapsed() >= kStatIntervalMs) {
        reportStats();
    }

    // 点动状态变化只会把速度帧入队，不直接写 socket
    perfromJogTasks();

    // 位置查询在途：等回包，超时则放弃本次（下一次查另一轴）
    if (waitingPosReply) {
        if (posReplyWait.elapsed() < kPosReplyTimeout) {
            return;
        }
        AxisStats& stat = axisStat(lastQueriedAxisId);
        stat.timeouts++;
        stat.timeoutStreak++;
        const char* axisName = (lastQueriedAxisId == 2) ? "X" : "Y";
        if (stat.timeoutStreak == kTimeoutWarnStreak || stat.timeoutStreak % 25 == 0) {
            qCWarning(lcBus) << axisName << "轴位置查询连续超时" << stat.timeoutStreak << "次"
                             << "(每次" << kPosReplyTimeout << "ms)，检查该轴电机供电/485 接线/网关";
        } else {
            qCDebug(lcBus) << axisName << "轴位置查询超时";
        }
        waitingPosReply = false;
        missedPosReplyCount++;
        busIdle.restart();
    }

    if (busIdle.elapsed() < kBusGapMs) {
        return;
    }

    // 控制指令优先
    if (!txQueue.isEmpty()) {
        writeFrame(txQueue.dequeue());
        return;
    }

    // 设零等特殊序列期间暂停位置轮询
    if (!pushsend) {
        updataCurrentPos();
    }
}


void ScanControlHuiChuan::enqueueFrame(const QByteArray& frame, bool urgent)
{
    if (!tcpSocket || tcpSocket->state() != QAbstractSocket::ConnectedState) {
        qCWarning(lcBus) << "网关未连接，指令未发送:" << describeFrame(frame);
        return;
    }
    if (txQueue.size() >= kMaxTxQueue) {
        statQueueDrops++;
        qCWarning(lcBus) << "发送队列已满(" << kMaxTxQueue << ")，丢弃最旧指令:" << describeFrame(txQueue.head());
        txQueue.dequeue();
    }
    if (urgent) {
        txQueue.prepend(frame);
    } else {
        txQueue.enqueue(frame);
    }
    statQueuePeak = qMax(statQueuePeak, txQueue.size());
}


bool ScanControlHuiChuan::writeFrame(const QByteArray& frame)
{
    const qint64 written = tcpSocket->write(frame);
    tcpSocket->flush();
    busIdle.restart();
    const bool isPoll = frame.size() >= 2 && quint8(frame[1]) == 0x92;
    if (written != frame.size()) {
        statWriteFails++;
        qCWarning(lcBus) << "写 socket 失败, written =" << written << describeFrame(frame)
                         << tcpSocket->errorString();
        return false;
    }
    if (!isPoll) {
        statCmdFrames++;
        // 控制指令逐条记录；位置查询频率太高，只进统计
        qCInfo(lcBus) << "TX" << describeFrame(frame) << "| 队列剩余" << txQueue.size();
        qCDebug(lcBus) << "TX hex:" << frame.toHex(' ').toUpper();
    }
    return true;
}


void ScanControlHuiChuan::resetBusState()
{
    txQueue.clear();
    rxBuffer.clear();
    waitingPosReply = false;
    missedPosReplyCount = 0;
    queryXNext = true;
    // 重连后位置可能已经变化，不能拿断线前的值做跳变过滤
    hasLastX = false;
    hasLastY = false;
    rejectCountX = 0;
    rejectCountY = 0;
    busIdle.restart();
    statX = AxisStats();
    statY = AxisStats();
    statCmdFrames = statWriteFails = statQueueDrops = statQueuePeak = statJunkBytes = 0;
    statWindow.start();
}


void ScanControlHuiChuan::reportStats()
{
    const qint64 windowMs = qMax<qint64>(1, statWindow.elapsed());

    auto axisLine = [windowMs](const char* name, const AxisStats& s) {
        const QString last = s.sinceUpdate.isValid()
                ? QString("%1ms前").arg(s.sinceUpdate.elapsed())
                : QString("从未");
        const int rate = s.queries > 0 ? s.replies * 100 / s.queries : 0;
        return QString("%1[查询%2 回包%3(%4%) 超时%5 迟到%6 过滤%7 刷新%8Hz 最后更新%9]")
                .arg(name).arg(s.queries).arg(s.replies).arg(rate)
                .arg(s.timeouts).arg(s.late).arg(s.rejects)
                .arg(s.replies * 1000.0 / windowMs, 0, 'f', 1)
                .arg(last);
    };

    auto isStale = [](const AxisStats& s) {
        return !s.sinceUpdate.isValid() || s.sinceUpdate.elapsed() > kStaleMs;
    };

    const QString summary = QString("通讯统计 %1s: %2 %3 指令%4 队列峰值%5 写失败%6 队列丢弃%7 非位置字节%8")
            .arg(windowMs / 1000.0, 0, 'f', 1)
            .arg(axisLine("X", statX), axisLine("Y", statY))
            .arg(statCmdFrames).arg(statQueuePeak).arg(statWriteFails)
            .arg(statQueueDrops).arg(statJunkBytes);

    const bool staleX = isStale(statX);
    const bool staleY = isStale(statY);
    const bool abnormal = staleX || staleY
            || statX.timeouts || statY.timeouts
            || statX.rejects || statY.rejects
            || statWriteFails || statQueueDrops;

    if (abnormal) {
        qCWarning(lcBus).noquote() << summary;
        if (staleX) qCWarning(lcPos) << "X 轴位置停更超过" << kStaleMs << "ms，界面显示的 X 不是实时值";
        if (staleY) qCWarning(lcPos) << "Y 轴位置停更超过" << kStaleMs << "ms，界面显示的 Y 不是实时值";
    } else {
        qCInfo(lcBus).noquote() << summary;
    }

    // 窗口计数清零；连续超时和最后更新时间跨窗口保留
    for (AxisStats* s : {&statX, &statY}) {
        s->queries = s->replies = s->timeouts = s->late = s->rejects = 0;
    }
    statCmdFrames = statWriteFails = statQueueDrops = statJunkBytes = 0;
    statQueuePeak = txQueue.size();
    statWindow.start();
}



void ScanControlHuiChuan::updataCurrentPos()
{

    if (!tcpSocket || tcpSocket->state() != QAbstractSocket::ConnectedState) {
        return;
    }

    // 在途/超时判断已在 performTasks 里统一处理，这里只负责发查询帧
    if (waitingPosReply) {
        return;
    }

    quint8 axisId = queryXNext ? 2 : 1;   // 2 = X轴电机, 1 = Y轴电机
    queryXNext = !queryXNext;
    lastQueriedAxisId = axisId;

    QByteArray frame92;
    frame92.append(0x3E);
    frame92.append(0x92);
    frame92.append(static_cast<char>(axisId));
    frame92.append(static_cast<char>(0x00));
    frame92.append(calculateChecksum(frame92));

    if (writeFrame(frame92)) {
        waitingPosReply = true;
        posReplyWait.restart();
        axisStat(axisId).queries++;
    } else {
        waitingPosReply = false;
    }
}





void ScanControlHuiChuan::perfromJogTasks()
{
    // 状态没变就什么都不做：
    //   按住期间 axisJog 一直是 *Pressed，一次速度命令足以让电机持续转，
    //   不需要每个 tick 重复发(原代码会重发，挤占网关并干扰回包)。
    //   松开瞬间 axisJog 变成 *Released，下面 switch 会发 0 速停止命令。
    if (axisJog == lastAxisJog) return;

    if (axisInch != AxisJog::NotAxisJog) {
        // 寸动模式接管中，不处理点动
        return;
    }

    const int motorIdY = 0x01;   // Y 轴电机
    const int motorIdX = 0x02;   // X 轴电机

    // 反转后让按键和屏幕方向对齐：
    //   X 轴电机 +方向 默认与 UI +X 反相 -> 默认 X+ 按 -speed；勾选 invertXAxis 后改成 +speed
    //   Y 轴电机 +方向 默认与 UI +Y 同相 -> 默认 Y+ 按 +speed；勾选 invertYAxis 后改成 -speed
    const int xPlusSpeed = invertXAxis ?  static_cast<int>(speed) : -static_cast<int>(speed);
    const int xMinusSpeed = -xPlusSpeed;
    const int yPlusSpeed = invertYAxis ? -static_cast<int>(speed) :  static_cast<int>(speed);
    const int yMinusSpeed = -yPlusSpeed;

    switch (axisJog) {
    case XJogAddPressed:    sendCommandTcp(motorIdX, xPlusSpeed,  0xA2); break;
    case XJogSubPressed:    sendCommandTcp(motorIdX, xMinusSpeed, 0xA2); break;
    case XJogAddReleased:
    case XJogSubReleased:   sendCommandTcp(motorIdX, 0,           0xA2); break;

    case YJogAddPressed:    sendCommandTcp(motorIdY, yPlusSpeed,  0xA2); break;
    case YJogSubPressed:    sendCommandTcp(motorIdY, yMinusSpeed, 0xA2); break;
    case YJogAddReleased:
    case YJogSubReleased:   sendCommandTcp(motorIdY, 0,           0xA2); break;

    default:                /* NotAxisJog 或 Z/R 轴，暂不支持 */            break;
    }


    lastAxisJog = axisJog;
}




void ScanControlHuiChuan::sendCommandTcp(quint8 motorId, int speedValue,int cmd) {



    if (!tcpSocket || tcpSocket->state() != QAbstractSocket::ConnectedState) {
        qCWarning(lcBus) << "点动/速度指令未发送: 网关未连接, 电机" << motorId << "速度" << speedValue / 6000.0;
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

    // 合并帧并入队，由调度器串行发出
    frame.append(dataFrame);
    enqueueFrame(frame);
    //tcpSocket->waitForBytesWritten(100);

    //    qDebug() << QString::fromLocal8Bit("TCP 发送速度控制命令: ID =") << motorId
    //             << QString::fromLocal8Bit(" 速度值 =") << speedValue
    //             << QString::fromLocal8Bit(" 帧数据: ") << frame.toHex(' ').toUpper();

}

void ScanControlHuiChuan::runPosintion(int64_t angleControl, uint32_t maxSpeed)
{
    // 不再 clear rxBuffer：A4 命令和位置回包是两条独立的协议帧，
    // parseAngleByChecksum 已经能从混合流里挑出 92/08DA/08D9 帧，
    // 这里粗暴 clear 反而会把还没消费完的位置回包丢掉。
    QByteArray frame;


    /* ---------- CMD FRAME ---------- */
    frame.append(static_cast<char>(0x3E));   // 帧头
    frame.append(static_cast<char>(0xA4));   // 命令
    frame.append(static_cast<char>(motorId));   // 电机ID
    frame.append(static_cast<char>(0x0C));   // 数据长度(12 byte data)

    quint8 cmdSum = calculateChecksum(frame); // CMD[0..3]校验
    frame.append(static_cast<char>(cmdSum));  // CMD_SUM


    /* ---------- DATA FRAME ---------- */
    QByteArray dataFrame;

    // angleControl (8 byte)
    quint8* angleBytes = reinterpret_cast<quint8*>(&angleControl);
    for(int i = 0; i < 8; ++i){
        dataFrame.append(static_cast<char>(angleBytes[i]));
    }

    // maxSpeed (uint32_t, 4 byte)
    quint8* speedBytes = reinterpret_cast<quint8*>(&maxSpeed);
    for(int i = 0; i < 4; ++i){
        dataFrame.append(static_cast<char>(speedBytes[i]));
    }

    // DATA_SUM
    quint8 dataSum = calculateChecksum(dataFrame); // DATA[0..11]
    dataFrame.append(static_cast<char>(dataSum));

    /* ---------- 拼接总帧 ---------- */
    frame.append(dataFrame);

    /* ---------- 入队发送 ---------- */
    enqueueFrame(frame);

    //tcpSocket->waitForBytesWritten(150);
}



void ScanControlHuiChuan::on_start(){


}
void ScanControlHuiChuan::on_stop(){


}
void ScanControlHuiChuan::on_end(){

    // 停止命令优先：丢掉还没发出的运动指令，两条 0x81 插到队首，
    // 等当前在途的位置回包结束后立即发出，不与回包撞帧。
    if (!txQueue.isEmpty()) {
        qCInfo(lcBus) << "停止: 丢弃" << txQueue.size() << "条未发出的指令";
    }
    txQueue.clear();

    auto buildStop = [this](quint8 id) {
        QByteArray frame;
        frame.append(static_cast<char>(0x3E));      // 帧头
        frame.append(static_cast<char>(0x81));      // 命令
        frame.append(static_cast<char>(id));        // 电机ID
        frame.append(static_cast<char>(0x00));      // 数据长度
        frame.append(static_cast<char>(calculateChecksum(frame)));
        return frame;
    };
    enqueueFrame(buildStop(0x02), true);
    enqueueFrame(buildStop(0x01), true);

}
void ScanControlHuiChuan::on_backZero(){


}


void ScanControlHuiChuan::on_alarmReset(){

    QByteArray frame;
    frame.append(static_cast<char>(0x3E));      // 帧头
    frame.append(static_cast<char>(0x9B));      // 命令
    frame.append(static_cast<char>(motorId));   // 电机ID
    frame.append(static_cast<char>(0x00));      // 数据长度
    quint8 cmdSum = calculateChecksum(frame);   // 校验和
    frame.append(static_cast<char>(cmdSum));

    enqueueFrame(frame);
}
