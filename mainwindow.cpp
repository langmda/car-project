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

//手机发给树莓派的命令
#define MOVE_FORWARD 0X01
#define TURN_LEFT 0X02
#define TURN_RIGHT 0X03
#define BACK_OFF 0X04
#define CAR_MODE_SWITCH 0X05
#define LED_SWITCH 0x06

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

#define PULSE_PER_ROUND 1560
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

    tcpSocket = new QTcpSocket(this);
    tcpSocket->abort();
    tcpSocket->connectToHost(QHostAddress(RASPI_IP), RASPI_PORT);

    cameraSocket = new QTcpSocket(this);
    cameraSocket->abort();
    cameraSocket->setSocketOption(QTcpSocket::LowDelayOption, 1);
    cameraSocket->connectToHost(QHostAddress(RASPI_IP), 8889);
    connect(tcpSocket, &QTcpSocket::readyRead, this, &MainWindow::readTcpData);
    connect(cameraSocket, &QTcpSocket::readyRead, this, &MainWindow::receiveCameraData);

    musicSwitchTimer = new QTimer(this);
    musicSwitchTimer->setSingleShot(true);
    musicSwitchTimer->setInterval(1000);
    connect(musicSwitchTimer, &QTimer::timeout, this, [&]() {
        isManualMusicSwitch = false;
    });

    ui->DataRecordTableWidget->setColumnCount(3);
    QStringList headers;
    headers << "时间戳" << "左编码器值" << "右编码器值";
    ui->DataRecordTableWidget->setHorizontalHeaderLabels(headers);
    ui->DataRecordTableWidget->setEditTriggers(QAbstractItemView::NoEditTriggers);
    ui->DataRecordTableWidget->setColumnWidth(0, 120);
    ui->DataRecordTableWidget->setColumnWidth(1, 120);
    ui->DataRecordTableWidget->setColumnWidth(2, 120);

    ui->FaultTableWidget->setColumnCount(2);
    QStringList faultHeaders;
    faultHeaders << "时间戳" << "故障事件";
    ui->FaultTableWidget->setHorizontalHeaderLabels(faultHeaders);
    ui->FaultTableWidget->setEditTriggers(QAbstractItemView::NoEditTriggers);
    ui->FaultTableWidget->setColumnWidth(0, 120);
    ui->FaultTableWidget->setColumnWidth(1, 120);

    /*
    trajectoryScene = new QGraphicsScene(this);
    ui->graphicsView->setScene(trajectoryScene);
    ui->graphicsView->setRenderHint(QPainter::Antialiasing);
    // 设置坐标系：原点在中心，x向右，y向上（根据您的需要）
    trajectoryScene->setSceneRect(-20, -20, 40, 40); // 10x10米范围
    */
    trajectoryScene = new QGraphicsScene(this);
    ui->graphicsView->setScene(trajectoryScene);
    ui->graphicsView->setSceneRect(-20, -20, 40, 40);
    ui->graphicsView->centerOn(0, 0);  // 强制视图中心对准原点
}

MainWindow::~MainWindow()
{
    if (isRecording && videoFile)
    {
        videoFile->close();
        delete videoFile;
        videoFile = nullptr;
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

    QMessageBox::warning(this, "提示", "未连接到树莓派，请检查网络！");

    ui->FrontButton->setEnabled(true);
    ui->LeftButton->setEnabled(true);
    ui->RightButton->setEnabled(true);
    ui->BackButton->setEnabled(true);
    ui->SwitchModeButton->setEnabled(true);
    ui->LedButton->setEnabled(true);

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

    QDateTime current = QDateTime::currentDateTime();
    QString timestamp = current.toString("yyyy-MM-dd hh:mm:ss.zzz");
    int row = ui->FaultTableWidget->rowCount();
    ui->FaultTableWidget->insertRow(row);
    ui->FaultTableWidget->setItem(row, 0, new QTableWidgetItem(timestamp));
    ui->FaultTableWidget->setItem(row, 1, new QTableWidgetItem("摄像头TCP连接错误"));

    cameraSocket->abort();
    cameraSocket->connectToHost(QHostAddress(RASPI_IP), 8889);
    return;
}

void MainWindow::readTcpData()
{
    while (tcpSocket->bytesAvailable() > 0)
    {
        char peekCmd;
        tcpSocket->peek(&peekCmd, 1);
        if (static_cast<quint8>(peekCmd) == 0x0F)
        {
            char cmd;
            tcpSocket->read(&cmd, 1);
            qDebug() << "📸 收到拍照命令";
            on_captureBtn_clicked();
            continue;
        }
        else if (static_cast<quint8>(peekCmd) == 0x10)
        {
            char cmd;
            tcpSocket->read(&cmd, 1);
            qDebug() << "🎥 收到录像切换命令";
            on_recordBtn_clicked();
            continue;
        }
        if (tcpSocket->canReadLine())
        {
            QByteArray line = tcpSocket->readLine().trimmed();
            if (line.isEmpty()) continue;

            QJsonParseError jsonError;
            QJsonDocument doc = QJsonDocument::fromJson(line, &jsonError);
            if (jsonError.error != QJsonParseError::NoError) {
                QMessageBox::warning(this, "提示", "❌ JSON解析失败");
                continue;
            }
            QJsonObject obj = doc.object();
            QString msgType = obj["type"].toString();

            if (msgType == "music_list")
            {
                QJsonArray musicArray = obj["data"].toArray();
                musicListCache.clear();
                for (int i=0; i<musicArray.size(); i++)
                {musicListCache.append(musicArray[i].toString());}
                if (musicListCache.isEmpty())
                {
                    QMessageBox::warning(this, "提示", "未检测到音乐文件缓存");
                    return;
                }
                updateMusicWidget();
                updateMusicLabel();
                updateMusicHighlight ();
            }
            else if (msgType == "status_update")
            {
                QJsonObject dataObj = obj["data"].toObject();
                QJsonObject sensorObj = dataObj["sensor"].toObject();
                QJsonObject musicObj = dataObj["music"].toObject();
                sensorUpdata(sensorObj);
                musicUpdata(musicObj);
            }
            else if (msgType == "voice_result")
            {
                QJsonObject dataObj = obj["data"].toObject();
                QString text = dataObj["text"].toString();
                double timestamp = dataObj["timestamp"].toDouble();
                QDateTime time = QDateTime::fromSecsSinceEpoch(timestamp);
                QString timeStr = time.toString("hh:mm:ss");
                QString displayText = QString("[%1] 识别结果：%2").arg(timeStr).arg(text);
                ui->voiceResultTextEdit->append(displayText);
            }
            else if (msgType == "pose_update")
            {
                QJsonObject data = obj["data"].toObject();
                double x = data["x"].toDouble();
                double y = data["y"].toDouble();
                double theta = data["theta"].toDouble();
                robotX = x;
                robotY = y;
                robotTheta = theta;

                // 添加轨迹点
                trajectoryPoints.append(QPointF(x, y));
                // 限制点数
                while (trajectoryPoints.size() > 500) trajectoryPoints.removeFirst();

                // 更新显示
                updateTrajectoryDisplay();
            }
        }
    }
}

void MainWindow::receiveCameraData() {
    cameraBuffer.append(cameraSocket->readAll());

    while (cameraBuffer.size() > 0) {
        if (!receivingFrame) {
            if (cameraBuffer.size() < 4) return;
            expectedFrameSize = (static_cast<unsigned char>(cameraBuffer[0]) << 24) |
                                (static_cast<unsigned char>(cameraBuffer[1]) << 16) |
                                (static_cast<unsigned char>(cameraBuffer[2]) << 8) |
                                static_cast<unsigned char>(cameraBuffer[3]);
            cameraBuffer.remove(0, 4);
            receivingFrame = true;
        }

        if (receivingFrame) {
            if (cameraBuffer.size() < expectedFrameSize) return;
            QByteArray jpegData = cameraBuffer.left(expectedFrameSize);
            cameraBuffer.remove(0, expectedFrameSize);
            receivingFrame = false;

            QImage originalImage;
            if (originalImage.loadFromData(jpegData, "JPEG")) {
                QImage rotatedImage = originalImage.mirrored(true, true);
                QPixmap pixmap = QPixmap::fromImage(rotatedImage);
                ui->videoLabel->setPixmap(pixmap.scaled(ui->videoLabel->size(),
                                                        Qt::KeepAspectRatio, Qt::SmoothTransformation));

                if (isRecording && videoFile && videoFile->isOpen()) {
                    QByteArray rotatedJpegData;
                    QBuffer buffer(&rotatedJpegData);
                    buffer.open(QIODevice::WriteOnly);
                    rotatedImage.save(&buffer, "JPEG", 70);
                    buffer.close();
                    videoFile->write(rotatedJpegData);
                }
            } else {
                qDebug() << "JPEG解码失败";
            }
        }
    }
}

/***********功能页面切换按钮的函数***********/
void MainWindow::on_CarControlButton_clicked() { ui->stackedWidget->setCurrentWidget(ui->CarControlPage); }
void MainWindow::on_SenSorDataButton_clicked() { ui->stackedWidget->setCurrentWidget(ui->SensorDataPage); }
void MainWindow::on_MusicButton_clicked() { ui->stackedWidget->setCurrentWidget(ui->MusicPage); }
void MainWindow::on_ParameteButton_clicked() { ui->stackedWidget->setCurrentWidget(ui->ParameterPage); }
void MainWindow::on_DataRecordButton_clicked() { ui->stackedWidget->setCurrentWidget(ui->DataRecordPage); }
void MainWindow::on_FaultButton_clicked() { ui->stackedWidget->setCurrentWidget(ui->FaultPage); }
void MainWindow::on_VoiceInteractionButton_clicked() { ui->stackedWidget->setCurrentWidget(ui->VoiceInteractionPage); }
void MainWindow::on_CameraButton_clicked() { ui->stackedWidget->setCurrentWidget(ui->CameraPage); }
void MainWindow::on_TrajectoryButton_clicked(){ ui->stackedWidget->setCurrentWidget(ui->TrajectoryPage );}

void MainWindow::on_CarControl_BackHomeButton_clicked() { ui->stackedWidget->setCurrentWidget(ui->HomePage); }
void MainWindow::on_SensorData_BackHomeButton_clicked() { ui->stackedWidget->setCurrentWidget(ui->HomePage); }
void MainWindow::on_Music_BackHomeButton_clicked() { ui->stackedWidget->setCurrentWidget(ui->HomePage); }
void MainWindow::on_Parameter_BackHomeButton_clicked() { ui->stackedWidget->setCurrentWidget(ui->HomePage); }
void MainWindow::on_DataRecord_BackHomeButton_clicked() { ui->stackedWidget->setCurrentWidget(ui->HomePage); }
void MainWindow::on_Fault_BackHomeButton_clicked() { ui->stackedWidget->setCurrentWidget(ui->HomePage); }
void MainWindow::on_VoiceInteraction_BackHomeButton_clicked() { ui->stackedWidget->setCurrentWidget(ui->HomePage); }
void MainWindow::on_Camera_BackHomeButton_clicked() { ui->stackedWidget->setCurrentWidget(ui->HomePage); }
void MainWindow::on_Trajectory_BackHomeButton_clicked() { ui->stackedWidget->setCurrentWidget(ui->HomePage); }
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

/*************传感器的函数************/
void MainWindow::sensorUpdata(QJsonObject sensorObj)
{
    int temp = sensorObj["temp"].toInt();
    int humi = sensorObj["humi"].toInt();
    int encoderLeft = sensorObj["encoder_left_cnt"].toInt();
    int encoderRight = sensorObj["encoder_right_cnt"].toInt();
    int piAdc = sensorObj["pi_bat_voltage"].toInt();
    int motorAdc = sensorObj["motor_bat_voltage"].toInt();

    addEncoderData(encoderLeft, encoderRight);
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

    ui->TempLabel->setText(QString("温度：%1℃").arg(temp));
    ui->HumiLabel->setText(QString("湿度：%1%").arg(humi));
    ui->SpeedLabel->setText(QString("车速：%1 mm/s").arg(speed, 0, 'f', 1));
    ui->piBatLabel->setText(QString("树莓派电池电量：%1%").arg(piBatPercent, 0, 'f', 1));
    ui->MotorBatLabel->setText(QString("电机电池电量：%1%").arg(motorBatPercent, 0, 'f', 1));
}
/*************参数调试的函数************/
void MainWindow::on_UpdataParameterBtn_clicked()
{
    if(tcpSocket->state() != QTcpSocket::ConnectedState)
    {
        connect_error();
        return;
    }

    PIDParams params;
    params.cmd = PID_PARAMETER;

    bool ok;

    params.Kp = ui->KpLineEdit->text().toFloat(&ok);
    if (!ok) { QMessageBox::warning(this, "提示", "Kp格式错误"); return; }

    params.Ki = ui->KiLineEdit->text().toFloat(&ok);
    if (!ok) { QMessageBox::warning(this, "提示", "Ki格式错误"); return; }

    params.I_Fil = ui->IfilLineEdit->text().toFloat(&ok);
    if (!ok) { QMessageBox::warning(this, "提示", "I_Fil格式错误"); return; }

    params.I_Lim = ui->IlimLineEdit->text().toInt(&ok);
    if (!ok) { QMessageBox::warning(this, "提示", "I_Lim格式错误"); return; }

    params.PWM_Lim = ui->PWMlimLineEdit->text().toInt(&ok);
    if (!ok) { QMessageBox::warning(this, "提示", "PWM_Lim格式错误"); return; }

    QByteArray cmdData((char*)&params, sizeof(PIDParams));
    tcpSocket->write(cmdData);

    ui->KpLabel->setText(QString("KP : %1").arg(params.Kp));
    ui->KiLabel->setText(QString("KI : %1").arg(params.Ki));
    ui->IfilLabel->setText(QString("I_Fil : %1").arg(params.I_Fil));
    ui->IlimLabel->setText(QString("I_lim : %1").arg(params.I_Lim));
    ui->PWMlimLabel->setText(QString("PWM_lim : %1").arg(params.PWM_Lim));
}
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
void MainWindow::addEncoderData(int left, int right)
{
    QDateTime current = QDateTime::currentDateTime();
    QString timestamp = current.toString("yyyy-MM-dd hh:mm:ss.zzz");

    int row = ui->DataRecordTableWidget->rowCount();
    ui->DataRecordTableWidget->insertRow(row);
    ui->DataRecordTableWidget->setItem(row, 0, new QTableWidgetItem(timestamp));
    ui->DataRecordTableWidget->setItem(row, 1, new QTableWidgetItem(QString::number(left)));
    ui->DataRecordTableWidget->setItem(row, 2, new QTableWidgetItem(QString::number(right)));
}
/*************实时画面的函数************/
void MainWindow::on_captureBtn_clicked()
{
    if(cameraSocket->state() != QTcpSocket::ConnectedState)
    {camera_connect_error(); return;}

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
    if(cameraSocket->state() != QTcpSocket::ConnectedState)
    {camera_connect_error(); return;}

    if (!isRecording) {
        if (videoSaveDir.isEmpty()) {
            QMessageBox::warning(this, "提示", "请先选择视频保存目录");
            return;
        }

        QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss");
        QString filePath = QString("%1/rec_%2.mjpeg").arg(videoSaveDir).arg(timestamp);

        videoFile = new QFile(filePath);
        if (!videoFile->open(QIODevice::WriteOnly)) {
            QMessageBox::warning(this, "提示", "无法创建视频文件，请检查目录权限");
            delete videoFile;
            videoFile = nullptr;
            return;
        }

        isRecording = true;
        ui->recordBtn->setText("停止录像");
        QMessageBox::information(this, "提示", "开始录像，文件保存在：\n" + filePath);
    } else {
        if (videoFile) {
            videoFile->close();
            delete videoFile;
            videoFile = nullptr;
        }
        isRecording = false;
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
/*************历史轨迹的函数************/
void MainWindow::updateTrajectoryDisplay()
{
    trajectoryScene->clear();

    // 绘制轨迹折线
    if (trajectoryPoints.size() >= 2) {
        QPen pen(Qt::blue, 0.05);
        for (int i = 0; i < trajectoryPoints.size() - 1; ++i) {
            QLineF line(trajectoryPoints[i], trajectoryPoints[i+1]);
            trajectoryScene->addLine(line, pen);
        }
    }

    // 绘制小车（圆形+箭头）
    qreal radius = 0.15; // 车半径0.15米
    QBrush brush(Qt::red);
    QPen pen(Qt::black, 0.02);
    trajectoryScene->addEllipse(robotX - radius, robotY - radius,
                                radius*2, radius*2, pen, brush);
    // 绘制箭头（从圆心沿朝向延伸0.3米）
    qreal arrowLen = 0.3;
    qreal endX = robotX + arrowLen * cos(robotTheta);
    qreal endY = robotY + arrowLen * sin(robotTheta);
    trajectoryScene->addLine(robotX, robotY, endX, endY, QPen(Qt::red, 0.03));

    // 自动缩放视图以显示所有图形
    if (!trajectoryPoints.isEmpty() || (robotX != 0 || robotY != 0)) {
        QRectF rect = trajectoryScene->itemsBoundingRect();
        // 给边界加一点边距
        rect = rect.adjusted(-0.5, -0.5, 0.5, 0.5);
        ui->graphicsView->fitInView(rect, Qt::KeepAspectRatio);
    }
}

void MainWindow::on_TrajectoryClearButton_clicked()
{
    trajectoryPoints.clear();
    updateTrajectoryDisplay();
}



