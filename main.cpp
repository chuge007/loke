#include "mainwindow.h"
#include <QApplication>
#include <QLoggingCategory>
#include <QFile>
#include <QTextStream>
#include <QDateTime>
#include <QDir>
#include <QMutex>
#include <QCoreApplication>


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

    const QString line = QString("[%1][%2] %3")
                             .arg(timestamp)
                             .arg(level)
                             .arg(msg);

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

    g_logFile.setFileName(logDir.filePath(fileName));
    g_logFile.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text);

    qInstallMessageHandler(fileMessageHandler);
}


int main(int argc, char *argv[])
{
    QLoggingCategory::setFilterRules("qt.modbus.lowlevel=false");

    QApplication a(argc, argv);

    // 初始化文件日志（必须在 QApplication 之后，applicationDirPath 才有效）
    initFileLogger();
    qInfo() << "==================== Locke 启动 ====================";

    MainWindow w;
    w.show();

    return a.exec();
}
