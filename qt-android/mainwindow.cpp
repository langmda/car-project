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

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    ui->stackedWidget->setCurrentWidget(ui->HomePage);

    photoSaveDir = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    videoSaveDir = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
    if (photoSaveDir.isEmpty())
    {
        photoSaveDir = QDir::currentPath() + "/Pictures";
        QDir().mkpath(photoSaveDir);
    }
    if (videoSaveDir.isEmpty())
    {
        videoSaveDir = QDir::currentPath() + "/Videos";
        QDir().mkpath(videoSaveDir);
    }
    ui->labelPhotoDir->setText("照片目录：" + photoSaveDir);
    ui->labelVideoDir->setText("视频目录：" + videoSaveDir);

    // 连接TCP
    tcpSocket = new QTcpSocket(this);
    tcpSocket->abort();
    tcpSocket->connectToHost(QHostAddress(RASPI_IP), RASPI_PORT);
    connect(tcpSocket, &QTcpSocket::readyRead, this, &MainWindow::readTcpData);

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
    ui->KpLabel->setText("KP : 50.0");
    ui->KiLabel->setText("KI : 1.0");
    ui->IlimLabel->setText("I_lim : 800");
    ui->PWMlimLabel->setText("PWM_lim : 4320");

    // === 摄像头工作线程 ===
    m_cameraThread = new QThread();
    m_cameraWorker = new CameraWorker();
    m_cameraWorker->moveToThread(m_cameraThread);     //把m_cameraWorker的线程归属改成了m_cameraThread，之后发送信号发送给m_cameraWorker，槽函数都是在m_cameraThread处理，而不是在主线程中处理
    m_cameraThread->start();                          //线程默认调用exec()事件循环

    // 主线程 → 工作线程（信号都是自定义信号，发送给摄像头工作线程，摄像头工作线程在事件循环中取出信号，执行槽函数）
    connect(this, &MainWindow::requestStartCamera,
            m_cameraWorker, &CameraWorker::start);
    connect(this, &MainWindow::requestStopCamera,
            m_cameraWorker, &CameraWorker::stop);
    connect(this, &MainWindow::requestStartRecording,
            m_cameraWorker, &CameraWorker::startRecording);
    connect(this, &MainWindow::requestStopRecording,
            m_cameraWorker, &CameraWorker::stopRecording);

    // 工作线程 → 主线程（信号都是自定义信号，发送给主线程，主线程在事件循环中取出信号，执行槽函数）
    connect(m_cameraWorker, &CameraWorker::frameReady,
            this, &MainWindow::onFrameReady);
    connect(m_cameraWorker, &CameraWorker::connected,
            this, &MainWindow::onCameraConnected);
    connect(m_cameraWorker, &CameraWorker::errorOccurred,
            this, &MainWindow::onCameraError);
    connect(m_cameraWorker, &CameraWorker::recordingStarted,
            this, &MainWindow::onRecordingStarted);
    connect(m_cameraWorker, &CameraWorker::recordingFailed,
            this, &MainWindow::onRecordingFailed);
}

MainWindow::~MainWindow()
{
    if (m_cameraThread) {
        if (m_cameraThread->isRunning()) {
            QMetaObject::invokeMethod(m_cameraWorker, "stop",
                                      Qt::BlockingQueuedConnection);
            m_cameraThread->quit();
            m_cameraThread->wait();
        }
        delete m_cameraWorker;
        delete m_cameraThread;
        m_cameraWorker = nullptr;
        m_cameraThread = nullptr;
    }
    delete ui;
}

void MainWindow::connect_error()
{
    ui->FrontButton->setEnabled(false);
    ui->LeftButton->setEnabled(false);
    ui->RightButton->setEnabled(false);
    ui->BackButton->setEnabled(false);
    ui->SwitchModeButton->setEnabled(false);
    ui->LedButton->setEnabled(false);
    ui->RightCirlcleButton->setEnabled(false);

    QMessageBox::warning(this, "提示", "未连接到树莓派，请检查网络！");

    ui->FrontButton->setEnabled(true);
    ui->LeftButton->setEnabled(true);
    ui->RightButton->setEnabled(true);
    ui->BackButton->setEnabled(true);
    ui->SwitchModeButton->setEnabled(true);
    ui->LedButton->setEnabled(true);
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

void MainWindow::camera_connect_error()
{
    QMessageBox::warning(this, "提示", "未连接到树莓派，请检查网络！");
}

void MainWindow::readTcpData()
{
    while (tcpSocket->bytesAvailable() > 0) // 持续读取所有可用数据
    {
        char peekCmd;
        tcpSocket->peek(&peekCmd, 1);  // 偷看第一个字节，不取走
        //单字节命令
        if (static_cast<quint8>(peekCmd) == 0x0F)   // 如果是拍照命令 (0x0F)
        {
            char cmd;
            tcpSocket->read(&cmd, 1);  // 正式读取命令
            qDebug() << "📸 收到拍照命令";
            on_captureBtn_clicked();
            continue;  // 继续循环，处理下一个数据
        }
        else if (static_cast<quint8>(peekCmd) == 0x10)  // 如果是录像切换命令 (0x10)
        {
            char cmd;
            tcpSocket->read(&cmd, 1);  // 正式读取命令
            qDebug() << "🎥 收到录像切换命令";
            on_recordBtn_clicked();
            continue;  // 继续循环，处理下一个数据
        }
        //JSON数据
        // 1. 读取树莓派发送的数据（按行读取，每行是一份JSON数据）
        if (tcpSocket->canReadLine())
        {
            QByteArray line = tcpSocket->readLine().trimmed();  // 读取一行并去掉换行/空格
            if (line.isEmpty()) continue;

            // 2. 解析JSON数据
            QJsonParseError jsonError;
            QJsonDocument doc = QJsonDocument::fromJson(line, &jsonError);
            if (jsonError.error != QJsonParseError::NoError) {      // JSON解析失败
                QMessageBox::warning(this, "提示", "❌ JSON解析失败");
                continue;
            }
            QJsonObject obj = doc.object();
            QString msgType = obj["type"].toString();   //获取JSON数据类型

            // 3. 根据数据类型进行对应的处理
            if (msgType == "music_list")    //音乐文件列表
            {
                //获取数据
                QJsonArray musicArray = obj["data"].toArray();
                musicListCache.clear();
                for (int i=0; i<musicArray.size(); i++)
                {musicListCache.append(musicArray[i].toString());}
                //更新音乐列表和当前播放音乐标签
                if (musicListCache.isEmpty())   //没有检测到音乐文件信息缓存，报错
                {
                    QMessageBox::warning(this, "提示", "未检测到音乐文件缓存");
                    return;
                }
                updateMusicWidget();
                updateMusicLabel();
                updateMusicHighlight ();
            }
            else if (msgType == "status_update")   //状态更新，包括传感器数据、音乐播放状态
            {
                QJsonObject dataObj = obj["data"].toObject();

                QJsonObject sensorObj = dataObj["sensor"].toObject();
                QJsonObject musicObj = dataObj["music"].toObject();

                sensorUpdata(sensorObj);
                musicUpdata(musicObj);
            }
            else if (msgType == "voice_result")     //语音识别结果
            {
                QJsonObject dataObj = obj["data"].toObject();
                QString text = dataObj["text"].toString();
                double timestamp = dataObj["timestamp"].toDouble();

                // 转换为本地时间显示
                QDateTime time = QDateTime::fromSecsSinceEpoch(timestamp);
                QString timeStr = time.toString("hh:mm:ss");

                QString displayText = QString("[%1] 识别结果：%2").arg(timeStr).arg(text);
                ui->voiceResultTextEdit->append(displayText);  // 需要先在UI中添加一个QTextEdit
            }
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
    tcpSocket->write((char*)&cmd, 1);
}
void MainWindow::on_LeftButton_clicked()
{
    if(tcpSocket->state() != QTcpSocket::ConnectedState) {connect_error();}
    quint8 cmd = TURN_LEFT;
    tcpSocket->write((char*)&cmd, 1);
}
void MainWindow::on_RightButton_clicked()
{
    if(tcpSocket->state() != QTcpSocket::ConnectedState) {connect_error();}
    quint8 cmd = TURN_RIGHT;
    tcpSocket->write((char*)&cmd, 1);
}
void MainWindow::on_BackButton_clicked()
{
    if(tcpSocket->state() != QTcpSocket::ConnectedState) {connect_error();}
    quint8 cmd = BACK_OFF;
    tcpSocket->write((char*)&cmd, 1);
}
void MainWindow::on_SwitchModeButton_clicked()
{
    if(tcpSocket->state() != QTcpSocket::ConnectedState) {connect_error();}
    quint8 cmd = CAR_MODE_SWITCH;
    tcpSocket->write((char*)&cmd, 1);
    if(ui->CarModeLabel->text() == "当前模式：自主避障")
    {ui->CarModeLabel->setText("当前模式：手动操作");}
    else if(ui->CarModeLabel->text() == "当前模式：手动操作")
    {ui->CarModeLabel->setText("当前模式：自主避障");}
}
void MainWindow::on_LedButton_clicked()
{
    if(tcpSocket->state() != QTcpSocket::ConnectedState) {connect_error();}
    quint8 cmd = LED_SWITCH;
    tcpSocket->write((char*)&cmd, 1);
}
void MainWindow::on_RightCirlcleButton_clicked()
{
    if(tcpSocket->state() != QTcpSocket::ConnectedState) {connect_error();}
    quint8 cmd = CUSTOMIZATION;
    tcpSocket->write((char*)&cmd, 1);
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
    QByteArray cmdData;
    cmdData.resize(9);  // cmd(1) + rightPWM(4) + leftPWM(4) = 9字节

    // 第1字节：命令码
    cmdData[0] = static_cast<char>(PWM_UPDATE);  // 0x19

    // 第2-5字节：rightPWM (小端模式)
    cmdData[1] = static_cast<char>(rightPWM & 0xFF);
    cmdData[2] = static_cast<char>((rightPWM >> 8) & 0xFF);
    cmdData[3] = static_cast<char>((rightPWM >> 16) & 0xFF);
    cmdData[4] = static_cast<char>((rightPWM >> 24) & 0xFF);

    // 第6-9字节：leftPWM (小端模式)
    cmdData[5] = static_cast<char>(leftPWM & 0xFF);
    cmdData[6] = static_cast<char>((leftPWM >> 8) & 0xFF);
    cmdData[7] = static_cast<char>((leftPWM >> 16) & 0xFF);
    cmdData[8] = static_cast<char>((leftPWM >> 24) & 0xFF);

    // 调试输出（发送到Qt的调试控制台）
    qDebug() << "Sending PWM Update - Left:" << leftPWM << "Right:" << rightPWM;
    qDebug() << "Hex data:" << cmdData.toHex();

    // 发送数据
    tcpSocket->write(cmdData);

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
    tcpSocket->write((char*)&cmd, 1);
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
    if(tcpSocket->state() != QTcpSocket::ConnectedState) {connect_error();}
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
    tcpSocket->write((char*)&cmd, 1);
    updateMusicLabel();
    updateMusicHighlight();
    ui->SwitchBtn->setIcon(QIcon(":/image/暂停.png"));
    is_playing = 1;
}
void MainWindow::on_NextBtn_clicked()
{
    if(tcpSocket->state() != QTcpSocket::ConnectedState) {connect_error();}
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
    tcpSocket->write((char*)&cmd, 1);
    updateMusicLabel();
    updateMusicHighlight();
    ui->SwitchBtn->setIcon(QIcon(":/image/暂停.png"));
    is_playing = 1;
}
void MainWindow::on_VolumeSlider_valueChanged(int value)
{
    if(tcpSocket->state() != QTcpSocket::ConnectedState) {connect_error();}
    QByteArray cmdData;
    cmdData.append(VOLUMN_MUSIC);
    cmdData.append(static_cast<char>(value));
    tcpSocket->write(cmdData);
}
void MainWindow::on_MusicListWidget_itemClicked(QListWidgetItem *item)
{
    if(tcpSocket->state() != QTcpSocket::ConnectedState) {connect_error();}
    if (!item) return;
    currentMusicIndex = ui->MusicListWidget->row(item);
    isManualMusicSwitch = true;
    musicSwitchTimer->start();
    updateMusicLabel();
    updateMusicHighlight();
    QByteArray cmdData;
    cmdData.append(PLAY_SPECIFIC_MUSIC);
    cmdData.append(static_cast<char>(currentMusicIndex));
    tcpSocket->write(cmdData);
    ui->SwitchBtn->setIcon(QIcon(":/image/暂停.png"));
    is_playing = 1;
}
void MainWindow::on_ProgressSlider_sliderReleased()
{
    if(tcpSocket->state() != QTcpSocket::ConnectedState) {connect_error();}
    int value = ui->ProgressSlider->value();
    QByteArray cmdData;
    cmdData.append(PROGRESS_MUSIC);
    cmdData.append(static_cast<char>(value));
    tcpSocket->write(cmdData);
}
void MainWindow::on_MusicModeBtn_released()
{
    if(tcpSocket->state() != QTcpSocket::ConnectedState) {connect_error(); return;}
    quint8 cmd = MUSIC_MODE_SWITCH;
    tcpSocket->write((char*)&cmd, 1);
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
    bool playing = musicObj["is_playing"].toBool();
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
    int temp = sensorObj["temp"].toInt();
    int humi = sensorObj["humi"].toInt();
    int encoderLeft = sensorObj["encoder_left_cnt"].toInt();
    int encoderRight = sensorObj["encoder_right_cnt"].toInt();
    int piAdc = sensorObj["pi_bat_voltage"].toInt();
    int motorAdc = sensorObj["motor_bat_voltage"].toInt();
    float yaw = static_cast<float>(sensorObj["yaw"].toDouble());

    addEncoderData(encoderLeft, encoderRight, yaw);  // 传入 yaw
    Sensor_Fault(temp, humi, encoderLeft, encoderRight, piAdc, motorAdc);

    float wheel_circum = PI * WHEEL_DIAMETER;
    float speed_left = ((float)encoderLeft / PULSE_PER_ROUND) * wheel_circum;
    float speed_right = ((float)encoderRight / PULSE_PER_ROUND) * wheel_circum;
    float speed = (speed_left + speed_right)/2;

    float piVol_mV = (static_cast<float>(piAdc) / 4096.0f) * 3.3f * (133.0f/33.0f) * 1000.0f + 100;
    float motorVol_mV = (static_cast<float>(motorAdc) / 4096.0f) * 3.3f * (133.0f/33.0f) * 1000.0f + 100;

    double piBatPercent = (piVol_mV - VOL_BAT_EMPTY) / (double)(VOL_BAT_FULL - VOL_BAT_EMPTY) * 100.0;
    piBatPercent = qBound(0.0, piBatPercent, 100.0);
    double motorBatPercent = (motorVol_mV - VOL_BAT_EMPTY) / (double)(VOL_BAT_FULL - VOL_BAT_EMPTY) * 100.0;
    motorBatPercent = qBound(0.0, motorBatPercent, 100.0);

    // 一阶低通滤波
    filteredPiBatPercent = filterAlpha * piBatPercent + (1.0f - filterAlpha) * filteredPiBatPercent;
    filteredMotorBatPercent = filterAlpha * motorBatPercent + (1.0f - filterAlpha) * filteredMotorBatPercent;

    ui->TempLabel->setText(QString("温度：%1℃").arg(temp));
    ui->HumiLabel->setText(QString("湿度：%1%").arg(humi));
    ui->SpeedLabel->setText(QString("车速：%1 mm/s").arg(speed, 0, 'f', 1));
    ui->piBatLabel->setText(QString("树莓派电池电量：%1%").arg(filteredPiBatPercent, 0, 'f', 1));
    ui->MotorBatLabel->setText(QString("电机电池电量：%1%").arg(filteredMotorBatPercent, 0, 'f', 1));
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
    float currentKp = 0.0f, currentKi = 0.0f;
    int32_t currentILim = 0, currentPWMLim = 0;
    bool ok;

    // 解析 KpLabel 文本，格式如 "KP : 0.01"
    QString kpText = ui->KpLabel->text().remove("KP : ");
    currentKp = kpText.toFloat(&ok);
    if (!ok) currentKp = 0.01f;

    QString kiText = ui->KiLabel->text().remove("KI : ");
    currentKi = kiText.toFloat(&ok);
    if (!ok) currentKi = 0.001f;

    QString ilimText = ui->IlimLabel->text().remove("I_lim : ");
    currentILim = ilimText.toInt(&ok);
    if (!ok) currentILim = 1000;

    QString pwmText = ui->PWMlimLabel->text().remove("PWM_lim : ");
    currentPWMLim = pwmText.toInt(&ok);
    if (!ok) currentPWMLim = 4320;

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

    // 3. 处理Ki输入（float类型）
    if (!ui->KiLineEdit->text().isEmpty()) {
        QString kiInput = ui->KiLineEdit->text();
        bool isFloat = false;
        float tempValue = kiInput.toFloat(&isFloat);
        if (isFloat) {
            currentKi = tempValue;
        } else {
            QMessageBox::warning(this, "提示", "Ki格式错误，请输入有效的数字（如：0.1 或 2）");
            return;
        }
    }

    // 4. 处理I_Lim输入（int32_t类型）
    if (!ui->IlimLineEdit->text().isEmpty()) {
        QString ilimInput = ui->IlimLineEdit->text();
        bool isInt = false;
        int tempValue = ilimInput.toInt(&isInt);
        if (isInt) {
            currentILim = static_cast<int32_t>(tempValue);
        } else {
            // 尝试转换为浮点数再取整
            bool isFloat = false;
            float floatValue = ilimInput.toFloat(&isFloat);
            if (isFloat) {
                currentILim = static_cast<int32_t>(floatValue);
                QMessageBox::information(this, "提示", QString("I_Lim已从浮点数转换为整数：%1").arg(currentILim));
            } else {
                QMessageBox::warning(this, "提示", "I_Lim格式错误，请输入有效的整数（如：800）");
                return;
            }
        }
    }

    // 5. 处理PWM_Lim输入（int32_t类型）
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

    // 6. 可选：添加数值范围检查
    if (currentKp < 0) {
        QMessageBox::warning(this, "提示", "Kp不能为负数");
        return;
    }
    if (currentKi < 0) {
        QMessageBox::warning(this, "提示", "Ki不能为负数");
        return;
    }
    if (currentILim < 0) {
        QMessageBox::warning(this, "提示", "I_Lim不能为负数");
        return;
    }
    if (currentPWMLim < 0) {
        QMessageBox::warning(this, "提示", "PWM_Lim不能为负数");
        return;
    }

    // 7. 打包发送
    PIDParams params;
    params.cmd = PID_PARAMETER;
    params.Kp = currentKp;
    params.Ki = currentKi;
    params.I_Lim = currentILim;
    params.PWM_Lim = currentPWMLim;

    QByteArray cmdData((char*)&params, sizeof(PIDParams));
    tcpSocket->write(cmdData);

    // 8. 更新Label显示新值
    ui->KpLabel->setText(QString("KP : %1").arg(params.Kp, 0, 'f', 1));  // 保留3位小数
    ui->KiLabel->setText(QString("KI : %1").arg(params.Ki, 0, 'f', 1));  // 保留3位小数
    ui->IlimLabel->setText(QString("I_lim : %1").arg(params.I_Lim));
    ui->PWMlimLabel->setText(QString("PWM_lim : %1").arg(params.PWM_Lim));

    // 9. 清空输入框（可选）
    ui->KpLineEdit->clear();
    ui->KiLineEdit->clear();
    ui->IlimLineEdit->clear();
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
void MainWindow::on_captureBtn_clicked()
{
    if (!cameraOn || lastFrameImage.isNull()) {
        camera_connect_error();
        return;
    }

    if (photoSaveDir.isEmpty()) {
        QMessageBox::warning(this, "提示", "请先选择照片保存目录");
        return;
    }

    QPixmap pixmap = ui->videoLabel->pixmap(Qt::ReturnByValue);
    if (pixmap.isNull()) {
        QMessageBox::warning(this, "提示", "没有可用的图像");
        return;
    }

    QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss_zzz");
    QString fileName = QString("%1/photo_%2.jpg").arg(photoSaveDir).arg(timestamp);

    if (pixmap.save(fileName, "JPEG", 90)) {
        QMessageBox::information(this, "提示", "照片已保存至：\n" + fileName);
    } else {
        QMessageBox::warning(this, "提示", "保存失败，请检查目录权限");
    }
}
void MainWindow::on_recordBtn_clicked()
{
    if (!cameraOn) { camera_connect_error(); return; }

    if (!isRecording) {
        if (videoSaveDir.isEmpty()) {
            QMessageBox::warning(this, "提示", "请先选择视频保存目录");
            return;
        }
        QDir dir(videoSaveDir);
        if (!dir.exists() && !dir.mkpath(".")) {
            QMessageBox::warning(this, "提示", "无法创建目录：" + videoSaveDir);
            return;
        }
        QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss");
        QString path = QString("%1/rec_%2.mp4").arg(videoSaveDir).arg(timestamp);

        isRecording = true;
        emit requestStartRecording(path);       // 交给 worker
        ui->recordBtn->setText("停止录像");
    } else {
        emit requestStopRecording();
        isRecording = false;
        ui->recordBtn->setText("开始录像");
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
    if (tcpSocket->state() != QTcpSocket::ConnectedState) {
        connect_error();
        return;
    }

    if (!cameraOn) {
        // 通知树莓派打开摄像头
        quint8 cmd = CAMERA_ON;
        tcpSocket->write((char*)&cmd, 1);        // ★ 改成这样

        // 延迟 200ms 再连（等树莓派摄像头线程起来）
        QTimer::singleShot(200, this, [this]() {
            emit requestStartCamera(RASPI_IP, 8889);
        });

        ui->cameraSwitchBtn->setText("关闭摄像头");
        cameraOn = true;
    } else {
        quint8 cmd = CAMERA_OFF;
        tcpSocket->write((char*)&cmd, 1);        // ★ 改成这样
        emit requestStopCamera();
        ui->cameraSwitchBtn->setText("开启摄像头");
        cameraOn = false;
        ui->videoLabel->clear();
        lastFrameImage = QImage();
    }
}
void MainWindow::onFrameReady(const QImage &img)
{
    lastFrameImage = img;   // 拍照用

    // 只有当前在摄像头页面才刷新，省 CPU
    if (ui->stackedWidget->currentWidget() == ui->CameraPage) {
        ui->videoLabel->setPixmap(
            QPixmap::fromImage(img).scaled(ui->videoLabel->size(),
                                           Qt::KeepAspectRatio,
                                           Qt::FastTransformation));
    }
}
void MainWindow::onCameraConnected()
{
    qDebug() << "✅ 摄像头连接成功";
}

void MainWindow::onCameraError(const QString &msg)
{
    QMessageBox::warning(this, "提示", "摄像头错误：" + msg);

    QDateTime current = QDateTime::currentDateTime();
    QString ts = current.toString("yyyy-MM-dd hh:mm:ss.zzz");
    int row = ui->FaultTableWidget->rowCount();
    ui->FaultTableWidget->insertRow(row);
    ui->FaultTableWidget->setItem(row, 0, new QTableWidgetItem(ts));
    ui->FaultTableWidget->setItem(row, 1, new QTableWidgetItem("摄像头TCP连接错误"));

    ui->cameraSwitchBtn->setText("开启摄像头");
    cameraOn = false;
}

void MainWindow::onRecordingStarted(const QString &path)
{
    QMessageBox::information(this, "提示", "开始录像，文件：\n" + path);
}

void MainWindow::onRecordingFailed(const QString &msg)
{
    QMessageBox::warning(this, "提示", "录像启动失败：" + msg);
    isRecording = false;
    ui->recordBtn->setText("开始录像");
}
/*************实时画面的函数************/


/*************故障诊断的函数************/
void MainWindow::Sensor_Fault(int temp, int humi, int encoderLeft, int encoderRight, int piAdc, int motorAdc)
{
    if(temp > 50 || temp < -10 || (temp == 0 && humi == 0))
    {
        QDateTime current = QDateTime::currentDateTime();
        QString timestamp = current.toString("yyyy-MM-dd hh:mm:ss.zzz");
        int row = ui->FaultTableWidget->rowCount();
        ui->FaultTableWidget->insertRow(row);
        ui->FaultTableWidget->setItem(row, 0, new QTableWidgetItem(timestamp));
        ui->FaultTableWidget->setItem(row, 1, new QTableWidgetItem("温度异常"));
    }

    if(humi > 95 || humi < 25 || (temp == 0 && humi == 0))
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
    if(Line_Follow_Mode == 0)
    {
        if(tcpSocket->state() != QTcpSocket::ConnectedState) {connect_error();}
        quint8 cmd = LINE_FOLLOW_MODE_SWITCH;
        tcpSocket->write((char*)&cmd, 1);
        ui->Line_Follow_Button->setText("关闭");
        Line_Follow_Mode = 1;
    }
    else
    {
        if(tcpSocket->state() != QTcpSocket::ConnectedState) {connect_error();}
        quint8 cmd = LINE_FOLLOW_MODE_SWITCH;
        tcpSocket->write((char*)&cmd, 1);
        ui->Line_Follow_Button->setText("开启");
        Line_Follow_Mode = 0;
    }
}
void MainWindow::on_Trajectory_Z_Button_clicked()
{
    if(tcpSocket->state() != QTcpSocket::ConnectedState) {connect_error();}
    quint8 cmd = TRAJECTORY_Z;
    tcpSocket->write((char*)&cmd, 1);
}
void MainWindow::on_Trajectory_J_Button_clicked()
{
    if(tcpSocket->state() != QTcpSocket::ConnectedState) {connect_error();}
    quint8 cmd = TRAJECTORY_J;
    tcpSocket->write((char*)&cmd, 1);
}
void MainWindow::on_Trajectory_U_Button_clicked()
{
    if(tcpSocket->state() != QTcpSocket::ConnectedState) {connect_error();}
    quint8 cmd = TRAJECTORY_U;
    tcpSocket->write((char*)&cmd, 1);
}
/*************轨迹行走的函数************/

/*************语音识别的函数************/
void MainWindow::on_VoiceInteractionSwitchBtn_clicked()
{
    if(tcpSocket->state() != QTcpSocket::ConnectedState) {connect_error();}
    quint8 cmd = VOICE_INTERACTION_SWITCH;
    tcpSocket->write((char*)&cmd, 1);

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
/*************语音识别的函数************/

void MainWindow::on_pushButton1_clicked()
{
    if(tcpSocket->state() != QTcpSocket::ConnectedState) {connect_error();}
    quint8 cmd = 0x31;
    tcpSocket->write((char*)&cmd, 1);
}

void MainWindow::on_pushButton2_clicked()
{
    if(tcpSocket->state() != QTcpSocket::ConnectedState) {connect_error();}
    quint8 cmd = 0x32;
    tcpSocket->write((char*)&cmd, 1);
}

void MainWindow::on_pushButton3_clicked()
{
    if(tcpSocket->state() != QTcpSocket::ConnectedState) {connect_error();}
    quint8 cmd = 0x33;
    tcpSocket->write((char*)&cmd, 1);
}
