#ifndef LOCKE_LOG_H
#define LOCKE_LOG_H

#include <QLoggingCategory>
#include <QAbstractSocket>
#include <QByteArray>
#include <QString>

// 日志分类，可用环境变量 QT_LOGGING_RULES 单独开关，例如：
//   QT_LOGGING_RULES="locke.bus.debug=true"   打开总线逐帧调试日志
//   QT_LOGGING_RULES="locke.pos.info=false"   关闭位置类信息日志
//
// 输出格式（控制台与日志文件完全一致，均为 UTF-8）：
//   [yyyy-MM-dd hh:mm:ss.zzz][LEVEL][PID n][category] 内容 (file:line)
// 每行带 PID，便于在同一份按日追加的日志里区分多次运行。
Q_DECLARE_LOGGING_CATEGORY(lcBus)    // 网关通讯：连接、发帧、收帧、超时、统计
Q_DECLARE_LOGGING_CATEGORY(lcPos)    // 位置回包：跳变过滤、异常值
Q_DECLARE_LOGGING_CATEGORY(lcScan)   // 扫查流程：路径、到位、重发、超时
Q_DECLARE_LOGGING_CATEGORY(lcUi)     // 操作员动作：按钮、勾选

namespace lockeLog {

// 把 socket 状态/错误转成固定中文名，保证各控制器日志口径一致。
// 非 ASCII 字面量统一走 fromUtf8：Qt5.12+MSVC 下 QStringLiteral("中文") 会被按 GBK 解析。
inline QString socketStateName(QAbstractSocket::SocketState s)
{
    switch (s) {
    case QAbstractSocket::UnconnectedState: return QString::fromUtf8("未连接");
    case QAbstractSocket::HostLookupState:  return QString::fromUtf8("域名解析中");
    case QAbstractSocket::ConnectingState:  return QString::fromUtf8("连接中");
    case QAbstractSocket::ConnectedState:   return QString::fromUtf8("已连接");
    case QAbstractSocket::BoundState:       return QString::fromUtf8("已绑定");
    case QAbstractSocket::ClosingState:     return QString::fromUtf8("关闭中");
    case QAbstractSocket::ListeningState:   return QString::fromUtf8("监听中");
    }
    return QString::fromUtf8("未知状态(%1)").arg(static_cast<int>(s));
}

inline QString socketErrorName(QAbstractSocket::SocketError e)
{
    switch (e) {
    case QAbstractSocket::ConnectionRefusedError:         return QString::fromUtf8("连接被拒绝");
    case QAbstractSocket::RemoteHostClosedError:          return QString::fromUtf8("对端关闭连接");
    case QAbstractSocket::HostNotFoundError:              return QString::fromUtf8("找不到主机");
    case QAbstractSocket::SocketAccessError:              return QString::fromUtf8("套接字权限不足");
    case QAbstractSocket::SocketResourceError:            return QString::fromUtf8("系统资源不足");
    case QAbstractSocket::SocketTimeoutError:             return QString::fromUtf8("套接字超时");
    case QAbstractSocket::DatagramTooLargeError:          return QString::fromUtf8("数据包过大");
    case QAbstractSocket::OperationError:                 return QString::fromUtf8("状态不允许该操作");
    case QAbstractSocket::NetworkError:                   return QString::fromUtf8("网络故障(网卡/链路)");
    case QAbstractSocket::AddressInUseError:              return QString::fromUtf8("地址已被占用");
    case QAbstractSocket::SocketAddressNotAvailableError: return QString::fromUtf8("地址不可用");
    case QAbstractSocket::UnsupportedSocketOperationError:return QString::fromUtf8("不支持的套接字操作");
    case QAbstractSocket::ProxyAuthenticationRequiredError:return QString::fromUtf8("代理认证失败");
    case QAbstractSocket::SslHandshakeFailedError:        return QString::fromUtf8("SSL 握手失败");
    case QAbstractSocket::SslInternalError:               return QString::fromUtf8("SSL 内部错误");
    case QAbstractSocket::SslInvalidUserDataError:        return QString::fromUtf8("SSL 数据无效");
    case QAbstractSocket::UnfinishedSocketOperationError: return QString::fromUtf8("上一操作未完成");
    case QAbstractSocket::ProxyConnectionRefusedError:    return QString::fromUtf8("代理拒绝连接");
    case QAbstractSocket::ProxyConnectionClosedError:     return QString::fromUtf8("代理关闭连接");
    case QAbstractSocket::ProxyConnectionTimeoutError:    return QString::fromUtf8("代理连接超时");
    case QAbstractSocket::ProxyNotFoundError:             return QString::fromUtf8("找不到代理");
    case QAbstractSocket::ProxyProtocolError:             return QString::fromUtf8("代理协议错误");
    case QAbstractSocket::TemporaryError:                 return QString::fromUtf8("临时错误(可重试)");
    case QAbstractSocket::UnknownSocketError:             return QString::fromUtf8("未知错误");
    }
    return QString::fromUtf8("其他错误(%1)").arg(static_cast<int>(e));
}

} // namespace lockeLog

#endif // LOCKE_LOG_H
