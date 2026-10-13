#include "cameraworker.h"
#include <QDebug>
#include <QThread>
#include <QtEndian>

CameraWorker::CameraWorker(QObject *parent) : QObject(parent) {}

CameraWorker::~CameraWorker()
{
    if (m_writer) { m_writer->close(); delete m_writer; m_writer = nullptr; }
    if (m_socket) { m_socket->abort(); delete m_socket; m_socket = nullptr; }
}

void CameraWorker::start(const QString &ip, quint16 port)
{
    if (m_socket) return;

    m_buffer.clear();
    m_socket = new QTcpSocket(this);   // 父对象是 this，归属工作线程
    connect(m_socket, &QTcpSocket::readyRead,     this, &CameraWorker::onReadyRead);
    connect(m_socket, &QTcpSocket::connected,     this, &CameraWorker::onConnected);
    connect(m_socket, &QTcpSocket::errorOccurred, this, &CameraWorker::onSocketError);

    m_connectTimer = new QTimer(this);
    m_connectTimer->setSingleShot(true);
    connect(m_connectTimer, &QTimer::timeout, this, &CameraWorker::onConnectTimeout);
    m_connectTimer->start(3000);

    m_socket->connectToHost(ip, port);
    qDebug() << "CameraWorker::start thread =" << QThread::currentThread();
}

void CameraWorker::stop()
{
    if (m_connectTimer) { m_connectTimer->stop(); m_connectTimer->deleteLater(); m_connectTimer = nullptr; }
    if (m_socket) { m_socket->abort(); m_socket->deleteLater(); m_socket = nullptr; }
    m_buffer.clear();

    if (m_writer) { m_writer->close(); delete m_writer; m_writer = nullptr; }
    m_recording = false;
    m_pendingStart = false;
    emit recordingStopped();
}

void CameraWorker::startRecording(const QString &filePath)
{
    if (m_recording) return;
    m_recordPath = filePath;
    m_pendingStart = true;   // 等第一帧到了再 open（需要知道宽高）
    m_recording = true;
}

void CameraWorker::stopRecording()
{
    if (m_writer) { m_writer->close(); delete m_writer; m_writer = nullptr; }
    m_recording = false;
    m_pendingStart = false;
    emit recordingStopped();
}

void CameraWorker::onConnected()
{
    if (m_connectTimer) { m_connectTimer->stop(); m_connectTimer->deleteLater(); m_connectTimer = nullptr; }
    emit connected();
}

void CameraWorker::onConnectTimeout()
{
    if (m_socket && m_socket->state() != QTcpSocket::ConnectedState) {
        emit errorOccurred("摄像头连接超时");
        stop();
    }
}

void CameraWorker::onSocketError(QAbstractSocket::SocketError)
{
    QString msg = m_socket ? m_socket->errorString() : "socket error";
    emit errorOccurred(msg);
}

void CameraWorker::onReadyRead()
{
    if (!m_socket) return;
    m_buffer.append(m_socket->readAll());

    static constexpr int     HEAD_LEN  = 2;
    static constexpr int     LEN_LEN   = 4;
    static constexpr int     CRC_LEN   = 1;
    static constexpr quint32 MAX_FRAME = 200000;

    while (!m_buffer.isEmpty())
    {
        int headerPos = findFrameHeader(m_buffer);
        if (headerPos < 0) {
            if (m_buffer.size() > 1) m_buffer = m_buffer.right(1);
            return;
        }
        if (headerPos > 0) m_buffer.remove(0, headerPos);

        if (m_buffer.size() < HEAD_LEN + LEN_LEN) return;

        quint32 payloadLen = qFromBigEndian<quint32>(
            reinterpret_cast<const uchar*>(m_buffer.constData() + HEAD_LEN));

        if (payloadLen == 0 || payloadLen > MAX_FRAME) {
            m_buffer.remove(0, 1);
            continue;
        }

        int totalLen = HEAD_LEN + LEN_LEN + int(payloadLen) + CRC_LEN;
        if (m_buffer.size() < totalLen) return;

        quint8 recvCrc = static_cast<quint8>(m_buffer[totalLen - 1]);
        QByteArray crcRange = m_buffer.mid(HEAD_LEN, LEN_LEN + payloadLen);
        quint8 calcCrc = crc8Update(0x00, crcRange);
        if (recvCrc != calcCrc) { m_buffer.remove(0, 1); continue; }

        QByteArray payload = m_buffer.mid(HEAD_LEN + LEN_LEN, payloadLen);
        m_buffer.remove(0, totalLen);

        if (payload.size() < 8) continue;
        quint64 ts_us = qFromBigEndian<quint64>(
            reinterpret_cast<const uchar*>(payload.constData()));
        QByteArray jpegData = payload.mid(8);

        processPayload(jpegData, ts_us);
    }
}

void CameraWorker::processPayload(const QByteArray &jpegData, quint64 ts_us)
{
    QImage img;
    if (!img.loadFromData(jpegData, "JPEG")) {
        qDebug() << "JPEG 解码失败, size =" << jpegData.size();
        return;
    }
    QImage rotated = img.mirrored(true, true);

    // ① 丢给主线程显示（异步、不阻塞）
    emit frameReady(rotated);

    // ② 录像：★ 直接写原始 JPEG，不再重编码
    if (m_recording) {
        if (m_pendingStart) {
            m_writer = new Mp4Writer();
            if (!m_writer->open(m_recordPath, rotated.width(), rotated.height())) {
                emit recordingFailed("无法创建 MP4 文件");
                delete m_writer; m_writer = nullptr;
                m_recording = false; m_pendingStart = false;
                return;
            }
            m_pendingStart = false;
            emit recordingStarted(m_recordPath);
        }
        if (m_writer && m_writer->isOpen()) {
            m_writer->writeFrame(jpegData, ts_us);
        }
    }
}

quint8 CameraWorker::crc8Update(quint8 crc, const QByteArray &data)
{
    for (char ch : data) {
        crc ^= static_cast<quint8>(ch);
        for (int i = 0; i < 8; ++i)
            crc = (crc & 0x80) ? quint8((crc << 1) ^ 0x07) : quint8(crc << 1);
    }
    return crc;
}

int CameraWorker::findFrameHeader(const QByteArray &buf)
{
    for (int i = 0; i + 1 < buf.size(); ++i)
        if (quint8(buf[i]) == 0xAA && quint8(buf[i+1]) == 0x55) return i;
    return -1;
}
