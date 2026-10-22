#ifndef LOCKE_LOG_H
#define LOCKE_LOG_H

#include <QLoggingCategory>

// 日志分类，可用环境变量 QT_LOGGING_RULES 单独开关，例如：
//   QT_LOGGING_RULES="locke.bus.debug=true"   打开总线逐帧调试日志
//   QT_LOGGING_RULES="locke.pos.info=false"   关闭位置类信息日志
Q_DECLARE_LOGGING_CATEGORY(lcBus)    // 网关通讯：连接、发帧、超时、统计
Q_DECLARE_LOGGING_CATEGORY(lcPos)    // 位置回包：跳变过滤、异常值
Q_DECLARE_LOGGING_CATEGORY(lcScan)   // 扫查流程：路径、到位、重发、超时
Q_DECLARE_LOGGING_CATEGORY(lcUi)     // 操作员动作：按钮、勾选

#endif // LOCKE_LOG_H
