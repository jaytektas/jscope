#pragma once

#include "usb/JUsbTransport.h"

#include <cstring>
#include <sstream>
#include <string>
#include <vector>

// A JUsbTransport that records what was written and replays canned replies.
//
// This is the reason JUsbTransport exists. The 1008C's initialisation is
// undocumented vendor magic ported from a Python reference, and the only way to
// know the port is faithful is to compare the bytes it emits, in order, against
// the bytes the reference emits. A test that needed the device attached could
// not make that comparison at all, and a test that merely checked the driver
// "worked" would pass on a sequence that happened to be wrong.

inline namespace jf {

class JUsbTranscriptTransport : public JUsbTransport {
public:
    // Queue a reply for the next read. Replies are consumed in order; a read
    // with none queued returns zeros, which is what an unprogrammed status
    // command should look like rather than a failure.
    void queueReply(std::vector<uint8_t> bytes) { m_replies.push_back(std::move(bytes)); }
    void queueReply(std::initializer_list<uint8_t> bytes) { m_replies.emplace_back(bytes); }

    // Reply to a command with its echo followed by `payload`.
    void queueEchoedReply(uint8_t opcode, std::vector<uint8_t> payload = {}) {
        std::vector<uint8_t> r{ opcode };
        r.insert(r.end(), payload.begin(), payload.end());
        m_replies.push_back(std::move(r));
    }

    // Every write, in order.
    const std::vector<std::vector<uint8_t>>& writes() const { return m_writes; }

    // The opcode of each write — the sequence a protocol test asserts on.
    std::vector<uint8_t> opcodes() const {
        std::vector<uint8_t> out;
        for (const auto& w : m_writes) if (!w.empty()) out.push_back(w[0]);
        return out;
    }

    // Hex of one write, for a readable failure message.
    std::string writeHex(size_t index) const {
        if (index >= m_writes.size()) return "<no such write>";
        std::ostringstream ss;
        static const char* hx = "0123456789abcdef";
        for (uint8_t b : m_writes[index]) { ss << hx[b >> 4] << hx[b & 0xF] << ' '; }
        return ss.str();
    }

    bool wroteExactly(size_t index, const std::vector<uint8_t>& expected) const {
        return index < m_writes.size() && m_writes[index] == expected;
    }

    void reset() { m_writes.clear(); m_replies.clear(); m_replyIndex = 0; m_failWrites = false; }

    // Make every transfer fail, to check the driver reports rather than hangs.
    void setFailing(bool on) { m_failWrites = on; }

    // ---- JUsbTransport ------------------------------------------------------
    bool bulkOut(uint8_t, const uint8_t* data, size_t length, unsigned) override {
        if (m_failWrites) { m_lastError = "simulated transport failure"; return false; }
        m_writes.emplace_back(data, data + length);
        return true;
    }

    int bulkIn(uint8_t, uint8_t* data, size_t maxLength, unsigned) override {
        if (m_failWrites) { m_lastError = "simulated transport failure"; return -1; }
        std::memset(data, 0, maxLength);
        if (m_replyIndex >= m_replies.size()) return static_cast<int>(maxLength);

        const std::vector<uint8_t>& r = m_replies[m_replyIndex++];
        const size_t n = std::min(maxLength, r.size());
        std::memcpy(data, r.data(), n);
        return static_cast<int>(maxLength);      // the device always fills the packet
    }

    const std::string& lastError() const override { return m_lastError; }

private:
    std::vector<std::vector<uint8_t>> m_writes;
    std::vector<std::vector<uint8_t>> m_replies;
    size_t      m_replyIndex{0};
    bool        m_failWrites{false};
    std::string m_lastError;
};

} // inline namespace jf
