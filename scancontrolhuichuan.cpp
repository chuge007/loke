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
#include <QProcess>

namespace {
// 调度节拍：10ms 检查一次，真正的发送节奏由下面的间隔控制
constexpr int kTickMs          = 10;
// 两帧之间至少留出的总线空闲时间，给网关转发和电机应答留余量。
// 9600bps 下 1 字节约 1.04ms：A4 命令 18 字节约 19ms，回显又 19ms，
// 间隔必须大于 38ms，否则下一帧会和回显撞在总线上。
constexpr int kBusGapMs        = 60;
// 位置查询等待回包的超时；9600 下一问一答约 20ms，加网关缓冲留足 300ms
constexpr int kPosReplyTimeout = 300;
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
// 空闲(没有运动指令)时的位置查询间隔：待机时把轮询降到 2Hz，减轻网关与 485 压力
constexpr int kIdleGapMs          = 500;
// 运动指令发出后，这段时间内按“运动中”高频轮询
constexpr int kActiveHoldMs       = 2500;
// 运动指令刚发出后的静默窗口：先让驱动器启动，别立刻抢总线做位置查询
constexpr int kCmdQuietMs         = 120;
// 非运动轴的保底刷新间隔(心跳)，避免静止轴位置长时间不更新
constexpr int kOtherAxisRefreshMs = 1000;
// 0x80 写出后要等这么久才拿回包重建坐标偏移：更早的在途回包还是旧坐标
constexpr int kAnchorGuardMs = 250;

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
    case 0x80: text = "电机关闭(断使能)"; break;
    case 0x88: text = "电机运行(恢复使能)"; break;
    case 0x95:
        if (f.size() >= 10)
            text = QString("写多圈角度=%1mm(写RAM)").arg(qint32(le(5, 4)) / 6000.0, 0, 'f', 3);
        else text = "写多圈角度(帧长异常)";
        break;
    default:   text = QString("cmd=0x%1").arg(cmd, 2, 16, QChar('0')); break;
    }
    return QString("%1轴 %2").arg(axis, text);
}

// 网关 TCP 服务卡死后，只有 Wi-Fi 链路重连才能让它重新接受连接。
// 这里解析当前 SSID/接口名，执行 disconnect + connect，把现场的人工操作自动化。
bool resetWifiLink()
{
    QProcess p;
    p.start("netsh", QStringList() << "wlan" << "show" << "interfaces");
    if (!p.waitForStarted(2000) || !p.waitForFinished(4000)) {
        p.kill();
        qCWarning(lcBus) << "重置 Wi-Fi 失败: netsh 无法执行";
        return false;
    }
    const QString out = QString::fromLocal8Bit(p.readAllStandardOutput());
    QString ssid;
    QString iface;
    for (const QString &line : out.split(QLatin1Char('\n'))) {
        const int colon = line.indexOf(QLatin1Char(':'));
        if (colon < 0)
            continue;
        const QString key = line.left(colon).trimmed();
        const QString val = line.mid(colon + 1).trimmed();
        // 注意不能匹配到 BSSID
        if (key.compare(QStringLiteral("SSID"), Qt::CaseInsensitive) == 0 && ssid.isEmpty())
            ssid = val;
        else if (key.compare(QStringLiteral("Name"), Qt::CaseInsensitive) == 0
                 || key == QString::fromUtf8("名称"))
            iface = val;
    }
    const bool wasAssociated = !ssid.isEmpty();
    if (!wasAssociated) {
        // 已掉线时 netsh 不再显示 SSID，但固定热点的 Profile 仍可用于主动重连。
        ssid = QStringLiteral("TS_F1_8825");
        qCWarning(lcBus) << "当前 Wi-Fi 已断开，改用已保存热点重新连接" << ssid;
    }
    if (iface.isEmpty()) {
        qCWarning(lcBus) << "重置 Wi-Fi 失败: 未解析到无线接口名，netsh 输出:"
                         << out.left(500).simplified();
        return false;
    }

    qCWarning(lcBus) << "重置 Wi-Fi 链路:" << iface << ssid;
    if (wasAssociated) {
        QProcess d;
        d.start("netsh", QStringList() << "wlan" << "disconnect"
                                       << QStringLiteral("interface=%1").arg(iface));
        d.waitForFinished(3000);
    }

    QProcess c;
    c.start("netsh", QStringList() << "wlan" << "connect"
                                   << QStringLiteral("name=%1").arg(ssid)
                                   << QStringLiteral("ssid=%1").arg(ssid)
                                   << QStringLiteral("interface=%1").arg(iface));
    const bool ok = c.waitForFinished(8000) && c.exitCode() == 0;
    qCInfo(lcBus) << "重置 Wi-Fi 结果:" << (ok ? "成功" : "失败");
    return ok;
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







void ScanControlHuiChuan::processFrame(const QByteArray& frame)
{
    // ===== 长度校验 =====
    // CMD(5) + DATA(7) = 12 byte
    if (frame.size() < 12) {
        qCDebug(lcBus) << "[0x90] 帧长错误:" << frame.size();
        return;
    }

    const uint8_t* buf = reinterpret_cast<const uint8_t*>(frame.constData());

    // ===== 帧头校验 =====
    if (buf[0] != 0x3E || buf[1] != 0x90) {
        qCDebug(lcBus) << "[0x90] 帧头错误:" << frame.toHex(' ').toUpper();
        return;
    }

    uint8_t id      = buf[2];
    uint8_t len     = buf[3];   // 应为 0x06
    uint8_t cmd_sum = buf[4];

    // ===== CMD 校验 =====
    uint8_t calc_cmd_sum = buf[0] + buf[1] + buf[2] + buf[3];
    if (cmd_sum != calc_cmd_sum) {
        qCDebug(lcBus) << "[0x90] 命令校验错误:" << frame.toHex(' ').toUpper();
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
        qCDebug(lcBus) << "[0x90] 数据校验错误:" << frame.toHex(' ').toUpper();
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
    qCDebug(lcPos) << "[0x90] ID:" << id
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
    qCDebug(lcBus) << "[0x92] 收到帧, size:" << frame.size()
             << "hex:" << frame.toHex(' ');

    // ===== 长度校验 =====
    // CMD(5) + DATA(9) = 14 byte
    if (frame.size() < 14) {
        qCDebug(lcBus) << "[0x92] 帧长错误:" << frame.size();
        return;
    }


    const uint8_t* buf = reinterpret_cast<const uint8_t*>(frame.constData());

    // ===== 帧头 + 命令校验 =====
    if (buf[0] != 0x3E || buf[1] != 0x92) {
        qCDebug(lcBus) << "[0x92] 帧头错误";
        return;
    }

    uint8_t id      = buf[2];
    uint8_t len     = buf[3];   // 必须是 0x08
    uint8_t cmd_sum = buf[4];

    if (len != 0x08) {
        qCDebug(lcBus) << "[0x92] 数据长度错误: 期望 0x08, 实际:" << len;
        return;
    }


    // ===== CMD 校验 =====
    uint8_t calc_cmd_sum = buf[0] + buf[1] + buf[2] + buf[3];
    if (cmd_sum != calc_cmd_sum) {
        qCDebug(lcBus) << "[0x92] 命令校验错误";
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
        qCDebug(lcBus) << "[0x92] 数据校验错误";
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
    qCDebug(lcPos) << "[0x92] 解析成功 ID:" << id
             << "motorAngle(raw):" << motorAngle
             << "angle(deg):" << angleDeg
             << "mm:" << angleDeg / 60.0;

    // ===== 信号 =====
    if(id==2){
        emit positionChangex(angleDeg/60.0);     // 实际工程角度（°）
    }else {
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

    // 0x80 之后驱动器多圈计数被清，读数整体平移：用这一帧重建 offset，
    // 界面从断使能前的位置接着走，不再跳到接近 0。
    // 0x80 刚写出时可能还有一帧旧坐标的回包在路上，要等过了保护时间再对齐。
    const bool anchorPending = (axisId == 2) ? anchorPendingX : anchorPendingY;
    const bool anchorDue = anchorPending
            && (!sinceMotorOff.isValid() || sinceMotorOff.elapsed() >= kAnchorGuardMs);
    if (anchorDue)
        anchorPositionReference(axisId, mm);

    if ((axisId == 2) ? anchorPendingX : anchorPendingY) {
        // 还没拿到 0x80 之后的有效读数：界面保持最后坐标，别拿另一套坐标系的数刷新
        stat.sinceUpdate.restart();
        qCDebug(lcPos) << axisName << "轴等待坐标对齐，忽略本帧" << mm << "mm";
        return;
    }

    // 之后的过滤、显示、对外信号统一用软件坐标（驱动器读数 + offset）
    const double mmSw = mm + ((axisId == 2) ? posOffsetX : posOffsetY);

    double& lastMm   = (axisId == 2) ? lastXmm : lastYmm;
    bool&   hasLast  = (axisId == 2) ? hasLastX : hasLastY;
    int&    rejCount = (axisId == 2) ? rejectCountX : rejectCountY;

    // 跳变过滤：strict 帧放宽到 500mm；滑窗兜底维持 100mm。
    // 连续被拒 kMaxJumpRejects 次视为真实变化，强制接受，避免该轴永久不刷新。
    const double jumpLimit = strictAxis ? 500.0 : 100.0;
    if (hasLast && qAbs(mmSw - lastMm) > jumpLimit) {
        if (++rejCount < kMaxJumpRejects) {
            stat.rejects++;
            qCWarning(lcPos) << axisName << "轴位置跳变被拒:" << mmSw << "上次:" << lastMm
                             << "第" << rejCount << "次" << "strict:" << strictAxis;
            return;
        }
        qCWarning(lcPos) << axisName << "轴连续" << rejCount << "次跳变，按真实位置接受:"
                         << mmSw << "上次:" << lastMm;
    }
    // "0 噪声"过滤：仅在不可信的滑窗匹配时启用
    if (!strictAxis && hasLast && qAbs(lastMm) > 2.0 && qAbs(mmSw) < 0.001) {
        stat.rejects++;
        qCDebug(lcPos) << axisName << "轴滑窗匹配到疑似假 0，丢弃, 上次:" << lastMm;
        return;
    }

    qCDebug(lcPos) << axisName << "轴位置更新:" << mmSw << "mm"
                   << "(驱动器读数" << mm << ", strict:" << strictAxis
                   << ", 上次:" << (hasLast ? lastMm : 0) << ")";
    rejCount = 0;
    lastMm = mmSw;
    hasLast = true;
    stat.sinceUpdate.restart();
    if (axisId == 2) emit positionChangex(mmSw);
    else             emit positionChangey(mmSw);
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
    // layer 同时回报命中层级，日志里能看出网关是否吃帧头/粘包。
    auto tryAt = [this](int idx, int &layer) -> int {
        layer = 0;
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
                    layer = 1;
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
                    layer = 2;
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
                    layer = 3;
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
        int layer = 0;
        int consumed = tryAt(i, layer);
        if (consumed > 0) {
            // 按命中层级计数：透明网关下 L1 应占 100%，L2/L3 一旦出现就是字节流被改写
            if (layer == 1)      ++statParseL1;
            else if (layer == 2) ++statParseL2;
            else if (layer == 3) ++statParseL3;
            qCDebug(lcBus) << "位置回包命中 L" << layer << "偏移" << i
                           << "帧:" << rxBuffer.mid(i, consumed).toHex(' ').toUpper();
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
    qCInfo(lcBus) << "汇川控制器析构, 未发出指令" << txQueue.size() << "条"
                  << "socket 状态" << (tcpSocket ? lockeLog::socketStateName(tcpSocket->state())
                                                : QString::fromUtf8("无"));
    // 退出时先标记为主动断开，避免触发“意外断开”误报
    userDisconnect = true;
    // 先停掉调度和重连，防止关闭过程中又发查询、又发起连接
    if (timer) timer->stop();
    if (reconnectTimer) reconnectTimer->stop();
    if (connectTimeoutTimer) connectTimeoutTimer->stop();

    // 程序退出前断使能，避免电机一直锁住、现场无法手推。
    // releaseMotorsDirect 内部会等数据真正写进内核，不能写完立刻 abort。
    releaseMotorsDirect("程序退出", true);

    if (tcpSocket) {
        // 优雅关闭：先发 FIN 并等网关确认。直接 abort 会丢弃发送缓冲，
        // 把刚写的 0x80 一起冲掉，电机就会保持使能。
        if (tcpSocket->state() == QAbstractSocket::ConnectedState) {
            tcpSocket->disconnectFromHost();
            if (tcpSocket->state() != QAbstractSocket::UnconnectedState
                && !tcpSocket->waitForDisconnected(500)) {
                qCWarning(lcBus) << "等待网关断开超时(500ms)，强制 abort:"
                                 << tcpSocket->errorString();
                tcpSocket->abort();
            }
        } else if (tcpSocket->state() != QAbstractSocket::UnconnectedState) {
            tcpSocket->abort();
        }
    }
    delete tcpSocket;
    tcpSocket = nullptr;
    if (settings) {

        delete settings;
        settings = nullptr;
    }

    if (reconnectTimer) {
        reconnectTimer->stop();
        delete reconnectTimer;
        reconnectTimer = nullptr;
    }
    if (timer) {
        timer->stop();
        delete timer;
        timer = nullptr;
    }
}




void ScanControlHuiChuan::on_connectBtn_clicked()
{
    // 操作员手动操作优先：停掉自动重连，避免和手动连接抢同一个 socket
    if (reconnectTimer)
        reconnectTimer->stop();
    if (connectTimeoutTimer)
        connectTimeoutTimer->stop();
    reconnectAttempts = 0;

    // 只连接一次 socket 信号，防止每次点击"连接"都重复 connect 导致回调多次触发
    if (!isSocketSignalConnected) {
        isSocketSignalConnected = true;

        // 连接成功
        connect(tcpSocket, &QTcpSocket::connected, this, [this]{
            connectedSince.restart();
            // 关掉 Nagle：位置查询帧只有 5 字节，Nagle 可能压住等 ACK(最长约 40ms)，
            // 再叠加网关打包时间就会逼近 300ms 回包超时。每次(重)连接 socket 都是新的，必须重设。
            // Qt5 的 setSocketOption 返回 void，用 socketOption 回读确认是否真的生效。
            tcpSocket->setSocketOption(QAbstractSocket::LowDelayOption, 1);
            if (tcpSocket->socketOption(QAbstractSocket::LowDelayOption).toInt() != 0) {
                qCInfo(lcBus) << "已启用 TCP_NODELAY(低延迟发送)";
            } else {
                qCWarning(lcBus) << "TCP_NODELAY 未生效，位置查询可能被 Nagle 延迟";
            }
            if (reconnectTimer)
                reconnectTimer->stop();
            if (connectTimeoutTimer)
                connectTimeoutTimer->stop();
            const int attempts = reconnectAttempts;
            reconnectAttempts = 0;
            lastWifiResetAttempt = -1;
            qCInfo(lcBus) << "网关已连接" << PlcIP << PlcPort
                          << "本地" << tcpSocket->localAddress().toString() << tcpSocket->localPort()
                          << "对端" << tcpSocket->peerAddress().toString() << tcpSocket->peerPort()
                          << (attempts > 0 ? QString("(自动重连第%1次成功)").arg(attempts) : QString());
            userDisconnect = false;
            resetBusState();
            emit tcpStateChange(true);
        });

        // 断开连接：把连接保持时长、错误、在途查询、最后位置时间一次性记全
        connect(tcpSocket, &QTcpSocket::disconnected, this, [this]{
            if (connectTimeoutTimer)
                connectTimeoutTimer->stop();
            const bool wasUserDisconnect = userDisconnect;
            const qint64 heldMs = connectedSince.isValid() ? connectedSince.elapsed() : -1;
            const qint64 xAge = statX.sinceUpdate.isValid() ? statX.sinceUpdate.elapsed() : -1;
            const qint64 yAge = statY.sinceUpdate.isValid() ? statY.sinceUpdate.elapsed() : -1;
            if (wasUserDisconnect) {
                qCInfo(lcBus) << "网关已断开(操作员主动断开)" << PlcIP << PlcPort
                              << "连接保持" << heldMs << "ms";
            } else {
                qCCritical(lcBus) << "网关连接意外断开" << PlcIP << PlcPort
                                  << "连接保持" << heldMs << "ms"
                                  << "错误" << lockeLog::socketErrorName(tcpSocket->error())
                                  << tcpSocket->errorString()
                                  << "未发出指令" << txQueue.size() << "条被丢弃"
                                  << "在途位置查询" << (waitingPosReply ? (lastQueriedAxisId == 2 ? "X" : "Y") : "无")
                                  << "等待" << posReplyWait.elapsed() << "ms"
                                  << "最后位置 X" << xAge << "ms前/Y" << yAge << "ms前"
                                  << "电机仍保持使能(0x80 已无法送达)，需手推请重连后按结束或重启网关";
            }
            userDisconnect = false;
            resetBusState();
            emit tcpStateChange(false);
            if (!wasUserDisconnect)
                scheduleReconnect("意外断开");
        });

        // socket 状态每次变化都记一笔，断连前后能还原完整过程
        connect(tcpSocket, &QTcpSocket::stateChanged, this,
                [](QAbstractSocket::SocketState state){
            qCInfo(lcBus) << "网关 socket 状态 ->" << lockeLog::socketStateName(state)
                          << "(" << static_cast<int>(state) << ")";
        });

        // ❗连接失败 / 网络错误（关键）
        connect(tcpSocket,
                QOverload<QAbstractSocket::SocketError>::of(&QTcpSocket::error),
                this,
                [this](QAbstractSocket::SocketError err){
            // 错误信号可能在 waitForBytesWritten() 内被同步再次触发：
            // 若在错误处理里再调用 releaseMotorsDirect，就会递归到栈溢出，
            // 表现为关闭程序时日志刷屏几千行后进程直接消失。
            if (handlingSocketError) {
                qCDebug(lcBus) << "socket 错误处理中，忽略嵌套错误" << static_cast<int>(err);
                return;
            }
            handlingSocketError = true;
            qCWarning(lcBus) << "网关 socket 错误: 代码" << static_cast<int>(err)
                             << lockeLog::socketErrorName(err) << tcpSocket->errorString()
                             << "状态" << lockeLog::socketStateName(tcpSocket->state())
                             << "操作员主动断开:" << userDisconnect;
            if (connectTimeoutTimer)
                connectTimeoutTimer->stop();

            // RemoteHostClosedError 表示对端已经关闭，再写 0x80 只会触发无效 socket 告警。
            // 其它仍保持连接的故障，尽最大努力把断使能发出去。
            if (!userDisconnect
                && err != QAbstractSocket::RemoteHostClosedError
                && tcpSocket->state() == QAbstractSocket::ConnectedState) {
                releaseMotorsDirect("socket 错误");
            }

            // 已建立连接的清理由 disconnected 统一完成，避免提前清掉断链诊断并重复通知 UI。
            if (tcpSocket->state() == QAbstractSocket::UnconnectedState) {
                resetBusState();
                emit tcpStateChange(false);
            }
            if (!userDisconnect)
                scheduleReconnect("socket 错误");
            handlingSocketError = false;
        });

        // 收到数据（只连接一次）
        // 不再因为 "现在没在等位置回包" 就 clear() 缓冲：
        // 实测 0x92 回包延迟有时 80~150ms，等到回包到达时 waitingPosReply 可能已经被
        // 上层超时复位，再 clear 就把回包丢了。这里只解析有协议自证身份的位置帧
        // (parseAngleByChecksum 内部按 92 ID 08 / 08 DA(D9) 锚点)，其它字节做溢出保护。
        connect(tcpSocket, &QTcpSocket::readyRead, this, [this]() {
            const QByteArray chunk = tcpSocket->readAll();
            qCDebug(lcBus) << "RX" << chunk.size() << "字节, 缓冲"
                           << rxBuffer.size() + chunk.size()
                           << "hex:" << chunk.toHex(' ').toUpper();
            rxBuffer.append(chunk);
            parseAngleByChecksum();
            // 解析完后还很大说明全是非位置帧的 ACK，丢掉防膨胀
            if (rxBuffer.size() > 256) {
                qCWarning(lcBus) << "接收缓冲仍有" << rxBuffer.size()
                                 << "字节未识别数据，截尾保留32字节";
                rxBuffer.remove(0, rxBuffer.size() - 32);
            }
        });


    }



    const QAbstractSocket::SocketState st = tcpSocket->state();
    qCInfo(lcBus) << "连接按钮: 当前状态" << lockeLog::socketStateName(st)
                  << "目标" << PlcIP << PlcPort;

    // 状态切换
    if (st == QAbstractSocket::ConnectedState) {
        if (userDisconnect) {
            qCInfo(lcBus) << "断开已在进行中，忽略重复点击";
            return;
        }
        userDisconnect = true;
        qCInfo(lcBus) << "操作员主动断开网关" << PlcIP << PlcPort
                      << "连接保持" << (connectedSince.isValid() ? connectedSince.elapsed() : -1) << "ms";
        // 先让 0x80 断使能发出去（2 帧，留 400ms），再真正断开；
        // 否则一断开队列就被清空，电机保持使能，手推不动。
        // 清掉待发指令，避免 0x80 排在一堆运动指令后面来不及发出。
        if (!txQueue.isEmpty()) {
            qCInfo(lcBus) << "断开前丢弃" << txQueue.size() << "条未发出的指令";
            txQueue.clear();
        }
        releaseMotors();
        QTimer::singleShot(400, this, [this]{
            if (tcpSocket && tcpSocket->state() == QAbstractSocket::ConnectedState) {
                qCInfo(lcBus) << "断使能等待结束，执行断开";
                tcpSocket->disconnectFromHost();
            }
        });
    } else {
        if (st == QAbstractSocket::HostLookupState
            || st == QAbstractSocket::ConnectingState
            || st == QAbstractSocket::ClosingState) {
            qCInfo(lcBus) << "取消尚未完成的 socket 操作后重新连接";
            tcpSocket->abort();
        }

        userDisconnect = false;
        QElapsedTimer connectElapsed;
        connectElapsed.start();
        qCInfo(lcBus) << "发起网关 TCP 连接" << PlcIP << PlcPort;
        tcpSocket->setProxy(QNetworkProxy::NoProxy);
        tcpSocket->connectToHost(PlcIP, PlcPort);

        if (tcpSocket->waitForConnected(3000)) { // 等待最多3秒
            qCInfo(lcBus) << "网关连接建立耗时" << connectElapsed.elapsed() << "ms";
        } else {
            qCCritical(lcBus) << "网关连接失败(3s 超时)" << PlcIP << PlcPort
                              << "错误代码" << static_cast<int>(tcpSocket->error())
                              << lockeLog::socketErrorName(tcpSocket->error())
                              << tcpSocket->errorString()
                              << "状态" << lockeLog::socketStateName(tcpSocket->state())
                              << "请检查 IP/端口、网线和网关电源";
            if (tcpSocket->state() != QAbstractSocket::UnconnectedState)
                tcpSocket->abort();
            emit tcpStateChange(false);
            scheduleReconnect("手动连接失败");
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
    // 设零就是重建坐标系：偏移归零，不需要再对齐
    posOffsetX = 0; posOffsetY = 0;
    savedSwX = 0;   savedSwY = 0;
    anchorPendingX = false; anchorPendingY = false;
    absMemX = 0;    absMemY = 0;
    absMemValidX = true; absMemValidY = true;
    savePositionMemory();
    // UI 立即归零，给操作员一个明确反馈
    emit positionChangex(0);
    emit positionChangey(0);

    // 电机关闭状态下 0x19/0x76 不会执行，先恢复两轴运行
    ensureMotorRunning(0x01);
    ensureMotorRunning(0x02);

    // 四条命令依次入队，由调度器按 kBusGapMs 间隔串行发出，不会粘帧
    buildAndSend(0x19, 0x01);
    buildAndSend(0x19, 0x02);
    // 0x76 重启 -> 让 0x19 写入的零点立即生效
    buildAndSend(0x76, 0x01);
    buildAndSend(0x76, 0x02);
    // 协议：电机重启后回到上电默认的“开启”状态
    motorOffX = false;
    motorOffY = false;

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
    if (!jogAllowed("X+ 按下")) return;
    updateCurPos = false;
    axisJog = AxisJog::XJogAddPressed;

}

void ScanControlHuiChuan::on_xAddBtn_released()
{
    if (!jogAllowed("X+ 松开")) return;
    updateCurPos = false;
    axisJog = AxisJog::XJogAddReleased;

}

void ScanControlHuiChuan::on_xSubBtn_pressed()
{
    if (!jogAllowed("X- 按下")) return;
    updateCurPos = false;
    axisJog = AxisJog::XJogSubPressed;
}

void ScanControlHuiChuan::on_xSubBtn_released()
{
    if (!jogAllowed("X- 松开")) return;
    updateCurPos = false;
    axisJog = AxisJog::XJogSubReleased;
}

void ScanControlHuiChuan::on_yAddBtn_pressed()
{
    if (!jogAllowed("Y+ 按下")) return;
    updateCurPos = false;
    axisJog = AxisJog::YJogAddPressed;
}

void ScanControlHuiChuan::on_yAddBtn_released()
{
    if (!jogAllowed("Y+ 松开")) return;
    updateCurPos = false;
    axisJog = AxisJog::YJogAddReleased;
}

void ScanControlHuiChuan::on_ySubBtn_pressed()
{
    if (!jogAllowed("Y- 按下")) return;
    updateCurPos = false;
    axisJog = AxisJog::YJogSubPressed;
}

void ScanControlHuiChuan::on_ySubBtn_released()
{
    if (!jogAllowed("Y- 松开")) return;
    updateCurPos = false;
    axisJog = AxisJog::YJogSubReleased;
}

bool ScanControlHuiChuan::jogAllowed(const char* action)
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





void ScanControlHuiChuan::init()
{
    settings = new QSettings("./scan_setting.ini", QSettings::IniFormat);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0) && QT_CONFIG(textcodec)
    // Qt5 的 INI 默认按本地 8 位编码读写，中文区域名会乱码/匹配不上
    settings->setIniCodec("UTF-8");
#endif
    // 上次退出发过 0x80，驱动器多圈基准已丢，载入软件记忆供 0x95 写回
    loadPositionMemory();
    qCInfo(lcBus) << "汇川控制器初始化: 调度节拍" << kTickMs << "ms, 帧间隔" << kBusGapMs
                  << "ms, 空闲间隔" << kIdleGapMs << "ms, 位置回包超时" << kPosReplyTimeout
                  << "ms, 队列上限" << kMaxTxQueue;

    timer = new QTimer(this);
    // 调度节拍 10ms：每个节拍最多发一帧。
    // 发送规则：上一帧发出/回包到达后至少空闲 kBusGapMs；
    // 有位置查询在途时什么都不发，直到回包或超时，保证总线上同一时刻只有一问一答。
    timer->setInterval(kTickMs);
    busIdle.start();
    posReplyWait.start();
    statWindow.start();



    connect(timer, &QTimer::timeout, this, &ScanControlHuiChuan::performTasks);

    // 意外断线后自动重连：网关/网络恢复后无需人工点“连接”
    reconnectTimer = new QTimer(this);
    reconnectTimer->setSingleShot(true);
    reconnectTimer->setInterval(5000);
    connect(reconnectTimer, &QTimer::timeout, this, &ScanControlHuiChuan::tryAutoReconnect);

    connectTimeoutTimer = new QTimer(this);
    connectTimeoutTimer->setSingleShot(true);
    connectTimeoutTimer->setInterval(15000);
    connect(connectTimeoutTimer, &QTimer::timeout, this, [this]{
        if (!tcpSocket || userDisconnect)
            return;
        const QAbstractSocket::SocketState state = tcpSocket->state();
        if (state != QAbstractSocket::HostLookupState
            && state != QAbstractSocket::ConnectingState)
            return;
        qCWarning(lcBus) << "自动重连超时，中止本次连接";
        tcpSocket->abort();
        scheduleReconnect("连接超时");
    });

    timer->start();
    tcpSocket=new QTcpSocket(this);

}



void ScanControlHuiChuan::performTasks()
{
    if (!tcpSocket || tcpSocket->state() != QAbstractSocket::ConnectedState) {
        // 断线期间每 5 秒提醒一次，避免完全静默导致无法判断是断线还是卡死
        if (statWindow.elapsed() >= 5000) {
            statWindow.restart();
            qCWarning(lcBus) << "调度器: 网关未连接, 状态"
                             << (tcpSocket ? lockeLog::socketStateName(tcpSocket->state())
                                           : QString::fromUtf8("socket 不存在"))
                             << "目标" << PlcIP << PlcPort << "队列" << txQueue.size();
        }
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

    // 2.5s 内有运动指令视为“运动中”：高频轮询；否则降频到 2Hz
    const bool motionActive = sinceMotionCmd.isValid()
            && sinceMotionCmd.elapsed() < kActiveHoldMs;

    // 控制指令优先，指令之间的间隔始终用较短的 kBusGapMs
    if (!txQueue.isEmpty()) {
        if (busIdle.elapsed() < kBusGapMs)
            return;
        writeFrame(txQueue.dequeue());
        return;
    }

    // 运动指令刚发出：先让驱动器启动，暂不安排位置查询
    if (motionActive && sinceMotionCmd.elapsed() < kCmdQuietMs) {
        return;
    }

    if (busIdle.elapsed() < (motionActive ? kBusGapMs : kIdleGapMs)) {
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
        qCWarning(lcBus) << "网关未连接，指令未发送:" << describeFrame(frame)
                         << "状态" << (tcpSocket ? lockeLog::socketStateName(tcpSocket->state())
                                                 : QString::fromUtf8("socket 不存在"))
                         << "hex:" << frame.toHex(' ').toUpper();
        return;
    }
    if (txQueue.size() >= kMaxTxQueue) {
        statQueueDrops++;
        qCWarning(lcBus) << "发送队列已满(" << kMaxTxQueue << ")，丢弃最旧指令:"
                         << describeFrame(txQueue.head())
                         << "hex:" << txQueue.head().toHex(' ').toUpper();
        txQueue.dequeue();
    }
    if (urgent) {
        txQueue.prepend(frame);
    } else {
        txQueue.enqueue(frame);
    }
    statQueuePeak = qMax(statQueuePeak, txQueue.size());
}


// 新运动指令要下发时，把该轴还排在队列里没发出去的 0x80 丢掉：
// 0x80 一旦写出就会清掉驱动器多圈计数，后面的绝对坐标全部错位，
// 界面也会因为读数作废而停在原地。马上要动电机，本来就不该再断使能。
void ScanControlHuiChuan::dropPendingMotorOff(quint8 motorId)
{
    for (int i = txQueue.size() - 1; i >= 0; --i) {
        const QByteArray& f = txQueue.at(i);
        if (f.size() >= 3 && quint8(f[1]) == 0x80 && quint8(f[2]) == motorId) {
            qCInfo(lcBus) << (motorId == 0x02 ? "X" : "Y")
                          << "轴马上要发运动指令，丢弃尚未发出的 0x80 断使能";
            txQueue.removeAt(i);
        }
    }
}


bool ScanControlHuiChuan::writeFrame(const QByteArray& frame)
{
    const quint8 cmd = frame.size() >= 2 ? quint8(frame[1]) : 0;
    // 0x80 真正发出去的这一刻，驱动器就会清掉多圈计数：
    // 必须在写 socket 之前把软件侧最后可信位置记下来，供下一帧位置回包重建 offset。
    if (cmd == 0x80)
        rememberPositionBeforeOff(frame.size() >= 3 ? quint8(frame[2]) : quint8(0x00));

    const qint64 written = tcpSocket->write(frame);
    tcpSocket->flush();
    busIdle.restart();
    const bool isPoll = (cmd == 0x92);
    // 记录运动指令：决定后续轮询节奏(运动中/空闲)以及优先查询哪一轴
    if (cmd == 0xA4 || cmd == 0xA2) {
        sinceMotionCmd.restart();
        activeAxisId = frame.size() >= 3 ? quint8(frame[2]) : 0;
    }
    if (written != frame.size()) {
        statWriteFails++;
        qCWarning(lcBus) << "写 socket 失败, 应写" << frame.size() << "实际" << written
                         << describeFrame(frame) << "状态"
                         << lockeLog::socketStateName(tcpSocket->state())
                         << "错误代码" << static_cast<int>(tcpSocket->error())
                         << lockeLog::socketErrorName(tcpSocket->error())
                         << tcpSocket->errorString() << "hex:" << frame.toHex(' ').toUpper();
        return false;
    }
    // 每一帧（含位置查询）都记原始字节；位置查询用 Debug 级别避免 Info 刷屏
    qCDebug(lcBus) << "TX hex:" << frame.toHex(' ').toUpper();
    if (!isPoll) {
        statCmdFrames++;
        qCInfo(lcBus) << "TX" << describeFrame(frame) << "| 队列剩余" << txQueue.size();
    }
    return true;
}


void ScanControlHuiChuan::resetBusState()
{
    qCInfo(lcBus) << "复位总线状态: 丢弃指令" << txQueue.size() << "条, 接收缓冲"
                  << rxBuffer.size() << "字节, 在途位置查询" << waitingPosReply
                  << ", 连续超时 X" << statX.timeoutStreak << "/Y" << statY.timeoutStreak;
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
    // 断线前可能发过 0x80，重连后电机状态未知：下一条运动指令前统一补 0x88
    motorOffX = true;
    motorOffY = true;
    busIdle.restart();
    // 断线前的运动状态已失效，重连后先按空闲节奏轮询
    sinceMotionCmd.invalidate();
    activeAxisId = 0;
    statX = AxisStats();
    statY = AxisStats();
    statCmdFrames = statWriteFails = statQueueDrops = statQueuePeak = statJunkBytes = 0;
    statParseL1 = statParseL2 = statParseL3 = 0;
    statWindow.start();
}


// 意外断线后延时重连：网关/网络恢复后自动恢复，不需要人工点“连接”。
// 间隔 5 秒，最多 24 次（约 2 分钟）；操作员点“连接/断开”会立即接管。
void ScanControlHuiChuan::scheduleReconnect(const char* reason)
{
    if (userDisconnect || !reconnectTimer)
        return;
    if (reconnectTimer->isActive())
        return;
    if (reconnectAttempts >= 24) {
        qCCritical(lcBus) << "自动重连已停止(" << reason << ")：连续" << reconnectAttempts
                          << "次失败，请检查网关电源/网线后手动点连接";
        return;
    }
    ++reconnectAttempts;
    qCWarning(lcBus) << "将在 5 秒后自动重连(" << reason << ")，第" << reconnectAttempts << "次";
    reconnectTimer->setInterval(5000);
    reconnectTimer->start();
}

void ScanControlHuiChuan::tryAutoReconnect()
{
    if (userDisconnect || !tcpSocket)
        return;

    if (connectTimeoutTimer)
        connectTimeoutTimer->stop();
    if (tcpSocket->state() != QAbstractSocket::UnconnectedState) {
        // 错误后 socket 可能仍停留在 Connected/Closing；先彻底释放，再发起本次连接。
        qCWarning(lcBus) << "清理上一次未结束的 socket 状态:"
                         << lockeLog::socketStateName(tcpSocket->state());
        tcpSocket->abort();
        if (reconnectTimer)
            reconnectTimer->stop();
    }

    // 连续 3 次连不上时尝试恢复 Wi-Fi；无论是否掉线都使用固定热点 Profile。
    if (reconnectAttempts >= 3 && reconnectAttempts % 3 == 0
        && lastWifiResetAttempt != reconnectAttempts) {
        lastWifiResetAttempt = reconnectAttempts;
        qCWarning(lcBus) << "连续" << reconnectAttempts << "次重连失败，尝试重置 Wi-Fi 链路";
        if (resetWifiLink()) {
            qCInfo(lcBus) << "Wi-Fi 已重置，3 秒后继续重连";
            reconnectTimer->start(3000);
            return;
        }
        qCWarning(lcBus) << "Wi-Fi 重置失败，继续直接重连";
    }

    qCInfo(lcBus) << "自动重连网关" << PlcIP << PlcPort << "第" << reconnectAttempts << "次";
    tcpSocket->setProxy(QNetworkProxy::NoProxy);
    tcpSocket->connectToHost(PlcIP, PlcPort);
    connectTimeoutTimer->start();
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

    const qint64 uptimeMs = connectedSince.isValid() ? connectedSince.elapsed() : -1;
    const QString summary = QString("通讯统计 %1s(连接保持%2ms, 状态%3, 队列%4): %5 %6 指令%7 队列峰值%8 写失败%9 队列丢弃%10 非位置字节%11 解析L1/L2/L3=%12/%13/%14")
            .arg(windowMs / 1000.0, 0, 'f', 1)
            .arg(uptimeMs)
            .arg(tcpSocket ? lockeLog::socketStateName(tcpSocket->state()) : QString::fromUtf8("无"))
            .arg(txQueue.size())
            .arg(axisLine("X", statX), axisLine("Y", statY))
            .arg(statCmdFrames).arg(statQueuePeak).arg(statWriteFails)
            .arg(statQueueDrops).arg(statJunkBytes)
            .arg(statParseL1).arg(statParseL2).arg(statParseL3);

    const bool staleX = isStale(statX);
    const bool staleY = isStale(statY);
    // L2/L3 只会在帧头被吃/残帧时命中；网关透明时恒为 0，是配置正确性的直接证据。
    const bool streamDamaged = (statParseL2 > 0 || statParseL3 > 0);
    // 指令应答也会计入非位置字节，只有明显超出正常应答量才告警
    const bool junkFlood = (statJunkBytes > 512);
    const bool abnormal = staleX || staleY
            || statX.timeouts || statY.timeouts
            || statX.rejects || statY.rejects
            || statWriteFails || statQueueDrops
            || streamDamaged || junkFlood;

    if (abnormal) {
        qCWarning(lcBus).noquote() << summary;
        if (staleX) qCWarning(lcPos) << "X 轴位置停更超过" << kStaleMs << "ms，界面显示的 X 不是实时值";
        if (staleY) qCWarning(lcPos) << "Y 轴位置停更超过" << kStaleMs << "ms，界面显示的 Y 不是实时值";
        if (streamDamaged)
            qCWarning(lcBus) << "位置回包降级命中 L2" << statParseL2 << "次/L3" << statParseL3
                             << "次，网关在改写字节流。请检查工作模式是否为"
                                "TCP Server + 透明传输(不能是 Modbus RTU 转换)，打包时间是否为 0";
        if (junkFlood)
            qCWarning(lcBus) << "窗口内非位置字节" << statJunkBytes
                             << "字节，已超出指令应答的正常量。请检查网关心跳包/注册包/"
                                "主动上报是否关闭、本地回显是否关闭";
    } else {
        qCInfo(lcBus).noquote() << summary;
    }

    // 窗口计数清零；连续超时和最后更新时间跨窗口保留
    for (AxisStats* s : {&statX, &statY}) {
        s->queries = s->replies = s->timeouts = s->late = s->rejects = 0;
    }
    statCmdFrames = statWriteFails = statQueueDrops = statJunkBytes = 0;
    statParseL1 = statParseL2 = statParseL3 = 0;
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

    // 选中本次要查的轴：运动中优先查运动轴（扫查时只有它在动，另一轴只需心跳），
    // 空闲时两轴轮流查。这样运动轴采样更快，同时不再把一半帧浪费在静止轴上。
    quint8 axisId = 0;
    const bool motionActive = sinceMotionCmd.isValid()
            && sinceMotionCmd.elapsed() < kActiveHoldMs;
    if (motionActive && activeAxisId != 0) {
        const quint8 otherId = (activeAxisId == 2) ? 1 : 2;
        const AxisStats& other = axisStat(otherId);
        // 另一轴连续超时(可能未上电/断线)时把心跳放宽到 3s，避免白白占用总线
        const int refreshMs = other.timeoutStreak >= kTimeoutWarnStreak
                ? 3000 : kOtherAxisRefreshMs;
        const bool otherStale = !other.sinceUpdate.isValid()
                || other.sinceUpdate.elapsed() > refreshMs;
        axisId = otherStale ? otherId : activeAxisId;
    } else {
        axisId = queryXNext ? 2 : 1;      // 2 = X轴电机, 1 = Y轴电机
        queryXNext = !queryXNext;
    }
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

    case NotAxisJog:        break;
    default:
        qCWarning(lcUi) << "点动: Z/R 轴暂不支持, 状态" << axisJog;
        break;
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
    qCInfo(lcBus) << "点动速度指令入队: 电机" << motorId << "速度"
                  << (speedValue / 6000.0) << "mm/s, hex:" << frame.toHex(' ').toUpper();
    // 若电机处于 0x80 关闭状态，必须先补 0x88，否则速度指令不会执行
    ensureMotorRunning(motorId);
    enqueueFrame(frame);
}

void ScanControlHuiChuan::runPosintion(int64_t angleControl, uint32_t maxSpeed)
{
    // 不再 clear rxBuffer：A4 命令和位置回包是两条独立的协议帧，
    // parseAngleByChecksum 已经能从混合流里挑出 92/08DA/08D9 帧，
    // 这里粗暴 clear 反而会把还没消费完的位置回包丢掉。
    QByteArray frame;

    // angleControl 是软件坐标（mm*6000），0xA4 要的是驱动器坐标系里的绝对角度：
    // 减掉对齐偏移后，即使 0x80 把驱动器多圈计数清过，走过的距离仍然等于软件坐标差。
    const double offsetMm = (motorId == 0x02) ? posOffsetX : posOffsetY;
    const int64_t driverAngle = angleControl - static_cast<int64_t>(qRound64(offsetMm * 6000.0));


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
    int64_t angleToSend = driverAngle;
    quint8* angleBytes = reinterpret_cast<quint8*>(&angleToSend);
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
    qCInfo(lcBus) << "位置指令入队: 电机" << static_cast<int>(motorId)
                  << "目标" << (angleControl * 0.01 / 60.0) << "mm"
                  << "(驱动器坐标" << (driverAngle * 0.01 / 60.0) << "mm, 偏移"
                  << offsetMm << "mm)"
                  << "限速" << (maxSpeed / 6000.0) << "mm/s"
                  << "hex:" << frame.toHex(' ').toUpper();
    // 若电机处于 0x80 关闭状态，必须先补 0x88，否则位置指令不会执行
    ensureMotorRunning(motorId);
    enqueueFrame(frame);
}



void ScanControlHuiChuan::on_start(){
    qCInfo(lcBus) << "开始: 控制器 on_start 无额外指令，位置目标由主界面下发";
}
void ScanControlHuiChuan::on_stop(){
    qCInfo(lcBus) << "暂停: 汇川控制器 on_stop 不发指令，电机继续执行已下发的目标";
}

// 协议 V2.35：0x80 关闭后电机“仍然回复命令，但不会执行动作”，
// 必须先发 0x88 电机运行，后续 0xA2/0xA4 才会动作。
void ScanControlHuiChuan::ensureMotorRunning(quint8 motorId)
{
    // 排在该轴后面的 0x80 还没发出去就先丢掉了：马上要发运动指令，
    // 再断使能会平白把多圈基准弄丢（并发时会出现 0x80 夹在 0x88 前面）。
    dropPendingMotorOff(motorId);

    bool& off = (motorId == 0x02) ? motorOffX : motorOffY;
    if (!off)
        return;
    QByteArray frame;
    frame.append(static_cast<char>(0x3E));      // 帧头
    frame.append(static_cast<char>(0x88));      // Motor Running：从关闭状态恢复
    frame.append(static_cast<char>(motorId));   // 电机ID
    frame.append(static_cast<char>(0x00));      // 数据长度
    frame.append(static_cast<char>(calculateChecksum(frame)));
    qCInfo(lcBus) << (motorId == 0x02 ? "X" : "Y")
                  << "轴处于电机关闭状态，先发 0x88 恢复运行";
    enqueueFrame(frame);
    off = false;
    // 0x80 之后读数会整体平移，这里不用发 0x95：等下一帧位置回包时
    // 由 anchorPositionReference 重建 offset，0xA4 目标自动减掉它。
}

// 记下软件坐标：0x80 一写出，驱动器的多圈计数就作废了
void ScanControlHuiChuan::rememberPositionBeforeOff(quint8 motorId)
{
    if (motorId == 0x00 || motorId == 0x02) {
        if (hasLastX) { savedSwX = lastXmm; absMemValidX = true; }
        else if (absMemValidX) { savedSwX = absMemX; }
        absMemX = savedSwX;
        anchorPendingX = absMemValidX;
    }
    if (motorId == 0x00 || motorId == 0x01) {
        if (hasLastY) { savedSwY = lastYmm; absMemValidY = true; }
        else if (absMemValidY) { savedSwY = absMemY; }
        absMemY = savedSwY;
        anchorPendingY = absMemValidY;
    }
    savePositionMemory();

    // 从这一刻起，晚于 kAnchorGuardMs 收到的回包才属于新坐标系
    sinceMotorOff.restart();

    auto fmt = [](bool valid, double mm) {
        return valid ? QString::number(mm, 'f', 3) : QString("未知");
    };
    qCInfo(lcBus) << "记忆坐标(断使能后按此对齐): X" << fmt(absMemValidX, savedSwX)
                  << "Y" << fmt(absMemValidY, savedSwY) << "mm";
}

// 0x80 之后驱动器多圈计数被清，读数从 153.2 变成 1.28 这种残值。
// 这里用“断使能前的软件坐标 - 当前读数”重建 offset：
//   软件坐标 = 读数 + offset
// 界面因此连续，且后续 0xA4 目标减掉 offset 后，实际走的距离就等于软件坐标差。
void ScanControlHuiChuan::anchorPositionReference(quint8 axisId, double rawMm)
{
    const bool isX = (axisId == 2);
    bool& pending = isX ? anchorPendingX : anchorPendingY;
    const bool valid = isX ? absMemValidX : absMemValidY;
    const double savedSw = isX ? savedSwX : savedSwY;
    double& offset = isX ? posOffsetX : posOffsetY;
    const char* axisName = isX ? "X" : "Y";

    pending = false;
    if (!valid) {
        offset = 0;
        qCWarning(lcBus) << axisName
                         << "轴没有可信坐标基准，断使能后无法对齐，请重新“设置零点”";
        return;
    }

    const double oldOffset = offset;
    offset = savedSw - rawMm;
    qCInfo(lcBus) << axisName << "轴位置基准重新对齐: 断使能前" << QString::number(savedSw, 'f', 3)
                  << "mm, 驱动器读数" << QString::number(rawMm, 'f', 3)
                  << "mm, 偏移" << QString::number(oldOffset, 'f', 3)
                  << "->" << QString::number(offset, 'f', 3);

    // 对齐后软件坐标会跳一段，别让跳变过滤把这帧丢掉
    if (isX) { hasLastX = false; rejectCountX = 0; }
    else     { hasLastY = false; rejectCountY = 0; }
}

void ScanControlHuiChuan::savePositionMemory()
{
    if (!settings)
        return;
    settings->beginGroup("position");
    settings->setValue("validX", absMemValidX);
    settings->setValue("validY", absMemValidY);
    settings->setValue("absX", absMemX);
    settings->setValue("absY", absMemY);
    // released=1 表示发过 0x80，重启后驱动器读数已平移，必须重新对齐
    settings->setValue("released", anchorPendingX || anchorPendingY);
    settings->endGroup();
    settings->sync();
}

void ScanControlHuiChuan::loadPositionMemory()
{
    if (!settings)
        return;
    settings->beginGroup("position");
    absMemX = settings->value("absX", 0.0).toDouble();
    absMemY = settings->value("absY", 0.0).toDouble();
    absMemValidX = settings->value("validX", false).toBool();
    absMemValidY = settings->value("validY", false).toBool();
    const bool released = settings->value("released", false).toBool();
    settings->endGroup();

    // 只有上次退出前确实发过 0x80，驱动器读数才整体平移过、需要重新对齐；
    // 没发过（崩溃/断电）说明驱动器自己的多圈计数还在，直接用它。
    savedSwX = absMemX;
    savedSwY = absMemY;
    anchorPendingX = released && absMemValidX;
    anchorPendingY = released && absMemValidY;
    if (anchorPendingX || anchorPendingY) {
        qCInfo(lcBus) << "载入位置记忆(上次退出发过 0x80): X" << absMemX << "Y" << absMemY
                      << "mm, 连上后按第一帧位置读数重新对齐坐标";
    }
}

// 断使能（Motor Off，0x80）：清除运行状态并断开电机输出，电机可自由手推。
// 0x81 只是减速停止，驱动器仍保持力矩，所以必须用 0x80。
// 走发送队列，保证与位置查询一问一答，避免和回包撞帧。
void ScanControlHuiChuan::releaseMotors()
{
    if (!tcpSocket || tcpSocket->state() != QAbstractSocket::ConnectedState) {
        qCWarning(lcBus) << "断使能失败: 网关未连接，电机仍保持使能。"
                            "意外掉线时程序无法发送，需在网关配置通讯超时自动断使能";
        return;
    }
    auto buildOff = [this](quint8 id) {
        QByteArray frame;
        frame.append(static_cast<char>(0x3E));      // 帧头
        frame.append(static_cast<char>(0x80));      // Motor Off：断使能
        frame.append(static_cast<char>(id));        // 电机ID
        frame.append(static_cast<char>(0x00));      // 数据长度
        frame.append(static_cast<char>(calculateChecksum(frame)));
        return frame;
    };
    qCInfo(lcBus) << "断使能: X/Y 发送 0x80 电机关闭，电机可手推";
    enqueueFrame(buildOff(0x02));
    enqueueFrame(buildOff(0x01));
    motorOffX = true;
    motorOffY = true;
}

// 链路异常/退出时绕开队列直接写，尽最大努力在链路彻底断开前发出 0x80
void ScanControlHuiChuan::releaseMotorsDirect(const char* reason, bool waitForSend)
{
    if (!tcpSocket || tcpSocket->state() != QAbstractSocket::ConnectedState) {
        qCWarning(lcBus) << "断使能未送达(" << reason << "): 链路已断，电机仍保持使能";
        return;
    }
    auto buildOff = [this](quint8 id) {
        QByteArray frame;
        frame.append(static_cast<char>(0x3E));
        frame.append(static_cast<char>(0x80));
        frame.append(static_cast<char>(id));
        frame.append(static_cast<char>(0x00));
        frame.append(static_cast<char>(calculateChecksum(frame)));
        return frame;
    };
    const QByteArray data = buildOff(0x02) + buildOff(0x01);
    // 与队列路径一样：0x80 写入前先记住软件坐标，下一帧读数用来重建 offset
    rememberPositionBeforeOff(0x00);
    const qint64 written = tcpSocket->write(data);
    tcpSocket->flush();
    busIdle.restart();
    if (written != data.size()) {
        qCWarning(lcBus) << "断使能(" << reason << "): 写入不完整" << written << "/" << data.size()
                         << tcpSocket->errorString();
        return;
    }

    if (waitForSend) {
        // flush 只把数据交给内核；退出路径紧接着就会关闭 socket，
        // 不等内核真正发完，0x80 会随发送缓冲一起丢掉，电机保持使能。
        QElapsedTimer waitTimer;
        waitTimer.start();
        qint64 lastPending = -1;
        while (tcpSocket->state() == QAbstractSocket::ConnectedState
               && tcpSocket->bytesToWrite() > 0
               && waitTimer.elapsed() < 300) {
            const qint64 pending = tcpSocket->bytesToWrite();
            if (pending == lastPending)
                break;          // 等了一轮没有进展，别再空转
            lastPending = pending;
            const int left = int(300 - waitTimer.elapsed());
            if (left <= 0 || !tcpSocket->waitForBytesWritten(left))
                break;
        }
        if (tcpSocket->bytesToWrite() > 0) {
            qCWarning(lcBus) << "断使能(" << reason << "): 300ms 内仍有"
                             << tcpSocket->bytesToWrite() << "字节未发出"
                             << tcpSocket->errorString();
        }
    }

    motorOffX = true;
    motorOffY = true;
    qCInfo(lcBus) << "断使能(" << reason << "): 0x80 已写入" << written << "字节"
                  << data.toHex(' ').toUpper();
}

void ScanControlHuiChuan::on_end(){

    qCInfo(lcBus) << "结束: X/Y 轴发送 0x81 停止";
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

    // 0x81 之后驱动器仍保持使能，再补两条 0x80 才能真正松开电机。
    // 延后 600ms，确保停止帧先发出、电机已经减速停下。
    QTimer::singleShot(600, this, [this]{
        if (!txQueue.isEmpty()) {
            qCInfo(lcBus) << "断使能延后: 队列仍有" << txQueue.size() << "条指令，本次跳过";
            return;
        }
        releaseMotors();
    });
}
void ScanControlHuiChuan::on_backZero(){
    qCDebug(lcBus) << "回原: 由主界面下发 0xA4 位置指令，控制器 on_backZero 不处理";
}


void ScanControlHuiChuan::on_alarmReset(){

    auto buildClearError = [this](quint8 id) {
        QByteArray frame;
        frame.append(static_cast<char>(0x3E));      // 帧头
        frame.append(static_cast<char>(0x9B));      // 命令：清除电机错误标志
        frame.append(static_cast<char>(id));        // 电机ID
        frame.append(static_cast<char>(0x00));      // 数据长度
        frame.append(static_cast<char>(calculateChecksum(frame)));
        return frame;
    };
    if (!tcpSocket || tcpSocket->state() != QAbstractSocket::ConnectedState) {
        qCWarning(lcUi) << "点击 复位(清除报警) 被忽略: 网关未连接";
        return;
    }
    qCInfo(lcUi) << "点击 复位(清除报警): X/Y 轴发送 0x9B";
    enqueueFrame(buildClearError(0x02));
    enqueueFrame(buildClearError(0x01));
}
