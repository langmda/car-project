#include "mainwindow.h"
#include "./ui_mainwindow.h"

#include <QtCore>
#include <QTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QByteArray>
#include <QString>
#include <QBuffer>
#include <QDateTime>
#include <QFileDialog>
#include <QTextStream>
#include <QMessageBox>
#include <QTableWidgetItem>
#include <QMouseEvent>
#include <QPainter>

//手机发给树莓派的命令
#define MOVE_FORWARD 0X01
#define TURN_LEFT 0X02
#define TURN_RIGHT 0X03
#define BACK_OFF 0X04
#define CAR_MODE_SWITCH 0X05
#define LED_SWITCH 0x06
#define CUSTOMIZATION 0x18
#define PWM_UPDATE 0x19

#define SWITCH_MUSIC 0X07
#define PREV_MUSIC 0X08
#define NEXT_MUSIC 0X09
#define VOLUMN_MUSIC 0X0A
#define PLAY_SPECIFIC_MUSIC 0x0B
#define PROGRESS_MUSIC 0x0C
#define MUSIC_MODE_SWITCH 0x0D

#define PID_PARAMETER 0X0E

#define TAKE_PHOTO 0x0F
#define SWITCH_RECORD 0x10
#define CAMERA_ON 0x12
#define CAMERA_OFF 0x13

#define TRAJECTORY_Z 0x14
#define TRAJECTORY_J 0x15
#define TRAJECTORY_U 0x16

#define LINE_FOLLOW_MODE_SWITCH 0x17

#define VOICE_INTERACTION_SWITCH 0x20
//参数
#define PULSE_PER_ROUND 2600
#define WHEEL_DIAMETER 85
#define PI 3.14
#define ENCODER_TARGET_TIME 1000

#define VOL_BAT_FULL 12600
#define VOL_BAT_EMPTY 10800

//模式
#define AUTOMATIC  0x01		//自主避障模式
#define MANUAL     0x02		//手动操作模式
#define LINE_FOLLOW 0x03	//循线模式

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , car_mode(MANUAL)
{
    ui->setupUi(this);

    ui->stackedWidget->setCurrentWidget(ui->HomePage);

#ifdef Q_OS_ANDROID
    // Android 上使用公共目录
    photoSaveDir = "/storage/emulated/0/Pictures";
    videoSaveDir = "/storage/emulated/0/Pictures";
#else
    photoSaveDir = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    videoSaveDir = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
    if (photoSaveDir.isEmpty()) {
        photoSaveDir = QDir::homePath() + "/Pictures";
    }
    if (videoSaveDir.isEmpty()) {
        videoSaveDir = QDir::homePath() + "/Movies";
    }
#endif
    // 确保目录存在
    QDir().mkpath(photoSaveDir);
    QDir().mkpath(videoSaveDir);
    ui->labelPhotoDir->setText("照片目录：" + photoSaveDir);
    ui->labelVideoDir->setText("视频目录：" + videoSaveDir);

    // 连接TCP
    tcpSocket = new QTcpSocket(this);
    tcpSocket->abort();
    tcpSocket->connectToHost(QHostAddress(RASPI_IP), RASPI_PORT);
    connect(tcpSocket, &QTcpSocket::readyRead, this, &MainWindow::readTcpData);

    localCmdTimer.start();      // 用于抑制 0x0F/0x10 的命令回环

    // === 摄像头工作线程 ===
    // 帧接收、CRC 校验、JPEG 落盘（录像）全部在工作线程完成，主线程只负责显示与 UI
    m_cameraThread = new QThread();          // 不设父对象，由析构函数负责回收
    m_cameraWorker = new CameraWorker();
    m_cameraWorker->moveToThread(m_cameraThread);
    m_cameraThread->start();                 // 启动线程事件循环

    // 主线程 → 工作线程
    connect(this, &MainWindow::requestStartCamera,
            m_cameraWorker, &CameraWorker::start);
    connect(this, &MainWindow::requestStopCamera,
            m_cameraWorker, &CameraWorker::stop);
    connect(this, &MainWindow::requestStartRecording,
            m_cameraWorker, &CameraWorker::startRecording);
    connect(this, &MainWindow::requestStopRecording,
            m_cameraWorker, &CameraWorker::stopRecording);

    // 工作线程 → 主线程
    connect(m_cameraWorker, &CameraWorker::videoFrame,
            this, &MainWindow::on_videoFrame);
    connect(m_cameraWorker, &CameraWorker::connected,
            this, &MainWindow::on_cameraConnected);
    connect(m_cameraWorker, &CameraWorker::errorOccurred,
            this, &MainWindow::on_cameraError);
    connect(m_cameraWorker, &CameraWorker::recordingStarted,
            this, &MainWindow::on_recordingStarted);
    connect(m_cameraWorker, &CameraWorker::recordingFailed,
            this, &MainWindow::on_recordingFailed);
    connect(m_cameraWorker, &CameraWorker::recordingStopped,
            this, &MainWindow::on_recordingStopped);

    // 初始化音乐播放页面的定时器
    musicSwitchTimer = new QTimer(this);
    musicSwitchTimer->setSingleShot(true);
    musicSwitchTimer->setInterval(1000);
    connect(musicSwitchTimer, &QTimer::timeout, this, [&]() {
        isManualMusicSwitch = false;
    });

    // 初始化数据记录页面的TableWidget
    ui->DataRecordTableWidget->setColumnCount(4);
    QStringList headers;
    headers << "时间戳" << "左编码器值" << "右编码器值" << "航向角(°)";
    ui->DataRecordTableWidget->setHorizontalHeaderLabels(headers);
    ui->DataRecordTableWidget->setEditTriggers(QAbstractItemView::NoEditTriggers);
    ui->DataRecordTableWidget->setColumnWidth(0, 140);
    ui->DataRecordTableWidget->setColumnWidth(1, 100);
    ui->DataRecordTableWidget->setColumnWidth(2, 100);
    ui->DataRecordTableWidget->setColumnWidth(3, 100);

    // 初始化故障时间页面的TableWidget
    ui->FaultTableWidget->setColumnCount(2);
    QStringList faultHeaders;
    faultHeaders << "时间戳" << "故障事件";
    ui->FaultTableWidget->setHorizontalHeaderLabels(faultHeaders);
    ui->FaultTableWidget->setEditTriggers(QAbstractItemView::NoEditTriggers);
    ui->FaultTableWidget->setColumnWidth(0, 120);
    ui->FaultTableWidget->setColumnWidth(1, 120);

    //填入参数调试页面的QLabel值
    ui->KpLabel->setText("KP : ");
    ui->YawKpLabel->setText("YAW_KP : ");
    ui->PWMlimLabel->setText("PWM_lim : ");
}

MainWindow::~MainWindow()
{
    // 先在工作线程里收尾（停 socket、关录像文件），再退出线程并回收
    if (m_cameraThread) {
        if (m_cameraThread->isRunning()) {
            QMetaObject::invokeMethod(m_cameraWorker, "stop",
                                      Qt::BlockingQueuedConnection);
            m_cameraThread->quit();
            m_cameraThread->wait(3000);
        }
        delete m_cameraWorker;      // 已 moveToThread 出去，必须手动 delete
        delete m_cameraThread;
        m_cameraWorker = nullptr;
        m_cameraThread = nullptr;
    }
    mp4Writer = nullptr;            // 录像文件由工作线程负责关闭
    delete ui;
}

void MainWindow::connect_error()
{
    ui->FrontButton->setEnabled(false);
    ui->RightButton->setEnabled(false);
    ui->LeftButton->setEnabled(false);
    ui->BackButton->setEnabled(false);
    ui->RightCirlcleButton->setEnabled(false);

    QMessageBox::warning(this, "提示", "未连接到树莓派，请检查网络！");

    ui->FrontButton->setEnabled(true);
    ui->RightButton->setEnabled(true);
    ui->LeftButton->setEnabled(true);
    ui->BackButton->setEnabled(true);
    ui->RightCirlcleButton->setEnabled(true);

    QDateTime current = QDateTime::currentDateTime();
    QString timestamp = current.toString("yyyy-MM-dd hh:mm:ss.zzz");
    int row = ui->FaultTableWidget->rowCount();
    ui->FaultTableWidget->insertRow(row);
    ui->FaultTableWidget->setItem(row, 0, new QTableWidgetItem(timestamp));
    ui->FaultTableWidget->setItem(row, 1, new QTableWidgetItem("TCP连接错误"));

    tcpSocket->abort();
    tcpSocket->connectToHost(QHostAddress(RASPI_IP), RASPI_PORT);
    return;
}

void MainWindow::readTcpData()              //主TCP收到数据时触发，接收数据包，解析出帧体并处理
{
    tcpBuffer.append(tcpSocket->readAll());

    static constexpr int     HEAD_LEN    = 2;
    static constexpr int     LEN_LEN     = 4;
    static constexpr int     CRC_LEN     = 1;
    static constexpr quint32 MAX_PAYLOAD = 64 * 1024;

    while (tcpBuffer.size() > 0)
    {
        // 1. 找帧头
        int headerPos = findFrameHeader(tcpBuffer);
        if (headerPos < 0) {
            if (tcpBuffer.size() > 1) tcpBuffer = tcpBuffer.right(1);
            return;
        }
        if (headerPos > 0) {
            qDebug() << "🗑️ 主TCP丢弃" << headerPos << "字节垃圾";
            tcpBuffer.remove(0, headerPos);
        }

        // 2. 长度字段到齐？
        if (tcpBuffer.size() < HEAD_LEN + LEN_LEN) return;

        // 3. 解析长度（大端）
        quint32 payloadLen = qFromBigEndian<quint32>(
            reinterpret_cast<const uchar*>(tcpBuffer.constData() + HEAD_LEN));

        if (payloadLen == 0 || payloadLen > MAX_PAYLOAD) {
            qDebug() << "⚠️ 主TCP非法长度:" << payloadLen;
            tcpBuffer.remove(0, 1);
            continue;
        }

        // 4. 整帧到齐？
        int totalLen = HEAD_LEN + LEN_LEN + static_cast<int>(payloadLen) + CRC_LEN;
        if (tcpBuffer.size() < totalLen) return;

        // 5. CRC 校验
        quint8 recvCrc = static_cast<quint8>(tcpBuffer[totalLen - 1]);
        QByteArray crcRange = tcpBuffer.mid(HEAD_LEN, LEN_LEN + payloadLen);
        quint8 calcCrc = crc8Update(0x00, crcRange);

        if (recvCrc != calcCrc) {
            qDebug() << "❌ 主TCP CRC失败";
            tcpBuffer.remove(0, 1);
            continue;
        }

        // 6. 提取 payload 并分发
        QByteArray payload = tcpBuffer.mid(HEAD_LEN + LEN_LEN, payloadLen);
        tcpBuffer.remove(0, totalLen);
        handleTcpPayload(payload);
    }
}

void MainWindow::sendCommandFrame(const QByteArray &payload)        //发送数据包，帧头（0xAA 0x55）+ 负载长度 + 负载 + CRC校验位
{
    if (tcpSocket->state() != QTcpSocket::ConnectedState) {
        connect_error();
        return;
    }

    QByteArray frame;
    frame.append(static_cast<char>(0xAA));
    frame.append(static_cast<char>(0x55));

    quint32 len = static_cast<quint32>(payload.size());
    frame.append(static_cast<char>((len >> 24) & 0xFF));
    frame.append(static_cast<char>((len >> 16) & 0xFF));
    frame.append(static_cast<char>((len >> 8) & 0xFF));
    frame.append(static_cast<char>(len & 0xFF));

    frame.append(payload);

    // CRC8 覆盖 LEN(4) + PAYLOAD
    QByteArray crcRange = frame.mid(2, 4) + payload;
    quint8 crc = crc8Update(0x00, crcRange);
    frame.append(static_cast<char>(crc));

    tcpSocket->write(frame);
}

void MainWindow::handleTcpPayload(const QByteArray &payload)        //处理主TCP收到的数据包解析后的帧体
{
    if (payload.isEmpty()) return;

    quint8 first = static_cast<quint8>(payload[0]);

    // 单字节命令
    // 注意：本机按钮发出的 0x0F/0x10 会被 car.py 广播回所有客户端（包括自己），
    // 这里用 500ms 抑制窗口避免"点一次、切两次"的回环。
    if (first == 0x0F) {
        if (localCmdTimer.elapsed() - lastCaptureCmdMs < 500) {
            qDebug() << "↩️ 忽略本机拍照命令回环";
            return;
        }
        qDebug() << "📸 收到拍照命令";
        on_captureBtn_clicked();
        return;
    }
    if (first == 0x10) {
        if (localCmdTimer.elapsed() - lastRecordCmdMs < 500) {
            qDebug() << "↩️ 忽略本机录像命令回环";
            return;
        }
        qDebug() << "🎥 收到录像切换命令";
        on_recordBtn_clicked();
        return;
    }

    // JSON 消息（首字节是 '{'）
    if (first == '{') {
        QJsonParseError err;
        QJsonDocument doc = QJsonDocument::fromJson(payload, &err);
        if (err.error != QJsonParseError::NoError) {
            qDebug() << "❌ JSON解析失败:" << err.errorString();
            return;
        }
        QJsonObject obj = doc.object();
        QString msgType = obj["type"].toString();

        if (msgType == "music_list") {
            QJsonArray musicArray = obj["data"].toArray();
            musicListCache.clear();
            for (int i = 0; i < musicArray.size(); i++)
                musicListCache.append(musicArray[i].toString());
            if (musicListCache.isEmpty()) {
                QMessageBox::warning(this, "提示", "未检测到音乐文件缓存");
                return;
            }
            updateMusicWidget();
            updateMusicLabel();
            updateMusicHighlight();
        }
        else if (msgType == "sensor_data") {
            sensorUpdata(obj["sensor"].toObject());
        }
        else if (msgType == "music_status") {
            musicUpdata(obj["music"].toObject());
        }
        else if (msgType == "voice_result") {
            QJsonObject dataObj = obj["data"].toObject();
            appendVoiceResult(dataObj["text"].toString(),
                              dataObj["timestamp"].toDouble());
        }
    }
}
/***********功能页面切换按钮的函数***********/
void MainWindow::on_CarControlButton_clicked() { ui->stackedWidget->setCurrentWidget(ui->CarControlPage); }
void MainWindow::on_SenSorDataButton_clicked() { ui->stackedWidget->setCurrentWidget(ui->SensorDataPage); }
void MainWindow::on_MusicButton_clicked() { ui->stackedWidget->setCurrentWidget(ui->MusicPage); }
void MainWindow::on_ParameterButton_clicked() { ui->stackedWidget->setCurrentWidget(ui->ParameterPage); }
void MainWindow::on_DataRecordButton_clicked() { ui->stackedWidget->setCurrentWidget(ui->DataRecordPage); }
void MainWindow::on_FaultButton_clicked() { ui->stackedWidget->setCurrentWidget(ui->FaultPage); }
void MainWindow::on_VoiceInteractionButton_clicked() { ui->stackedWidget->setCurrentWidget(ui->VoiceInteractionPage); }
void MainWindow::on_CameraButton_clicked() { ui->stackedWidget->setCurrentWidget(ui->CameraPage); }
void MainWindow::on_TrajectoryButton_clicked(){ ui->stackedWidget->setCurrentWidget(ui->TrajectoryPage); }


void MainWindow::on_CarControl_BackHomeButton_clicked() { ui->stackedWidget->setCurrentWidget(ui->HomePage); }
void MainWindow::on_SensorData_BackHomeButton_clicked() { ui->stackedWidget->setCurrentWidget(ui->HomePage); }
void MainWindow::on_Music_BackHomeButton_clicked() { ui->stackedWidget->setCurrentWidget(ui->HomePage); }
void MainWindow::on_Parameter_BackHomeButton_clicked() { ui->stackedWidget->setCurrentWidget(ui->HomePage); }
void MainWindow::on_DataRecord_BackHomeButton_clicked() { ui->stackedWidget->setCurrentWidget(ui->HomePage); }
void MainWindow::on_Fault_BackHomeButton_clicked() { ui->stackedWidget->setCurrentWidget(ui->HomePage); }
void MainWindow::on_VoiceInteraction_BackHomeButton_clicked() { ui->stackedWidget->setCurrentWidget(ui->HomePage); }
void MainWindow::on_Camera_BackHomeButton_clicked() { ui->stackedWidget->setCurrentWidget(ui->HomePage); }
void MainWindow::on_Trajectory_BackHomeButton_clicked() { ui->stackedWidget->setCurrentWidget(ui->HomePage);  }
/***********功能页面切换按钮的函数***********/


/***********小车控制的函数***********/
void MainWindow::on_FrontButton_clicked()
{
    if(tcpSocket->state() != QTcpSocket::ConnectedState) {connect_error();}
    quint8 cmd = MOVE_FORWARD;
    sendCommandFrame(QByteArray(1, static_cast<char>(cmd)));
}
void MainWindow::on_LeftButton_clicked()
{
    if(tcpSocket->state() != QTcpSocket::ConnectedState)
    {   connect_error();
        return;
    }

    quint8 cmd = TURN_LEFT;
    sendCommandFrame(QByteArray(1, static_cast<char>(cmd)));
}
void MainWindow::on_RightButton_clicked()
{
    if(tcpSocket->state() != QTcpSocket::ConnectedState)
    {   connect_error();
        return;
    }

    quint8 cmd = TURN_RIGHT;
    sendCommandFrame(QByteArray(1, static_cast<char>(cmd)));
}
void MainWindow::on_BackButton_clicked()
{
    if(tcpSocket->state() != QTcpSocket::ConnectedState)
    {   connect_error();
        return;
    }

    quint8 cmd = BACK_OFF;
    sendCommandFrame(QByteArray(1, static_cast<char>(cmd)));
}
void MainWindow::on_SwitchModeButton_clicked()
{
    ui->SwitchModeButton->setEnabled(false);
    if(tcpSocket->state() != QTcpSocket::ConnectedState)
    {
        connect_error();
        ui->SwitchModeButton->setEnabled(true);
        return;
    }
    ui->SwitchModeButton->setEnabled(true);

    if (car_mode == MANUAL) {
        car_mode = AUTOMATIC;
        ui->CarModeLabel->setText("当前模式：自主避障");
        ui->Line_Follow_Button->setText("开启");
    }
    else {   // AUTOMATIC 或 LINE_FOLLOW → 切到 MANUAL
        car_mode = MANUAL;
        ui->CarModeLabel->setText("当前模式：手动操控");
        ui->Line_Follow_Button->setText("开启");
    }

    quint8 cmd = CAR_MODE_SWITCH;
    sendCommandFrame(QByteArray(1, static_cast<char>(cmd)));
}
void MainWindow::on_LedButton_clicked()
{
    if(tcpSocket->state() != QTcpSocket::ConnectedState) {connect_error();return;}
    quint8 cmd = LED_SWITCH;
    sendCommandFrame(QByteArray(1, static_cast<char>(cmd)));
}
void MainWindow::on_RightCirlcleButton_clicked()
{
    if(tcpSocket->state() != QTcpSocket::ConnectedState) {connect_error();return;}
    quint8 cmd = CUSTOMIZATION;
    sendCommandFrame(QByteArray(1, static_cast<char>(cmd)));
}
void MainWindow::on_UpdatePWMBtn_clicked()
{
    // 1. 检查网络连接状态
    if(tcpSocket->state() != QTcpSocket::ConnectedState)
    {
        connect_error();
        return;
    }

    // 2. 读取并验证RightPWM值
    int32_t rightPWM = 0;
    bool rightOk = false;

    if (!ui->RightPWMlineEdit->text().isEmpty()) {
        QString rightText = ui->RightPWMlineEdit->text().trimmed();
        int tempValue = rightText.toInt(&rightOk);

        if (!rightOk) {
            // 尝试转换为浮点数再取整
            bool isFloat = false;
            float floatValue = rightText.toFloat(&isFloat);
            if (isFloat) {
                rightPWM = static_cast<int32_t>(floatValue);
                rightOk = true;
                QMessageBox::information(this, "提示", QString("右PWM已从浮点数转换为整数：%1").arg(rightPWM));
            } else {
                QMessageBox::warning(this, "提示", "右PWM格式错误，请输入有效的数字（如：3600）");
                return;
            }
        } else {
            rightPWM = static_cast<int32_t>(tempValue);
        }
    } else {
        rightPWM = 3600; // 如果输入框为空，使用默认值
    }

    // 3. 读取并验证LeftPWM值
    int32_t leftPWM = 0;
    bool leftOk = false;

    if (!ui->LeftPWMlineEdit->text().isEmpty()) {
        QString leftText = ui->LeftPWMlineEdit->text().trimmed();
        int tempValue = leftText.toInt(&leftOk);

        if (!leftOk) {
            // 尝试转换为浮点数再取整
            bool isFloat = false;
            float floatValue = leftText.toFloat(&isFloat);
            if (isFloat) {
                leftPWM = static_cast<int32_t>(floatValue);
                leftOk = true;
                QMessageBox::information(this, "提示", QString("左PWM已从浮点数转换为整数：%1").arg(leftPWM));
            } else {
                QMessageBox::warning(this, "提示", "左PWM格式错误，请输入有效的数字（如：3600）");
                return;
            }
        } else {
            leftPWM = static_cast<int32_t>(tempValue);
        }
    } else {
        leftPWM = 3600; // 如果输入框为空，使用默认值
    }

    // 4. 数值范围检查（0-7200）
    if (rightPWM < 0 || rightPWM > 7200) {
        QMessageBox::warning(this, "提示", QString("右PWM值必须在0到7200之间，当前值：%1").arg(rightPWM));
        return;
    }
    if (leftPWM < 0 || leftPWM > 7200) {
        QMessageBox::warning(this, "提示", QString("左PWM值必须在0到7200之间，当前值：%1").arg(leftPWM));
        return;
    }

    // 5. 手动构造字节数组发送（确保小端模式，与STM32兼容）
    QByteArray payload;
    payload.append(static_cast<char>(PWM_UPDATE));
    payload.append(static_cast<char>(rightPWM & 0xFF));
    payload.append(static_cast<char>((rightPWM >> 8) & 0xFF));
    payload.append(static_cast<char>((rightPWM >> 16) & 0xFF));
    payload.append(static_cast<char>((rightPWM >> 24) & 0xFF));
    payload.append(static_cast<char>(leftPWM & 0xFF));
    payload.append(static_cast<char>((leftPWM >> 8) & 0xFF));
    payload.append(static_cast<char>((leftPWM >> 16) & 0xFF));
    payload.append(static_cast<char>((leftPWM >> 24) & 0xFF));
    sendCommandFrame(payload);

    // 6. 清空输入框
    ui->RightPWMlineEdit->clear();
    ui->LeftPWMlineEdit->clear();

    // 7. 提示发送成功
    QMessageBox::information(this, "提示", QString("PWM值已发送\n右PWM：%1\n左PWM：%2").arg(rightPWM).arg(leftPWM));
}
/***********小车控制的函数***********/


/***********音乐播放器的函数***********/
void MainWindow::on_SwitchBtn_clicked()
{
    if(tcpSocket->state() != QTcpSocket::ConnectedState) {connect_error(); return;}
    quint8 cmd = SWITCH_MUSIC;
    sendCommandFrame(QByteArray(1, static_cast<char>(cmd)));
    if (is_playing == 0)
    {
        ui->SwitchBtn->setIcon(QIcon(":/image/暂停.png"));
        is_playing = 1;
    }
    else
    {
        ui->SwitchBtn->setIcon(QIcon(":/image/播放.png"));
        is_playing = 0;
    }
}
void MainWindow::on_PrevBtn_clicked()
{
    if(tcpSocket->state() != QTcpSocket::ConnectedState) {connect_error();return;}
    if (musicListCache.isEmpty())
    {
        QMessageBox::warning(this, "提示", "未检测到音乐文件缓存");
        return;
    }
    currentMusicIndex--;
    if (currentMusicIndex < 0) {currentMusicIndex = musicListCache.size() - 1;}
    isManualMusicSwitch = true;
    musicSwitchTimer->start();
    quint8 cmd = PREV_MUSIC;
    sendCommandFrame(QByteArray(1, static_cast<char>(cmd)));
    updateMusicLabel();
    updateMusicHighlight();
    ui->SwitchBtn->setIcon(QIcon(":/image/暂停.png"));
    is_playing = 1;
}
void MainWindow::on_NextBtn_clicked()
{
    if(tcpSocket->state() != QTcpSocket::ConnectedState) {connect_error();return;}
    if (musicListCache.isEmpty())
    {
        QMessageBox::warning(this, "提示", "未检测到音乐文件缓存");
        return;
    }
    currentMusicIndex++;
    if (currentMusicIndex >= musicListCache.size()) {currentMusicIndex = 0;}
    isManualMusicSwitch = true;
    musicSwitchTimer->start();
    quint8 cmd = NEXT_MUSIC;
    sendCommandFrame(QByteArray(1, static_cast<char>(cmd)));
    updateMusicLabel();
    updateMusicHighlight();
    ui->SwitchBtn->setIcon(QIcon(":/image/暂停.png"));
    is_playing = 1;
}
void MainWindow::on_VolumeSlider_valueChanged(int value)
{
    QByteArray payload;
    payload.append(static_cast<char>(VOLUMN_MUSIC));
    payload.append(static_cast<char>(value));
    sendCommandFrame(payload);
}
void MainWindow::on_MusicListWidget_itemClicked(QListWidgetItem *item)
{
    if(tcpSocket->state() != QTcpSocket::ConnectedState) {connect_error();return;}
    if (!item) return;
    currentMusicIndex = ui->MusicListWidget->row(item);
    isManualMusicSwitch = true;
    musicSwitchTimer->start();
    updateMusicLabel();
    updateMusicHighlight();
    QByteArray payload;
    payload.append(static_cast<char>(PLAY_SPECIFIC_MUSIC));
    payload.append(static_cast<char>(currentMusicIndex));
    sendCommandFrame(payload);
    ui->SwitchBtn->setIcon(QIcon(":/image/暂停.png"));
    is_playing = 1;
}
void MainWindow::on_ProgressSlider_sliderReleased()
{
    if(tcpSocket->state() != QTcpSocket::ConnectedState) {connect_error();return;}
    int value = ui->ProgressSlider->value();
    QByteArray payload;
    payload.append(static_cast<char>(PROGRESS_MUSIC));
    payload.append(static_cast<char>(value));
    sendCommandFrame(payload);
}
void MainWindow::on_MusicModeBtn_released()
{
    if(tcpSocket->state() != QTcpSocket::ConnectedState) {connect_error(); return;}
    quint8 cmd = MUSIC_MODE_SWITCH;
    sendCommandFrame(QByteArray(1, static_cast<char>(cmd)));
    if(music_mode == 0)
    {
        ui->MusicModeBtn->setText("单曲循环");
        music_mode = 1;
    }
    else if(music_mode == 1)
    {
        ui->MusicModeBtn->setText("随机播放");
        music_mode = 2;
    }
    else if(music_mode == 2)
    {
        ui->MusicModeBtn->setText("列表循环");
        music_mode = 0;
    }
}
void MainWindow::updateMusicLabel()
{
    currentMusicIndex = qBound(0, currentMusicIndex, musicListCache.size() - 1);
    QString showText = QString("当前播放：%1").arg(musicListCache[currentMusicIndex]);
    ui->MusicLabel->setText(showText);
}
void MainWindow::updateMusicWidget()
{
    ui->MusicListWidget->clear();
    ui->MusicListWidget->addItems(musicListCache);
}
void MainWindow::updateMusicHighlight()
{
    if (musicListCache.isEmpty() || currentMusicIndex < 0 || currentMusicIndex >= musicListCache.size())
    {
        ui->MusicListWidget->clearSelection();
        return;
    }
    QListWidgetItem *targetItem = ui->MusicListWidget->item(currentMusicIndex);
    if (targetItem)
    {
        ui->MusicListWidget->setCurrentItem(targetItem);
        targetItem->setSelected(true);
    }
}
void MainWindow::musicUpdata(QJsonObject musicObj)
{
    bool playing = musicObj["music_is_playing"].toBool();
    if (playing)
    {
        ui->SwitchBtn->setIcon(QIcon(":/image/暂停.png"));
    } else
    {
        ui->SwitchBtn->setIcon(QIcon(":/image/播放.png"));
    }
    is_playing = playing ? 1 : 0;
    int position = musicObj["position"].toInt();
    int length = musicObj["length"].toInt();
    int percent = musicObj["percent"].toInt();
    int remoteIndex = musicObj["current_index"].toInt();

    if (isManualMusicSwitch && remoteIndex == currentMusicIndex)
    {
        isManualMusicSwitch = false;
        musicSwitchTimer->stop();
    }
    else
    {
        currentMusicIndex = remoteIndex;
    }

    if (!ui->ProgressSlider->isSliderDown())
    {ui->ProgressSlider->setValue(percent);}

    int current_minute = position / 1000 / 60;
    int current_seconds = (position / 1000) % 60;
    int total_minute = length /1000 /60;
    int total_secondes = (length /1000) %60;
    QString showText = QString("%1:%2/%3:%4")
                           .arg(current_minute, 1, 10, QChar('0'))
                           .arg(current_seconds, 2, 10, QChar('0'))
                           .arg(total_minute, 1, 10, QChar('0'))
                           .arg(total_secondes, 2, 10, QChar('0'));
    ui->progressLabel->setText(showText);

    if (!isManualMusicSwitch)
    {
        updateMusicLabel();
        updateMusicHighlight();
    }
}
/***********音乐播放器的函数***********/


/*************传感器的函数************/
void MainWindow::sensorUpdata(QJsonObject sensorObj)
{
    float temp = sensorObj["temp"].toDouble();
    float humi = sensorObj["humi"].toDouble();
    int encoderLeft = sensorObj["encoder_left_cnt"].toInt();
    int encoderRight = sensorObj["encoder_right_cnt"].toInt();
    int piAdc = sensorObj["pi_bat_voltage"].toInt();
    int motorAdc = sensorObj["motor_bat_voltage"].toInt();
    float yaw = static_cast<float>(sensorObj["yaw"].toDouble());
    int mode = sensorObj["mode"].toInt();

    addEncoderData(encoderLeft, encoderRight, yaw);  // 传入 yaw
    Sensor_Fault(temp, humi, encoderLeft, encoderRight, piAdc, motorAdc);

    float wheel_circum = PI * WHEEL_DIAMETER;
    float speed_left = ((float)encoderLeft / PULSE_PER_ROUND) * wheel_circum;
    float speed_right = ((float)encoderRight / PULSE_PER_ROUND) * wheel_circum;
    float speed = (speed_left + speed_right)/2;

    float piVol_mV = (static_cast<float>(piAdc) / 4096.0f) * 3.3f * (133.0f/33.0f) * 1000.0f + 100;
    float motorVol_mV = (static_cast<float>(motorAdc) / 4096.0f) * 3.3f * (133.0f/33.0f) * 1000.0f + 100;

    float piBatPercent = (piVol_mV - VOL_BAT_EMPTY) / (float)(VOL_BAT_FULL - VOL_BAT_EMPTY) * 100.0f;
    piBatPercent = qBound(0.0f, piBatPercent, 100.0f);
    float motorBatPercent = (motorVol_mV - VOL_BAT_EMPTY) / (float)(VOL_BAT_FULL - VOL_BAT_EMPTY) * 100.0f;
    motorBatPercent = qBound(0.0f, motorBatPercent, 100.0f);

    // 一阶低通滤波，抑制电量读数跳动
    filteredPiBatPercent    = filterAlpha * piBatPercent    + (1.0f - filterAlpha) * filteredPiBatPercent;
    filteredMotorBatPercent = filterAlpha * motorBatPercent + (1.0f - filterAlpha) * filteredMotorBatPercent;

    ui->TempLabel->setText(QString("温度：%1℃").arg(temp));
    ui->HumiLabel->setText(QString("湿度：%1%").arg(humi));
    ui->SpeedLabel->setText(QString("车速：%1 mm/s").arg(speed, 0, 'f', 1));
    ui->piBatLabel->setText(QString("树莓派电池电量：%1%").arg(filteredPiBatPercent, 0, 'f', 1));
    ui->MotorBatLabel->setText(QString("电机电池电量：%1%").arg(filteredMotorBatPercent, 0, 'f', 1));

    QString modeText;
    switch (mode)
    {
        case AUTOMATIC:
            modeText = "当前模式：自主避障";
            car_mode = AUTOMATIC;
            ui->Line_Follow_Button->setText("开启");
            break;
        case MANUAL:
            modeText = "当前模式：手动操控";
            car_mode = MANUAL;
            ui->Line_Follow_Button->setText("开启");
            break;
        case LINE_FOLLOW:
            modeText = "当前模式：红外循迹";
            car_mode = LINE_FOLLOW;
            ui->Line_Follow_Button->setText("关闭");
            break;
        default:
            return;
    }
    ui->CarModeLabel->setText(modeText);
}
/*************传感器的函数************/


/*************参数调试的函数************/
void MainWindow::on_UpdataParameterBtn_clicked()
{
    if(tcpSocket->state() != QTcpSocket::ConnectedState)
    {
        connect_error();
        return;
    }

    // 1. 从Label中读取当前参数值（作为默认值）
    float currentKp = 0.0f, currentYawKp = 0.0f;
    int32_t currentPWMLim = 0;
    bool ok;

    // 解析 KpLabel 文本，格式如 "KP : 0.01"
    QString kpText = ui->KpLabel->text().remove("KP : ");
    currentKp = kpText.toFloat(&ok);
    if (!ok) currentKp = 0.0f;

    QString yawkpText = ui->YawKpLabel->text().remove("YAW_KP : ");
    currentYawKp = yawkpText.toFloat(&ok);
    if (!ok) currentYawKp = 0.0f;

    QString pwmText = ui->PWMlimLabel->text().remove("PWM_lim : ");
    currentPWMLim = pwmText.toInt(&ok);
    if (!ok) currentPWMLim = 0;

    // 2. 处理Kp输入（float类型）
    if (!ui->KpLineEdit->text().isEmpty()) {
        QString kpInput = ui->KpLineEdit->text();
        bool isFloat = false;
        float tempValue = kpInput.toFloat(&isFloat);
        if (isFloat) {
            currentKp = tempValue;
        } else {
            QMessageBox::warning(this, "提示", "Kp格式错误，请输入有效的数字（如：0.5 或 1）");
            return;
        }
    }

    // 3. 处理YawKp输入（float类型）
    if (!ui->YawKpLineEdit->text().isEmpty()) {
        QString yawkpInput = ui->YawKpLineEdit->text();
        bool isFloat = false;
        float tempValue = yawkpInput.toFloat(&isFloat);
        if (isFloat) {
            currentYawKp = tempValue;
        } else {
            QMessageBox::warning(this, "提示", "YawKp格式错误，请输入有效的数字（如：0.1 或 2）");
            return;
        }
    }

    // 4. 处理PWM_Lim输入（int32_t类型）
    if (!ui->PWMlimLineEdit->text().isEmpty()) {
        QString pwmInput = ui->PWMlimLineEdit->text();
        bool isInt = false;
        int tempValue = pwmInput.toInt(&isInt);
        if (isInt) {
            currentPWMLim = static_cast<int32_t>(tempValue);
        } else {
            // 尝试转换为浮点数再取整
            bool isFloat = false;
            float floatValue = pwmInput.toFloat(&isFloat);
            if (isFloat) {
                currentPWMLim = static_cast<int32_t>(floatValue);
                QMessageBox::information(this, "提示", QString("PWM_Lim已从浮点数转换为整数：%1").arg(currentPWMLim));
            } else {
                QMessageBox::warning(this, "提示", "PWM_Lim格式错误，请输入有效的整数（如：4320）");
                return;
            }
        }
    }

    // 5. 可选：添加数值范围检查
    if (currentKp < 0) {
        QMessageBox::warning(this, "提示", "Kp不能为负数");
        return;
    }
    if (currentYawKp < 0) {
        QMessageBox::warning(this, "提示", "YawKp不能为负数");
        return;
    }
    if (currentPWMLim < 0) {
        QMessageBox::warning(this, "提示", "PWM_Lim不能为负数");
        return;
    }

    // 6. 打包发送
    PIDParams params;
    params.cmd = PID_PARAMETER;
    params.Kp = currentKp;
    params.Yaw_Kp = currentYawKp;
    params.PWM_Lim = currentPWMLim;

    QByteArray payload(reinterpret_cast<const char*>(&params), sizeof(PIDParams));
    sendCommandFrame(payload);

    // 7. 更新Label显示新值
    ui->KpLabel->setText(QString("KP : %1").arg(params.Kp, 0, 'f', 1));
    ui->YawKpLabel->setText(QString("Yaw_Kp : %1").arg(params.Yaw_Kp, 0, 'f', 1));
    ui->PWMlimLabel->setText(QString("PWM_lim : %1").arg(params.PWM_Lim));

    // 8. 清空输入框（可选）
    ui->KpLineEdit->clear();
    ui->YawKpLineEdit->clear();
    ui->PWMlimLineEdit->clear();

    QMessageBox::information(this, "提示", "参数已发送并更新");
}
/*************参数调试的函数************/


/*************数据记录的函数************/
void MainWindow::on_DataRecordBtn_clicked()
{
    QString fileName = QFileDialog::getSaveFileName(this, "导出数据", "", "CSV文件 (*.csv)");
    if (fileName.isEmpty())
        return;

    QFile file(fileName);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        QMessageBox::warning(this, "提示", "无法创建文件");
        return;
    }

    QTextStream out(&file);
    int colCount = ui->DataRecordTableWidget->columnCount();
    QStringList headers;
    for (int c = 0; c < colCount; ++c)
        headers << ui->DataRecordTableWidget->horizontalHeaderItem(c)->text();
    out << headers.join(",") << "\n";

    int rowCount = ui->DataRecordTableWidget->rowCount();
    for (int r = 0; r < rowCount; ++r)
    {
        QStringList rowData;
        for (int c = 0; c < colCount; ++c)
        {
            QTableWidgetItem* item = ui->DataRecordTableWidget->item(r, c);
            rowData << (item ? item->text() : "");
        }
        out << rowData.join(",") << "\n";
    }

    file.close();
    QMessageBox::information(this, "提示", "导出成功");
}
void MainWindow::addEncoderData(int left, int right, float yaw)
{
    QDateTime current = QDateTime::currentDateTime();
    QString timestamp = current.toString("yyyy-MM-dd hh:mm:ss.zzz");

    int row = ui->DataRecordTableWidget->rowCount();
    ui->DataRecordTableWidget->insertRow(row);
    ui->DataRecordTableWidget->setItem(row, 0, new QTableWidgetItem(timestamp));
    ui->DataRecordTableWidget->setItem(row, 1, new QTableWidgetItem(QString::number(left)));
    ui->DataRecordTableWidget->setItem(row, 2, new QTableWidgetItem(QString::number(right)));
    ui->DataRecordTableWidget->setItem(row, 3, new QTableWidgetItem(QString::number(yaw, 'f', 1))); // 保留一位小数
}
void MainWindow::on_DataClearBtn_clicked()
{
    ui->DataRecordTableWidget->clearContents();
    ui->DataRecordTableWidget->setRowCount(0);
    QMessageBox::information(this, "提示", "数据已清空");
}
/*************数据记录的函数************/


/*************实时画面的函数************/
// 说明：摄像头 socket、帧拆包、CRC 校验、JPEG 落盘都在 CameraWorker（工作线程）中完成，
//       主线程只负责显示、保存照片和录像调度，UI 不会被视频流阻塞。
void MainWindow::on_captureBtn_clicked()
{
    if (!cameraOn) {
        QMessageBox::warning(this, "提示", "请先开启摄像头");
        return;
    }
    if (photoSaveDir.isEmpty()) {
        QMessageBox::warning(this, "提示", "请先选择照片保存目录");
        return;
    }
    if (lastFrameImage.isNull()) {
        QMessageBox::warning(this, "提示", "还没有收到摄像头画面");
        return;
    }

    QDir dir(photoSaveDir);
    if (!dir.exists() && !dir.mkpath(".")) {
        QMessageBox::warning(this, "提示", "无法创建目录：" + photoSaveDir);
        return;
    }

    QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss_zzz");
    QString fileName = QString("%1/photo_%2.jpg").arg(photoSaveDir).arg(timestamp);

    // 保存原始分辨率、已镜像的图像
    if (lastFrameImage.save(fileName, "JPEG", 95)) {
        QMessageBox::information(this, "提示", "照片已保存至：\n" + fileName);
    } else {
        QMessageBox::warning(this, "提示", "保存失败，请检查目录权限或存储空间");
    }
}

void MainWindow::on_recordBtn_clicked()
{
    if (!cameraOn) {
        QMessageBox::warning(this, "提示", "请先开启摄像头");
        return;
    }

    if (!isRecording)
    {
        /* ---------- 开始录像 ---------- */
        if (videoSaveDir.isEmpty()) {
            QMessageBox::warning(this, "提示", "请先选择视频保存目录");
            return;
        }
        QDir dir(videoSaveDir);
        if (!dir.exists() && !dir.mkpath(".")) {
            QMessageBox::warning(this, "提示", "无法创建目录：" + videoSaveDir + "\n请检查存储权限");
            return;
        }

        QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss");
        currentVideoPath = QString("%1/rec_%2.mp4").arg(videoSaveDir).arg(timestamp);

        recordingPendingStart = true;
        isRecording = true;
        emit requestStartRecording(currentVideoPath);   // 交给工作线程，收到第一帧后建文件

        ui->recordBtn->setText("停止录像");
        QMessageBox::information(this, "提示", "开始录像，文件将保存在：\n" + currentVideoPath);
    }
    else
    {
        /* ---------- 停止录像 ---------- */
        emit requestStopRecording();                    // 由工作线程关闭并写完 moov
        isRecording = false;
        recordingPendingStart = false;
        ui->recordBtn->setText("开始录像");
        QMessageBox::information(this, "提示", "录像已停止");
    }
}

void MainWindow::on_selectPhotoDirBtn_clicked()
{
    QString dir = QFileDialog::getExistingDirectory(this, "选择照片保存目录", photoSaveDir);
    if (!dir.isEmpty()) {
        photoSaveDir = dir;
        ui->labelPhotoDir->setText("照片目录：" + dir);
        QMessageBox::information(this, "提示", "照片保存目录已设置为：" + dir);
    }
}

void MainWindow::on_selectVideoDirBtn_clicked()
{
    QString dir = QFileDialog::getExistingDirectory(this, "选择视频保存目录", videoSaveDir);
    if (!dir.isEmpty()) {
        videoSaveDir = dir;
        ui->labelVideoDir->setText("视频目录：" + dir);
        QMessageBox::information(this, "提示", "视频保存目录已设置为：" + dir);
    }
}

void MainWindow::on_cameraSwitchBtn_clicked()
{
    ui->cameraSwitchBtn->setEnabled(false);

    if (tcpSocket->state() != QTcpSocket::ConnectedState) {
        connect_error();
        ui->cameraSwitchBtn->setEnabled(true);
        return;
    }
    ui->cameraSwitchBtn->setEnabled(true);

    if (!cameraOn)
    {
        /* ---------- 开启摄像头 ---------- */
        cameraConnected = false;

        // ① 通知树莓派打开摄像头（car.py 收到 0x12 后 camera_enabled.set() 并开始监听 8889）
        quint8 cmd = CAMERA_ON;
        sendCommandFrame(QByteArray(1, static_cast<char>(cmd)));

        // ② 让工作线程延迟 200ms 再连接，给树莓派的摄像头线程留出初始化时间
        emit requestStartCamera(RASPI_IP, CAMERA_PORT);

        ui->cameraSwitchBtn->setText("关闭摄像头");
        cameraOn = true;
    }
    else
    {
        /* ---------- 关闭摄像头 ---------- */
        quint8 cmd = CAMERA_OFF;
        sendCommandFrame(QByteArray(1, static_cast<char>(cmd)));

        emit requestStopCamera();

        if (isRecording) {                     // 关摄像头前先收尾录像，避免 mdat 没写完
            isRecording = false;
            recordingPendingStart = false;
            ui->recordBtn->setText("开始录像");
        }

        cameraConnected = false;
        cameraOn = false;
        ui->cameraSwitchBtn->setText("开启摄像头");
        ui->videoLabel->clear();
        lastFrameImage = QImage();
    }
}

// 工作线程每收到一帧就发一次（原始 JPEG + 采集时间戳）
void MainWindow::on_videoFrame(const QByteArray &jpegData, quint64 ts_us)
{
    QImage img;
    if (!img.loadFromData(jpegData, "JPEG")) {
        qDebug() << "JPEG 解码失败，帧大小:" << jpegData.size();
        return;
    }
    QImage rotated = img.mirrored(true, true);
    lastFrameImage   = rotated;      // 拍照与录像尺寸检查用
    lastFrameTimeUs  = ts_us;

    if (ui->stackedWidget->currentWidget() != ui->CameraPage) return;   // 没在实时画面页就不刷 UI

    ui->videoLabel->setPixmap(QPixmap::fromImage(rotated).scaled(
        ui->videoLabel->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

void MainWindow::on_cameraConnected()
{
    qDebug() << "✅ 摄像头TCP连接成功";
    cameraConnected = true;
}

void MainWindow::on_cameraError(const QString &msg)
{
    qDebug() << "❌ 摄像头连接错误:" << msg;

    QDateTime current = QDateTime::currentDateTime();
    QString timestamp = current.toString("yyyy-MM-dd hh:mm:ss.zzz");
    int row = ui->FaultTableWidget->rowCount();
    ui->FaultTableWidget->insertRow(row);
    ui->FaultTableWidget->setItem(row, 0, new QTableWidgetItem(timestamp));
    ui->FaultTableWidget->setItem(row, 1, new QTableWidgetItem("摄像头TCP连接错误"));

    if (cameraOn) {
        QMessageBox::warning(this, "提示", "摄像头连接失败：" + msg);
        cameraOn = false;
        cameraConnected = false;
        ui->cameraSwitchBtn->setText("开启摄像头");
        ui->videoLabel->clear();
    }
}

void MainWindow::on_recordingStarted(const QString &path)
{
    recordingPendingStart = false;
    qDebug() << "🎥 录像中：" << path;
}

void MainWindow::on_recordingFailed(const QString &msg)
{
    isRecording = false;
    recordingPendingStart = false;
    ui->recordBtn->setText("开始录像");
    QMessageBox::warning(this, "提示", "录像失败：" + msg);
}

void MainWindow::on_recordingStopped()
{
    isRecording = false;
    recordingPendingStart = false;
    ui->recordBtn->setText("开始录像");
}
/*************实时画面的函数************/


/*************数据包协议工具函数************/
quint8 MainWindow::crc8Update(quint8 crc, const QByteArray &data)   //CRC8: poly=0x07, init=0x00（与 car.py 的 crc8_update 一致）
{
    for (char ch : data) {
        crc ^= static_cast<quint8>(ch);
        for (int i = 0; i < 8; ++i) {
            if (crc & 0x80)
                crc = static_cast<quint8>((crc << 1) ^ 0x07);
            else
                crc = static_cast<quint8>(crc << 1);
        }
    }
    return crc;
}

int MainWindow::findFrameHeader(const QByteArray &buf)              //寻找帧头（AA 55）在缓存区中的位置
{
    const int n = buf.size();
    for (int i = 0; i + 1 < n; ++i) {
        if (static_cast<quint8>(buf[i])     == 0xAA &&
            static_cast<quint8>(buf[i + 1]) == 0x55) {
            return i;
        }
    }
    return -1;
}
/*************数据包协议工具函数************/


/*************故障诊断的函数************/
void MainWindow::Sensor_Fault(float temp, float humi, int encoderLeft, int encoderRight, int piAdc, int motorAdc)
{
    if(temp > 80.0f || temp < -40.0f || (temp == 0.0f && humi == 0.0f))
    {
        QDateTime current = QDateTime::currentDateTime();
        QString timestamp = current.toString("yyyy-MM-dd hh:mm:ss.zzz");
        int row = ui->FaultTableWidget->rowCount();
        ui->FaultTableWidget->insertRow(row);
        ui->FaultTableWidget->setItem(row, 0, new QTableWidgetItem(timestamp));
        ui->FaultTableWidget->setItem(row, 1, new QTableWidgetItem("温度异常"));
    }

    if(humi > 100.0f || humi < 0.0f || (temp == 0.0f && humi == 0.0f))
    {
        QDateTime current = QDateTime::currentDateTime();
        QString timestamp = current.toString("yyyy-MM-dd hh:mm:ss.zzz");
        int row = ui->FaultTableWidget->rowCount();
        ui->FaultTableWidget->insertRow(row);
        ui->FaultTableWidget->setItem(row, 0, new QTableWidgetItem(timestamp));
        ui->FaultTableWidget->setItem(row, 1, new QTableWidgetItem("湿度异常"));
    }

    if(encoderLeft > 50000 || encoderLeft < -50000)
    {
        QDateTime current = QDateTime::currentDateTime();
        QString timestamp = current.toString("yyyy-MM-dd hh:mm:ss.zzz");
        int row = ui->FaultTableWidget->rowCount();
        ui->FaultTableWidget->insertRow(row);
        ui->FaultTableWidget->setItem(row, 0, new QTableWidgetItem(timestamp));
        ui->FaultTableWidget->setItem(row, 1, new QTableWidgetItem("编码器读数异常"));
    }

    if(encoderRight > 50000 || encoderRight < -50000)
    {
        QDateTime current = QDateTime::currentDateTime();
        QString timestamp = current.toString("yyyy-MM-dd hh:mm:ss.zzz");
        int row = ui->FaultTableWidget->rowCount();
        ui->FaultTableWidget->insertRow(row);
        ui->FaultTableWidget->setItem(row, 0, new QTableWidgetItem(timestamp));
        ui->FaultTableWidget->setItem(row, 1, new QTableWidgetItem("编码器读数异常"));
    }

    if(piAdc >  4096 || piAdc < 3072)
    {
        QDateTime current = QDateTime::currentDateTime();
        QString timestamp = current.toString("yyyy-MM-dd hh:mm:ss.zzz");
        int row = ui->FaultTableWidget->rowCount();
        ui->FaultTableWidget->insertRow(row);
        ui->FaultTableWidget->setItem(row, 0, new QTableWidgetItem(timestamp));
        ui->FaultTableWidget->setItem(row, 1, new QTableWidgetItem("电量读数异常"));
    }

    if(motorAdc >  4096 || motorAdc < 3072)
    {
        QDateTime current = QDateTime::currentDateTime();
        QString timestamp = current.toString("yyyy-MM-dd hh:mm:ss.zzz");
        int row = ui->FaultTableWidget->rowCount();
        ui->FaultTableWidget->insertRow(row);
        ui->FaultTableWidget->setItem(row, 0, new QTableWidgetItem(timestamp));
        ui->FaultTableWidget->setItem(row, 1, new QTableWidgetItem("电量读数异常"));
    }
}
/*************故障诊断的函数************/

/*************轨迹行走的函数************/
void MainWindow::on_Line_Follow_Button_clicked()
{
    ui->Line_Follow_Button->setEnabled(false);
    if(tcpSocket->state() != QTcpSocket::ConnectedState)
    {
        connect_error();
        ui->Line_Follow_Button->setEnabled(true);
        return;
    }
    ui->Line_Follow_Button->setEnabled(true);

    if (car_mode == LINE_FOLLOW) {
        // 从循迹切到手动
        car_mode = MANUAL;
        ui->CarModeLabel->setText("当前模式：手动操控");
        ui->Line_Follow_Button->setText("开启");
    } else {
        // 从手动/避障切到循迹
        car_mode = LINE_FOLLOW;
        ui->CarModeLabel->setText("当前模式：红外循迹");
        ui->Line_Follow_Button->setText("关闭");
    }

    quint8 cmd = LINE_FOLLOW_MODE_SWITCH;
    sendCommandFrame(QByteArray(1, static_cast<char>(cmd)));
}
void MainWindow::on_Trajectory_Z_Button_clicked()
{
    if(tcpSocket->state() != QTcpSocket::ConnectedState) {connect_error();return;}
    quint8 cmd = TRAJECTORY_Z;
    sendCommandFrame(QByteArray(1, static_cast<char>(cmd)));
}
void MainWindow::on_Trajectory_J_Button_clicked()
{
    if(tcpSocket->state() != QTcpSocket::ConnectedState) {connect_error();return;}
    quint8 cmd = TRAJECTORY_J;
    sendCommandFrame(QByteArray(1, static_cast<char>(cmd)));
}
void MainWindow::on_Trajectory_U_Button_clicked()
{
    if(tcpSocket->state() != QTcpSocket::ConnectedState) {connect_error();return;}
    quint8 cmd = TRAJECTORY_U;
    sendCommandFrame(QByteArray(1, static_cast<char>(cmd)));
}
/*************轨迹行走的函数************/

/*************语音识别的函数************/
void MainWindow::on_VoiceInteractionSwitchBtn_clicked()
{
    ui->VoiceInteractionSwitchBtn->setEnabled(false);
    if(tcpSocket->state() != QTcpSocket::ConnectedState)
    {
        connect_error();
        ui->VoiceInteractionSwitchBtn->setEnabled(true);
        return;
    }
    ui->VoiceInteractionSwitchBtn->setEnabled(true);

    quint8 cmd = VOICE_INTERACTION_SWITCH;
    sendCommandFrame(QByteArray(1, static_cast<char>(cmd)));

    if(VoiceInteractionOn == false)
    {
        ui->VoiceInteractionSwitchBtn->setText("关闭语音识别");
        VoiceInteractionOn = true;
    }
    else
    {
        ui->VoiceInteractionSwitchBtn->setText("打开语音识别");
        VoiceInteractionOn = false;
    }
}
void MainWindow::appendVoiceResult(const QString &text, double timestamp)
{
    QDateTime time = QDateTime::fromSecsSinceEpoch(static_cast<qint64>(timestamp));
    QString timeStr = time.toString("hh:mm:ss");
    QString line = QString("[%1] 识别结果：%2").arg(timeStr, text);

    ui->voiceResultTextEdit->appendPlainText(line);   // 自动换行追加
    // 移动光标到底部，保证滚动条自动跟随
    ui->voiceResultTextEdit->moveCursor(QTextCursor::End);
    ui->voiceResultTextEdit->ensureCursorVisible();
}
/*************语音识别的函数************/
