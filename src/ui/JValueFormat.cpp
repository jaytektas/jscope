#include "JValueFormat.h"

#include <cmath>
#include <cstdio>

inline namespace jf {

namespace {

// Three significant figures is what fits a readout column and what a scope's own
// display gives you; more digits than the measurement justifies is false
// precision on a quantised signal.
std::string withPrefix(double v, const char* unit) {
    char buf[48];
    const double a = std::abs(v);
    if (a == 0.0)          std::snprintf(buf, sizeof buf, "0 %s", unit);
    else if (a < 1.0e-9)   std::snprintf(buf, sizeof buf, "%.3g p%s", v * 1.0e12, unit);
    else if (a < 1.0e-6)   std::snprintf(buf, sizeof buf, "%.3g n%s", v * 1.0e9,  unit);
    else if (a < 1.0e-3)   std::snprintf(buf, sizeof buf, "%.3g u%s", v * 1.0e6,  unit);
    else if (a < 1.0)      std::snprintf(buf, sizeof buf, "%.3g m%s", v * 1.0e3,  unit);
    else if (a < 1.0e3)    std::snprintf(buf, sizeof buf, "%.3g %s",  v,          unit);
    else if (a < 1.0e6)    std::snprintf(buf, sizeof buf, "%.3g k%s", v / 1.0e3,  unit);
    else                   std::snprintf(buf, sizeof buf, "%.3g M%s", v / 1.0e6,  unit);
    return buf;
}

} // namespace

std::string JValueFormat::volts(double v)   { return withPrefix(v, "V"); }
std::string JValueFormat::seconds(double s) { return withPrefix(s, "s"); }
std::string JValueFormat::hertz(double hz)  { return withPrefix(hz, "Hz"); }

std::string JValueFormat::percent(double pct) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.1f %%", pct);
    return buf;
}

std::string JValueFormat::measurement(JMeasurementKind kind, double value) {
    switch (jMeasurementUnit(kind)) {
        case JMeasurementUnit::Volts:   return volts(value);
        case JMeasurementUnit::Seconds: return seconds(value);
        case JMeasurementUnit::Hertz:   return hertz(value);
        case JMeasurementUnit::Percent: return percent(value);
    }
    return volts(value);
}

} // inline namespace jf
