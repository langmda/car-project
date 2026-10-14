#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QtNetwork/QTcpSocket>
#include <QtNetwork/QHostAddress>
#include <QMessageBox>
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
#include <QThread>
#include <QImage>
#include <QElapsedTimer>
#include "mp4writer.h"
#include "cameraworker.h"

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

    //摄像头工作线程回传给主线程的槽（都在主线程执行）
    void on_videoFrame(const QByteArray &jpegData, quint64 ts_us);
    void on_cameraConnected();
    void on_cameraError(const QString &msg);
    void on_recordingStarted(const QString &path);
    void on_recordingFailed(const QString &msg);
    void on_recordingStopped();

    //轨迹行走页面的槽函数
    void on_Line_Follow_Button_clicked();
    void on_Trajectory_Z_Button_clicked();
    void on_Trajectory_J_Button_clicked();
    void on_Trajectory_U_Button_clicked();

    //语音识别页面的槽函数
    void on_VoiceInteractionSwitchBtn_clicked();

signals:
    void newFrameReceived(const QByteArray &imageData, const QJsonArray &boxes, double timestamp);
    // 主线程 → 摄像头工作线程（跨线程信号，槽在工作线程执行）
    void requestStartCamera(const QString &ip, quint16 port);
    void requestStopCamera();
    void requestStartRecording(const QString &path);
    void requestStopRecording();

private:
    void readTcpData();
    void connect_error();
    void updateMusicLabel();
    void updateMusicWidget();
    void updateMusicHighlight();
    void musicUpdata(QJsonObject musicObj);
    void sensorUpdata(QJsonObject sensorObj);
    void addEncoderData(int left, int right, float yaw);
    void Sensor_Fault(float  temp, float  humi, int encoderLeft, int encoderRight, int piAdc, int motorAdc);
    void appendVoiceResult(const QString &text, double timestamp);
    static quint8 crc8Update(quint8 crc, const QByteArray &data);
    static int findFrameHeader(const QByteArray &buf);
    void sendCommandFrame(const QByteArray &payload);
    void handleTcpPayload(const QByteArray &payload);

    Ui::MainWindow *ui;

    // TCP连接
    QTcpSocket *tcpSocket;
    const QString RASPI_IP = "192.168.43.67";
    const quint16 RASPI_PORT = 8888;
    const quint16 CAMERA_PORT = 8889;
    QByteArray tcpBuffer;

    // 摄像头（跑在独立工作线程里，主线程只负责显示/保存/录像调度）
    QThread      *m_cameraThread = nullptr;
    CameraWorker *m_cameraWorker = nullptr;
    QString photoSaveDir;
    QString videoSaveDir;
    bool isRecording = false;
    Mp4Writer *mp4Writer = nullptr;       // 由 m_cameraWorker 持有，主线程只保留指针用于释放
    bool recordingPendingStart = false;   // 用户点了"开始"但还没收到第一帧
    QString currentVideoPath;             // 待写入的 .mp4 路径
    bool cameraConnected = false;
    bool cameraOn = false;
    QImage lastFrameImage;                // 最近一帧已镜像的图像（拍照用）
    quint64 lastFrameTimeUs = 0;          // 最近一帧的采集时间戳（微秒）

    // 防回环：本地按钮刚发出的 0x0F/0x10，忽略随之广播回来的同一条命令
    QElapsedTimer localCmdTimer;
    qint64 lastCaptureCmdMs = -1000;
    qint64 lastRecordCmdMs  = -1000;

    // 音乐相关
    QStringList musicListCache;
    int currentMusicIndex = 0;
    int is_playing = 0;
    int music_mode = 0;
    bool isManualMusicSwitch = false;
    QTimer *musicSwitchTimer;

    // PID参数结构体（必须与 car.py 的 CMD_PID_PARAM=0x0E 解析方式一致：
    // cmd(1) + Kp(float,LE) + Yaw_Kp(float,LE) + PWM_Lim(int32,LE) = 13 字节）
    #pragma pack(1)
        struct PIDParams
        {
            uint8_t cmd;
            float Kp;
            float Yaw_Kp;
            int32_t PWM_Lim;
        };
    #pragma pack()

    //电池电量滤波
    float filteredPiBatPercent = 100.0f;    // 初始值设为100%避免显示异常
    float filteredMotorBatPercent = 100.0f;
    const float filterAlpha = 0.25f;        // 滤波系数，可调

    //语音交互
    bool VoiceInteractionOn = false;

    //小车运动模式
    uint8_t car_mode;
};

#endif // MAINWINDOW_H
