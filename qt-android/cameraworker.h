#ifndef CAMERAWORKER_H
#define CAMERAWORKER_H

#include <QObject>
#include <QTcpSocket>
#include <QImage>
#include <QTimer>
#include <QByteArray>
#include "mp4writer.h"

// 摄像头工作对象：整个生命周期都跑在独立的工作线程里。
// 职责：
//   1. 与树莓派的 8889 端口建立 TCP 连接；
//   2. 按 car.py 的二进制帧协议（AA 55 + LEN(4BE) + PAYLOAD + CRC8）拆包，
//      PAYLOAD = 8 字节大端微秒时间戳 + 原始 JPEG；
//   3. 解码一帧只为了拿到宽高（录像需要），随后把「原始 JPEG + 时间戳」
//      通过 videoFrame 信号交给主线程显示 / 拍照；
//   4. 录像时把原始 JPEG 直接写进 MP4（不重新编码，省 CPU 也不掉画质）。
class CameraWorker : public QObject
{
    Q_OBJECT
public:
    explicit CameraWorker(QObject *parent = nullptr);
    ~CameraWorker() override;

public slots:
    // 下面这些槽都在工作线程执行（worker 已被 moveToThread）
    void start(const QString &ip, quint16 port);
    void stop();
    void startRecording(const QString &filePath);
    void stopRecording();

signals:
    void videoFrame(const QByteArray &jpegData, quint64 ts_us);   // 原始 JPEG + 采集时间戳
    void connected();
    void errorOccurred(const QString &msg);
    void recordingStarted(const QString &path);
    void recordingFailed(const QString &msg);
    void recordingStopped();

private slots:
    void onReadyRead();
    void onConnected();
    void onSocketError(QAbstractSocket::SocketError err);
    void onConnectTimeout();

private:
    void processPayload(const QByteArray &jpegData, quint64 ts_us);

    static quint8 crc8Update(quint8 crc, const QByteArray &data);
    static int findFrameHeader(const QByteArray &buf);

    QTcpSocket *m_socket = nullptr;
    QTimer     *m_connectTimer = nullptr;   // 连接超时（3s）
    QTimer     *m_delayTimer = nullptr;     // 连接前延迟，等树莓派 camera_server 进入 accept
    QByteArray  m_buffer;

    // 录像
    Mp4Writer *m_writer = nullptr;
    bool       m_recording = false;
    bool       m_pendingStart = false;   // 等第一帧到了再 open（需要知道宽高）
    QString    m_recordPath;
};

#endif // CAMERAWORKER_H
