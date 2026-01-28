#include "ScanControlHuiChuan.h"

#include <QDebug>
#include <qwidget.h>
#include <QDebug>
#include <qwidget.h>
#include <QMessageBox>
#include <QTimer>
#include <QProgressDialog>
#include <QCoreApplication>

ScanControlHuiChuan::ScanControlHuiChuan(QObject *parent) :
    ScanControlAbstract(parent)
{
    qRegisterMetaType<QModbusDevice::State>("QModbusDevice::State");
    QMetaObject::invokeMethod(this, "init", Qt::QueuedConnection);
}

ScanControlHuiChuan::~ScanControlHuiChuan()
{
    QMetaObject::invokeMethod(this, "destroy", Qt::QueuedConnection);
}

bool ScanControlHuiChuan::isXCrossed()
{
    bool state = false;
    float xTemp = static_cast<float>(zeroPoint.x())  + xScanLenght;
    if(static_cast<qreal>(xTemp) > limitPoint.x()) {
        state = true;
        qDebug() << "scan X-axis crossed";
    }

    return state;
}

bool ScanControlHuiChuan::isYCrossed()
{
    bool state = false;
    float yTemp = static_cast<float>(zeroPoint.y())  + yScanLenght;
    if(static_cast<qreal>(yTemp) > limitPoint.y()) {
        state = true;
        qDebug() << "scan Y-axis crossed";
    }

    return state;
}

bool ScanControlHuiChuan::isJogCrossed(int &address, float &data)
{
    bool state = true;

    switch (axisInch) {
        case AxisJog::NotAxisJog: state = false; break;
        case AxisJog::XJogAdd: {
            address = R_REGISTER_BASE+X_TARTPOS;
            data = static_cast<float>(currentPos.x()) + jopStep;
            if(static_cast<qreal>(data) >= limitPoint.x()) state = false;
        }break;
        case AxisJog::XJogSub: {
            address = R_REGISTER_BASE+X_TARTPOS;
            data = static_cast<float>(currentPos.x()) - jopStep;
            if(static_cast<qreal>(data) < 0) state = false;
        }break;
        case AxisJog::YJogAdd: {
            address = R_REGISTER_BASE+Y_TARTPOS;
            data = static_cast<float>(currentPos.y()) + jopStep;
            if(static_cast<qreal>(data) >= limitPoint.y()) state = false;
        }break;
        case AxisJog::YJogSub: {
            address = R_REGISTER_BASE+Y_TARTPOS;
            data = static_cast<float>(currentPos.y()) - jopStep;
            if(static_cast<qreal>(data) < 0) state = false;
        }break;
    }

    //没有极限点不做先为限位
    if(!isHaveMachine || !isHaveLimit) state = true;

    return state;
}

void ScanControlHuiChuan::setManualModel(bool states)
{
    manState[0] = states;
}

bool ScanControlHuiChuan::nextScan()
{
    if(!tasks.isEmpty()){
        manState[1] = true;
    }else {
        manState[1] = false;
    }
    return manState[1];
}

void ScanControlHuiChuan::initWidget()
{
//    QDoubleValidator *doubleValidator = new QDoubleValidator(this);
//    doubleValidator->setRange(0.0, 500.000);
//    doubleValidator->setDecimals(3);

//    ui->x_lenght->setValidator(doubleValidator);
//    ui->y_lenght->setValidator(doubleValidator);
//    ui->y_step->setValidator(doubleValidator);
    modbusClient = new QModbusTcpClient(this);

    settings = new QSettings("./scan_setting.ini", QSettings::IniFormat);

    timer = new QTimer(this);
    timer->setInterval(50);


    posTimer.start(200); // 100ms 读一次位置
    connect(&posTimer, &QTimer::timeout, this, &ScanControlHuiChuan::updataCurrentPos);


}

void ScanControlHuiChuan::connectFun()
{
    connect(modbusClient, &QModbusClient::stateChanged, this, &ScanControlHuiChuan::modbusState);
    connect(timer, &QTimer::timeout, this, &ScanControlHuiChuan::performTasks);

//    connect(this, &ScanControlHuiChuan::machineStart, this, &ScanControlHuiChuan::on_startScanBtn_clicked);
//    connect(this, &ScanControlHuiChuan::machineStop, this, &ScanControlHuiChuan::on_stopScanBtn_clicked);
}

void ScanControlHuiChuan::readSetting()
{
    if(settings == nullptr) return;

    float xPos   = settings->value("Virtual_origin_X", 0).toFloat();
    float yPos   = settings->value("Virtual_origin_Y", 0).toFloat();
    float xLimit = settings->value("Limit_position_X", 0).toFloat();
    float yLimit = settings->value("Limit_position_Y", 0).toFloat();
    xScanLenght = settings->value("X_Lenght", 0).toFloat();
    yScanLenght = settings->value("Y_Lenght", 0).toFloat();
    yScanStep = settings->value  ("Y_Step", 0).toFloat();
    isHaveMachine = settings->value("Have_Machine_origin", false).toBool();
    isHaveLimit = settings->value("Have_Limit_origin", false).toBool();
    bool temp = settings->value("Single_Scan", false).toBool();
    if(xPos < 0 || xPos > xLimit) xPos = 0;
    if(yPos < 0 || yPos > yLimit) yPos = 0;
//    if(isHaveMachine) ui->setMachineBtn->setEnabled(false);
//    if(isHaveLimit) ui->setLimitBtn->setEnabled(false);
    if(!temp) {
        scanModel = ScanModel::NormalScan;
    }else {
        scanModel = ScanModel::SingleScan;
    }
    qDebug() << xLimit << yLimit << xScanLenght <<
                yScanLenght <<
                yScanStep;

    zeroPoint.setX(static_cast<qreal>(xPos));
    zeroPoint.setY(static_cast<qreal>(yPos));
    limitPoint.setX(static_cast<qreal>(xLimit));
    limitPoint.setY(static_cast<qreal>(yLimit));

//    ui->x_lenght->setText(QString::number(xLenght));
//    ui->y_lenght->setText(QString::number(yLenght));
//    ui->y_step->setText(QString::number(yStep));
//    ui->singleScan->setChecked(temp);
}

void ScanControlHuiChuan::writeSetting()
{
    if(settings == nullptr) return;

    settings->setValue("Virtual_origin_X", zeroPoint.x());
    settings->setValue("Virtual_origin_Y", zeroPoint.y());
    settings->setValue("Limit_position_X", limitPoint.x());
    settings->setValue("Limit_position_Y", limitPoint.y());

    settings->setValue("X_Lenght", static_cast<qreal>(xScanLenght));
    settings->setValue("Y_Lenght", static_cast<qreal>(yScanLenght));
    settings->setValue("Y_Step", static_cast<qreal>(yScanStep));
    settings->setValue("Single_Scan", scanModel == ScanModel::NormalScan ? false : true);

    settings->sync();

}

void ScanControlHuiChuan::initStates()
{
    isInit = true;
    isStartScan = false;
    isPerform = false;
    isStopScan = false;
    isKeepScan = false;
    isAxisStop = false;
    updateCurPos = false;
    isEndScan = false;
    isJogDone = false;
    if(!tasks.isEmpty())tasks.clear();
}

//float ScanControlHuiChuan::readModbusFloatData(int v1, int v2)
//{
//    uint32_t intValue = (static_cast<uint32_t>(v1) << 16) | static_cast<uint32_t>(v2);
//    return *reinterpret_cast<float*>(&intValue);
//}

float ScanControlHuiChuan::readModbusFloatData(quint16 lowWord, quint16 highWord)
{
    quint32 u32 =
        (static_cast<quint32>(highWord) << 16) |
         static_cast<quint32>(lowWord);

    float f;
    memcpy(&f, &u32, sizeof(float));
    return f;
}

//QPair<quint16, quint16> ScanControlHuiChuan::writeModbusFloatData(float value) {
//    quint32 intValue = *reinterpret_cast<uint32_t*>(&value);
//    quint16 v2 = (intValue >> 16) & 0xFFFF;
//    quint16 v1 = intValue & 0xFFFF;

//    return QPair<quint16, quint16>(v1, v2);
//}

QPair<quint16, quint16>
ScanControlHuiChuan::writeModbusFloatData(float value)
{
    quint32 u32;
    memcpy(&u32, &value, sizeof(float));

    quint16 lowWord  =  u32 & 0xFFFF;        // 低 16 位
    quint16 highWord = (u32 >> 16) & 0xFFFF; // 高 16 位

    // 返回顺序：低位在前，高位在后
    return { lowWord, highWord };
}


void ScanControlHuiChuan::writeHoldingRegistersData(int address, quint16 size, float data)
{
    if(modbusClient->state() != QModbusDevice::ConnectedState)
        return;

    QModbusDataUnit modbusData(QModbusDataUnit::HoldingRegisters, address, size);

    auto d = writeModbusFloatData(data);
    modbusData.setValue(0, d.first);
    modbusData.setValue(1, d.second);

    auto reply = modbusClient->sendWriteRequest(modbusData, 0);
    if(reply){
        if (!reply->isFinished())
        {
            connect(reply, &QModbusReply::finished, [=](){
                if(reply->error() == QModbusDevice::NoError){
                    //判断如果是开始扫查触发，并开始执行任务
                    if(isStartScan){
                        isPerform = true;
                        if(manState[0]) manState[1] = false;
                    }
                    if(axisInch != AxisJog::NotAxisJog){
                        isJogDone = true;
                    }
                }else{
//                    isPerform = false;
//                    if(!isEndScan)  //如果是结束扫查，写失败就不处理
//                    writeHoldingRegistersData(address, size, data);

                }
                reply->deleteLater();
          });
        }else {
            reply->deleteLater();
        }
    }
}

void ScanControlHuiChuan::readAxisRunStatus(int address, quint16 size)
{
    if(modbusClient->state() != QModbusDevice::ConnectedState)
        return;

    QModbusDataUnit data(QModbusDataUnit::Coils, address, size);

    QModbusReply *reply = modbusClient->sendReadRequest(data, 1);
    if(reply){
        if (!reply->isFinished())
        {
            connect(reply, &QModbusReply::finished, [=](){
                if(reply->error() == QModbusDevice::NoError){
                    if(tasks.isEmpty()){
                        isPerform = false;
                        reply->deleteLater();
                        return ;
                    }

                    //正在执行自动扫查
                    if(isStartScan){
                        int temp = false;
                        if(tasks.head().first == "x"){
                            temp = reply->result().value(0);
                            if(temp) emit scanRowEnd(AxisState::XMoveDone);
                        }else if (tasks.head().first == "y") {
                            temp = reply->result().value(1);
                            if(temp) emit scanRowEnd(AxisState::YMoveDone);
                        }

                        if(temp){
                            isPerform = false;
                            if(!tasks.isEmpty()) tasks.pop_front();
                        }
                        if(tasks.count() != 0){
                            updataCurrentPos();
                        }else {
                            keepTime = 0;
                            emit scanTime("");
                            updateCurPos = true;
                        }
                    }
                }else {
//                    if(!isEndScan)  //如果是结束扫查，写失败就不处理
//                    readAxisRunStatus(address, size);
                }
                reply->deleteLater();
            });
        }else {
            delete reply;
        }
    }
}

void ScanControlHuiChuan::writeAxisStopStatus(int address)
{
    if(modbusClient->state() != QModbusDevice::ConnectedState)
        return;

    QModbusDataUnit data(QModbusDataUnit::Coils, address, 2);
    data.setValue(0, 1);
    data.setValue(1, 1);
    QModbusReply *reply = modbusClient->sendWriteRequest(data, 1);
    if(reply){
        if (!reply->isFinished())
        {
            connect(reply, &QModbusReply::finished, [=](){
                if(reply->error() == QModbusDevice::NoError){
                    if(isEndScan){
                        return ;    //结束扫查，不需要置位
                    }

                    if(isRunTarget){
                        isRunTarget = false;
                        return;
                    }

                    if(address == X_STOP){
                        isStopScan = false;
                        isAxisStop = true;
                        updateCurPos = true;
                    }else if (address == X_START) {
                        isKeepScan = false;
                        isStartScan = true;
                        isAxisStop = false;
                    }
                }else {
                    writeAxisStopStatus(address);
                }
                reply->deleteLater();
            });
        }else {
            reply->deleteLater();
        }
    }
}

void ScanControlHuiChuan::writeEndScanStatus()
{
    if(modbusClient->state() != QModbusDevice::ConnectedState)
        return;

    QModbusDataUnit modbusData(QModbusDataUnit::Coils,  END_SCAN, 1);

    modbusData.setValue(0, 1);

    auto reply = modbusClient->sendWriteRequest(modbusData, 1);
    if(reply){
        if (!reply->isFinished())
        {
            connect(reply, &QModbusReply::finished, [=](){
                if(reply->error() == QModbusDevice::NoError){
                    updateCurPos = true;
                    timer->start();
                }else {
                    writeEndScanStatus();
                }
                reply->deleteLater();
            });
        }else {
            reply->deleteLater();
        }
    }
}

void ScanControlHuiChuan::writeBackZero()
{
    if(modbusClient->state() != QModbusDevice::ConnectedState)
        return;

    QModbusDataUnit modbusData(QModbusDataUnit::HoldingRegisters,  X_VIRTUAL_ORIGIN, 4);

    auto xpos = writeModbusFloatData(static_cast<float>(zeroPoint.x()));
    auto ypos = writeModbusFloatData(static_cast<float>(zeroPoint.y()));
    modbusData.setValue(0, xpos.first);
    modbusData.setValue(1, xpos.second);
    modbusData.setValue(2, ypos.first);
    modbusData.setValue(3, ypos.second);

    auto reply = modbusClient->sendWriteRequest(modbusData, 1);
    if(reply){
        if (!reply->isFinished())
        {
            connect(reply, &QModbusReply::finished, [=](){
                if(reply->error() == QModbusDevice::NoError){
        //            writeAxisStopStatus(X_START);
                    writeEndScanStatus();

                }else {
                    writeBackZero();
                }
                reply->deleteLater();
            });
        }else {
            reply->deleteLater();
        }
    }
}

void ScanControlHuiChuan::on_start(){


      writeAxisJog(0x0000,true);

}

void ScanControlHuiChuan::on_stop(){

    writeAxisJog(0x0001,true);


}

void ScanControlHuiChuan::on_end(){


    writeAxisJog(0x0002,true);

}

void ScanControlHuiChuan::on_alarmReset(){

    writeAxisJog(0x0004,true);
}

void ScanControlHuiChuan::on_backZero(){

     writeAxisJog(0x0003,true);


}

bool ScanControlHuiChuan::sendPulseCommand(QModbusClient *modbusClient, QModbusDataUnit::RegisterType rGtype,
                                                  quint16 address)
{

    updateCurPos=true;

    if (modbusClient->state() != QModbusDevice::ConnectedState) {
        qWarning() << "Modbus client not connected.";
        return false;
    }

    modbusClient->setTimeout(5000);
    modbusClient->setNumberOfRetries(3);

    auto sendValue = [&](int value) -> bool {
        QModbusDataUnit command(rGtype, static_cast<quint16>(address), 1);
        QModbusReply* reply;
        command.setValue(0, value);

        {
            QMutexLocker locker(&mutex);

            reply = modbusClient->sendWriteRequest(command, 1);
            if (!reply) {
                qWarning() << "Failed to send Modbus write request (nullptr)";
                return false;  // ❌ 不要递归调用
            }
        }
        QTimer timer;
        QEventLoop loop;
        QObject::connect(reply, &QModbusReply::finished, &loop, &QEventLoop::quit);
        QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
        timer.start(2000);
        loop.exec();  // 等待 finished 信号

        if (reply->error() != QModbusDevice::NoError) {
            qWarning() << "[sendPulseCommand] Modbus write error:" << reply->errorString()
                       << "(value:" << value << " address:" << address << ")";
            reply->deleteLater();
            return false;
        }

        qDebug() << "sendPulseCommand success, value:" << value << " to address:" << address;
        reply->deleteLater();
        return true;
    };

    // Step 1: 先写入 1
    if (!sendValue(1))
        return false;

    // Step 2: 延时 100ms 后写入 0（使用 QTimer 延迟，不阻塞主线程事件循环）
    QEventLoop delayLoop;
    QTimer::singleShot(450, &delayLoop, &QEventLoop::quit);
    delayLoop.exec();

    updateCurPos=false;
    return sendValue(0);
}

void ScanControlHuiChuan::readAxisEndState()
{
    if(modbusClient->state() != QModbusDevice::ConnectedState)
        return;

    QModbusDataUnit data(QModbusDataUnit::Coils, X_AXIS_DONE, 2);

    QModbusReply *reply = modbusClient->sendReadRequest(data, 1);
    if(reply){
        if (!reply->isFinished())
        {
            connect(reply, &QModbusReply::finished, [=](){

                if(reply->error() == QModbusDevice::NoError){
                    if(isEndScan){

                        if(reply->result().value(0) == 1 &&
                           reply->result().value(1) == 1){
                            isEndScan = false;
                        }
                    }

                    //正在执行目标点位
                    if(isRunTarget){
                        int temp1 = reply->result().value(0);
                        int temp2 = reply->result().value(1);
                        if(temp1 && temp2){
                            isRunTarget = false;
                        }
                    }
                }else {
                }
                reply->deleteLater();
            });
        }else {
            reply->deleteLater();
        }
    }
}

void ScanControlHuiChuan::writeAxisVelocity(int address, quint16 size, float data)
{
    if(modbusClient->state() != QModbusDevice::ConnectedState)
        return;

    QModbusDataUnit modbusData(QModbusDataUnit::HoldingRegisters,  address, size);

    auto xpos = writeModbusFloatData(data);
    modbusData.setValue(0, xpos.first);
    modbusData.setValue(1, xpos.second);

    auto reply = modbusClient->sendWriteRequest(modbusData, 0);
    if(reply){
        if (!reply->isFinished())
        {
            connect(reply, &QModbusReply::finished, [=](){
                if(reply->error() == QModbusDevice::NoError){
                }else {
                    writeAxisVelocity(address, size, data);
                }
                reply->deleteLater();
            });
        }else {
            reply->deleteLater();
        }
    }
}



void ScanControlHuiChuan::readAxisVelocity(int address, quint16 size)
{
    if(modbusClient->state() != QModbusDevice::ConnectedState)
        return;

    QModbusDataUnit modbusData(QModbusDataUnit::HoldingRegisters,  address, size);

    auto reply = modbusClient->sendReadRequest(modbusData, 0);
    if(reply){
        if (!reply->isFinished())
        {
            connect(reply, &QModbusReply::finished, [=](){
                if(reply->error() == QModbusDevice::NoError){
                    float xVel = readModbusFloatData(reply->result().value(1), reply->result().value(0));
                    float yVel = readModbusFloatData(reply->result().value(3), reply->result().value(2));
                    float jogVel = readModbusFloatData(reply->result().value(9), reply->result().value(8));

        //            ui->x_velocity->setText(QString::number(xVel));
        //            ui->y_velocity->setText(QString::number(yVel));
                    emit velocityChange(xVel, yVel, jogVel);
                    isInit = false;
                    updateCurPos = true;
                }else {
                    readAxisVelocity(address, size);
                }
                reply->deleteLater();
            });
        }else {
            reply->deleteLater();
        }
    }
}

void ScanControlHuiChuan::readAxisJogStatus(int address)
{
    if(modbusClient->state() != QModbusDevice::ConnectedState)
        return;

    QModbusDataUnit modbusData(QModbusDataUnit::Coils,  address, 1);

    auto reply = modbusClient->sendReadRequest(modbusData, 1);
    if(reply){
        if (!reply->isFinished())
        {
            connect(reply, &QModbusReply::finished, [=](){
                if(reply->error() == QModbusDevice::NoError){
                    int temp = reply->result().value(0);
                    if(temp == 1){
                        axisInch = AxisJog::NotAxisJog;
                        isJogDone = false;
                        updateCurPos = true;
                    }
                    updataCurrentPos();
                }else {
            //            readAxisVelocity(address, size);
                }
                reply->deleteLater();
            });
        }else {
            reply->deleteLater();
        }
    }
}

void ScanControlHuiChuan::readAxisErrorID()
{
    if(modbusClient->state() != QModbusDevice::ConnectedState)
        return;

    QModbusDataUnit modbusData(QModbusDataUnit::HoldingRegisters,  AXIS_ERROR_ID, 4);

    auto reply = modbusClient->sendReadRequest(modbusData, 1);
    if(reply){
        if (!reply->isFinished())
        {
            connect(reply, &QModbusReply::finished, [=](){
                if(reply->error() == QModbusDevice::NoError){
                    int xErrorId = reply->result().value(0);
                    int yErrorId = reply->result().value(1);
                    //顺便读取机械按键的是否按下
                    int m_start = reply->result().value(2);
                    int m_stop = reply->result().value(3);

                    if(xErrorId || yErrorId)
                    emit axisError(xErrorId, yErrorId);

                    if(m_start == 1 && m_stop == 0 ){
                        emit machineStart();
                    }else if (m_start == 0 && m_stop == 1 ) {
                        emit machineStop();
                    }
                }
                reply->deleteLater();
            });
        }else {
            reply->deleteLater();
        }
    }
}

void ScanControlHuiChuan::writeAxisReset()
{
    if(modbusClient->state() != QModbusDevice::ConnectedState)
        return;

    QModbusDataUnit modbusData(QModbusDataUnit::Coils,  AXIS_RESET, 1);
    modbusData.setValue(0, 1);
    auto reply = modbusClient->sendWriteRequest(modbusData, 1);
    if(reply){
        if (!reply->isFinished())
        {
            connect(reply, &QModbusReply::finished, [=](){
                if(reply->error() != QModbusDevice::NoError){
                    writeAxisReset();
                }
                reply->deleteLater();
            });
        }else {
            reply->deleteLater();
        }
    }
}

void ScanControlHuiChuan::writeAxisLimitPosition()
{
    if(modbusClient->state() != QModbusDevice::ConnectedState)
        return;

    QModbusDataUnit modbusData(QModbusDataUnit::Coils,  LIMIT_POINT, 1);
    modbusData.setValue(0, 1);
    auto reply = modbusClient->sendWriteRequest(modbusData, 1);
    if(reply){
        if (!reply->isFinished())
        {
            connect(reply, &QModbusReply::finished, [=](){
                if(reply->error() == QModbusDevice::NoError){
                    limitPoint.setX(currentPos.x());
                    limitPoint.setY(currentPos.y());

                    settings->setValue("Limit_position_X", limitPoint.x());
                    settings->setValue("Limit_position_Y", limitPoint.y());
                    settings->setValue("Have_Limit_origin", true);
                    isHaveLimit = true;
                }else {
                    writeAxisLimitPosition();
                }
                reply->deleteLater();
            });
        }else {
            reply->deleteLater();
        }
    }
}

void ScanControlHuiChuan::writeAxisMachineOrigin()
{
    if(modbusClient->state() != QModbusDevice::ConnectedState)
        return;

    QModbusDataUnit modbusData(QModbusDataUnit::Coils,  MACHINE_ORIGIN, 1);
    modbusData.setValue(0, 1);
    auto reply = modbusClient->sendWriteRequest(modbusData, 1);
    if(reply){
        if (!reply->isFinished())
        {
            connect(reply, &QModbusReply::finished, [=](){
                if(reply->error() == QModbusDevice::NoError){
                    zeroPoint.setX(0);
                    zeroPoint.setY(0);

                    settings->setValue("Virtual_origin_X", limitPoint.x());
                    settings->setValue("Virtual_origin_Y", limitPoint.y());
                    settings->setValue("Have_Machine_origin", true);
                    isHaveMachine = true;
                }else {
                    writeAxisLimitPosition();
                }
                reply->deleteLater();
            });
        }else {
            reply->deleteLater();
        }
    }
}

void ScanControlHuiChuan::writeAxisJog(int address, bool data)
{
    if(modbusClient->state() != QModbusDevice::ConnectedState)
        return;

    QModbusDataUnit modbusData(QModbusDataUnit::Coils,  address, 1);
    modbusData.setValue(0, data);
    QModbusReply* reply;
    {
        QMutexLocker locker(&mutex);
        reply = modbusClient->sendWriteRequest(modbusData, 0);
        if (!reply) {
            return ;
        }
    }
    qDebug()<<"ScanControlHuiChuan******address:"<<address<<"data:"<<data;

    QTimer timer;
    QEventLoop loop;
    QObject::connect(reply, &QModbusReply::finished, &loop, &QEventLoop::quit);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    timer.start(2000);
    loop.exec();  // 等待 finished 信号

    bool success = (reply->error() == QModbusDevice::NoError);
    reply->deleteLater();

    axisInch = AxisJog::NotAxisJog;
}


void ScanControlHuiChuan::writeTargetPosition(double x, double y)
{
    if(modbusClient->state() != QModbusDevice::ConnectedState)
        return;

    QModbusDataUnit modbusData(QModbusDataUnit::HoldingRegisters, R_REGISTER_BASE + X_TARTPOS, 4);

    auto xpos = writeModbusFloatData(x);
    auto ypos = writeModbusFloatData(y);
    modbusData.setValue(0, xpos.first);
    modbusData.setValue(1, xpos.second);
    modbusData.setValue(2, ypos.first);
    modbusData.setValue(3, ypos.second);

    auto reply = modbusClient->sendWriteRequest(modbusData, 1);
    if(reply){
        if (!reply->isFinished())
        {
            connect(reply, &QModbusReply::finished, [=](){
                if(reply->error() == QModbusDevice::NoError){
                    updateCurPos = true;
                    isRunTarget = true;
                }else{
                    runTargetPosition(x, y);
                }
                reply->deleteLater();
            });
        }else {
            reply->deleteLater();
        }
    }
}

void ScanControlHuiChuan::initTasks()
{
    if(isInit){
        //readAxisVelocity(0x0001, 16);
        writeAxisVelocity(0x0004,2,30.2);
    }
}

void ScanControlHuiChuan::on_connectBtn_clicked()
{
    if(modbusClient->state() != QModbusDevice::ConnectedState ){
        modbusClient->setConnectionParameter(QModbusDevice::NetworkAddressParameter, PlcIP);
        modbusClient->setConnectionParameter(QModbusDevice::NetworkPortParameter, PlcPort);
        qDebug()<<"PlcIP"<<PlcIP;
        qDebug()<<"PlcPort"<<PlcPort;
        modbusClient->setNumberOfRetries(3);
        modbusClient->setTimeout(1000);

        if(modbusClient->connectDevice()){
        }else {
        }

        connect(modbusClient, &QModbusDevice::errorOccurred,
                this, [=](QModbusDevice::Error error) {
            qDebug() << "[Modbus] Error:"
                     << error
                     << modbusClient->errorString();
        });
    }else {
        modbusClient->disconnectDevice();
    }



}

void ScanControlHuiChuan::on_startScanBtn_clicked()
{
    if(modbusClient->state() != QModbusDevice::ConnectedState)
        return;

//    qDebug() << zeroPoint << currentPos;
//    if() return;
    if(isEndScan || isRunTarget) return;

    if(isXCrossed() || isYCrossed()) return;

    if(tasks.count() == 0){
        updateCurPos = false;
        creataTasksTable();
    }else {
        if(!isStartScan){
            updateCurPos = false;
            isKeepScan = true;
        }
    }
    isAxisStop = false;
    isRunTarget = false;
}

void ScanControlHuiChuan::on_stopScanBtn_clicked()
{
    if(modbusClient->state() != QModbusDevice::ConnectedState)
        return;

    if(isEndScan) return;

    if(tasks.count() == 0) {
        if(!isRunTarget){
//            updateCurPos = true;
//            isStopScan = false;
            return;
        }
    }

    if(!isAxisStop){
        isStopScan = true;
        isStartScan = false;
        updateCurPos = false;
    }
}

void ScanControlHuiChuan::on_endScanBtn_clicked()
{
    if(modbusClient->state() != QModbusDevice::ConnectedState)
        return;

    if(isEndScan) return;

    isStopScan = false;
    isPerform = false;
    isStartScan = false;
    isKeepScan = false;
    isAxisStop = false;
    updateCurPos = false;
    isRunTarget = false;
    if(!tasks.isEmpty())tasks.clear();
    isEndScan = true;
    if(timer->isActive())
        timer->stop();
    perfromEndScanTasks();
}

void ScanControlHuiChuan::on_setOriginBtn_clicked()
{
    if(modbusClient->state() != QModbusDevice::ConnectedState)
        return;

    if(isEndScan || isAxisStop) return;

    if(!isPerform){
        zeroPoint.setX(currentPos.x());
        zeroPoint.setY(currentPos.y());
        settings->setValue("Virtual_origin_X", zeroPoint.x());
        settings->setValue("Virtual_origin_Y", zeroPoint.y());
    }
}

void ScanControlHuiChuan::on_x_velocity_editingFinished(float val)
{
    setXAxisVelocity(val);
}

void ScanControlHuiChuan::on_y_velocity_editingFinished(float val)
{
    setYAxisVelocity(val);
}

void ScanControlHuiChuan::on_jog_velocity_editingFinished(float val)
{
    setJogVelocity(val);
}

void ScanControlHuiChuan::on_jogStep_1_clicked()
{
    setAxisJogStep(1);
}

void ScanControlHuiChuan::on_jogStep_5_clicked()
{
    setAxisJogStep(5);
}

void ScanControlHuiChuan::on_jogStep_10_clicked()
{
    setAxisJogStep(10);
}

void ScanControlHuiChuan::on_xAddBtn_clicked()
{
    if(modbusClient->state() != QModbusDevice::ConnectedState)
        return;

    if((tasks.count() != 0)  || isEndScan || isAxisStop || isJogDone || isRunTarget) return;
    updateCurPos = false;
    axisInch = AxisJog::XJogAdd;

}

void ScanControlHuiChuan::on_xSubBtn_clicked()
{
    if(modbusClient->state() != QModbusDevice::ConnectedState)
        return;

    if((tasks.count() != 0)  || isEndScan || isAxisStop || isJogDone || isRunTarget) return;
    updateCurPos = false;
    axisInch = AxisJog::XJogSub;

}

void ScanControlHuiChuan::on_yAddBtn_clicked()
{
    if(modbusClient->state() != QModbusDevice::ConnectedState)
        return;

    if((tasks.count() != 0) || isEndScan || isAxisStop || isJogDone || isRunTarget) return;
    updateCurPos = false;
    axisInch = AxisJog::YJogAdd;

}

void ScanControlHuiChuan::on_ySubBtn_clicked()
{
    if(modbusClient->state() != QModbusDevice::ConnectedState)
        return;

    if((tasks.count() != 0)  || isEndScan || isAxisStop || isJogDone || isRunTarget) return;
    updateCurPos = false;
    axisInch = AxisJog::YJogSub;

}

void ScanControlHuiChuan::on_zAddBtn_clicked()
{
    if(modbusClient->state() != QModbusDevice::ConnectedState)
        return;

    if((tasks.count() != 0) || isEndScan || isAxisStop || isJogDone || isRunTarget) return;
    updateCurPos = false;
    axisInch = AxisJog::ZJogAdd;

}

void ScanControlHuiChuan::on_zSubBtn_clicked()
{
    if(modbusClient->state() != QModbusDevice::ConnectedState)
        return;

    if((tasks.count() != 0)  || isEndScan || isAxisStop || isJogDone || isRunTarget) return;
    updateCurPos = false;
    axisInch = AxisJog::ZJogSub;

}

void ScanControlHuiChuan::on_rAddBtn_clicked()
{
    if(modbusClient->state() != QModbusDevice::ConnectedState)
        return;

    if((tasks.count() != 0) || isEndScan || isAxisStop || isJogDone || isRunTarget) return;
    updateCurPos = false;
    axisInch = AxisJog::RJogAdd;

}

void ScanControlHuiChuan::on_rSubBtn_clicked()
{
    if(modbusClient->state() != QModbusDevice::ConnectedState)
        return;

    if((tasks.count() != 0)  || isEndScan || isAxisStop || isJogDone || isRunTarget) return;
    updateCurPos = false;
    axisInch = AxisJog::RJogSub;

}

void ScanControlHuiChuan::on_xAddBtn_pressed()
{
    if(modbusClient->state() != QModbusDevice::ConnectedState)
        return;
    if((tasks.count() != 0)  || isEndScan || isAxisStop || isJogDone || isRunTarget) return;
    updateCurPos = false;
    axisJog = AxisJog::XJogAddPressed;

}

void ScanControlHuiChuan::on_xAddBtn_released()
{
    if(modbusClient->state() != QModbusDevice::ConnectedState)
        return;
    if((tasks.count() != 0)  || isEndScan || isAxisStop || isJogDone || isRunTarget) return;
    updateCurPos = false;
    axisJog = AxisJog::XJogAddReleased;

}

void ScanControlHuiChuan::on_xSubBtn_pressed()
{
    if(modbusClient->state() != QModbusDevice::ConnectedState)
        return;
    if((tasks.count() != 0)  || isEndScan || isAxisStop || isJogDone || isRunTarget) return;
    updateCurPos = false;
    axisJog = AxisJog::XJogSubPressed;
}

void ScanControlHuiChuan::on_xSubBtn_released()
{
    if(modbusClient->state() != QModbusDevice::ConnectedState)
        return;
    if((tasks.count() != 0)  || isEndScan || isAxisStop || isJogDone || isRunTarget) return;
    updateCurPos = false;
    axisJog = AxisJog::XJogSubReleased;
}

void ScanControlHuiChuan::on_yAddBtn_pressed()
{
    if(modbusClient->state() != QModbusDevice::ConnectedState)
        return;
    if((tasks.count() != 0)  || isEndScan || isAxisStop || isJogDone || isRunTarget) return;
    updateCurPos = false;
    axisJog = AxisJog::YJogAddPressed;
}

void ScanControlHuiChuan::on_yAddBtn_released()
{
    if(modbusClient->state() != QModbusDevice::ConnectedState)
        return;
    if((tasks.count() != 0)  || isEndScan || isAxisStop || isJogDone || isRunTarget) return;
    updateCurPos = false;
    axisJog = AxisJog::YJogAddReleased;
}

void ScanControlHuiChuan::on_ySubBtn_pressed()
{
    if(modbusClient->state() != QModbusDevice::ConnectedState)
        return;
    if((tasks.count() != 0)  || isEndScan || isAxisStop || isJogDone || isRunTarget) return;
    updateCurPos = false;
    axisJog = AxisJog::YJogSubPressed;
}

void ScanControlHuiChuan::on_ySubBtn_released()
{
    if(modbusClient->state() != QModbusDevice::ConnectedState)
        return;
    if((tasks.count() != 0)  || isEndScan || isAxisStop || isJogDone || isRunTarget) return;
    updateCurPos = false;
    axisJog = AxisJog::YJogSubReleased;
}

void ScanControlHuiChuan::on_zAddBtn_pressed()
{
    if(modbusClient->state() != QModbusDevice::ConnectedState)
        return;
    if((tasks.count() != 0)  || isEndScan || isAxisStop || isJogDone || isRunTarget) return;
    updateCurPos = false;
    axisJog = AxisJog::ZJogAddPressed;
}

void ScanControlHuiChuan::on_zAddBtn_released()
{
    if(modbusClient->state() != QModbusDevice::ConnectedState)
        return;
    if((tasks.count() != 0)  || isEndScan || isAxisStop || isJogDone || isRunTarget) return;
    updateCurPos = false;
    axisJog = AxisJog::ZJogAddReleased;
}

void ScanControlHuiChuan::on_zSubBtn_pressed()
{
    if(modbusClient->state() != QModbusDevice::ConnectedState)
        return;
    if((tasks.count() != 0)  || isEndScan || isAxisStop || isJogDone || isRunTarget) return;
    updateCurPos = false;
    axisJog = AxisJog::ZJogSubPressed;
}

void ScanControlHuiChuan::on_zSubBtn_released()
{
    if(modbusClient->state() != QModbusDevice::ConnectedState)
        return;
    if((tasks.count() != 0)  || isEndScan || isAxisStop || isJogDone || isRunTarget) return;
    updateCurPos = false;
    axisJog = AxisJog::ZJogSubReleased;
}

void ScanControlHuiChuan::on_rAddBtn_pressed()
{
    if(modbusClient->state() != QModbusDevice::ConnectedState)
        return;
    if((tasks.count() != 0)  || isEndScan || isAxisStop || isJogDone || isRunTarget) return;
    updateCurPos = false;
    axisJog = AxisJog::RJogAddPressed;
}

void ScanControlHuiChuan::on_rAddBtn_released()
{
    if(modbusClient->state() != QModbusDevice::ConnectedState)
        return;
    if((tasks.count() != 0)  || isEndScan || isAxisStop || isJogDone || isRunTarget) return;
    updateCurPos = false;
    axisJog = AxisJog::RJogAddReleased;
}

void ScanControlHuiChuan::on_rSubBtn_pressed()
{
    if(modbusClient->state() != QModbusDevice::ConnectedState)
        return;
    if((tasks.count() != 0)  || isEndScan || isAxisStop || isJogDone || isRunTarget) return;
    updateCurPos = false;
    axisJog = AxisJog::RJogSubPressed;
}

void ScanControlHuiChuan::on_rSubBtn_released()
{
    if(modbusClient->state() != QModbusDevice::ConnectedState)
        return;
    if((tasks.count() != 0)  || isEndScan || isAxisStop || isJogDone || isRunTarget) return;
    updateCurPos = false;
    axisJog = AxisJog::RJogSubReleased;
}

void ScanControlHuiChuan::on_alarmResetBtn_clicked()
{
    writeAxisReset();
}

void ScanControlHuiChuan::on_setLimitBtn_clicked()
{
    writeAxisLimitPosition();
}

void ScanControlHuiChuan::on_setMachineBtn_clicked()
{
    writeAxisMachineOrigin();
}

void ScanControlHuiChuan::on_singleScan_toggled(bool checked)
{
    if(checked){
        scanModel = ScanModel::SingleScan;
    }else {
        scanModel = ScanModel::NormalScan;
    }
}

void ScanControlHuiChuan::runTargetPosition(double x, double y)
{
    if(modbusClient->state() != QModbusDevice::ConnectedState)
        return;

    if((tasks.count() != 0)  || isEndScan || isAxisStop || isJogDone) return;

    if(!isRunTarget){
        updateCurPos = false;
        writeTargetPosition(x, y);
    }
}

void ScanControlHuiChuan::init()
{
    initWidget();
    connectFun();
    readSetting();
}

void ScanControlHuiChuan::destroy()
{
    if (settings) {
        writeSetting();
        delete settings;
        settings = nullptr;
    }
    if (modbusClient) {
         modbusClient->disconnectDevice();
         delete modbusClient;
         modbusClient= nullptr;
    }
    if (timer) {
        timer->stop();
        delete timer;
        timer = nullptr;
    }
}

void ScanControlHuiChuan::setXAxisVelocity(float vel)
{
    xVelocity = vel;
    writeAxisVelocity(R_REGISTER_BASE+X_VELOCITY, 2, xVelocity);
}

void ScanControlHuiChuan::setYAxisVelocity(float vel)
{
    yVelocity = vel;
    writeAxisVelocity(R_REGISTER_BASE+Y_VELOCITY, 2, yVelocity);
}

void ScanControlHuiChuan::setJogVelocity(float vel)
{
    jogVelocity = vel;
    writeAxisVelocity(R_REGISTER_BASE+JOG_VELOCITY, 2, jogVelocity);
}

void ScanControlHuiChuan::setAxisJogStep(int step)
{
    jopStep = step;
    qDebug() << jopStep;
}

void ScanControlHuiChuan::modbusState()
{
    if(modbusClient->state() == QModbusDevice::ConnectedState){
//        ui->connectBtn->setText("disconnect");
        timer->start();
        qDebug() << "PLC Connection Successful!";

    }else if (modbusClient->state() == QModbusDevice::UnconnectedState) {
//        ui->connectBtn->setText("connect");
        timer->stop();
        initStates();
        qDebug() << "PLC Connection Failure!";
    }
    emit modbusStateChange(modbusClient->state());
    //    emit scanRackTcpStateChangedSignal(modbusClient->state());
}

void ScanControlHuiChuan::performTasks()
{

    //updataCurrentPos();
    //perfromEndScanTasks();
    //initTasks();
    perfromJogTasks();
    //perfromStopScanTasks();
    //perfromStartScanTasks();
    //if(updateCurPos/*tasks.count() == 0 || isAxisStop (!isStartScan && !isPerform)*/){

    //}
//    if(isRunTarget){
//        readAxisRunStatus(X_AXIS_DONE, 2);
//    }
}

void ScanControlHuiChuan::creataTasksTable()
{
    int stepNum = 0;
    if(yScanLenght <= yScanStep || qFuzzyIsNull(yScanStep)){
        stepNum = 1;
    }else {
        float divisor = yScanLenght / yScanStep;
        stepNum = static_cast<int>(divisor);
        float  remainder = divisor - stepNum;

        if(remainder > 0){
            stepNum++;
        }
    }
    qDebug() << "Y-axis Scan Number " << stepNum;

    float xPos = static_cast<float>(zeroPoint.x());
    float yPos = static_cast<float>(zeroPoint.y());
    float xTemp = xPos + xScanLenght;
    tasks.clear();
    if(scanModel == ScanModel::NormalScan){
         emit scanRowNumChange(stepNum);
//        tasks.push_back(QPair<QString, float>("x", static_cast<float>(zeroPoint.x())));
//        tasks.push_back(QPair<QString, float>("y", static_cast<float>(zeroPoint.y())));
        if(stepNum > 1){
            bool b = true;
            for (int i=0; i<=stepNum-1; i++) {
                tasks.push_back(QPair<QString, float>("x", xTemp));
                tasks.push_back(QPair<QString, float>("y", yPos+(yScanStep*(i+1))));
                xTemp = b ? xPos : xPos + xScanLenght;
                b = !b;
            }
            tasks.pop_back();
        }else {
            tasks.push_back(QPair<QString, float>("x", xTemp));
        }
    }else if (scanModel == ScanModel::SingleScan) {
        emit scanRowNumChange(stepNum*2);
//        tasks.push_back(QPair<QString, float>("x", static_cast<float>(zeroPoint.x())));
//        tasks.push_back(QPair<QString, float>("y", static_cast<float>(zeroPoint.y())));

        if(stepNum > 1){
            for (int i=0; i<=stepNum-1; i++) {
                tasks.push_back(QPair<QString, float>("x", xTemp));
                tasks.push_back(QPair<QString, float>("x", static_cast<float>(zeroPoint.x())));
                tasks.push_back(QPair<QString, float>("y", yPos+(yScanStep*(i+1))));
            }
            tasks.pop_back();
        }else {
            tasks.push_back(QPair<QString, float>("x", xTemp));
            tasks.push_back(QPair<QString, float>("x", static_cast<float>(zeroPoint.x())));
        }
    }

    if(!tasks.isEmpty()){
        keepTime = 0;
        for (int i=0; i< tasks.count(); i++) {
            if(tasks[i].first == "x"){
                keepTime += xScanLenght / xVelocity;
            }else if(tasks[i].first == "y"){
                keepTime += yScanStep / yVelocity;
            }
        }
        keepTime *= 1000;
    }
//    qDebug() << "tasks count" << tasks.count();
//    for (int i=0; i< tasks.count(); i++) {
//        qDebug() << tasks[i];
//    }
    isStartScan = true;
}

void ScanControlHuiChuan::perfromStartScanTasks()
{
    if(tasks.count() == 0) {
//        if(!isInit || (axisInch != AxisJog::NotAxisJog)) updateCurPos = true;
        isStartScan = false;
        return;
    }

    if(isKeepScan){
        writeAxisStopStatus(X_START);
    }

    //执行任务列表
    if(isStartScan){
        if(!tasks.isEmpty()){
            if(tasks.head().first == "x" && !isPerform && (manState[0] ? manState[1] : true)){
                int address = R_REGISTER_BASE + X_TARTPOS;
                writeHoldingRegistersData(address, 2, tasks.head().second);
                return;
            }
            else if(tasks.head().first == "y" && !isPerform /*&& (manState[0] ? manState[1] : true)*/){
                int address = R_REGISTER_BASE + Y_TARTPOS;
                writeHoldingRegistersData(address, 2, tasks.head().second);
                return;
            }

            //读轴的动作状态
            if(isPerform){
                readAxisRunStatus(X_AXIS_DONE, 2);
                keepTime -= 50;
                scanTime(QTime().addMSecs(keepTime).toString("HH:mm:ss"));

                return;
            }
            updataCurrentPos();
        }
    }
}

void ScanControlHuiChuan::updataCurrentPos()
{
    if(modbusClient->state() != QModbusDevice::ConnectedState)
        return;

    //qDebug()<<" ScanControlHuiChuan updatePosition";
    QModbusDataUnit data(QModbusDataUnit::HoldingRegisters, 0x01F3, 4);

    QModbusReply *reply = modbusClient->sendReadRequest(data, 1);
    if(reply){
        if (!reply->isFinished())
        {
            connect(reply, &QModbusReply::finished, [=](){
                if(reply->error() == QModbusDevice::NoError){
                    float xPos = readModbusFloatData(reply->result().value(1), reply->result().value(0));
                    float yPos = readModbusFloatData(reply->result().value(3), reply->result().value(2));

                    currentPos.setX(static_cast<qreal>(xPos));
                    currentPos.setY(static_cast<qreal>(yPos));

                    emit positionChange(currentPos);

                }
                reply->deleteLater();
            });
        }else {
                reply->deleteLater();
        }
    }
}

void ScanControlHuiChuan::perfromStopScanTasks()
{
    if(isStopScan){
        writeAxisStopStatus(X_STOP);
    }
}

void ScanControlHuiChuan::perfromEndScanTasks()
{
    if(isEndScan){
        writeBackZero();
    }
}

void ScanControlHuiChuan::perfromJogTasks()
{
//    if(axisInch != AxisJog::NotAxisJog && axisJog == AxisJog::NotAxisJog){
//        if(!isJogDone){
//            int address = 0;
//            float data = 0;
//            bool state = isJogCrossed(address, data);

//            if(state){
//                writeHoldingRegistersData(address, 2, data);
//            }else {
//                axisInch = AxisJog::NotAxisJog;
//            }
//        }else {
//            if(axisInch == XJogAdd || axisInch == XJogSub){
//                readAxisJogStatus(X_AXIS_DONE);
//            }else if (axisInch == YJogAdd || axisInch == YJogSub) {
//                readAxisJogStatus(Y_AXIS_DONE);
//            }
//        }
//    }

//    if(axisJog != AxisJog::NotAxisJog && axisInch == AxisJog::NotAxisJog){

//        if (axisJog == lastAxisJog) {
//            return;  // 不往下执行
//        }

//    }

    if(axisJog != AxisJog::NotAxisJog && axisInch == AxisJog::NotAxisJog){
        switch (axisJog) {
            case XJogAddPressed:{
                writeAxisJog(X_ADD, true);
            }break;
            case XJogAddReleased:{
                writeAxisJog(X_ADD, false);
            }break;
            case XJogSubPressed:{
                writeAxisJog(X_SUB, true);
            }break;
            case XJogSubReleased:{
                writeAxisJog(X_SUB, false);
            }break;
            case YJogAddPressed:{
                writeAxisJog(Y_ADD, true);
            }break;
            case YJogAddReleased:{
                writeAxisJog(Y_ADD, false);
            }break;
            case YJogSubPressed:{
                writeAxisJog(Y_SUB, true);
            }break;
            case YJogSubReleased:{
                writeAxisJog(Y_SUB, false);
            }break;
        }

        lastAxisJog=axisJog;
    }
}


