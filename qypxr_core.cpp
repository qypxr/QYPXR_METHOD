#define _SILENCE_CXX20_U8PATH_DEPRECATION_WARNING

#include "qypxr_core.h"
#include <fstream>
#include <filesystem>
#include <chrono>
#include <vector>
#include <stdexcept>
#include <string>
#include <cstdint>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <initializer_list>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

extern "C" const volatile char QYPXR_WATERMARK[] = "QYPXR_METHOD_ULTIMATE_PROTECTED_C++_VERSION";

namespace fs = std::filesystem;

namespace Qypxr {

    void SecurityCheck() {
        // ВАЖНО: Если запускаешь из Visual Studio (F5) в конфигурации Release,
        // IsDebuggerPresent() вернет true и закроет программу.
        // Раскомментируй строчку ниже только перед финальной сборкой для пользователей:
        // #ifndef _DEBUG
        // if (IsDebuggerPresent()) { ExitProcess(0); }
        // #endif
    }

    inline uint32_t Read32(const uint8_t* p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3]; }
    inline void Write32(uint8_t* p, uint32_t v) { p[0] = (v >> 24) & 0xFF; p[1] = (v >> 16) & 0xFF; p[2] = (v >> 8) & 0xFF; p[3] = v & 0xFF; }
    inline uint64_t Read64(const uint8_t* p) { return ((uint64_t)Read32(p) << 32) | Read32(p + 4); }
    inline void Write64(uint8_t* p, uint64_t v) { Write32(p, (uint32_t)(v >> 32)); Write32(p + 4, (uint32_t)(v & 0xFFFFFFFF)); }

    struct Box {
        size_t offset = 0;
        size_t size = 0;
        std::string type = "";
        size_t end = 0;
        bool is64bit = false;
    };

    size_t HeaderSize(const Box& b) { return b.is64bit ? 16 : 8; }

    std::vector<Box> ParseBoxes(const std::vector<uint8_t>& data, size_t start, size_t end) {
        std::vector<Box> boxes;
        size_t offset = start;
        size_t limit = std::min(end, data.size());
        while (offset + 8 <= limit) {
            uint32_t raw_size = Read32(&data[offset]);
            bool is64bit = false;
            size_t size = raw_size;
            if (raw_size == 0) { size = limit - offset; }
            else if (raw_size == 1) {
                is64bit = true;
                if (offset + 16 > limit) break;
                size = (size_t)Read64(&data[offset + 8]);
            }
            if (size < (is64bit ? 16u : 8u) || offset + size > limit) break;
            std::string type(reinterpret_cast<const char*>(&data[offset + 4]), 4);
            boxes.push_back({ offset, size, type, offset + size, is64bit });
            offset += size;
        }
        return boxes;
    }

    void UpdateBoxSize(std::vector<uint8_t>& buf, size_t offset, const Box& box, int64_t added_bytes) {
        if (box.is64bit) {
            if (offset + 16 <= buf.size()) Write64(&buf[offset + 8], (uint64_t)((int64_t)box.size + added_bytes));
        }
        else {
            if (offset + 4 <= buf.size()) Write32(&buf[offset], (uint32_t)((int64_t)box.size + added_bytes));
        }
    }

    void UpdateChunkOffsets(std::vector<uint8_t>& buf, size_t start, size_t end, int64_t delta) {
        auto boxes = ParseBoxes(buf, start, end);
        for (const auto& b : boxes) {
            if (b.type == "stco") {
                size_t hs = HeaderSize(b);
                if (b.offset + hs + 8 > b.end) continue;
                uint32_t count = Read32(&buf[b.offset + hs + 4]);
                for (uint32_t i = 0; i < count; ++i) {
                    size_t pos = b.offset + hs + 8 + (size_t)i * 4;
                    if (pos + 4 <= b.end) Write32(&buf[pos], (uint32_t)((int64_t)Read32(&buf[pos]) + delta));
                }
            }
            else if (b.type == "co64") {
                size_t hs = HeaderSize(b);
                if (b.offset + hs + 8 > b.end) continue;
                uint32_t count = Read32(&buf[b.offset + hs + 4]);
                for (uint32_t i = 0; i < count; ++i) {
                    size_t pos = b.offset + hs + 8 + (size_t)i * 8;
                    if (pos + 8 <= b.end) Write64(&buf[pos], (uint64_t)((int64_t)Read64(&buf[pos]) + delta));
                }
            }
            else if (b.type == "moov" || b.type == "trak" || b.type == "mdia" || b.type == "minf" || b.type == "stbl") {
                UpdateChunkOffsets(buf, b.offset + HeaderSize(b), b.end, delta);
            }
        }
    }

    std::string DetectCodecFromStbl(const std::vector<uint8_t>& data, const Box& stbl) {
        auto children = ParseBoxes(data, stbl.offset + HeaderSize(stbl), stbl.end);
        for (const auto& b : children) {
            if (b.type == "stsd") {
                size_t content_start = b.offset + HeaderSize(b);
                if (content_start + 16 <= b.end) return std::string(reinterpret_cast<const char*>(&data[content_start + 12]), 4);
            }
        }
        return "unknown";
    }

    bool FindVideoStbl(const std::vector<uint8_t>& data, const Box& moov, Box& outTrak, Box& outMdia, Box& outMinf, Box& outStbl) {
        auto moov_children = ParseBoxes(data, moov.offset + HeaderSize(moov), moov.end);
        for (const auto& trak : moov_children) {
            if (trak.type != "trak") continue;
            auto trak_children = ParseBoxes(data, trak.offset + HeaderSize(trak), trak.end);
            for (const auto& mdia : trak_children) {
                if (mdia.type != "mdia") continue;
                auto mdia_children = ParseBoxes(data, mdia.offset + HeaderSize(mdia), mdia.end);
                bool isVide = false;
                for (const auto& hdlr : mdia_children) {
                    if (hdlr.type == "hdlr") {
                        size_t start = hdlr.offset + HeaderSize(hdlr);
                        if (start + 12 <= hdlr.end) {
                            for (size_t i = start; i + 4 <= hdlr.end; ++i) {
                                if (data[i] == 'v' && data[i + 1] == 'i' && data[i + 2] == 'd' && data[i + 3] == 'e') { isVide = true; break; }
                            }
                        }
                    }
                }
                if (!isVide) continue;
                for (const auto& minf : mdia_children) {
                    if (minf.type != "minf") continue;
                    auto minf_children = ParseBoxes(data, minf.offset + HeaderSize(minf), minf.end);
                    for (const auto& stbl : minf_children) {
                        if (stbl.type == "stbl") {
                            outTrak = trak; outMdia = mdia; outMinf = minf; outStbl = stbl;
                            return true;
                        }
                    }
                }
            }
        }
        return false;
    }

    bool SparseSampleTable(std::vector<uint8_t>& data, int targetFps) {
        auto top = ParseBoxes(data, 0, data.size());
        const Box* ftyp = nullptr; const Box* moov = nullptr; const Box* mdat = nullptr;
        std::vector<Box> frees;
        for (const auto& b : top) {
            if (b.type == "ftyp" && !ftyp) ftyp = &b;
            else if (b.type == "moov" && !moov) moov = &b;
            else if (b.type == "mdat" && !mdat) mdat = &b;
            else if (b.type == "free") frees.push_back(b);
        }
        if (!ftyp || !moov || !mdat) return false;

        Box trak, mdia, minf, stbl;
        if (!FindVideoStbl(data, *moov, trak, mdia, minf, stbl)) return false;

        auto ch = ParseBoxes(data, stbl.offset + HeaderSize(stbl), stbl.end);
        const Box* stts = nullptr, * stsz = nullptr, * stsc = nullptr, * stco = nullptr, * co64 = nullptr;
        for (const auto& b : ch) {
            if (b.type == "stts") stts = &b;
            else if (b.type == "stsz") stsz = &b;
            else if (b.type == "stsc") stsc = &b;
            else if (b.type == "stco") stco = &b;
            else if (b.type == "co64") co64 = &b;
        }
        if (!stts || !stsz || !stsc || (!stco && !co64)) return false;

        // Проверка границ stts
        if (stts->offset + 16 > stts->end) return false;
        uint32_t ne = Read32(&data[stts->offset + 12]);
        if (stts->offset + 16 + (size_t)ne * 8 > stts->end) return false;

        uint64_t N = 0, tot = 0;
        for (uint32_t i = 0; i < ne; ++i) {
            uint32_t c = Read32(&data[stts->offset + 16 + (size_t)i * 8]);
            uint32_t d = Read32(&data[stts->offset + 16 + (size_t)i * 8 + 4]);
            N += c; tot += (uint64_t)c * d;
        }
        if (N < 2) return false;

        // Проверка границ stsz (защита от краша при uniform sample_size или несовпадении N)
        if (stsz->offset + 20 > stsz->end) return false;
        uint32_t uniform_sample_size = Read32(&data[stsz->offset + 12]);
        uint32_t stsz_count = Read32(&data[stsz->offset + 16]);
        if (uniform_sample_size == 0) {
            if (stsz_count < N) N = stsz_count;
            if (stsz->offset + 20 + (size_t)N * 4 > stsz->end) return false;
        }
        if (N < 2) return false;

        uint32_t delta = (uint32_t)std::llround((double)tot / (double)N);
        if (delta == 0) delta = 1;

        // Длительность и timescale видео из mdhd с проверкой границ
        auto mdiaCh = ParseBoxes(data, mdia.offset + HeaderSize(mdia), mdia.end);
        const Box* mdhd = nullptr;
        for (const auto& b : mdiaCh) if (b.type == "mdhd") mdhd = &b;
        if (!mdhd || mdhd->offset + 28 > mdhd->end) return false;

        uint32_t ts = 0; uint64_t dur = 0;
        uint8_t mdhd_ver = data[mdhd->offset + 8];
        if (mdhd_ver == 0) {
            ts = Read32(&data[mdhd->offset + 20]);
            dur = Read32(&data[mdhd->offset + 24]);
        }
        else {
            if (mdhd->offset + 40 > mdhd->end) return false;
            ts = Read32(&data[mdhd->offset + 28]);
            dur = Read64(&data[mdhd->offset + 32]);
        }
        if (ts == 0) return false;

        uint64_t total = (uint64_t)((double)dur / (double)ts * (double)targetFps);
        if (total <= N) total = N + (uint64_t)targetFps; // Гарантируем добавление сэмплов даже на коротких видео
        if (total > 5000000ull) total = 5000000ull;       // Защита от переполнения памяти
        uint32_t K = (uint32_t)(total - N);

        const Box* chunkBox = stco ? stco : co64;
        const uint32_t esz = stco ? 4 : 8;
        if (chunkBox->offset + 16 > chunkBox->end) return false;
        uint32_t origChunks = Read32(&data[chunkBox->offset + 12]);
        if (chunkBox->offset + 16 + (size_t)origChunks * esz > chunkBox->end) return false;

        // Проверка границ stsc
        if (stsc->offset + 16 > stsc->end) return false;
        uint32_t scN = Read32(&data[stsc->offset + 12]);
        if (stsc->offset + 16 + (size_t)scN * 12 > stsc->end) return false;

        // --- Формирование новых таблиц
        std::vector<uint8_t> nStts(32, 0);
        Write32(&nStts[0], 32); std::memcpy(&nStts[4], "stts", 4); Write32(&nStts[12], 2);
        Write32(&nStts[16], (uint32_t)(N - 1)); Write32(&nStts[20], delta); Write32(&nStts[24], 1); Write32(&nStts[28], 1);

        uint32_t totalCount = (uint32_t)total;
        std::vector<uint8_t> nStsz(20 + (size_t)totalCount * 4, 0);
        Write32(&nStsz[0], (uint32_t)nStsz.size()); std::memcpy(&nStsz[4], "stsz", 4); Write32(&nStsz[16], totalCount);
        for (uint32_t i = 0; i < (uint32_t)N; ++i) {
            uint32_t szVal = (uniform_sample_size != 0) ? uniform_sample_size : Read32(&data[stsz->offset + 20 + (size_t)i * 4]);
            Write32(&nStsz[20 + (size_t)i * 4], szVal);
        }
        for (uint32_t i = (uint32_t)N; i < totalCount; ++i) {
            Write32(&nStsz[20 + (size_t)i * 4], 8);
        }

        std::vector<uint8_t> nStsc(16 + (size_t)(scN + 1) * 12, 0);
        Write32(&nStsc[0], (uint32_t)nStsc.size()); std::memcpy(&nStsc[4], "stsc", 4); Write32(&nStsc[12], scN + 1);
        if (scN > 0) std::memcpy(&nStsc[16], &data[stsc->offset + 16], (size_t)scN * 12);
        Write32(&nStsc[16 + (size_t)scN * 12], origChunks + 1);
        Write32(&nStsc[16 + (size_t)scN * 12 + 4], 1);
        Write32(&nStsc[16 + (size_t)scN * 12 + 8], 1);

        size_t chunkNewSize = 16 + (size_t)(origChunks + K) * esz;
        int64_t dMoov = ((int64_t)nStts.size() - (int64_t)stts->size)
            + ((int64_t)nStsz.size() - (int64_t)stsz->size)
            + ((int64_t)nStsc.size() - (int64_t)stsc->size)
            + ((int64_t)chunkNewSize - (int64_t)chunkBox->size);

        size_t freesSize = 0; for (const auto& f : frees) freesSize += f.size;
        if ((int64_t)moov->size + dMoov <= 0) return false;
        size_t newMoovSize = (size_t)((int64_t)moov->size + dMoov);
        size_t pre = ftyp->size + freesSize + newMoovSize;
        int64_t shift = (int64_t)pre - (int64_t)mdat->offset;
        size_t fileSize = pre + mdat->size + 8;
        size_t fake = fileSize - 8;

        std::vector<uint8_t> nChunk(chunkNewSize, 0);
        Write32(&nChunk[0], (uint32_t)chunkNewSize);
        std::memcpy(&nChunk[4], stco ? "stco" : "co64", 4);
        Write32(&nChunk[12], origChunks + K);
        if (stco) {
            for (uint32_t i = 0; i < origChunks; ++i)
                Write32(&nChunk[16 + (size_t)i * 4], (uint32_t)((int64_t)Read32(&data[stco->offset + 16 + (size_t)i * 4]) + shift));
            for (uint32_t i = 0; i < K; ++i)
                Write32(&nChunk[16 + (size_t)(origChunks + i) * 4], (uint32_t)fake);
        }
        else {
            for (uint32_t i = 0; i < origChunks; ++i)
                Write64(&nChunk[16 + (size_t)i * 8], (uint64_t)((int64_t)Read64(&data[co64->offset + 16 + (size_t)i * 8]) + shift));
            for (uint32_t i = 0; i < K; ++i)
                Write64(&nChunk[16 + (size_t)(origChunks + i) * 8], (uint64_t)fake);
        }

        // --- Сборка нового moov
        struct Rep { const Box* box; const std::vector<uint8_t>* rep; };
        std::vector<Rep> reps = { { stts, &nStts },{ stsz, &nStsz },{ stsc, &nStsc },{ chunkBox, &nChunk } };
        std::sort(reps.begin(), reps.end(), [](const Rep& a, const Rep& b) { return a.box->offset < b.box->offset; });

        std::vector<uint8_t> nm; nm.reserve(newMoovSize);
        size_t rp = moov->offset;
        for (const auto& r : reps) {
            if (r.box->offset < rp || r.box->end > moov->end) return false;
            nm.insert(nm.end(), data.begin() + rp, data.begin() + r.box->offset);
            nm.insert(nm.end(), r.rep->begin(), r.rep->end());
            rp = r.box->end;
        }
        nm.insert(nm.end(), data.begin() + rp, data.begin() + moov->end);

        for (const Box* b : std::initializer_list<const Box*>{ &stbl, &minf, &mdia, &trak, moov }) {
            size_t off = b->offset - moov->offset;
            if (b->is64bit) {
                if (off + 16 <= nm.size()) Write64(&nm[off + 8], (uint64_t)((int64_t)b->size + dMoov));
            }
            else {
                if (off + 4 <= nm.size()) Write32(&nm[off], (uint32_t)((int64_t)b->size + dMoov));
            }
        }

        // Коррекция смещений остальных дорожек (аудио и т.д.)
        size_t vstblRel = stbl.offset - moov->offset;
        auto mch = ParseBoxes(nm, HeaderSize(*moov), nm.size());
        for (const auto& t : mch) {
            if (t.type != "trak") continue;
            for (const auto& md : ParseBoxes(nm, t.offset + HeaderSize(t), t.end)) {
                if (md.type != "mdia") continue;
                for (const auto& mi : ParseBoxes(nm, md.offset + HeaderSize(md), md.end)) {
                    if (mi.type != "minf") continue;
                    for (const auto& sb : ParseBoxes(nm, mi.offset + HeaderSize(mi), mi.end)) {
                        if (sb.type != "stbl" || sb.offset == vstblRel) continue;
                        for (const auto& b : ParseBoxes(nm, sb.offset + HeaderSize(sb), sb.end)) {
                            if (b.type == "stco" && b.offset + 16 <= b.end) {
                                uint32_t c = Read32(&nm[b.offset + 12]);
                                for (uint32_t i = 0; i < c; ++i) {
                                    size_t q = b.offset + 16 + (size_t)i * 4;
                                    if (q + 4 <= b.end) Write32(&nm[q], (uint32_t)((int64_t)Read32(&nm[q]) + shift));
                                }
                            }
                            else if (b.type == "co64" && b.offset + 16 <= b.end) {
                                uint32_t c = Read32(&nm[b.offset + 12]);
                                for (uint32_t i = 0; i < c; ++i) {
                                    size_t q = b.offset + 16 + (size_t)i * 8;
                                    if (q + 8 <= b.end) Write64(&nm[q], (uint64_t)((int64_t)Read64(&nm[q]) + shift));
                                }
                            }
                        }
                    }
                }
            }
        }

        // --- Финальная сборка контейнера
        std::vector<uint8_t> res; res.reserve(fileSize);
        res.insert(res.end(), data.begin() + ftyp->offset, data.begin() + ftyp->end);
        for (const auto& f : frees) res.insert(res.end(), data.begin() + f.offset, data.begin() + f.end);
        res.insert(res.end(), nm.begin(), nm.end());
        size_t mdatPos = res.size();
        res.insert(res.end(), data.begin() + mdat->offset, data.begin() + mdat->end);
        if (mdat->is64bit) {
            if (mdatPos + 16 <= res.size()) Write64(&res[mdatPos + 8], mdat->size + 8);
        }
        else {
            if (mdatPos + 4 <= res.size()) Write32(&res[mdatPos], (uint32_t)(mdat->size + 8));
        }
        const uint8_t anchor[8] = { 0, 0, 0, 4, 0, 0, 0, 0 };
        res.insert(res.end(), anchor, anchor + 8);
        if (res.size() != fileSize) return false;
        data = std::move(res);
        return true;
    }

    Result OptimizeVideoSparse(const std::string& inputPath, int targetFps) {
        SecurityCheck();
        Result res;
        auto start = std::chrono::high_resolution_clock::now();
        try {
            (void)QYPXR_WATERMARK[0];
            fs::path inPath = fs::u8path(inputPath);
            fs::path stem = inPath.stem();
            stem += "_QYPXR";
            stem += inPath.has_extension() ? inPath.extension() : fs::path(".mp4");
            fs::path outPath = inPath.parent_path() / stem;
            auto u8 = outPath.u8string();
            res.outputPath = std::string(reinterpret_cast<const char*>(u8.data()), u8.size());

            std::ifstream inFile(inPath, std::ios::binary | std::ios::ate);
            if (!inFile.is_open()) throw std::runtime_error("Не удалось открыть исходный файл");
            std::streamsize streamSz = inFile.tellg();
            if (streamSz <= 32) throw std::runtime_error("Файл слишком мал или повреждён");
            size_t fileSize = (size_t)streamSz;
            inFile.seekg(0, std::ios::beg);

            std::vector<uint8_t> buffer(fileSize);
            if (!inFile.read(reinterpret_cast<char*>(buffer.data()), streamSz)) throw std::runtime_error("Ошибка чтения файла");
            inFile.close();
            res.originalSize = fileSize;

            if (targetFps < 60) targetFps = 400;
            if (!SparseSampleTable(buffer, targetFps)) throw std::runtime_error("Неподдерживаемая структура контейнера");
            res.processedSize = buffer.size();

            std::ofstream outFile(outPath, std::ios::binary);
            if (!outFile.is_open()) throw std::runtime_error("Не удалось сохранить результат");
            outFile.write(reinterpret_cast<const char*>(buffer.data()), (std::streamsize)buffer.size());
            outFile.close();

            res.success = true;
            res.message = "Оптимизация успешна";
        }
        catch (const std::exception& e) {
            res.success = false;
            res.message = e.what();
        }
        catch (...) {
            res.success = false;
            res.message = "Неизвестная системная ошибка при обработке";
        }
        res.timeSeconds = std::chrono::duration<double>(std::chrono::high_resolution_clock::now() - start).count();
        return res;
    }

    Result OptimizeVideo(const std::string& inputPath, int multiplier) {
        return OptimizeVideoSparse(inputPath, multiplier * 60);
    }
}