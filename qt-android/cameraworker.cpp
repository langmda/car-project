#include "cameraworker.h"

#include <QDebug>
#include <QThread>
#include <QtEndian>
#include <QTimer>

CameraWorker::CameraWorker(QObject *parent) : QObject(parent) {}

CameraWorker::~CameraWorker()
{
    // 此时线程事件循环通常已经停止，这里只做资源释放，不再发射信号
    if (m_delayTimer)   { m_delayTimer->stop(); delete m_delayTimer; m_delayTimer = nullptr; }
    if (m_connectTimer) { m_connectTimer->stop(); delete m_connectTimer; m_connectTimer = nullptr; }
    if (m_writer)       { m_writer->close(); delete m_writer; m_writer = nullptr; }
    if (m_socket)       { m_socket->abort(); delete m_socket; m_socket = nullptr; }
}

/* ---------------- 连接管理 ---------------- */
void CameraWorker::start(const QString &ip, quint16 port)
{
    if (m_socket) return;                       // 已经在连接/已连接

    m_buffer.clear();
    m_socket = new QTcpSocket(this);            // 父对象是 this，归属工作线程
    connect(m_socket, &QTcpSocket::readyRead,     this, &CameraWorker::onReadyRead);
    connect(m_socket, &QTcpSocket::connected,     this, &CameraWorker::onConnected);
    connect(m_socket, &QTcpSocket::errorOccurred, this, &CameraWorker::onSocketError);
    connect(m_socket, &QTcpSocket::disconnected,  this, [this]() {
        qDebug() << "🔌 摄像头连接已断开";
        emit errorOccurred(QStringLiteral("摄像头连接已断开"));
    });

    m_connectTimer = new QTimer(this);
    m_connectTimer->setSingleShot(true);
    connect(m_connectTimer, &QTimer::timeout, this, &CameraWorker::onConnectTimeout);

    // 主线程已经先发了 0x12(CAMERA_ON)，但 car.py 的 camera_server 每 0.2s 才检查一次
    // camera_enabled 并进入 accept，所以这里延迟一下再连，否则会被直接拒绝。
    m_delayTimer = new QTimer(this);
    m_delayTimer->setSingleShot(true);
    connect(m_delayTimer, &QTimer::timeout, this, [this, ip, port]() {
        if (m_delayTimer) {
            m_delayTimer->deleteLater();
            m_delayTimer = nullptr;
        }
        if (!m_socket || m_socket->state() != QTcpSocket::UnconnectedState) return;
        m_socket->connectToHost(ip, port);
        if (m_connectTimer) m_connectTimer->start(3000);
        qDebug() << "📷 CameraWorker::start →" << ip << port;
    });
    m_delayTimer->start(300);
}

void CameraWorker::stop()
{
    if (m_delayTimer) {
        m_delayTimer->stop();
        m_delayTimer->deleteLater();
        m_delayTimer = nullptr;
    }
    if (m_connectTimer) {
        m_connectTimer->stop();
        m_connectTimer->deleteLater();
        m_connectTimer = nullptr;
    }
    if (m_socket) {
        m_socket->abort();
        m_socket->deleteLater();
        m_socket = nullptr;
    }
    m_buffer.clear();

    if (m_writer) {
        m_writer->close();
        delete m_writer;
        m_writer = nullptr;
    }
    bool wasRecording = m_recording;
    m_recording = false;
    m_pendingStart = false;

    if (wasRecording) emit recordingStopped();
}

void CameraWorker::onConnected()
{
    if (m_connectTimer) {
        m_connectTimer->stop();
        m_connectTimer->deleteLater();
        m_connectTimer = nullptr;
    }
    qDebug() << "✅ 摄像头TCP连接成功";
    emit connected();
}

void CameraWorker::onConnectTimeout()
{
    if (m_socket && m_socket->state() != QTcpSocket::ConnectedState) {
        emit errorOccurred(QStringLiteral("摄像头连接超时"));
        stop();
    }
}

void CameraWorker::onSocketError(QAbstractSocket::SocketError)
{
    const QString msg = m_socket ? m_socket->errorString()
                                 : QStringLiteral("camera socket error");
    emit errorOccurred(msg);
}

/* ---------------- 录像控制 ---------------- */
void CameraWorker::startRecording(const QString &filePath)
{
    if (m_recording) return;
    m_recordPath   = filePath;
    m_pendingStart = true;      // 等第一帧到了再 open（需要知道宽高）
    m_recording    = true;
    qDebug() << "🎥 准备录像：" << filePath;
}

void CameraWorker::stopRecording()
{
    if (m_writer) {
        m_writer->close();
        delete m_writer;
        m_writer = nullptr;
    }
    const bool was = m_recording;
    m_recording    = false;
    m_pendingStart = false;
    if (was) emit recordingStopped();
}

/* ---------------- 帧协议解析 ---------------- */
void CameraWorker::onReadyRead()
{
    if (!m_socket) return;
    m_buffer.append(m_socket->readAll());

    static constexpr int     HEAD_LEN  = 2;
    static constexpr int     LEN_LEN   = 4;
    static constexpr int     CRC_LEN   = 1;
    static constexpr quint32 MAX_FRAME = 200000;   // JPEG 帧体上限

    while (!m_buffer.isEmpty())
    {
        // 1. 找帧头
        int headerPos = findFrameHeader(m_buffer);
        if (headerPos < 0) {
            if (m_buffer.size() > 1) m_buffer = m_buffer.right(1);
            return;                                  // 等下一批数据
        }
        if (headerPos > 0) m_buffer.remove(0, headerPos);

        // 2. 长度字段是否到齐
        if (m_buffer.size() < HEAD_LEN + LEN_LEN) return;

        // 3. 解析长度（大端）
        quint32 payloadLen = qFromBigEndian<quint32>(
            reinterpret_cast<const uchar*>(m_buffer.constData() + HEAD_LEN));

        if (payloadLen == 0 || payloadLen > MAX_FRAME) {
            m_buffer.remove(0, 1);                   // 长度非法，丢 1 字节重新同步
            continue;
        }

        // 4. 整帧是否到齐
        const int totalLen = HEAD_LEN + LEN_LEN + int(payloadLen) + CRC_LEN;
        if (m_buffer.size() < totalLen) return;

        // 5. CRC8 校验（覆盖 LEN + PAYLOAD）
        const quint8 recvCrc = static_cast<quint8>(m_buffer[totalLen - 1]);
        const QByteArray crcRange = m_buffer.mid(HEAD_LEN, LEN_LEN + payloadLen);
        const quint8 calcCrc = crc8Update(0x00, crcRange);
        if (recvCrc != calcCrc) {
            qDebug() << "❌ 摄像头帧 CRC 校验失败";
            m_buffer.remove(0, 1);
            continue;
        }

        // 6. 取出帧体：8 字节大端时间戳 + JPEG
        const QByteArray payload = m_buffer.mid(HEAD_LEN + LEN_LEN, payloadLen);
        m_buffer.remove(0, totalLen);

        if (payload.size() < 8) continue;
        const quint64 ts_us = qFromBigEndian<quint64>(
            reinterpret_cast<const uchar*>(payload.constData()));
        const QByteArray jpegData = payload.mid(8);

        processPayload(jpegData, ts_us);
    }
}

void CameraWorker::processPayload(const QByteArray &jpegData, quint64 ts_us)
{
    if (jpegData.isEmpty()) return;

    // ① 交给主线程显示 / 拍照（原始 JPEG，主线程自己解码，避免跨线程传 QImage）
    emit videoFrame(jpegData, ts_us);

    // ② 录像：只写原始 JPEG，不重新编码
    if (!m_recording) return;

    if (m_pendingStart) {
        QImage probe;
        if (!probe.loadFromData(jpegData, "JPEG")) {
            qDebug() << "❌ 首帧解码失败，无法确定分辨率";
            emit recordingFailed(QStringLiteral("首帧解码失败，无法开始录像"));
            m_recording = false;
            m_pendingStart = false;
            return;
        }
        m_writer = new Mp4Writer();
        if (!m_writer->open(m_recordPath, probe.width(), probe.height())) {
            emit recordingFailed(QStringLiteral("无法创建 MP4 文件"));
            delete m_writer;
            m_writer = nullptr;
            m_recording = false;
            m_pendingStart = false;
            return;
        }
        m_pendingStart = false;
        qDebug() << "🎥 开始写入：" << m_recordPath
                 << probe.width() << "x" << probe.height();
        emit recordingStarted(m_recordPath);
    }

    if (m_writer && m_writer->isOpen()) {
        m_writer->writeFrame(jpegData, ts_us);
    }
}

/* ---------------- 工具函数 ---------------- */
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
    for (int i = 0; i + 1 < buf.size(); ++i) {
        if (static_cast<quint8>(buf[i])     == 0xAA &&
            static_cast<quint8>(buf[i + 1]) == 0x55) {
            return i;
        }
    }
    return -1;
}
