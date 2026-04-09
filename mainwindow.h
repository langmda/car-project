#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QtNetwork/QTcpSocket>
#include <QtNetwork/QHostAddress>
#include <QMessageBox>
#include <QMainWindow>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDebug>
#include <QStringList>
#include <QListWidgetItem>
#include <QFileDialog>
#include <QStandardPaths>
#include <QDateTime>
#include <QFile>
#include <QTimer>

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    //功能页面切换的槽函数
    void on_CarControlButton_clicked();
    void on_SenSorDataButton_clicked();
    void on_MusicButton_clicked();
    void on_ParameteButton_clicked();
    void on_DataRecordButton_clicked();
    void on_CameraButton_clicked();
    void on_FaultButton_clicked();
    void on_TrajectoryButton_clicked();
    void on_VoiceInteractionButton_clicked();
    void on_Parameter_BackHomeButton_clicked();
    void on_CarControl_BackHomeButton_clicked();
    void on_SensorData_BackHomeButton_clicked();
    void on_Music_BackHomeButton_clicked();
    void on_DataRecord_BackHomeButton_clicked();
    void on_Camera_BackHomeButton_clicked();
    void on_Fault_BackHomeButton_clicked();
    void on_VoiceInteraction_BackHomeButton_clicked();
    void on_Trajectory_BackHomeButton_clicked();

    //小车控制的槽函数
    void on_FrontButton_clicked();
    void on_LeftButton_clicked();
    void on_RightButton_clicked();
    void on_BackButton_clicked();
    void on_SwitchModeButton_clicked();
    void on_LedButton_clicked();

    //音乐播放器的槽函数
    void on_SwitchBtn_clicked();
    void on_PrevBtn_clicked();
    void on_NextBtn_clicked();
    void on_VolumeSlider_valueChanged(int value);
    void on_MusicListWidget_itemClicked(QListWidgetItem *item);
    void on_ProgressSlider_sliderReleased();
    void on_MusicModeBtn_released();

    //参数调试页面的槽函数
    void on_UpdataParameterBtn_clicked();

    //数据记录页面的槽函数
    void on_DataRecordBtn_clicked();

    //实时画面页面的槽函数
    void on_captureBtn_clicked();
    void on_recordBtn_clicked();
    void on_selectPhotoDirBtn_clicked();
    void on_selectVideoDirBtn_clicked();

    //轨迹行走页面的槽函数

    void on_Trajectory_Square_Button_clicked();
    void on_Trajectory_Eight_Button_clicked();
    void on_Trajectory_Triangle_Button_clicked();
    void on_Trajectory_Star_Button_clicked();
    void on_Trajectory_Circle_Button_clicked();

signals:
    void newFrameReceived(const QByteArray &imageData, const QJsonArray &boxes, double timestamp);

private:
    void readTcpData();
    void receiveCameraData();
    void connect_error();
    void camera_connect_error();
    void updateMusicLabel();
    void updateMusicWidget();
    void updateMusicHighlight();
    void musicUpdata(QJsonObject musicObj);
    void sensorUpdata(QJsonObject sensorObj);
    void addEncoderData(int left, int right);
    void Sensor_Fault(int temp, int humi, int encoderLeft, int encoderRight, int piAdc, int motorAdc);

    Ui::MainWindow *ui;

    // TCP连接
    QTcpSocket *tcpSocket;
    const QString RASPI_IP = "192.168.43.67";
    const quint16 RASPI_PORT = 8888;

    // 摄像头
    QTcpSocket *cameraSocket;
    QByteArray cameraBuffer;
    int expectedFrameSize = 0;
    bool receivingFrame = false;
    QString photoSaveDir;
    QString videoSaveDir;
    bool isRecording = false;
    QFile *videoFile = nullptr;

    // 音乐相关
    QStringList musicListCache;
    int currentMusicIndex = 0;
    int is_playing = 0;
    int music_mode = 0;
    bool isManualMusicSwitch = false;
    QTimer *musicSwitchTimer;

// PID参数结构体
#pragma pack(1)
    struct PIDParams {
        uint8_t cmd;
        float Kp;
        float Ki;
        float I_Fil;
        int32_t I_Lim;
        int32_t PWM_Lim;
    };
#pragma pack()
};

#endif // MAINWINDOW_H
