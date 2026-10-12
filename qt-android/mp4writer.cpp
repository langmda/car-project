#include "mp4writer.h"
#include <QDebug>
#include <cstring>

Mp4Writer::Mp4Writer() {}
Mp4Writer::~Mp4Writer() { if (m_file.isOpen()) close(); }

void Mp4Writer::appendU16(QByteArray &b, quint16 v) {
    b.append(char((v >> 8) & 0xFF));
    b.append(char(v & 0xFF));
}
void Mp4Writer::appendU32(QByteArray &b, quint32 v) {
    b.append(char((v >> 24) & 0xFF));
    b.append(char((v >> 16) & 0xFF));
    b.append(char((v >> 8) & 0xFF));
    b.append(char(v & 0xFF));
}
void Mp4Writer::appendU64(QByteArray &b, quint64 v) {
    appendU32(b, quint32(v >> 32));
    appendU32(b, quint32(v & 0xFFFFFFFF));
}
void Mp4Writer::appendFourCC(QByteArray &b, const char *s) {
    b.append(s, 4);
}
void Mp4Writer::appendBoxHdr(QByteArray &b, const char *type, quint32 size) {
    appendU32(b, size);
    appendFourCC(b, type);
}

/* ---------------- open ---------------- */
bool Mp4Writer::open(const QString &filename, int width, int height)
{
    m_file.setFileName(filename);
    if (!m_file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qDebug() << "Mp4Writer::open:" << m_file.errorString();
        return false;
    }
    m_width = width;
    m_height = height;
    m_samples.clear();
    m_chunks.clear();
    m_curChunkStartTs = 0;
    m_hasChunk = false;

    // ftyp
    QByteArray ftyp;
    appendBoxHdr(ftyp, "ftyp", 24);
    appendFourCC(ftyp, "mp42");
    appendU32(ftyp, 0);
    appendFourCC(ftyp, "mp42");
    appendFourCC(ftyp, "isom");
    m_file.write(ftyp);

    // mdat 头（size 占位）
    m_mdatStart = m_file.pos();
    QByteArray mdatHdr;
    appendU32(mdatHdr, 0);
    appendFourCC(mdatHdr, "mdat");
    m_file.write(mdatHdr);
    m_mdatDataStart = m_file.pos();

    return true;
}

/* ---------------- writeFrame ---------------- */
bool Mp4Writer::writeFrame(const QByteArray &jpegData, quint64 ts_us)
{
    if (!m_file.isOpen() || jpegData.isEmpty()) return false;

    // ---- 判断是否需要开新 chunk ----
    bool needNewChunk = false;
    if (!m_hasChunk) {
        needNewChunk = true;                       // 第一帧，开第一个 chunk
    } else if (ts_us - m_curChunkStartTs >= CHUNK_DURATION_US) {
        needNewChunk = true;                       // 距当前 chunk 起点已满 1 秒
    }

    if (needNewChunk) {
        Chunk c;
        c.offset = quint32(m_file.pos());          // 新 chunk 从当前写入位置开始
        c.count  = 0;
        m_chunks.append(c);
        m_curChunkStartTs = ts_us;
        m_hasChunk = true;
    }

    // ---- 写数据 ----
    Sample s;
    s.size = quint32(jpegData.size());
    s.ts   = ts_us;
    m_file.write(jpegData);
    m_samples.append(s);

    // ---- 计入当前 chunk ----
    m_chunks.last().count += 1;

    return true;
}

/* ---------------- stts ---------------- */
QByteArray Mp4Writer::buildStts() const
{
    QByteArray p;
    appendU32(p, 0);               // version 0, flags 0

    int n = m_samples.size();
    QVector<QPair<quint32, quint32>> entries;  // (count, delta)

    if (n == 0) {
        appendU32(p, 0);
    } else if (n == 1) {
        entries.append({1, 33333});
    } else {
        for (int i = 0; i < n - 1; ++i) {
            quint64 delta = m_samples[i + 1].ts - m_samples[i].ts;
            if (delta == 0) delta = 33333;
            entries.append({1, quint32(delta)});
        }
        quint64 lastDelta = m_samples[n - 1].ts - m_samples[n - 2].ts;
        if (lastDelta == 0) lastDelta = 33333;
        entries.append({1, quint32(lastDelta)});
    }

    appendU32(p, entries.size());
    for (const auto &e : entries) {
        appendU32(p, e.first);
        appendU32(p, e.second);
    }

    QByteArray box;
    appendBoxHdr(box, "stts", p.size() + 8);
    box.append(p);
    return box;
}

/* ---------------- stsc（多 chunk 版） ---------------- */
/*
 * stsc 语义：(first_chunk, samples_per_chunk, sample_description_index)
 * 表示"从第 first_chunk 个 chunk 开始，每个 chunk 装 samples_per_chunk 个 sample"。
 *
 * 这里为了逻辑最清晰，每个 chunk 都写一条 entry（不做合并）。
 * 真实编码器会把连续相同 samples_per_chunk 的 chunk 合并成一条，减少表大小。
 */
QByteArray Mp4Writer::buildStsc() const
{
    QByteArray p;
    appendU32(p, 0);                          // version 0, flags 0

    const int chunkCount = m_chunks.size();
    appendU32(p, quint32(chunkCount));        // entry_count：每个 chunk 一条

    for (int i = 0; i < chunkCount; ++i) {
        appendU32(p, quint32(i + 1));         // first_chunk（1-based）
        appendU32(p, m_chunks[i].count);      // samples_per_chunk
        appendU32(p, 1);                      // sample_description_index
    }

    QByteArray box;
    appendBoxHdr(box, "stsc", p.size() + 8);
    box.append(p);
    return box;
}

/* ---------------- stco（多 chunk 版） ---------------- */
QByteArray Mp4Writer::buildStco() const
{
    QByteArray p;
    appendU32(p, 0);                          // version 0, flags 0
    appendU32(p, quint32(m_chunks.size()));   // entry_count
    for (const auto &c : m_chunks) {
        appendU32(p, c.offset);               // 每个 chunk 的文件偏移
    }

    QByteArray box;
    appendBoxHdr(box, "stco", p.size() + 8);
    box.append(p);
    return box;
}

/* ---------------- buildMoov ---------------- */
QByteArray Mp4Writer::buildMoov() const
{
    quint64 totalUs = 0;
    int n = m_samples.size();
    if (n >= 2) {
        totalUs = m_samples.last().ts - m_samples.first().ts
                  + (m_samples.last().ts - m_samples[n - 2].ts);
    } else if (n == 1) {
        totalUs = 33333;
    }

    const quint32 timescale = 1000000;

    // ---- mvhd ----
    QByteArray mvhd;
    {
        QByteArray p;
        appendU32(p, 0);
        appendU32(p, 0);
        appendU32(p, 0);
        appendU32(p, timescale);
        appendU32(p, quint32(totalUs));
        appendU32(p, 0x00010000);
        appendU16(p, 0x0100);
        appendU16(p, 0);
        appendU32(p, 0); appendU32(p, 0);
        appendU32(p, 0x00010000); appendU32(p, 0); appendU32(p, 0);
        appendU32(p, 0); appendU32(p, 0x00010000); appendU32(p, 0);
        appendU32(p, 0); appendU32(p, 0); appendU32(p, 0x40000000);
        for (int i = 0; i < 6; ++i) appendU32(p, 0);
        appendU32(p, 2);
        appendBoxHdr(mvhd, "mvhd", p.size() + 8);
        mvhd.append(p);
    }

    // ---- tkhd ----
    QByteArray tkhd;
    {
        QByteArray p;
        appendU32(p, 0x00000007);
        appendU32(p, 0);
        appendU32(p, 0);
        appendU32(p, 1);
        appendU32(p, 0);
        appendU32(p, quint32(totalUs));
        appendU32(p, 0); appendU32(p, 0);
        appendU16(p, 0);
        appendU16(p, 0);
        appendU16(p, 0);
        appendU16(p, 0);
        appendU32(p, 0x00010000); appendU32(p, 0); appendU32(p, 0);
        appendU32(p, 0); appendU32(p, 0x00010000); appendU32(p, 0);
        appendU32(p, 0); appendU32(p, 0); appendU32(p, 0x40000000);
        appendU32(p, quint32(m_width) << 16);
        appendU32(p, quint32(m_height) << 16);
        appendBoxHdr(tkhd, "tkhd", p.size() + 8);
        tkhd.append(p);
    }

    // ---- mdhd ----
    QByteArray mdhd;
    {
        QByteArray p;
        appendU32(p, 0);
        appendU32(p, 0);
        appendU32(p, 0);
        appendU32(p, timescale);
        appendU32(p, quint32(totalUs));
        appendU16(p, 0x55C4);
        appendU16(p, 0);
        appendBoxHdr(mdhd, "mdhd", p.size() + 8);
        mdhd.append(p);
    }

    // ---- hdlr ----
    QByteArray hdlr;
    {
        QByteArray p;
        appendU32(p, 0);
        appendU32(p, 0);
        appendFourCC(p, "vide");
        appendU32(p, 0); appendU32(p, 0); appendU32(p, 0);
        const char *name = "VideoHandler";
        p.append(name, int(std::strlen(name)) + 1);
        appendBoxHdr(hdlr, "hdlr", p.size() + 8);
        hdlr.append(p);
    }

    // ---- vmhd ----
    QByteArray vmhd;
    {
        QByteArray p;
        appendU32(p, 0x00000001);
        appendU16(p, 0);
        appendU16(p, 0); appendU16(p, 0); appendU16(p, 0);
        appendBoxHdr(vmhd, "vmhd", p.size() + 8);
        vmhd.append(p);
    }

    // ---- dref ----
    QByteArray dref;
    {
        QByteArray p;
        appendU32(p, 0);
        appendU32(p, 1);
        appendU32(p, 12);
        appendFourCC(p, "url ");
        appendU32(p, 0x00000001);
        appendBoxHdr(dref, "dref", p.size() + 8);
        dref.append(p);
    }

    // ---- dinf ----
    QByteArray dinf;
    {
        QByteArray p = dref;
        appendBoxHdr(dinf, "dinf", p.size() + 8);
        dinf.append(p);
    }

    // ---- stsd (MJPEG) ----
    QByteArray stsd;
    {
        QByteArray p;
        appendU32(p, 0);
        appendU32(p, 1);

        QByteArray vseP;
        for (int i = 0; i < 6; ++i) vseP.append('\0');
        appendU16(vseP, 1);
        appendU16(vseP, 0);
        appendU16(vseP, 0);
        for (int i = 0; i < 3; ++i) appendU32(vseP, 0);
        appendU16(vseP, quint16(m_width));
        appendU16(vseP, quint16(m_height));
        appendU32(vseP, 0x00480000);
        appendU32(vseP, 0x00480000);
        appendU32(vseP, 0);
        appendU16(vseP, 1);
        char comp[32] = {0};
        vseP.append(comp, 32);
        appendU16(vseP, 0x0018);
        appendU16(vseP, 0xFFFF);

        QByteArray vse;
        appendBoxHdr(vse, "mjpg", vseP.size() + 8);
        vse.append(vseP);
        p.append(vse);

        appendBoxHdr(stsd, "stsd", p.size() + 8);
        stsd.append(p);
    }

    // ---- stts / stsc / stsz / stco ----
    QByteArray stts = buildStts();
    QByteArray stsc = buildStsc();     // 多 chunk 版
    QByteArray stco = buildStco();     // 多 chunk 版

    QByteArray stsz;
    {
        QByteArray p;
        appendU32(p, 0);
        appendU32(p, 0);
        appendU32(p, m_samples.size());
        for (const auto &s : m_samples) appendU32(p, s.size);
        appendBoxHdr(stsz, "stsz", p.size() + 8);
        stsz.append(p);
    }

    // ---- stbl ----
    QByteArray stbl;
    {
        QByteArray p;
        p.append(stsd); p.append(stts);
        p.append(stsc); p.append(stsz); p.append(stco);
        appendBoxHdr(stbl, "stbl", p.size() + 8);
        stbl.append(p);
    }

    // ---- minf ----
    QByteArray minf;
    {
        QByteArray p;
        p.append(vmhd); p.append(dinf); p.append(stbl);
        appendBoxHdr(minf, "minf", p.size() + 8);
        minf.append(p);
    }

    // ---- mdia ----
    QByteArray mdia;
    {
        QByteArray p;
        p.append(mdhd); p.append(hdlr); p.append(minf);
        appendBoxHdr(mdia, "mdia", p.size() + 8);
        mdia.append(p);
    }

    // ---- trak ----
    QByteArray trak;
    {
        QByteArray p;
        p.append(tkhd); p.append(mdia);
        appendBoxHdr(trak, "trak", p.size() + 8);
        trak.append(p);
    }

    // ---- moov ----
    QByteArray moov;
    {
        QByteArray p;
        p.append(mvhd); p.append(trak);
        appendBoxHdr(moov, "moov", p.size() + 8);
        moov.append(p);
    }

    return moov;
}

/* ---------------- close ---------------- */
bool Mp4Writer::close()
{
    if (!m_file.isOpen()) return false;

    qint64 mdatEnd = m_file.pos();
    quint32 mdatSize = quint32(mdatEnd - m_mdatStart);
    m_file.seek(m_mdatStart);
    QByteArray sz;
    appendU32(sz, mdatSize);
    m_file.write(sz);

    m_file.seek(mdatEnd);
    QByteArray moov = buildMoov();
    m_file.write(moov);

    m_file.flush();
    m_file.close();
    return true;
}
