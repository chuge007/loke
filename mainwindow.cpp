#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "modbusconfig.h"

#include <QDir>
#include <QDebug>
#include <QTcpSocket>
#include <QElapsedTimer>
#include <QThread>


// mm → angleControl
inline int64_t mmToAngleControl(double mm)
{
    // 1 mm = 60° , 1° = 100 LSB
    return static_cast<int64_t>(mm * 60.0 * 100.0);  // mm * 6000
}



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


    settings->setValue("IP_Edit", ui->IP_Edit->text());
    settings->setValue("port_Edit", ui->port_Edit->text());

    settings->setValue("sweepSpeed", ui->sweepSpeed->text());

    settings->setValue("xlenght", ui->x_lenght->text());
    settings->setValue("ylenght", ui->y_lenght->text());
    settings->setValue("step", ui->y_step->text());

    // 三个勾选状态持久化，下次打开保持上次设置
    settings->setValue("loopScan",   ui->loopScanCheck->isChecked());
    settings->setValue("invertX",    ui->invertXCheck->isChecked());
    settings->setValue("invertY",    ui->invertYCheck->isChecked());

    // 速度类输入框也一并保存（之前有些只在 editingFinished 才存）
    settings->setValue("backOrigin_velocity", ui->backOrigin_velocity->text());
    settings->setValue("jog_velocity",        ui->jog_velocity->text());
    settings->setValue("originSpeed",         ui->originSpeed->text());
    settings->setValue("startSpeed",          ui->startSpeed->text());
    settings->setValue("endSpeed",            ui->endSpeed->text());
    settings->setValue("stepSpeed",           ui->stepSpeed->text());


    // 扫查轴下拉、检测区域下拉、Region 下拉
    settings->setValue("scanAxis",   ui->comboBox_2->currentIndex());
    settings->setValue("detectArea", ui->comboBox->currentIndex());
    settings->setValue("regionIdx",  ui->regin->currentIndex());

    // 设置区域里的输入框（x/y/z/r 三组）
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




    settings->beginGroup(ui->regin->currentText());

    settings->setValue("xlenght", ui->x_lenght->text());
    settings->setValue("ylenght", ui->y_lenght->text());
    settings->setValue("step", ui->y_step->text());


    settings->setValue("sweepSpeed", ui->sweepSpeed->text());
    settings->setValue("stepSpeed", ui->stepSpeed->text());

    settings->endGroup();
    settings->sync();  // 强制写入磁盘
}

void MainWindow::initWidget()
{
    //    ScanControlAbstract *temp;
    scanCtrl = new ScanControlHuiChuan();


    QString appDataPath = QCoreApplication::applicationDirPath()+"/date";
    QDir dir(appDataPath);
    if (!dir.exists())
        dir.mkpath(".");

    settings=new QSettings(appDataPath+"/settings.ini", QSettings::IniFormat);

    ui->backOrigin_velocity->setText(settings->value("backOrigin_velocity", "0").toString());
    ui->jog_velocity->setText(settings->value("jog_velocity", "0").toString());


    ui->sweepSpeed->setText(settings->value("sweepSpeed", "").toString());
    ui->stepSpeed->setText(settings->value("stepSpeed", "").toString());

    ui->x_lenght->setText(settings->value("xlenght", "").toString());
    ui->y_lenght->setText(settings->value("ylenght", "").toString());
    ui->y_step->setText(settings->value("step", "").toString());

    QString activePiece =  ui->regin->currentText();
    QStringList groups = settings->childGroups();
    if (groups.contains(activePiece)) {

        ui->sweepSpeed->setText(settings->value("sweepSpeed", "").toString());
        ui->stepSpeed->setText(settings->value("stepSpeed", "").toString());

        ui->x_lenght->setText(settings->value("xlenght", "").toString());
        ui->y_lenght->setText(settings->value("ylenght", "").toString());
        ui->y_step->setText(settings->value("step", "").toString());
    }

    settings->endGroup();

    ui->IP_Edit->setText(settings->value("IP_Edit", "192.168.1.13").toString());
    ui->port_Edit->setText(settings->value("port_Edit", "8802").toString());

    // 三个勾选状态：默认全不勾，从配置回读
    ui->loopScanCheck->setChecked(settings->value("loopScan", false).toBool());
    ui->invertXCheck->setChecked(settings->value("invertX",  false).toBool());
    ui->invertYCheck->setChecked(settings->value("invertY",  false).toBool());

    // 其余速度框、下拉框、设置区域输入框
    ui->originSpeed->setText(settings->value("originSpeed", "").toString());
    ui->startSpeed ->setText(settings->value("startSpeed",  "").toString());
    ui->endSpeed   ->setText(settings->value("endSpeed",    "").toString());
    ui->stepSpeed  ->setText(settings->value("stepSpeed",   "").toString());

    ui->comboBox_2 ->setCurrentIndex(settings->value("scanAxis",   0).toInt());
    ui->comboBox   ->setCurrentIndex(settings->value("detectArea", 0).toInt());
    ui->regin      ->setCurrentIndex(settings->value("regionIdx",  0).toInt());
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

    auto *scanSpeedValidator = new QDoubleValidator(0.0, 999999.0, 3, this);
    scanSpeedValidator->setNotation(QDoubleValidator::StandardNotation);
    ui->sweepSpeed->setValidator(scanSpeedValidator);

    auto *stepSpeedValidator = new QDoubleValidator(0.0, 999999.0, 3, this);
    stepSpeedValidator->setNotation(QDoubleValidator::StandardNotation);
    ui->stepSpeed->setValidator(stepSpeedValidator);

    scanCtrl->setModbusTcpIP(ui->IP_Edit->text());

    scanCtrl->setModbusPort(ui->port_Edit->text().toInt());

    scanCtrl->speed=ui->jog_velocity->text().toFloat()*6000;
}




void MainWindow::connectFun()
{


    connect(scanCtrl, &ScanControlAbstract::tcpStateChange,
            this, [this](bool connected) {
        ui->connectBtn->setText(connected ? "discon" : "conect");

    });


    connect(scanCtrl, &ScanControlAbstract::positionChangex, this, &MainWindow::updatePosition);
    connect(scanCtrl, &ScanControlAbstract::positionChangey, this, &MainWindow::updatePosition2);

    connect(ui->connectBtn, &QPushButton::clicked, scanCtrl, &ScanControlAbstract::on_connectBtn_clicked);
    connect(ui->connectBtn, &QPushButton::clicked, this, &MainWindow::on_connectBtn_clicked);

    connect(ui->startScanBtn, &QPushButton::clicked, this, &MainWindow::setStart);
    connect(ui->stopScanBtn, &QPushButton::clicked, scanCtrl, &ScanControlAbstract::on_stop);

    connect(ui->backZeroScanBtn, &QPushButton::clicked, this, &MainWindow::on_backZero);
    connect(ui->endScanBtn, &QPushButton::clicked, this, &MainWindow::scanEnd);

    connect(ui->xAddBtn, &QPushButton::pressed, scanCtrl, &ScanControlAbstract::on_xAddBtn_pressed);
    connect(ui->xAddBtn, &QPushButton::released, scanCtrl, &ScanControlAbstract::on_xAddBtn_released);
    connect(ui->xSubBtn, &QPushButton::pressed, scanCtrl, &ScanControlAbstract::on_xSubBtn_pressed);
    connect(ui->xSubBtn, &QPushButton::released, scanCtrl, &ScanControlAbstract::on_xSubBtn_released);

    connect(ui->yAddBtn, &QPushButton::pressed, scanCtrl, &ScanControlAbstract::on_yAddBtn_pressed);
    connect(ui->yAddBtn, &QPushButton::released, scanCtrl, &ScanControlAbstract::on_yAddBtn_released);
    connect(ui->ySubBtn, &QPushButton::pressed, scanCtrl, &ScanControlAbstract::on_ySubBtn_pressed);
    connect(ui->ySubBtn, &QPushButton::released, scanCtrl, &ScanControlAbstract::on_ySubBtn_released);


    connect(ui->backOrigin_velocity, &QLineEdit::editingFinished, this, &MainWindow::backOrigin_velocity);
    connect(ui->jog_velocity, &QLineEdit::editingFinished, this, &MainWindow::jog_velocity);



    connect(ui->setBtn, &QPushButton::clicked, this, &MainWindow::setBtn);
    connect(ui->setMOrigin, &QPushButton::clicked, this, &MainWindow::setMOrigin);

    // 把 X/Y 反向勾选同步到控制器，让点动按键方向也跟着翻
    auto syncInvert = [this]() {
        scanCtrl->invertXAxis = ui->invertXCheck->isChecked();
        scanCtrl->invertYAxis = ui->invertYCheck->isChecked();
        saveseting();   // 勾选变化立即落盘，避免崩溃丢配置
    };
    connect(ui->invertXCheck, &QCheckBox::toggled, this, syncInvert);
    connect(ui->invertYCheck, &QCheckBox::toggled, this, syncInvert);
    // 循环执行勾选变化也写盘
    connect(ui->loopScanCheck, &QCheckBox::toggled, this, [this](bool){ saveseting(); });
    syncInvert();   // 初始态同步一次（不会重复写盘，因为值没变）

    // 所有输入框 editingFinished 即落盘；下拉框 currentIndexChanged 即落盘
    auto saveOnEdit = [this]{ saveseting(); };
    QList<QLineEdit*> edits = {
        ui->IP_Edit, ui->port_Edit,
        ui->sweepSpeed,
        ui->x_lenght, ui->y_lenght, ui->y_step,
        ui->backOrigin_velocity, ui->jog_velocity,
        ui->originSpeed, ui->startSpeed, ui->endSpeed, ui->stepSpeed,

        ui->inputx_2, ui->inputy_2, ui->inputz_2, ui->inputr_2,
        ui->inputx_3, ui->inputy_3, ui->inputz_3, ui->inputr_3,
        ui->inputx_4, ui->inputy_4, ui->inputz_4, ui->inputr_4,
    };
    for (auto *e : edits) {
        connect(e, &QLineEdit::editingFinished, this, saveOnEdit);
    }
    connect(ui->comboBox_2, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int){ saveseting(); });
    connect(ui->comboBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int){ saveseting(); });
    connect(ui->regin, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int){ saveseting(); });
}




// X 轴电机物理 +方向 与 UI/路径上的 +X 方向相反，所以基础先取负，
// 用户再通过 invertXCheck 进一步翻转一次（适配设备实际安装方向）。
// 收到位置 → 立刻参与到位判定（事件驱动，不再依赖 50ms 轮询比较）。
void MainWindow::updatePosition(float pos)
{
    float v = -pos;
    if (ui->invertXCheck->isChecked()) v = -v;
    position.x = v;
    firstPosReceived = true;
    ui->xCurPos->setText(QString::number(v, 'f', 2));
    if (arriveActive) checkArrival();
}

void MainWindow::updatePosition2(float pos)
{
    float v = pos;
    if (ui->invertYCheck->isChecked()) v = -v;
    position.y = v;
    firstPosReceived = true;
    ui->yCurPos->setText(QString::number(v, 'f', 2));
    if (arriveActive) checkArrival();
}







void MainWindow::backOrigin_velocity(){

    saveseting();
}

void MainWindow::jog_velocity(){

    scanCtrl->speed=ui->jog_velocity->text().toFloat()*6000;
    qDebug()<<"jog_velocity:"<< scanCtrl->speed;
    saveseting();
}

void MainWindow::originSpeed(){

    saveseting();
}

void MainWindow::startSpeed(){

    saveseting();
}

void MainWindow::endSpeed(){

    saveseting();
}

uint32_t MainWindow::axisCommandSpeed(bool isXAxisCommand) const
{
    const bool scanAxisIsX = (ui->comboBox_2->currentIndex() == 0);
    const double sweepDps = ui->sweepSpeed->text().toDouble();
    double stepDps = ui->stepSpeed->text().toDouble();
    if (!(stepDps > 0.0)) {
        stepDps = sweepDps;
    }

    const bool isScanAxisCommand = isXAxisCommand ? scanAxisIsX : !scanAxisIsX;
    const double dps = isScanAxisCommand ? sweepDps : stepDps;
    return uint32_t(dps * 6000.0);
}

Point2D MainWindow::pathPointToPhysicalTargets(const Point2D& point) const
{
    Point2D targets;
    const bool scanAxisIsX = (ui->comboBox_2->currentIndex() == 0);

    if (scanAxisIsX) {
        targets.x = -point.x;
        targets.y =  point.y;
    } else {
        targets.x = -point.y;
        targets.y =  point.x;
    }

    if (ui->invertXCheck->isChecked()) targets.x = -targets.x;
    if (ui->invertYCheck->isChecked()) targets.y = -targets.y;
    return targets;
}



// 弓形扫描路径生成
//   xLength：扫查轴长(可正可负，符号决定 X 方向)
//   yLength：步进轴长(可正可负，符号决定 Y 方向；==0 表示单轴往复)
//   yStep  ：步进长度(>=0；==0 或 yLength==0 时退化为单轴 X 往复)
// 返回路径点序列，相邻两点之间只有一个轴在动，方便分轴下发。
std::vector<Point2D> MainWindow::generateBowScanPathDense(
        float xLength,
        float yLength,
        float yStep
        )
{
    std::vector<Point2D> path;

    // 起点
    path.push_back({0.0f, 0.0f});

    // 退化情形：步进长度或步进轴长为 0 → 仅做 X 单轴往复一次
    //   生成 (0,0) → (xLength,0) → (0,0)
    //   这样勾选"循环执行"时就是不停往复扫描。
    if (qFuzzyIsNull(yLength) || qFuzzyIsNull(yStep)) {
        if (!qFuzzyIsNull(xLength)) {
            path.push_back({xLength, 0.0f});
            path.push_back({0.0f,    0.0f});
        }
        qDebug() << "path size:" << path.size() << "(single-axis sweep)";
        return path;
    }

    // 正常弓形：根据 yLength 符号决定推进方向，yStep 取绝对值后加上正确符号
    const float ySign  = (yLength >= 0) ? 1.0f : -1.0f;
    const float stepAbs = std::fabs(yStep);
    const float dy     = ySign * stepAbs;     // 每次步进的 y 增量(带符号)
    const float yEnd   = yLength;             // 终止位置(同符号)

    // 安全保护：避免极端参数导致点数膨胀（>10000 直接截断）
    const int maxPoints = 10000;

    bool leftToRight = true;
    float x = 0.0f;
    float y = 0.0f;

    // 比较函数：朝 yEnd 的方向是否还没走完
    auto notReachedEnd = [&](float yy) {
        return ySign > 0 ? (yy <= yEnd + 1e-4f) : (yy >= yEnd - 1e-4f);
    };

    while (notReachedEnd(y) && (int)path.size() < maxPoints) {
        // 1. X 走整段
        x = leftToRight ? xLength : 0.0f;
        path.push_back({x, y});

        // 2. Y 步进一次
        y += dy;
        if (!notReachedEnd(y)) break;

        path.push_back({x, y});

        // 3. 方向反转
        leftToRight = !leftToRight;
    }

    qDebug() << "path size:" << path.size()
             << "xLen:" << xLength << "yLen:" << yLength << "yStep:" << yStep;
    return path;
}




void MainWindow::setBtn(){

    xlenght=ui->x_lenght->text().toFloat();
    ylenght=ui->y_lenght->text().toFloat();
    step=ui->y_step->text().toFloat();


    saveseting();

}

void MainWindow::on_connectBtn_clicked(){

    scanCtrl->setModbusTcpIP(ui->IP_Edit->text());
    scanCtrl->setModbusPort(ui->port_Edit->text().toInt());

}
void MainWindow::setOrigin(){



    saveseting();
}

void MainWindow::on_backZero(){


    int64_t angleControly = mmToAngleControl(0);


    uint32_t maxSpeed = static_cast<uint32_t>(ui->backOrigin_velocity->text().toDouble() * 6000.0);

    scanCtrl->pushsend=true;

    scanCtrl->motorId=0x02;

    scanCtrl->runPosintion(angleControly, maxSpeed);

    QTimer::singleShot(50, [this]() {
        int64_t angleControlx = mmToAngleControl(0);
        uint32_t maxSpeed = static_cast<uint32_t>(ui->backOrigin_velocity->text().toDouble() * 6000.0);
        scanCtrl->motorId=0x01;

        scanCtrl->runPosintion(angleControlx, maxSpeed);


    });

    QTimer::singleShot(120, [this]() {
        scanCtrl->pushsend=false;
    });

    ui->xAddBtn->setEnabled(true);

    ui->xSubBtn->setEnabled(true);

    ui->yAddBtn->setEnabled(true);

    ui->ySubBtn->setEnabled(true);

    ui->backOrigin_velocity->setEnabled(true);

    ui->jog_velocity->setEnabled(true);

    ui->setMOrigin->setEnabled(true);

}


void MainWindow::sendNextPoint()
{
    if(currentPathIndex >= path.size()){
        qDebug() << "Path finished";

        // Loop scan: when enabled and not stopped, go back to origin
        // using home speed, then restart scan from the first scan point.
        // path[0] is origin itself; arrive-check on origin will trigger
        // sendNextPoint() again with currentPathIndex == 1.
        if(!stopScan && ui->loopScanCheck->isChecked()){
            qDebug() << "Loop scan: back to origin then restart";

            on_backZero();

            currentPathIndex = 0;
            Point2D originPt;
            originPt.x = 0;
            originPt.y = 0;
            startArriveCheck(originPt);
            return;
        }

        // Non-loop: scan finished, go back to origin and stop.
        on_backZero();
        return;


    }

    Point2D p = path[currentPathIndex];
    // 关键优化：只下发实际有变化的轴的命令
    //   - 弓形路径相邻两点本来就只有一个轴在变，发"X 不动 + Y 不动"的废命令
    //     会和位置查询在网关里粘帧，导致真正要动的那条命令被吞 → 15s 卡死。
    //   - 现在比对前一点，哪个轴变了只发哪个；两个都变才间隔 50ms 防粘帧。
    Point2D prev{0.0f, 0.0f};
    if (currentPathIndex > 0) prev = path[currentPathIndex - 1];

    const Point2D currentTargets = pathPointToPhysicalTargets(p);
    const Point2D previousTargets = pathPointToPhysicalTargets(prev);

    bool xMotorChanged = std::fabs(currentTargets.x - previousTargets.x) > 0.01f;
    bool yMotorChanged = std::fabs(currentTargets.y - previousTargets.y) > 0.01f;
    // 第一点：电机不一定真的在原点，强制把两个物理轴都发一遍当作"go-home"
    if (currentPathIndex == 0) { xMotorChanged = true; yMotorChanged = true; }

    qDebug() << "Move to:" << p.x << p.y
             << "from:" << prev.x << prev.y
             << "motorX?" << xMotorChanged << "motorY?" << yMotorChanged;

    // 速度选择规则：
    //   - 扫查轴是 x：X 方向变化用扫查速度，Y 方向变化是步进 → 用步进速度
    //   - 扫查轴是 y：Y 方向变化用扫查速度，X 方向变化是步进 → 用步进速度
    //   - 第一点 go-home：X、Y 各自用对应"扫查方向"的扫查速度兜底
    //   - 步进速度若没填或非法，回退到扫查速度，避免 0 速命令把电机锁住
    const uint32_t xMaxSpeed = axisCommandSpeed(true);
    const uint32_t yMaxSpeed = axisCommandSpeed(false);

    auto sendX = [this, currentTargets, xMaxSpeed]() {
        scanCtrl->pushsend = true;
        scanCtrl->motorId  = 0x02;
        scanCtrl->runPosintion(mmToAngleControl(currentTargets.x), xMaxSpeed);
        scanCtrl->pushsend = false;
    };
    auto sendY = [this, currentTargets, yMaxSpeed]() {
        scanCtrl->pushsend = true;
        scanCtrl->motorId  = 0x01;
        scanCtrl->runPosintion(mmToAngleControl(currentTargets.y), yMaxSpeed);
        scanCtrl->pushsend = false;
    };

    if (xMotorChanged && yMotorChanged) {
        // 两轴都要动：X 立即发，Y 隔 50ms 防粘帧（保留原有节拍）
        sendX();
        QTimer::singleShot(50, this, sendY);
    } else if (xMotorChanged) {
        // 只动 X：直接发，没有可粘帧的对象
        sendX();
    } else if (yMotorChanged) {
        // 只动 Y：直接发，省掉以前那条没用的 X 废命令
        sendY();
    }

    // 都没变：电机已就位，直接进到位检测，会立刻命中

    startArriveCheck(p);
}




// 启动到位检测：记录目标点、开计时器，不再 50ms 轮询比较位置；
// 真正的判定在 updatePosition*() 收到回包后触发的 checkArrival() 里。
// arriveTimer 只作为"超时兜底"：
//   - 3s 内连一次位置回包都没收到 → TCP 异常，停扫查；
//   - 单点 15s 还没到位 → 强制推进，避免永久卡死。
void MainWindow::startArriveCheck(Point2D target)
{
    if(arriveTimer){
        arriveTimer->stop();
        arriveTimer->deleteLater();
        arriveTimer = nullptr;
    }
    if(stopScan){
        arriveActive = false;
        return;
    }

    arriveTarget = target;
    arriveActive = true;
    arriveResendCount = 0;
    arriveLastDist = -1.0f;
    arriveElapsed.start();
    arriveStallSince.start();

    arriveTimer = new QTimer(this);
    arriveTimer->setInterval(500);   // 兜底节拍 500ms 用来检测卡死/超时
    connect(arriveTimer, &QTimer::timeout, this, [this]() {
        if (!arriveActive) {
            arriveTimer->stop();
            arriveTimer->deleteLater();
            arriveTimer = nullptr;
            return;
        }
        if (!firstPosReceived) {
            if (arriveElapsed.elapsed() > 3000) {
                qDebug() << "[ARRIVE] no position reply within 3s, abort scan";
                arriveActive = false;
                stopScan = true;
                arriveTimer->stop();
                arriveTimer->deleteLater();
                arriveTimer = nullptr;
            }
            return;
        }

        // 当前距离：arriveTarget 保持逻辑扫查/步进坐标，position 保持 UI X/Y 坐标，
        // 按当前扫查轴做一次映射后再比较，避免把物理坐标和逻辑坐标混用。
        float dist;
        if (ui->comboBox_2->currentIndex() == 0) {
            float dx = arriveTarget.x - position.x;
            float dy = arriveTarget.y - position.y;
            dist = std::sqrt(dx*dx + dy*dy);
        } else {
            float dx = arriveTarget.y - position.x;
            float dy = arriveTarget.x - position.y;
            dist = std::sqrt(dx*dx + dy*dy);
        }

        // 距离明显在缩短(>0.2mm) 视为正在运动，重置 stall 计时
        if (arriveLastDist < 0 || arriveLastDist - dist > 0.2f) {
            arriveStallSince.restart();
        }
        arriveLastDist = dist;

        // 防呆：距离 > 1mm 且已经 2 秒没缩短 → 多半是命令在网关被吞了，重发当前点
        // 最多重发 2 次，仍然不动才认输强制推进。
        if (dist > 1.0f && arriveStallSince.elapsed() > 2000 && arriveResendCount < 2) {
            ++arriveResendCount;
            qDebug() << "[ARRIVE] stall detected, resend cmd #" << arriveResendCount
                     << "idx=" << currentPathIndex << "dist=" << dist;
            arriveStallSince.restart();
            // 重发：暂存路径下标，调用一次 sendNextPoint 的命令下发逻辑会重新比 prev → p。
            // 这里直接复用：currentPathIndex 不变，临时把它降一格再调 sendNextPoint，
            // 但 sendNextPoint 会把 prev 取成 path[currentPathIndex-1]，依然准确。
            // 简化做法：直接重发 X、Y 命令，不动 currentPathIndex。
            const Point2D targetPhysical = pathPointToPhysicalTargets(arriveTarget);
            const uint32_t xMaxSpeed = axisCommandSpeed(true);
            const uint32_t yMaxSpeed = axisCommandSpeed(false);

            scanCtrl->pushsend = true;
            scanCtrl->motorId  = 0x02;
            scanCtrl->runPosintion(mmToAngleControl(targetPhysical.x), xMaxSpeed);
            scanCtrl->pushsend = false;

            QTimer::singleShot(50, this, [this, targetPhysical, yMaxSpeed]() {
                scanCtrl->pushsend = true;
                scanCtrl->motorId  = 0x01;
                scanCtrl->runPosintion(mmToAngleControl(targetPhysical.y), yMaxSpeed);
                scanCtrl->pushsend = false;
            });
            return;
        }

        // 单点 8s 兜底超时（重发 2 次都没救回来才走到这）
        if (arriveElapsed.elapsed() > 8000) {
            qDebug() << "[ARRIVE] timeout, force advance. idx=" << currentPathIndex
                     << "dist=" << dist << "resends=" << arriveResendCount;
            arriveActive = false;
            arriveTimer->stop();
            arriveTimer->deleteLater();
            arriveTimer = nullptr;
            currentPathIndex++;
            sendNextPoint();
        }
    });
    arriveTimer->start();
}


// 事件驱动的到位判定：每次拿到真实位置回包就比一次距离，命中立刻推进。
// 这样跟通讯节奏完全对齐，不会出现"等了几个 50ms 还没到位回包"的空等。
void MainWindow::checkArrival()
{
    if (!arriveActive || stopScan || !firstPosReceived) return;

    float dist;
    if (ui->comboBox_2->currentIndex() == 0) {
        float dx = arriveTarget.x - position.x;
        float dy = arriveTarget.y - position.y;
        dist = std::sqrt(dx*dx + dy*dy);
    } else {
        float dx = arriveTarget.y - position.x;
        float dy = arriveTarget.x - position.y;
        dist = std::sqrt(dx*dx + dy*dy);
    }

    if (dist < 0.5f) {
        qDebug() << "Arrived idx:" << currentPathIndex << "dist:" << dist;
        arriveActive = false;
        if (arriveTimer) {
            arriveTimer->stop();
            arriveTimer->deleteLater();
            arriveTimer = nullptr;
        }
        currentPathIndex++;
        sendNextPoint();
    }
}





void MainWindow::setStart(){

    ui->xAddBtn->setEnabled(false);

    ui->xSubBtn->setEnabled(false);

    ui->yAddBtn->setEnabled(false);

    ui->ySubBtn->setEnabled(false);

    ui->backOrigin_velocity->setEnabled(false);

    ui->jog_velocity->setEnabled(false);

    ui->setMOrigin->setEnabled(false);


    xlenght=ui->x_lenght->text().toFloat();
    ylenght=ui->y_lenght->text().toFloat();
    step=ui->y_step->text().toFloat();

    path = generateBowScanPathDense(xlenght, ylenght, step);  // 原路径生成函数
    // 保存路径
    if(path.empty()) return;

    stopScan=false;

    // 启动扫描前重置位置接收标志，强制等到一次新的真实位置回包再判到位，
    // 防止用上一次残留的 position 误判 path[0]=(0,0) 已到位。
    firstPosReceived = false;

    currentPathIndex = 0;
    sendNextPoint();


    saveseting();

}

void MainWindow::scanEnd(){

    stopScan=true;

    if(arriveTimer){
        arriveTimer->stop();
    }

    scanCtrl->on_end();

    ui->xAddBtn->setEnabled(true);

    ui->xSubBtn->setEnabled(true);

    ui->yAddBtn->setEnabled(true);

    ui->ySubBtn->setEnabled(true);

    ui->backOrigin_velocity->setEnabled(true);

    ui->jog_velocity->setEnabled(true);

    ui->setMOrigin->setEnabled(true);
}

void MainWindow::setEnd(){


    saveseting();
}



void MainWindow::setMOrigin(){


    scanCtrl->on_setOriginBtn_clicked();
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
        ui->stepSpeed->setText(settings->value("stepSpeed", "").toString());
    }

    settings->endGroup();



}
//    auto isArrivedByPos = [&](const Point2D& target)->bool {

//        Point2D curr;
//        curr.x = ui->xCurPos->text().toFloat();
//        curr.y = ui->yCurPos->text().toFloat();

//        float dx = target.x - curr.x;
//        float dy = target.y - curr.y;

//        float dist = std::sqrt(dx*dx + dy*dy);

//        constexpr float POS_THRESHOLD = 0.5f;   // 到位阈值（单位与坐标一致，例如 mm / deg）

//        return dist < POS_THRESHOLD;
//    };



//    auto path = generateBowScanPathDense(xlenght, ylenght, step);

//    for (auto& p : path) {
//        qDebug() << "Move to:" << p.x << p.y;

//        double targetMmx = p.x;  // 假设 x 是导轨位移 mm

//        double targetMmy=p.y;

//        int64_t angleControlx = mmToAngleControl(-targetMmx);
//        int64_t angleControly = mmToAngleControl(targetMmy);

//        double speedDps = ui->sweepSpeed->text().toDouble();
//        uint32_t maxSpeed = static_cast<uint32_t>(speedDps * 6000.0);


//        scanCtrl->pushsend=true;

//        scanCtrl->runPosintion(angleControlx, maxSpeed);

//        scanCtrl->pushsend=true;

//        _sleep(500);
//        //scanCtrl2->runPosintion(angleControly, maxSpeed);

//        scanCtrl->pushsend=false;


//        QElapsedTimer timer;
//           timer.start();
//           QTimer* arriveTimer = new QTimer(this);

//           connect(arriveTimer, &QTimer::timeout, this, [=]() mutable {

//               Point2D curr;
//               curr.x = ui->xCurPos->text().toFloat();
//               curr.y = ui->yCurPos->text().toFloat();

//               float dx = p.x - curr.x;
//               float dy = p.y - curr.y;
//               float dist = std::sqrt(dx*dx + dy*dy);

//               if (dist < 0.5f) {
//                   arriveTimer->stop();
//                   arriveTimer->deleteLater();
//                   qDebug() << "Arrived at target";
//                   // 触发下一步逻辑
//               }
//           });

//           arriveTimer->start(100);  // 100ms 检查一次


//    }
