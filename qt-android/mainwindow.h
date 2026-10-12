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
    void on_ParameterButton_clicked();
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
    void on_RightCirlcleButton_clicked();
    void on_UpdatePWMBtn_clicked();

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
    void on_DataClearBtn_clicked();

    //实时画面页面的槽函数
    void on_captureBtn_clicked();
    void on_recordBtn_clicked();
    void on_selectPhotoDirBtn_clicked();
    void on_selectVideoDirBtn_clicked();
    void on_cameraSwitchBtn_clicked();

    //轨迹行走页面的槽函数
    void on_Line_Follow_Button_clicked();
    void on_Trajectory_Z_Button_clicked();
    void on_Trajectory_J_Button_clicked();
    void on_Trajectory_U_Button_clicked();

    //语音识别页面的槽函数
    void on_VoiceInteractionSwitchBtn_clicked();

    //测试语音用
    void on_pushButton1_clicked();
    void on_pushButton2_clicked();
    void on_pushButton3_clicked();

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
    void addEncoderData(int left, int right, float yaw);
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
    bool cameraConnected = false;
    QTimer *cameraConnectTimer;
    QTimer *cameraConnectDelayTimer;
    bool cameraOn = false;

    // 音乐相关
    QStringList musicListCache;
    int currentMusicIndex = 0;
    int is_playing = 0;
    int music_mode = 0;
    bool isManualMusicSwitch = false;
    QTimer *musicSwitchTimer;

    // PID参数结构体
    #pragma pack(1)
        struct PIDParams
        {
            uint8_t cmd;
            float Kp;
            float Ki;
            int32_t I_Lim;
            int32_t PWM_Lim;
        };
    #pragma pack()

    // 轨迹行走相关
    bool Line_Follow_Mode = 0;

    //语音交互
    bool VoiceInteractionOn = false;

    //电池电量滤波
    float filteredPiBatPercent = 100.0f;    // 初始值设为100%避免显示异常
    float filteredMotorBatPercent = 100.0f;
    const float filterAlpha = 0.25f;        // 滤波系数，可调
};



#endif // MAINWINDOW_H
