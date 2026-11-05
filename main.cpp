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

#include "locke_log.h"

Q_LOGGING_CATEGORY(lcBus,  "locke.bus")
Q_LOGGING_CATEGORY(lcPos,  "locke.pos")
Q_LOGGING_CATEGORY(lcScan, "locke.scan")
Q_LOGGING_CATEGORY(lcUi,   "locke.ui")

// 日志保留天数，超过的自动删除
static const int kLogKeepDays = 30;


// 全局日志文件与互斥锁
static QFile g_logFile;
static QMutex g_logMutex;

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

    const QString line = QString("[%1][%2]%3 %4%5")
                             .arg(timestamp)
                             .arg(level)
                             .arg(category)
                             .arg(msg)
                             .arg(where);

    // 输出到控制台
    fprintf(stderr, "%s\n", line.toLocal8Bit().constData());
    fflush(stderr);

    // 输出到日志文件
    if (g_logFile.isOpen()) {
        QTextStream out(&g_logFile);
        out.setCodec("UTF-8");
        out << line << '\n';
        out.flush();
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
    }
}


int main(int argc, char *argv[])
{
    // 默认关闭逐帧调试日志，只保留 info 及以上；需要时用 QT_LOGGING_RULES 打开
    QLoggingCategory::setFilterRules("qt.modbus.lowlevel=false\n"
                                     "locke.*.debug=false");

    QApplication a(argc, argv);

    // 初始化文件日志（必须在 QApplication 之后，applicationDirPath 才有效）
    initFileLogger();
    qInfo() << "==================== Locke 启动 ====================";
    qInfo() << "Qt" << qVersion() << "程序目录" << QCoreApplication::applicationDirPath()
            << "日志文件" << g_logFile.fileName();

    MainWindow w;
    w.show();

    const int exitCode = a.exec();
    qInfo() << "==================== Locke 退出, 返回码" << exitCode << "====================";
    return exitCode;
}
