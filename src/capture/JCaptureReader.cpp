#include "JCaptureReader.h"

#include "scope/JScopeFrame.h"
#include "scope/JScopeLog.h"

#include <j/config/Json.h>

#include <algorithm>
#include <cstring>
#include <fstream>
#include <vector>

inline namespace jf {

namespace {

JCaptureMeta metaFromJson(const JJson& j) {
    JCaptureMeta m;
    m.driverId            = j["driverId"].str();
    m.model               = j["model"].str();
    m.serialNumber        = j["serialNumber"].str();
    m.operatorNote        = j["operatorNote"].str();
    m.startedAtUtc        = j["startedAtUtc"].str();
    m.channelCount        = static_cast<uint8_t>(j["channelCount"].number());
    m.adcBits             = static_cast<uint8_t>(j["adcBits"].number());
    m.countsMin           = static_cast<int32_t>(j["countsMin"].number());
    m.countsMax           = static_cast<int32_t>(j["countsMax"].number());
    m.verticalDivisions   = static_cast<uint8_t>(j["verticalDivisions"].number());
    m.horizontalDivisions = static_cast<uint8_t>(j["horizontalDivisions"].number());

    const JJson& tb = j["timebase"];
    m.timebase.mode            = static_cast<JScopeAcquisitionMode>(
                                     static_cast<int>(tb["mode"].number()));
    m.timebase.secondsPerDiv   = tb["secondsPerDiv"].number();
    m.timebase.recordLength    = static_cast<uint32_t>(tb["recordLength"].number());
    m.timebase.sampleRate      = tb["sampleRate"].number();
    m.timebase.triggerPosition = tb["triggerPosition"].number();

    const JJson& tr = j["trigger"];
    m.trigger.mode          = static_cast<JScopeTriggerMode>(static_cast<int>(tr["mode"].number()));
    m.trigger.slope         = static_cast<JScopeTriggerSlope>(static_cast<int>(tr["slope"].number()));
    m.trigger.sourceChannel = static_cast<uint8_t>(tr["sourceChannel"].number());
    m.trigger.levelVolts    = tr["levelVolts"].number();

    const JJson& chans = j["channels"];
    for (size_t i = 0; i < chans.size(); ++i) {
        const JJson& c = chans[i];
        JScopeChannelConfig cfg;
        cfg.enabled     = c["enabled"].boolean();
        cfg.voltsPerDiv = c["voltsPerDiv"].number();
        cfg.offsetVolts = c["offsetVolts"].number();
        cfg.coupling    = static_cast<JScopeCoupling>(static_cast<int>(c["coupling"].number()));
        cfg.probeRatio  = c["probeRatio"].number();
        cfg.inverted    = c["inverted"].boolean();
        m.channels.push_back(cfg);
        m.channelLabels.push_back(c["label"].str());

        std::vector<double> steps;
        const JJson& s = c["voltsPerDivSteps"];
        for (size_t k = 0; k < s.size(); ++k) steps.push_back(s[k].number());
        m.voltsPerDivSteps.push_back(std::move(steps));
    }
    return m;
}

} // namespace

struct JCaptureReader::Impl {
    std::ifstream file;
    std::string   path;
    JCaptureMeta  meta;
    std::vector<JCaptureFormat::JIndexEntry> index;
    bool recovered{false};
    bool open{false};

    // Walk the frame records from `from` to the end of the file, rebuilding the
    // index. Used when the footer is missing, and stops at the first record that
    // does not look like one rather than reading past the end of a truncation.
    void scanForFrames(std::streamoff from) {
        index.clear();
        file.clear();

        // The file's real length, because seekg past the end does NOT fail on an
        // ifstream — only the read after it does. Without this a frame whose
        // payload was cut short by an interrupted write looks complete during the
        // scan, gets offered to the caller, and then reads garbage.
        file.seekg(0, std::ios::end);
        const std::streamoff fileSize = file.tellg();
        file.clear();
        file.seekg(from);

        while (file) {
            const std::streamoff at = file.tellg();
            JCaptureFormat::JFrameRecord rec{};
            if (!file.read(reinterpret_cast<char*>(&rec), sizeof rec)) break;
            if (rec.magic != JCaptureFormat::kFrameMagic) break;
            if (rec.headerBytes != sizeof(JScopeFrameHeader)) break;

            JScopeFrameHeader h{};
            if (!file.read(reinterpret_cast<char*>(&h), sizeof h)) break;

            // A truncated payload means the recording stopped mid-frame. That
            // frame is not usable; everything before it is.
            const std::streamoff payloadEnd =
                static_cast<std::streamoff>(file.tellg()) + rec.payloadBytes;
            if (payloadEnd > fileSize) break;
            file.seekg(payloadEnd);
            if (!file) break;

            index.push_back({ static_cast<uint64_t>(at), h.timestampSeconds,
                              h.startSampleIndex });
        }
        file.clear();
    }
};

JCaptureReader::JCaptureReader() : m_impl(std::make_unique<Impl>()) {}
JCaptureReader::~JCaptureReader() { close(); }

bool JCaptureReader::open(const std::string& path) {
    close();

    m_impl->file.open(path, std::ios::binary);
    if (!m_impl->file) {
        JLOGC(JScopeLog::kCapture, JLogLevel::Error) << "cannot open '" << path << "'";
        return false;
    }
    m_impl->path = path;

    JCaptureFormat::JFileHeader h{};
    if (!m_impl->file.read(reinterpret_cast<char*>(&h), sizeof h) ||
        std::memcmp(h.magic, JCaptureFormat::kMagic, sizeof h.magic) != 0) {
        JLOGC(JScopeLog::kCapture, JLogLevel::Error)
            << "'" << path << "' is not a .jscope capture";
        m_impl->file.close();
        return false;
    }
    if (h.version != JCaptureFormat::kVersion || h.flags != JCaptureFormat::kFlagNone) {
        // Refuse rather than misread: a flag this build does not know could mean
        // the payload is compressed, and reading it as raw samples would produce
        // a plausible-looking waveform of pure noise.
        JLOGC(JScopeLog::kCapture, JLogLevel::Error)
            << "'" << path << "' has version " << h.version << " flags " << h.flags
            << ", which this build does not understand";
        m_impl->file.close();
        return false;
    }

    std::string json(h.jsonBytes, '\0');
    if (!m_impl->file.read(json.data(), h.jsonBytes)) {
        JLOGC(JScopeLog::kCapture, JLogLevel::Error) << "'" << path << "' header is truncated";
        m_impl->file.close();
        return false;
    }
    auto parsed = JJson::tryParse(json);
    if (!parsed) {
        JLOGC(JScopeLog::kCapture, JLogLevel::Error) << "'" << path << "' metadata is unreadable";
        m_impl->file.close();
        return false;
    }
    m_impl->meta = metaFromJson(*parsed);

    const std::streamoff framesBegin = m_impl->file.tellg();

    // Try the footer; fall back to a scan. An interrupted recording is a normal
    // case, not a corrupt file.
    m_impl->recovered = true;
    m_impl->file.seekg(0, std::ios::end);
    const std::streamoff fileSize = m_impl->file.tellg();
    if (fileSize > static_cast<std::streamoff>(sizeof(JCaptureFormat::JFooter))) {
        m_impl->file.seekg(fileSize - static_cast<std::streamoff>(sizeof(JCaptureFormat::JFooter)));
        JCaptureFormat::JFooter footer{};
        if (m_impl->file.read(reinterpret_cast<char*>(&footer), sizeof footer) &&
            std::memcmp(footer.magic, JCaptureFormat::kFooterMagic, sizeof footer.magic) == 0) {
            m_impl->file.clear();
            m_impl->file.seekg(static_cast<std::streamoff>(footer.indexOffset));
            uint32_t magic = 0;
            uint64_t count = 0;
            if (m_impl->file.read(reinterpret_cast<char*>(&magic), sizeof magic) &&
                magic == JCaptureFormat::kIndexMagic &&
                m_impl->file.read(reinterpret_cast<char*>(&count), sizeof count)) {
                m_impl->index.resize(count);
                if (count == 0 ||
                    m_impl->file.read(reinterpret_cast<char*>(m_impl->index.data()),
                                      static_cast<std::streamsize>(
                                          count * sizeof(JCaptureFormat::JIndexEntry)))) {
                    m_impl->recovered = false;
                }
            }
        }
    }
    if (m_impl->recovered) {
        m_impl->scanForFrames(framesBegin);
        JLOGC(JScopeLog::kCapture, JLogLevel::Warn)
            << "'" << path << "' has no usable index — recovered "
            << m_impl->index.size() << " frame(s) by scanning; the recording did not close";
    }

    m_impl->open = true;
    JLOGC(JScopeLog::kCapture, JLogLevel::Info)
        << "opened '" << path << "': " << m_impl->meta.model << ", "
        << m_impl->index.size() << " frames, recorded " << m_impl->meta.startedAtUtc;
    return true;
}

void JCaptureReader::close() {
    if (!m_impl->open) return;
    m_impl->file.close();
    m_impl->index.clear();
    m_impl->open = false;
}

bool JCaptureReader::readFrame(uint64_t index, JScopeFrame& out) {
    if (!m_impl->open || index >= m_impl->index.size()) return false;

    m_impl->file.clear();
    m_impl->file.seekg(static_cast<std::streamoff>(m_impl->index[index].fileOffset));

    JCaptureFormat::JFrameRecord rec{};
    if (!m_impl->file.read(reinterpret_cast<char*>(&rec), sizeof rec) ||
        rec.magic != JCaptureFormat::kFrameMagic) return false;

    JScopeFrameHeader h{};
    if (!m_impl->file.read(reinterpret_cast<char*>(&h), sizeof h)) return false;

    if (out.maxChannels() < h.channelCount || out.maxSamples() < h.sampleCount)
        out.provision(std::max<uint8_t>(h.channelCount, JScopeLimits::kMaxChannels),
                      h.sampleCount);
    if (!out.shape(h.channelCount, h.sampleCount)) return false;
    out.header = h;

    for (uint8_t p = 0; p < h.channelCount; ++p)
        if (!m_impl->file.read(reinterpret_cast<char*>(out.plane(p)),
                               static_cast<std::streamsize>(h.sampleCount * sizeof(int16_t))))
            return false;
    return true;
}

uint64_t JCaptureReader::frameAtTime(double seconds) const {
    if (m_impl->index.empty()) return 0;
    const auto it = std::upper_bound(m_impl->index.begin(), m_impl->index.end(), seconds,
        [](double t, const JCaptureFormat::JIndexEntry& e) { return t < e.timestampSeconds; });
    if (it == m_impl->index.begin()) return 0;
    return static_cast<uint64_t>((it - 1) - m_impl->index.begin());
}

double JCaptureReader::startTime() const {
    return m_impl->index.empty() ? 0.0 : m_impl->index.front().timestampSeconds;
}
double JCaptureReader::endTime() const {
    return m_impl->index.empty() ? 0.0 : m_impl->index.back().timestampSeconds;
}

const JCaptureMeta& JCaptureReader::meta() const { return m_impl->meta; }
uint64_t JCaptureReader::frameCount() const { return m_impl->index.size(); }
bool JCaptureReader::wasRecovered() const { return m_impl->recovered; }
bool JCaptureReader::isOpen() const { return m_impl->open; }

} // inline namespace jf
