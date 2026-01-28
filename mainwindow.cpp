#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "modbusconfig.h"

#include <QDir>
#include <QDebug>
MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    initWidget();
    connectFun();
}

MainWindow::~MainWindow()
{


    delete ui;
}

void MainWindow::saveseting(){

    settings->setValue("backOrigin_velocity", ui->backOrigin_velocity->text());
    settings->setValue("jog_velocity", ui->jog_velocity->text());
    settings->setValue("originSpeed", ui->originSpeed->text());
    settings->setValue("startSpeed", ui->startSpeed->text());
    settings->setValue("endSpeed", ui->endSpeed->text());



    QString currentPiece = ui->regin->currentText();
    settings->beginGroup(currentPiece);


    settings->setValue("inputx_2", ui->inputx_2->text());
    settings->setValue("inputy_2", ui->inputy_2->text());
    settings->setValue("inputz_2", ui->inputz_2->text());
    settings->setValue("inputr_2", ui->inputr_2->text());

    settings->setValue("inputx_3", ui->inputx_3->text());
    settings->setValue("inputy_3", ui->inputy_3->text());
    settings->setValue("inputz_3", ui->inputz_3->text());
    settings->setValue("inputr_3", ui->inputr_3->text());

    settings->setValue("inputx_4", ui->inputx_4->text());
    settings->setValue("inputy_4", ui->inputy_4->text());
    settings->setValue("inputz_4", ui->inputz_4->text());
    settings->setValue("inputr_4", ui->inputr_4->text());

    settings->setValue("sweepSpeed", ui->sweepSpeed->text());

    settings->endGroup();
    settings->sync();  // 强制写入磁盘
}

void MainWindow::initWidget()
{
    //    ScanControlAbstract *temp;
    scanCtrlHunChuan = new ScanControlHuiChuan();
    scanCtrlTaiDa = new ScanControlTaiDa();

    scanCtrl = scanCtrlHunChuan;
    scanCtrl2 = scanCtrlTaiDa;

    scanCtrl->setModbusTcpIP(ui->IP_Edit->text());
    scanCtrl->setModbusPort(ui->port_Edit->text().toInt());
    scanCtrl2->setModbusTcpIP(ui->IP_Edit2->text());
    scanCtrl2->setModbusPort(ui->port_Edit2->text().toInt());

    QString appDataPath = QCoreApplication::applicationDirPath()+"/date";
    QDir dir(appDataPath);
    if (!dir.exists())
        dir.mkpath(".");

    settings=new QSettings(appDataPath+"/settings.ini", QSettings::IniFormat);

    ui->backOrigin_velocity->setText(settings->value("backOrigin_velocity", "0").toString());
    ui->jog_velocity->setText(settings->value("jog_velocity", "0").toString());
    ui->originSpeed->setText(settings->value("originSpeed", "0").toString());
    ui->startSpeed->setText(settings->value("startSpeed", "0").toString());
    ui->endSpeed->setText(settings->value("endSpeed", "0").toString());


    QString activePiece =  ui->regin->currentText();
    QStringList groups = settings->childGroups();
    if (groups.contains(activePiece)) {
        ui->inputx_2->setText(settings->value("inputx_2", "").toString());
        ui->inputy_2->setText(settings->value("inputy_2", "").toString());
        ui->inputz_2->setText(settings->value("inputz_2", "").toString());
        ui->inputr_2->setText(settings->value("inputr_2", "").toString());

        ui->inputx_3->setText(settings->value("inputx_3", "").toString());
        ui->inputy_3->setText(settings->value("inputy_3", "").toString());
        ui->inputz_3->setText(settings->value("inputz_3", "").toString());
        ui->inputr_3->setText(settings->value("inputr_3", "").toString());

        ui->inputx_4->setText(settings->value("inputx_4", "").toString());
        ui->inputy_4->setText(settings->value("inputy_4", "").toString());
        ui->inputz_4->setText(settings->value("inputz_4", "").toString());
        ui->inputr_4->setText(settings->value("inputr_4", "").toString());

        ui->sweepSpeed->setText(settings->value("sweepSpeed", "").toString());
    }

    settings->endGroup();

}



void MainWindow::connectFun()
{

    connect(scanCtrl, &ScanControlAbstract::modbusStateChange, [=](QModbusDevice::State state){
        if(state == QModbusDevice::ConnectedState){
            ui->connectBtn->setText("disconnect");
        }else if (state == QModbusDevice::UnconnectedState) {
            ui->connectBtn->setText("connect");
        }
    });

    connect(scanCtrl2, &ScanControlAbstract::modbusStateChange, [=](QModbusDevice::State state){
        if(state == QModbusDevice::ConnectedState){
            ui->connectBtn->setText("disconnect");
        }else if (state == QModbusDevice::UnconnectedState) {
            ui->connectBtn->setText("connect");
        }
    });


    connect(scanCtrl, &ScanControlAbstract::positionChange, this, &MainWindow::updatePosition);
    connect(scanCtrl2, &ScanControlAbstract::positionChange, this, &MainWindow::updatePosition2);

    connect(ui->connectBtn, &QPushButton::clicked, scanCtrl, &ScanControlAbstract::on_connectBtn_clicked);
    connect(ui->connectBtn, &QPushButton::clicked, scanCtrl2, &ScanControlAbstract::on_connectBtn_clicked);

    connect(ui->startScanBtn, &QPushButton::clicked, scanCtrl, &ScanControlAbstract::on_start);
    connect(ui->stopScanBtn, &QPushButton::clicked, scanCtrl, &ScanControlAbstract::on_stop);
    connect(ui->endScanBtn, &QPushButton::clicked, scanCtrl, &ScanControlAbstract::on_end);
    connect(ui->resetScanBtn, &QPushButton::clicked, scanCtrl, &ScanControlAbstract::on_alarmReset);
    connect(ui->backZeroScanBtn, &QPushButton::clicked, scanCtrl, &ScanControlAbstract::on_backZero);

    connect(ui->xAddBtn, &QPushButton::pressed, scanCtrl, &ScanControlAbstract::on_xAddBtn_pressed);
    connect(ui->xAddBtn, &QPushButton::released, scanCtrl, &ScanControlAbstract::on_xAddBtn_released);
    connect(ui->xSubBtn, &QPushButton::pressed, scanCtrl, &ScanControlAbstract::on_xSubBtn_pressed);
    connect(ui->xSubBtn, &QPushButton::released, scanCtrl, &ScanControlAbstract::on_xSubBtn_released);
    connect(ui->yAddBtn, &QPushButton::pressed, scanCtrl, &ScanControlAbstract::on_yAddBtn_pressed);
    connect(ui->yAddBtn, &QPushButton::released, scanCtrl, &ScanControlAbstract::on_yAddBtn_released);
    connect(ui->ySubBtn, &QPushButton::pressed, scanCtrl, &ScanControlAbstract::on_ySubBtn_pressed);
    connect(ui->ySubBtn, &QPushButton::released, scanCtrl, &ScanControlAbstract::on_ySubBtn_released);

    connect(ui->zAddBtn, &QPushButton::pressed, scanCtrl2, &ScanControlAbstract::on_zAddBtn_pressed);
    connect(ui->zAddBtn, &QPushButton::released, scanCtrl2, &ScanControlAbstract::on_zAddBtn_released);
    connect(ui->zSubBtn, &QPushButton::pressed, scanCtrl2, &ScanControlAbstract::on_zSubBtn_pressed);
    connect(ui->zSubBtn, &QPushButton::released, scanCtrl2, &ScanControlAbstract::on_zSubBtn_released);
    connect(ui->rAddBtn, &QPushButton::pressed, scanCtrl2, &ScanControlAbstract::on_rAddBtn_pressed);
    connect(ui->rAddBtn, &QPushButton::released, scanCtrl2, &ScanControlAbstract::on_rAddBtn_released);
    connect(ui->rSubBtn, &QPushButton::pressed, scanCtrl2, &ScanControlAbstract::on_rSubBtn_pressed);
    connect(ui->rSubBtn, &QPushButton::released, scanCtrl2, &ScanControlAbstract::on_rSubBtn_released);

    connect(ui->backOrigin_velocity, &QLineEdit::editingFinished, this, &MainWindow::backOrigin_velocity);
    connect(ui->jog_velocity, &QLineEdit::editingFinished, this, &MainWindow::jog_velocity);
    connect(ui->originSpeed, &QLineEdit::editingFinished, this, &MainWindow::originSpeed);
    connect(ui->startSpeed, &QLineEdit::editingFinished, this, &MainWindow::startSpeed);
    connect(ui->endSpeed, &QLineEdit::editingFinished, this, &MainWindow::endSpeed);
    connect(ui->setMOrigin, &QPushButton::clicked, this, &MainWindow::setMOrigin);

    connect(ui->regin,
            QOverload<const QString &>::of(&QComboBox::currentIndexChanged),
            this,
            &MainWindow::regin);

}


void MainWindow::updatePosition(QPointF pos)
{

    //qDebug()<<"updatePosition";
    ui->xCurPos->setText(QString::number(static_cast<double>(pos.x()), 'f', 3));
    ui->yCurPos->setText(QString::number(static_cast<double>(pos.y()), 'f', 3));

}

void MainWindow::updatePosition2(QPointF pos)
{

    //qDebug()<<"updatePosition";

    ui->zCurPos->setText(QString::number(static_cast<double>(pos.x()), 'f', 3));
    ui->rCurPos->setText(QString::number(static_cast<double>(pos.y()), 'f', 3));
}



void MainWindow::backOrigin_velocity(){

    scanCtrl->writeHoldingRegistersData(0x00c7,2,ui->backOrigin_velocity->text().toFloat());
    scanCtrl2->writeHoldingRegistersData(0x00c7,2,ui->backOrigin_velocity->text().toFloat());
    saveseting();
}

void MainWindow::jog_velocity(){


    scanCtrl->writeHoldingRegistersData(0x00cf,2,ui->jog_velocity->text().toFloat());
    scanCtrl2->writeHoldingRegistersData(0x00cf,2,ui->jog_velocity->text().toFloat());
    saveseting();
}

void MainWindow::originSpeed(){

    scanCtrl->writeHoldingRegistersData(0x00c9,2,ui->originSpeed->text().toFloat());
    scanCtrl2->writeHoldingRegistersData(0x00c9,2,ui->originSpeed->text().toFloat());
    saveseting();
}

void MainWindow::startSpeed(){

    scanCtrl->writeHoldingRegistersData(0x00cb,2,ui->startSpeed->text().toFloat());
    scanCtrl2->writeHoldingRegistersData(0x00cb,2,ui->startSpeed->text().toFloat());
    saveseting();
}

void MainWindow::endSpeed(){

    scanCtrl->writeHoldingRegistersData(0x00cd,2,ui->startSpeed->text().toFloat());
    scanCtrl2->writeHoldingRegistersData(0x00cd,2,ui->startSpeed->text().toFloat());
    saveseting();
}




void MainWindow::setOrigin(){

    scanCtrl->writeHoldingRegistersData(sweep,2,ui->sweepSpeed->text().toFloat());

    scanCtrl->writeHoldingRegistersData(xo,2,ui->inputx->text().toFloat());
    scanCtrl->writeHoldingRegistersData(yo,2,ui->inputy->text().toFloat());

    scanCtrl2->writeHoldingRegistersData(xo,2,ui->inputz->text().toFloat());
    scanCtrl2->writeHoldingRegistersData(yo,2,ui->inputr->text().toFloat());

    scanCtrl->writeAxisJog(origin,true);
    scanCtrl2->writeAxisJog(origin,true);
    saveseting();
}

void MainWindow::setStart(){

    scanCtrl->writeHoldingRegistersData(sweep,2,ui->sweepSpeed->text().toFloat());

    scanCtrl->writeHoldingRegistersData(xs,2,ui->inputx_2->text().toFloat());
    scanCtrl->writeHoldingRegistersData(ys,2,ui->inputy_2->text().toFloat());

    scanCtrl2->writeHoldingRegistersData(xs,2,ui->inputz_2->text().toFloat());
    scanCtrl2->writeHoldingRegistersData(ys,2,ui->inputr_2->text().toFloat());

    scanCtrl->writeAxisJog(start,true);
    scanCtrl2->writeAxisJog(start,true);
    saveseting();
}

void MainWindow::setEnd(){

    scanCtrl->writeHoldingRegistersData(sweep,2,ui->sweepSpeed->text().toFloat());

    scanCtrl->writeHoldingRegistersData(xe,2,ui->inputx_2->text().toFloat());
    scanCtrl->writeHoldingRegistersData(ye,2,ui->inputy_2->text().toFloat());

    scanCtrl2->writeHoldingRegistersData(xe,2,ui->inputz_2->text().toFloat());
    scanCtrl2->writeHoldingRegistersData(ye,2,ui->inputr_2->text().toFloat());

    scanCtrl->writeAxisJog(end,true);
    scanCtrl2->writeAxisJog(end,true);
    saveseting();
}



void MainWindow::setMOrigin(){


    scanCtrl->writeHoldingRegistersData(sweep,2,ui->sweepSpeed->text().toFloat());

    scanCtrl->writeHoldingRegistersData(0x0063,2,ui->inputx->text().toFloat());
    scanCtrl->writeHoldingRegistersData(0x0065,2,ui->inputy->text().toFloat());

    scanCtrl2->writeHoldingRegistersData(0x0063,2,ui->inputz->text().toFloat());
    scanCtrl2->writeHoldingRegistersData(0x0065,2,ui->inputr->text().toFloat());

    scanCtrl->writeAxisJog(0x0005,true);
    scanCtrl2->writeAxisJog(0x0005,true);
    saveseting();
}


void MainWindow::regin(){

    if(ui->regin->currentIndex()==0){

        origin=0x0063;
        start=0x0064;
        end=0x0065;

        xo=0x0000;
        yo=0x0018;

        xs=0x0008;
        ys=0x0020;

        xe=0x0010;
        ye=0x0028;

        sweep=0X012B;

    }else if (ui->regin->currentIndex()==1) {


        origin=0x0066;
        start=0x0067;
        end=0x0068;

        xo=0x0002;
        yo=0x001A;

        xs=0x000A;
        ys=0x0022;

        xe=0x0012;
        ye=0x002A;

        sweep=0X012D;
    }else if (ui->regin->currentIndex()==2) {

        origin=0x0069;
        start=0x006A;
        end=0x006B;

        xo=0x0004;
        yo=0x001C;

        xs=0x000C;
        ys=0x0024;

        xe=0x0014;
        ye=0x002C;

        sweep=0X012F;
    }else if (ui->regin->currentIndex()==3) {

        origin=0x006C;
        start=0x006D;
        end=0x006E;

        xo=0x0006;
        yo=0x001E;

        xs=0x000E;
        ys=0x0026;

        xe=0x0016;
        ye=0x002E;

        sweep=0X0131;
    }


    QString activePiece =  ui->regin->currentText();
    QStringList groups = settings->childGroups();
    if (groups.contains(activePiece)) {
        ui->inputx_2->setText(settings->value("inputx_2", "").toString());
        ui->inputy_2->setText(settings->value("inputy_2", "").toString());
        ui->inputz_2->setText(settings->value("inputz_2", "").toString());
        ui->inputr_2->setText(settings->value("inputr_2", "").toString());

        ui->inputx_3->setText(settings->value("inputx_3", "").toString());
        ui->inputy_3->setText(settings->value("inputy_3", "").toString());
        ui->inputz_3->setText(settings->value("inputz_3", "").toString());
        ui->inputr_3->setText(settings->value("inputr_3", "").toString());

        ui->inputx_4->setText(settings->value("inputx_4", "").toString());
        ui->inputy_4->setText(settings->value("inputy_4", "").toString());
        ui->inputz_4->setText(settings->value("inputz_4", "").toString());
        ui->inputr_4->setText(settings->value("inputr_4", "").toString());

        ui->sweepSpeed->setText(settings->value("sweepSpeed", "").toString());
    }

    settings->endGroup();



}
