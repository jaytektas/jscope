#pragma once

#include <cstdint>
#include <string>

inline namespace jf {

class JScopeFrame;

// Writes one frame as CSV, for interop.
//
// The .jscope container is what a capture is stored in — CSV cannot hold a
// megapoint deep record usefully, and it loses the counts-to-volts transform
// that makes a recording still interpretable later. But a single visible window
// pasted into a spreadsheet is a real need, and the header mirrors the comment
// block hantek1008py's csvexport.py already writes, so existing tooling reads it.
class JCsvExport {
public:
    // `first`/`count` select the visible window; count == 0 means the whole frame.
    static bool write(const std::string& path, const JScopeFrame& frame,
                      const std::string& deviceModel,
                      size_t first = 0, size_t count = 0);
};

} // inline namespace jf
