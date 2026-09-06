#include "JHantek1008Calibration.h"

#include "scope/JScopeLog.h"

#include <j/config/Json.h>

#include <fstream>
#include <sstream>

inline namespace jf {

namespace {
// With no calibration, midscale is the only defensible guess.
constexpr double kMidscale = 2048.0;
}

void JHantek1008Calibration::setZeroOffset(uint8_t rangeId, uint8_t channel, double counts) {
    if (rangeId < 1 || rangeId > kRanges || channel >= kChannels) return;
    m_offsets[rangeId - 1][channel] = counts;
}

double JHantek1008Calibration::zeroOffset(uint8_t rangeId, uint8_t channel) const {
    if (!m_complete || rangeId < 1 || rangeId > kRanges || channel >= kChannels)
        return kMidscale;
    return m_offsets[rangeId - 1][channel];
}

double JHantek1008Calibration::zeroOffsetForVScale(double vscale, uint8_t channel) const {
    return zeroOffset(JHantek1008Tables::vscaleId(vscale), channel);
}

void JHantek1008Calibration::clear() {
    for (auto& range : m_offsets) range.fill(kMidscale);
    m_complete = false;
}

bool JHantek1008Calibration::save(const std::string& path,
                                  const std::string& serialNumber) const {
    if (!m_complete) return false;

    JJson root = JJson::object();
    root["serialNumber"] = serialNumber;
    root["channels"]     = static_cast<double>(kChannels);

    JJson ranges = JJson::array();
    for (uint8_t rid = 1; rid <= kRanges; ++rid) {
        JJson entry = JJson::object();
        entry["vscale"] = JHantek1008Tables::vscaleForId(rid);
        JJson offsets = JJson::array();
        for (uint8_t c = 0; c < kChannels; ++c) offsets.push(m_offsets[rid - 1][c]);
        entry["zeroOffsets"] = offsets;
        ranges.push(entry);
    }
    root["ranges"] = ranges;

    std::ofstream f(path);
    if (!f) {
        JLOGC(JScopeLog::kHantek, JLogLevel::Warn)
            << "cannot write calibration to '" << path << "'";
        return false;
    }
    f << root.dump(2);
    JLOGC(JScopeLog::kHantek, JLogLevel::Info)
        << "calibration saved for unit '" << serialNumber << "' to " << path;
    return f.good();
}

bool JHantek1008Calibration::load(const std::string& path,
                                  const std::string& serialNumber) {
    std::ifstream f(path);
    if (!f) return false;

    std::ostringstream ss;
    ss << f.rdbuf();
    auto parsed = JJson::tryParse(ss.str());
    if (!parsed || !parsed->isObject()) {
        JLOGC(JScopeLog::kHantek, JLogLevel::Warn)
            << "calibration file '" << path << "' is unreadable";
        return false;
    }

    // Offsets belong to a physical unit. Applying another 1008C's would be worse
    // than having none at all, because it would look like a working calibration.
    const std::string stored = (*parsed)["serialNumber"].str();
    if (stored != serialNumber) {
        JLOGC(JScopeLog::kHantek, JLogLevel::Info)
            << "stored calibration is for unit '" << stored << "', not '"
            << serialNumber << "' — measuring fresh offsets";
        return false;
    }

    const JJson& ranges = (*parsed)["ranges"];
    for (size_t i = 0; i < ranges.size() && i < kRanges; ++i) {
        const JJson& offsets = ranges[i]["zeroOffsets"];
        for (size_t c = 0; c < offsets.size() && c < kChannels; ++c)
            m_offsets[i][c] = offsets[c].number();
    }
    m_complete = true;
    JLOGC(JScopeLog::kHantek, JLogLevel::Info)
        << "calibration loaded for unit '" << serialNumber << "' from " << path;
    return true;
}

} // inline namespace jf
