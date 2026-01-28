#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QDoubleValidator>


#include "scancontroltaida.h"
#include "scancontrolhuichuan.h"

namespace Ui {
class MainWindow;
}

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

    QSettings *settings;
    int origin;
    int start;
    int end;


    int xo;
    int yo;

    int xs;
    int ys;

    int xe;
    int ye;

    int sweep;
private:
    void initWidget();
    void connectFun();
    void updateWight();
    void saveseting();
private slots:

    void updatePosition(QPointF pos);
    void updatePosition2(QPointF pos);
    void setMOrigin();

    void setOrigin();
    void setStart();
    void setEnd();

    void backOrigin_velocity();
    void jog_velocity();
    void originSpeed();
    void startSpeed();
    void endSpeed();

    void regin();


private:
    Ui::MainWindow *ui;

    ScanControlAbstract *scanCtrl;
    ScanControlAbstract *scanCtrl2;

    ScanControlHuiChuan *scanCtrlHunChuan;
    ScanControlTaiDa *scanCtrlTaiDa;

};

#endif // MAINWINDOW_H
