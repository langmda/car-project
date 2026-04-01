#include "mainwindow.h"

#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);		//创建QApplication对象
    MainWindow w;		//创建主控件对象
    w.show();			//显示主控件
    return a.exec();	//启动事件检测（让程序阻塞，检测用户操作，比如点按钮、关窗口，而不是创建窗口后立刻退出）
}
