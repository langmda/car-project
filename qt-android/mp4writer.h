#ifndef MP4WRITER_H
#define MP4WRITER_H

#include <QFile>
#include <QString>
#include <QByteArray>
#include <QVector>

class Mp4Writer
{
public:
    Mp4Writer();
    ~Mp4Writer();

    bool open(const QString &filename, int width, int height);
    bool writeFrame(const QByteArray &jpegData, quint64 ts_us);
    bool close();

    bool isOpen() const { return m_file.isOpen(); }

private:
    struct Sample {
        quint32 size;
        quint64 ts;
    };

    // 一个 chunk 的描述：起始偏移 + 包含多少帧
    struct Chunk {
        quint32 offset;   // 该 chunk 数据在文件中的字节偏移
        quint32 count;    // 该 chunk 包含的 sample（帧）数
    };

    static const quint64 CHUNK_DURATION_US = 1000000; // 每 chunk 约 1 秒

    static void appendU16(QByteArray &b, quint16 v);
    static void appendU32(QByteArray &b, quint32 v);
    static void appendU64(QByteArray &b, quint64 v);
    static void appendFourCC(QByteArray &b, const char *s);
    static void appendBoxHdr(QByteArray &b, const char *type, quint32 size);

    QByteArray buildMoov() const;
    QByteArray buildStts() const;
    QByteArray buildStsc() const;   // 新增
    QByteArray buildStco() const;   // 新增

    QFile m_file;
    int m_width = 0;
    int m_height = 0;
    qint64 m_mdatStart = 0;
    qint64 m_mdatDataStart = 0;
    QVector<Sample> m_samples;
    QVector<Chunk>  m_chunks;        // 新增
    quint64 m_curChunkStartTs = 0;   // 当前 chunk 第一帧的 ts
    bool    m_hasChunk = false;      // 是否已经开过 chunk
};

#endif // MP4WRITER_H
