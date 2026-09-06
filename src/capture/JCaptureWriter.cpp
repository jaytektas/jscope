#include "JCaptureWriter.h"

#include "JCaptureFormat.h"
#include "scope/JScopeDriver.h"
#include "scope/JScopeFrame.h"
#include "scope/JScopeLog.h"

#include <j/concurrent/WorkerThread.h>
#include <j/config/Json.h>
#include <j/core/MainThreadDispatcher.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <cstring>
#include <fstream>
#include <mutex>
#include <vector>

inline namespace jf {

namespace {

std::string nowUtcIso8601() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t t = std::chrono::system_clock::to_time_t(now);
    // Both spellings of the same thing: gmtime_r is POSIX, gmtime_s is what the
    // Windows CRT provides, and plain gmtime returns a shared static that a
    // second thread can overwrite between the call and the strftime.
    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    char buf[32];
    std::strftime(buf, sizeof buf, "%Y-%m-%dT%H:%M:%SZ", &tm);
    return buf;
}

JJson metaToJson(const JCaptureMeta& m) {
    JJson j = JJson::object();
    j["driverId"]            = m.driverId;
    j["model"]               = m.model;
    j["serialNumber"]        = m.serialNumber;
    j["operatorNote"]        = m.operatorNote;
    j["startedAtUtc"]        = m.startedAtUtc;
    j["channelCount"]        = static_cast<double>(m.channelCount);
    j["adcBits"]             = static_cast<double>(m.adcBits);
    j["countsMin"]           = static_cast<double>(m.countsMin);
    j["countsMax"]           = static_cast<double>(m.countsMax);
    j["verticalDivisions"]   = static_cast<double>(m.verticalDivisions);
    j["horizontalDivisions"] = static_cast<double>(m.horizontalDivisions);

    JJson tb = JJson::object();
    tb["mode"]            = static_cast<double>(static_cast<int>(m.timebase.mode));
    tb["secondsPerDiv"]   = m.timebase.secondsPerDiv;
    tb["recordLength"]    = static_cast<double>(m.timebase.recordLength);
    tb["sampleRate"]      = m.timebase.sampleRate;
    tb["triggerPosition"] = m.timebase.triggerPosition;
    j["timebase"] = tb;

    JJson tr = JJson::object();
    tr["mode"]          = static_cast<double>(static_cast<int>(m.trigger.mode));
    tr["slope"]         = static_cast<double>(static_cast<int>(m.trigger.slope));
    tr["sourceChannel"] = static_cast<double>(m.trigger.sourceChannel);
    tr["levelVolts"]    = m.trigger.levelVolts;
    j["trigger"] = tr;

    JJson chans = JJson::array();
    for (size_t i = 0; i < m.channels.size(); ++i) {
        JJson c = JJson::object();
        c["label"]       = (i < m.channelLabels.size()) ? m.channelLabels[i]
                                                        : ("CH" + std::to_string(i + 1));
        c["enabled"]     = m.channels[i].enabled;
        c["voltsPerDiv"] = m.channels[i].voltsPerDiv;
        c["offsetVolts"] = m.channels[i].offsetVolts;
        c["coupling"]    = static_cast<double>(static_cast<int>(m.channels[i].coupling));
        c["probeRatio"]  = m.channels[i].probeRatio;
        c["inverted"]    = m.channels[i].inverted;

        JJson steps = JJson::array();
        if (i < m.voltsPerDivSteps.size())
            for (double v : m.voltsPerDivSteps[i]) steps.push(v);
        c["voltsPerDivSteps"] = steps;

        chans.push(c);
    }
    j["channels"] = chans;
    return j;
}

} // namespace

struct JCaptureWriter::Impl {
    JCaptureWriter& owner;
    explicit Impl(JCaptureWriter& o) : owner(o) {}

    std::ofstream            file;
    std::string              path;
    JWorkerThread            worker;
    std::mutex               fileMutex;

    std::vector<JCaptureFormat::JIndexEntry> index;
    std::atomic<uint64_t>    framesWritten{0};
    std::atomic<uint64_t>    bytesWritten{0};
    std::atomic<size_t>      pending{0};
    std::atomic<bool>        backlogReported{false};
    bool                     open{false};

    void postError(std::string message) {
        JLOGC(JScopeLog::kCapture, JLogLevel::Error) << message;
        JCaptureWriter* o = &owner;
        JMainThreadDispatcher::instance().post(
            [o, m = std::move(message)]() mutable { o->onError.emit(std::move(m)); });
    }
};

JCaptureWriter::JCaptureWriter() : m_impl(std::make_unique<Impl>(*this)) {}
JCaptureWriter::~JCaptureWriter() { close(); }

JCaptureMeta JCaptureWriter::metaFrom(const JScopeDriver& driver) {
    const JScopeCapabilities& caps = driver.capabilities();
    JCaptureMeta m;
    m.driverId            = driver.driverId();
    m.model               = caps.model;
    m.serialNumber        = caps.serialNumber;
    m.startedAtUtc        = nowUtcIso8601();
    m.channelCount        = caps.channelCount();
    m.adcBits             = caps.adcBits;
    m.countsMin           = caps.countsMin;
    m.countsMax           = caps.countsMax;
    m.verticalDivisions   = caps.verticalDivisions;
    m.horizontalDivisions = caps.horizontalDivisions;
    m.timebase            = driver.timebaseConfig();
    m.trigger             = driver.triggerConfig();
    for (uint8_t c = 0; c < caps.channelCount(); ++c) {
        m.channels.push_back(driver.channelConfig(c));
        m.channelLabels.push_back(caps.channels[c].label);
        m.voltsPerDivSteps.push_back(caps.channels[c].voltsPerDiv);
    }
    return m;
}

bool JCaptureWriter::open(const std::string& path, const JCaptureMeta& meta) {
    close();

    m_impl->file.open(path, std::ios::binary | std::ios::trunc);
    if (!m_impl->file) {
        m_impl->postError("cannot open '" + path + "' for writing");
        return false;
    }
    m_impl->path = path;

    const std::string json = metaToJson(meta).dump(2);

    JCaptureFormat::JFileHeader h{};
    std::memcpy(h.magic, JCaptureFormat::kMagic, sizeof h.magic);
    h.headerBytes = sizeof h;
    h.version     = JCaptureFormat::kVersion;
    h.flags       = JCaptureFormat::kFlagNone;
    h.jsonBytes   = static_cast<uint32_t>(json.size());

    m_impl->file.write(reinterpret_cast<const char*>(&h), sizeof h);
    m_impl->file.write(json.data(), static_cast<std::streamsize>(json.size()));
    m_impl->file.flush();
    if (!m_impl->file) {
        m_impl->postError("cannot write the header of '" + path + "'");
        m_impl->file.close();
        return false;
    }

    m_impl->index.clear();
    m_impl->framesWritten.store(0);
    m_impl->bytesWritten.store(sizeof h + json.size());
    m_impl->pending.store(0);
    m_impl->backlogReported.store(false);
    m_impl->open = true;

    JLOGC(JScopeLog::kCapture, JLogLevel::Info)
        << "recording to '" << path << "' (" << meta.model << ", "
        << int(meta.channelCount) << " channels)";
    return true;
}

void JCaptureWriter::write(const JScopeFrame& frame) {
    if (!m_impl->open) return;

    // Copy on the CALLING thread, write on the worker. The frame this came from
    // returns to the acquisition pool the moment this returns.
    const size_t payload = static_cast<size_t>(frame.header.channelCount)
                         * frame.header.sampleCount * sizeof(int16_t);
    std::vector<uint8_t> buffer(sizeof(JScopeFrameHeader) + payload);
    std::memcpy(buffer.data(), &frame.header, sizeof(JScopeFrameHeader));

    uint8_t* out = buffer.data() + sizeof(JScopeFrameHeader);
    for (uint8_t p = 0; p < frame.header.channelCount; ++p) {
        const size_t bytes = frame.header.sampleCount * sizeof(int16_t);
        std::memcpy(out, frame.plane(p), bytes);
        out += bytes;
    }

    const double   timestamp = frame.header.timestampSeconds;
    const uint64_t startIdx  = frame.header.startSampleIndex;
    const size_t   depth     = m_impl->pending.fetch_add(1) + 1;

    if (depth > kBacklogWarning && !m_impl->backlogReported.exchange(true))
        m_impl->postError("the disk is not keeping up with this recording");

    m_impl->worker.post([this, buf = std::move(buffer), payload, timestamp, startIdx] {
        std::lock_guard<std::mutex> lk(m_impl->fileMutex);
        if (!m_impl->file) { m_impl->pending.fetch_sub(1); return; }

        JCaptureFormat::JIndexEntry entry{};
        entry.fileOffset       = static_cast<uint64_t>(m_impl->file.tellp());
        entry.timestampSeconds = timestamp;
        entry.startSampleIndex = startIdx;

        JCaptureFormat::JFrameRecord rec{};
        rec.magic        = JCaptureFormat::kFrameMagic;
        rec.headerBytes  = sizeof(JScopeFrameHeader);
        rec.payloadBytes = static_cast<uint32_t>(payload);

        m_impl->file.write(reinterpret_cast<const char*>(&rec), sizeof rec);
        m_impl->file.write(reinterpret_cast<const char*>(buf.data()),
                           static_cast<std::streamsize>(buf.size()));

        if (m_impl->file) {
            m_impl->index.push_back(entry);
            m_impl->framesWritten.fetch_add(1);
            m_impl->bytesWritten.fetch_add(sizeof rec + buf.size());
        } else {
            m_impl->postError("a write to the capture file failed");
        }
        m_impl->pending.fetch_sub(1);
    });
}

void JCaptureWriter::close() {
    if (!m_impl->open) return;

    // Drain before the footer: an index written while frames are still queued
    // would describe a file that does not exist yet.
    m_impl->worker.waitForIdle();

    std::lock_guard<std::mutex> lk(m_impl->fileMutex);
    if (m_impl->file) {
        const uint64_t indexOffset = static_cast<uint64_t>(m_impl->file.tellp());

        const uint32_t magic = JCaptureFormat::kIndexMagic;
        const uint64_t count = m_impl->index.size();
        m_impl->file.write(reinterpret_cast<const char*>(&magic), sizeof magic);
        m_impl->file.write(reinterpret_cast<const char*>(&count), sizeof count);
        m_impl->file.write(reinterpret_cast<const char*>(m_impl->index.data()),
                           static_cast<std::streamsize>(
                               count * sizeof(JCaptureFormat::JIndexEntry)));

        JCaptureFormat::JFooter footer{};
        std::memcpy(footer.magic, JCaptureFormat::kFooterMagic, sizeof footer.magic);
        footer.indexOffset = indexOffset;
        footer.frameCount  = count;
        m_impl->file.write(reinterpret_cast<const char*>(&footer), sizeof footer);
        m_impl->file.close();

        JLOGC(JScopeLog::kCapture, JLogLevel::Info)
            << "recording closed: " << count << " frames, "
            << (m_impl->bytesWritten.load() / 1024) << " KiB in '" << m_impl->path << "'";
    }
    m_impl->open = false;
}

bool     JCaptureWriter::isOpen() const        { return m_impl->open; }
uint64_t JCaptureWriter::framesWritten() const { return m_impl->framesWritten.load(); }
uint64_t JCaptureWriter::bytesWritten() const  { return m_impl->bytesWritten.load(); }
size_t   JCaptureWriter::pendingFrames() const { return m_impl->pending.load(); }

} // inline namespace jf
