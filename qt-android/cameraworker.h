#ifndef CAMERAWORKER_H
#define CAMERAWORKER_H

#include <QObject>
#include <QTcpSocket>
#include <QImage>
#include <QTimer>
#include "mp4writer.h"

class CameraWorker : public QObject
{
    Q_OBJECT
public:
    explicit CameraWorker(QObject *parent = nullptr);
    ~CameraWorker();

public slots:
    // 这些槽在工作线程执行（worker 被 moveToThread 过去了）
    void start(const QString &ip, quint16 port);
    void stop();
    void startRecording(const QString &filePath);
    void stopRecording();

signals:
    void frameReady(const QImage &image);          // 给主线程显示
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
    QTimer     *m_connectTimer = nullptr;
    QByteArray  m_buffer;

    // 录像
    Mp4Writer *m_writer = nullptr;
    bool       m_recording = false;
    bool       m_pendingStart = false;
    QString    m_recordPath;
};

#endif // CAMERAWORKER_H
