#include "mainwindow.h"
#include <QApplication>
#include <QLoggingCategory>
#include <QFile>
#include <QTextStream>
#include <QDateTime>
#include <QDir>
#include <QMutex>
#include <QCoreApplication>
#include <QFileInfo>
#include <QSysInfo>
#include <cstdio>

#include "locke_log.h"

#ifdef Q_OS_WIN
#include <QProcess>
#include <QNetworkInterface>
#include <QThread>
#include <qt_windows.h>
#endif

Q_LOGGING_CATEGORY(lcBus,  "locke.bus")
Q_LOGGING_CATEGORY(lcPos,  "locke.pos")
Q_LOGGING_CATEGORY(lcScan, "locke.scan")
Q_LOGGING_CATEGORY(lcUi,   "locke.ui")

// 日志保留天数，超过的自动删除
static const int kLogKeepDays = 30;

// 全局日志文件与互斥锁
static QFile g_logFile;
static QMutex g_logMutex;

// 本次运行标识（时间+PID），用于在同一天的追加日志里区分不同进程
static QString g_sessionId;

// 自定义日志处理函数：同时输出到控制台和日志文件
static void fileMessageHandler(QtMsgType type,
                               const QMessageLogContext &context,
                               const QString &msg)
{
    QMutexLocker locker(&g_logMutex);

    QString level;
    switch (type) {
    case QtDebugMsg:    level = "DEBUG"; break;
    case QtInfoMsg:     level = "INFO ";  break;
    case QtWarningMsg:  level = "WARN ";  break;
    case QtCriticalMsg: level = "ERROR"; break;
    case QtFatalMsg:    level = "FATAL"; break;
    }

    const QString timestamp =
        QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss.zzz");

    // 带上分类，方便按 [locke.bus] 等过滤；警告及以上附加源码位置
    QString category;
    if (context.category && qstrcmp(context.category, "default") != 0)
        category = QString("[%1]").arg(context.category);

    QString where;
    if ((type == QtWarningMsg || type == QtCriticalMsg || type == QtFatalMsg) && context.file)
        where = QString(" (%1:%2)").arg(QFileInfo(context.file).fileName()).arg(context.line);

    // 每行都带 PID，定位同一份追加日志里到底是哪次运行出的错
    const QString line = QString("[%1][%2][PID %3]%4 %5%6")
                             .arg(timestamp)
                             .arg(level)
                             .arg(QCoreApplication::applicationPid())
                             .arg(category)
                             .arg(msg)
                             .arg(where);

    // 控制台与日志文件统一 UTF-8，杜绝中文一半 GBK 一半 UTF-8 的乱码。
    // 先在 main() 里 SetConsoleOutputCP(CP_UTF8)，cmd/QtCreator 都能正常显示。
    const QByteArray utf8 = line.toUtf8();
    fwrite(utf8.constData(), 1, static_cast<size_t>(utf8.size()), stderr);
    fputc('\n', stderr);
    fflush(stderr);

    // 直接写字节，不经过 QTextStream 的本地编码转换
    if (g_logFile.isOpen()) {
        g_logFile.write(utf8);
        g_logFile.write("\n");
        g_logFile.flush();
    }

    if (type == QtFatalMsg)
        abort();
}

static void initFileLogger()
{
    // 日志目录：可执行文件目录/log
    const QString logDirPath = QCoreApplication::applicationDirPath() + "/log";
    QDir logDir(logDirPath);
    if (!logDir.exists())
        logDir.mkpath(".");

    const QString fileName =
        QString("Locke_%1.log")
            .arg(QDateTime::currentDateTime().toString("yyyyMMdd"));

    // 清理过期日志
    const QDateTime expire = QDateTime::currentDateTime().addDays(-kLogKeepDays);
    for (const QFileInfo &fi : logDir.entryInfoList(QStringList() << "Locke_*.log", QDir::Files)) {
        if (fi.lastModified() < expire)
            QFile::remove(fi.absoluteFilePath());
    }

    g_logFile.setFileName(logDir.filePath(fileName));
    const bool opened = g_logFile.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text);

    qInstallMessageHandler(fileMessageHandler);

    if (!opened) {
        qWarning() << "日志文件打开失败，仅输出到控制台:" << g_logFile.fileName()
                   << g_logFile.errorString();
    } else if (g_logFile.size() == 0) {
        // 写 UTF-8 BOM：老版 Windows 记事本据此识别编码，中文不再显示成乱码
        const QByteArray bom = QByteArray::fromHex("EFBBBF");
        g_logFile.write(bom);
        g_logFile.flush();
    }
}


#ifdef Q_OS_WIN
// 控制台命令输出可能是 UTF-8，也可能是本地 ANSI，先按 UTF-8 解，乱码再按 ANSI
static QString decodeConsoleText(const QByteArray &raw)
{
    if (raw.isEmpty())
        return QString();
    const QString utf8 = QString::fromUtf8(raw);
    if (!utf8.contains(QChar(QChar::ReplacementCharacter)))
        return utf8;
    // UTF-8 解码出现替换字符，说明子进程输出的是本地 ANSI(GBK)。
    // 记下原始字节，现场若仍有乱码可直接对照编码。
    qCDebug(lcBus) << "命令输出非 UTF-8，按本地编码重试, 原始字节:" << raw.toHex(' ');
    return QString::fromLocal8Bit(raw);
}

// 读取 WLAN 自动配置是否已被禁用（只读命令，普通权限即可执行）
static bool wifiAutoconfigDisabled()
{
    QProcess p;
    p.start("netsh", QStringList() << "wlan" << "show" << "settings");
    bool ok = p.waitForStarted(2000) && p.waitForFinished(4000);
    if (!ok) {
        p.kill();
        p.waitForFinished(500);
        return false;
    }
    // 只看“自动配置逻辑”那一行：英文 "...Auto configuration logic is disabled on interface..."
    // 中文 "自动配置逻辑...已禁用..."。不能全篇找 disabled —— 同一输出里还有
    // "MAC randomization disabled on interface WLAN"，会造成误判为已锁定。
    // 另外非 ASCII 字面量必须走 fromUtf8：Qt 5.12+MSVC 下本工程的 QStringLiteral
    // 会把 UTF-8 源码当 GBK 解释，中文常量永远匹配不上。
    const QString out = decodeConsoleText(p.readAllStandardOutput());
    const QString cnKey = QString::fromUtf8("自动配置逻辑");
    const QString cnOff = QString::fromUtf8("禁用");
    for (const QString &line : out.split(QLatin1Char('\n'))) {
        const bool isAutoconfigLine =
                line.contains(QStringLiteral("auto configuration logic"), Qt::CaseInsensitive)
                || line.contains(cnKey);
        if (!isAutoconfigLine)
            continue;
        return line.contains(QStringLiteral("disabled"), Qt::CaseInsensitive)
                || line.contains(cnOff);
    }
    return false;
}

// 从 netsh wlan show interfaces 里取接口名（英文 "Name" / 中文 "名称"）。
// QNetworkInterface::name() 在部分驱动上返回 "wireless_32768" 这类内部名，
// netsh 不认，必须用 netsh 自己显示的接口名。
static QString wlanInterfaceName()
{
    QProcess p;
    p.start("netsh", QStringList() << "wlan" << "show" << "interfaces");
    if (!p.waitForStarted(2000) || !p.waitForFinished(4000)) {
        p.kill();
        p.waitForFinished(500);
        return QString();
    }
    const QString out = decodeConsoleText(p.readAllStandardOutput());
    const QString cnName = QString::fromUtf8("名称");
    for (const QString &line : out.split(QLatin1Char('\n'))) {
        const int colon = line.indexOf(QLatin1Char(':'));
        if (colon < 0)
            continue;
        const QString key = line.left(colon).trimmed();
        const QString value = line.mid(colon + 1).trimmed();
        if (value.isEmpty())
            continue;
        if (key.compare(QStringLiteral("Name"), Qt::CaseInsensitive) == 0 || key == cnName)
            return value;
    }
    return QString();
}

// 读取本机所有已保存的 Wi-Fi 配置名
static QStringList wlanProfiles()
{
    QStringList result;
    QProcess p;
    p.start("netsh", QStringList() << "wlan" << "show" << "profiles");
    if (!p.waitForStarted(2000) || !p.waitForFinished(5000)) {
        p.kill();
        return result;
    }
    const QString out = decodeConsoleText(p.readAllStandardOutput());
    const QString cnKey = QString::fromUtf8("所有用户配置文件");
    for (const QString &line : out.split(QLatin1Char('\n'))) {
        const int colon = line.indexOf(QLatin1Char(':'));
        if (colon < 0)
            continue;
        const QString key = line.left(colon).trimmed();
        const QString value = line.mid(colon + 1).trimmed();
        if (value.isEmpty() || value == QStringLiteral("<None>"))
            continue;
        if (key.compare(QStringLiteral("All User Profile"), Qt::CaseInsensitive) == 0
            || key == cnKey)
            result << value;
    }
    return result;
}

// 读取某个配置的连接模式：auto / manual / 空
static QString wlanProfileMode(const QString &profile)
{
    QProcess p;
    p.start("netsh", QStringList() << "wlan" << "show" << "profile"
                                   << QStringLiteral("name=%1").arg(profile));
    if (!p.waitForStarted(2000) || !p.waitForFinished(5000)) {
        p.kill();
        return QString();
    }
    const QString out = decodeConsoleText(p.readAllStandardOutput());
    const QString cnKey = QString::fromUtf8("连接模式");
    for (const QString &line : out.split(QLatin1Char('\n'))) {
        if (!line.contains(QStringLiteral("Connection mode"), Qt::CaseInsensitive)
            && !line.contains(cnKey))
            continue;
        if (line.contains(QStringLiteral("automatic"), Qt::CaseInsensitive)
            || line.contains(QString::fromUtf8("自动")))
            return QStringLiteral("auto");
        return QStringLiteral("manual");
    }
    return QString();
}

// 固定热点策略：TS_F1_8825 自动连接，本机其它已保存 Wi-Fi 全部手动连接。
// 换一台电脑、多出未知热点也能自动收敛，不依赖人工列名单。
static void enforceWifiPolicy()
{
    const QString target = QStringLiteral("TS_F1_8825");
    const QStringList profiles = wlanProfiles();
    if (profiles.isEmpty()) {
        qWarning() << "未读取到已保存的 Wi-Fi 配置，跳过热点固定";
        return;
    }
    if (!profiles.contains(target)) {
        qWarning() << "未找到 Wi-Fi 配置" << target
                   << "：请先手动连接一次该热点并保存密码，再启动程序";
        return;
    }

    int changed = 0;
    for (const QString &profile : profiles) {
        const QString want = (profile == target) ? QStringLiteral("auto")
                                                 : QStringLiteral("manual");
        if (wlanProfileMode(profile) == want)
            continue;
        QProcess p;
        p.start("netsh", QStringList() << "wlan" << "set" << "profileparameter"
                                       << QStringLiteral("name=%1").arg(profile)
                                       << QStringLiteral("connectionmode=%1").arg(want));
        if (!p.waitForStarted(2000) || !p.waitForFinished(5000)) {
            p.kill();
            qWarning() << "设置 Wi-Fi 连接模式失败:" << profile << want;
            continue;
        }
        ++changed;
        qInfo() << "Wi-Fi 固定:" << profile << "->" << want;
    }
    qInfo() << "Wi-Fi 热点策略检查完成: 目标" << target << "自动，其它手动，本次修改"
            << changed << "个配置";
}

// 无线网卡省电策略：节能模式设为“最高性能”，避免空闲/息屏后网卡休眠导致
// Wi-Fi 链路假死。现场表现是上位机突然“网关连接意外断开”，要等网卡被唤醒
// 才恢复，日志里连接保持时长会断在很随机的位置。
// 对应 锁定WiFi.cmd 的第 3 步；powercfg 写的是当前电源方案，一次生效长期保留，
// 但换电源方案或系统重置后会还原，所以每次启动对齐一次（重复执行无副作用）。
// 修改电源方案通常需要管理员权限，失败只告警，不影响程序启动。
static void enforceWifiPowerPerformance()
{
    // 没有无线网卡的机器直接跳过，省掉三次进程开销
    bool hasWifi = false;
    for (const QNetworkInterface &iface : QNetworkInterface::allInterfaces()) {
        if (iface.type() == QNetworkInterface::Wifi) {
            hasWifi = true;
            break;
        }
    }
    if (!hasWifi) {
        qInfo() << "未检测到无线网卡，跳过网卡省电设置";
        return;
    }

    // 19cbb8fa... = 无线适配器设置，12bbebe6... = 节能模式，值 0 = 最高性能
    const QString subGroup = QStringLiteral("19cbb8fa-5279-450e-9fac-8a3d5fedd0c1");
    const QString setting  = QStringLiteral("12bbebe6-58d6-4636-95bb-3217ef867c1a");

    const QStringList keys = QStringList() << QStringLiteral("/setacvalueindex")
                                           << QStringLiteral("/setdcvalueindex");
    const QStringList names = QStringList() << QString::fromUtf8("交流")
                                            << QString::fromUtf8("电池");

    bool allOk = true;
    for (int i = 0; i < keys.size(); ++i) {
        QProcess p;
        p.start(QStringLiteral("powercfg"),
                QStringList() << keys.at(i) << QStringLiteral("SCHEME_CURRENT")
                              << subGroup << setting << QStringLiteral("0"));
        if (!p.waitForStarted(2000) || !p.waitForFinished(5000)) {
            p.kill();
            p.waitForFinished(500);
            qWarning() << "设置无线网卡电源模式失败(命令未执行)" << names.at(i);
            allOk = false;
            continue;
        }
        if (p.exitStatus() != QProcess::NormalExit || p.exitCode() != 0) {
            qWarning() << "设置无线网卡电源模式失败" << names.at(i)
                       << "退出码" << p.exitCode()
                       << decodeConsoleText(p.readAllStandardError()).simplified().left(200);
            allOk = false;
        }
    }

    if (!allOk) {
        qWarning() << "无线网卡仍可能空闲休眠；请以管理员身份运行一次 锁定WiFi.cmd";
        return;
    }

    // 让改动立即作用到当前生效的电源方案
    QProcess apply;
    apply.start(QStringLiteral("powercfg"),
                QStringList() << QStringLiteral("/setactive")
                              << QStringLiteral("SCHEME_CURRENT"));
    if (apply.waitForStarted(2000) && apply.waitForFinished(5000)) {
        qInfo() << "无线网卡电源模式已设为最高性能：空闲/息屏后不再自动休眠";
    } else {
        apply.kill();
        apply.waitForFinished(500);
        qWarning() << "电源方案激活失败，网卡设置可能延迟生效";
    }
}



// 兜底：Qt 报告的无线网卡名（个别机器与 netsh 的接口名不一致）
static QString qtWifiInterfaceName()
{
    for (const QNetworkInterface &iface : QNetworkInterface::allInterfaces()) {
        if (iface.type() == QNetworkInterface::Wifi
            && (iface.flags() & QNetworkInterface::IsUp)) {
            return iface.name();
        }
    }
    return QString();
}

// 启动时保持 WLAN 自动配置开启。
// 旧版为了防止漫游，用 set autoconfig enabled=no 锁死 Wi-Fi；
// 副作用是链路一旦掉线，Windows 连“手动重连”都会被拒绝（netsh 会报
// WLAN auto configuration is disabled），现场表现为 Wi-Fi 明明连着却永远连不上网关。
// 现在只在检测到被旧版禁用时自动恢复，正常启动不做任何改动。
static void lockWifiOnStartup()
{
    // 没有无线网卡的机器（纯有线上网）直接跳过
    bool hasWifi = false;
    for (const QNetworkInterface &iface : QNetworkInterface::allInterfaces()) {
        if (iface.type() == QNetworkInterface::Wifi) {
            hasWifi = true;
            break;
        }
    }
    if (!hasWifi) {
        qInfo() << "未检测到无线网卡，跳过 Wi-Fi 检查";
        return;
    }

    if (!wifiAutoconfigDisabled()) {
        qInfo() << "Wi-Fi 自动配置已开启：掉线后 Windows 可自动重连";
        return;
    }

    // 被旧版锁定过：启动时自动恢复，否则掉线后无法重连
    const QString ifaceName = wlanInterfaceName();
    QString name = ifaceName;
    if (name.isEmpty())
        name = qtWifiInterfaceName();

    const QString dir = QCoreApplication::applicationDirPath();
    const QString script = dir + QString::fromUtf8("/恢复WiFi.cmd");
    const bool hasScript = QFileInfo::exists(script) && !name.isEmpty();

    QProcess p;
    p.setProcessChannelMode(QProcess::MergedChannels);
    p.setWorkingDirectory(dir);
    if (hasScript) {
        qInfo() << "检测到 WLAN 自动配置被禁用，执行恢复脚本" << script << "接口" << name;
        QStringList args;
        args << "/c" << QString::fromUtf8("恢复WiFi.cmd") << "silent" << name;
        p.start("cmd.exe", args);
    } else {
        if (name.isEmpty()) {
            qWarning() << "未找到无线接口，无法恢复 WLAN 自动配置，请手动运行 恢复WiFi.cmd";
            return;
        }
        qInfo() << "检测到 WLAN 自动配置被禁用，执行 netsh 恢复" << name;
        p.start("netsh", QStringList() << "wlan" << "set" << "autoconfig" << "enabled=yes"
                                       << QStringLiteral("interface=%1").arg(name));
    }

    if (!p.waitForStarted(3000)) {
        qWarning() << "恢复 Wi-Fi 失败: 命令无法启动" << p.errorString();
        return;
    }
    if (!p.waitForFinished(10000)) {
        p.kill();
        p.waitForFinished(1000);
        qWarning() << "恢复 Wi-Fi 超时（可能正在等待管理员授权）";
    }

    const QString out = decodeConsoleText(p.readAll()).simplified();
    if (!out.isEmpty())
        qInfo() << "恢复 Wi-Fi 输出:" << out.left(300);

    if (!wifiAutoconfigDisabled())
        qInfo() << "Wi-Fi 自动配置已恢复：掉线后会自动重连";
    else
        qWarning() << "Wi-Fi 自动配置恢复失败：请以管理员身份运行 恢复WiFi.cmd";
}
#endif

int main(int argc, char *argv[])
{
    // 现场排查断连需要逐帧/解析细节，默认打开 locke.*.debug；
    // qt.modbus.lowlevel 未使用，关掉避免无关刷屏。
    QLoggingCategory::setFilterRules("qt.modbus.lowlevel=false\n"
                                     "locke.*.debug=true");

#ifdef Q_OS_WIN
    // 控制台按 UTF-8 显示，与日志文件的编码彻底统一，中文不再一半 GBK 一半 UTF-8
    const bool consoleUtf8Ok = SetConsoleOutputCP(CP_UTF8) != 0;
    SetConsoleCP(CP_UTF8);
#else
    const bool consoleUtf8Ok = true;
#endif

    QApplication a(argc, argv);

    // 初始化文件日志（必须在 QApplication 之后，applicationDirPath 才有效）
    initFileLogger();

    g_sessionId = QDateTime::currentDateTime().toString("yyyyMMdd-hhmmss-zzz")
                  + QString("-P%1").arg(QCoreApplication::applicationPid());

    qInfo() << "==================== Locke 启动 会话" << g_sessionId << "====================";
    qInfo() << "Qt" << qVersion() << "系统" << QSysInfo::prettyProductName()
            << "架构" << QSysInfo::currentCpuArchitecture();
    qInfo() << "程序目录" << QCoreApplication::applicationDirPath()
            << "工作目录" << QDir::currentPath()
            << "日志文件" << g_logFile.fileName() << "(UTF-8)";
    qInfo() << "命令行" << a.arguments().join(QLatin1Char(' '));
    // 非 ASCII 一律 fromUtf8：本工程 Qt5.12+MSVC 下 QStringLiteral("中文") 会被当 GBK
    qInfo() << "控制台 UTF-8"
            << (consoleUtf8Ok ? QString::fromUtf8("已启用")
                              : QString::fromUtf8("设置失败(日志文件不受影响)"))
            << "调试日志 locke.*.debug=on";

#ifdef Q_OS_WIN
    // 启动时检查 WLAN 自动配置，并按策略固定热点：
    // TS_F1_8825 自动连接，其它已保存 Wi-Fi 一律手动，避免运行中自动切换网络；
    // 再把网卡省电模式设为最高性能，防止空闲/息屏后网卡休眠导致链路假死
    lockWifiOnStartup();
    enforceWifiPolicy();
    enforceWifiPowerPerformance();
#endif

    // 把窗口放进作用域：先让 MainWindow 析构（内部销毁控制器、发 0x80、优雅关 socket），
    // 再打印"退出"横幅，否则日志会先出现"退出"，后面才出现控制器析构，看起来像没退干净。
    int exitCode = 0;
    {
        MainWindow w;
        w.show();
        exitCode = a.exec();
    }
    qInfo() << "==================== Locke 退出, 会话" << g_sessionId
            << "返回码" << exitCode << "====================";
    return exitCode;
}
