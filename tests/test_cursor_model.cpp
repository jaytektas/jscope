#include "measure/JCursorModel.h"
#include "support/JTestReport.h"

#include <cmath>

using namespace jf;

namespace {

void testDeltas(JTestReport& r) {
    JCursorModel c;
    c.setX(1.0e-3, 3.0e-3);
    c.setY(-0.5, 1.5);

    r.check(std::abs(c.deltaX() - 2.0e-3) < 1e-12, "dX is the separation of the X cursors");
    r.check(std::abs(c.deltaY() - 2.0) < 1e-12,    "dY is the separation of the Y cursors");
    r.check(c.haveFrequency(), "a non-zero dX has a frequency");
    r.check(std::abs(c.frequency() - 500.0) < 1e-9,
            "1/dX is the frequency a period between the cursors implies");
}

void testDegenerateFrequency(JTestReport& r) {
    JCursorModel c;
    c.setX(1.0e-3, 1.0e-3);
    r.check(!c.haveFrequency(), "coincident cursors have no frequency");
    r.check(c.frequency() == 0.0, "and report zero rather than an infinity");
}

// A user drags cursors past one another constantly. The window must be between
// them either way round, not empty when they cross.
void testWindowIsOrderIndependent(JTestReport& r) {
    JCursorModel c;
    c.setXEnabled(true);
    const double dt = 1.0e-5;

    size_t first = 0, count = 0;
    c.setX(2.0e-3, 5.0e-3);
    r.check(c.sampleWindow(dt, 1000, first, count), "an ordered pair yields a window");
    r.check(first == 200 && count == 300, "the window covers the samples between them");

    c.setX(5.0e-3, 2.0e-3);              // dragged past each other
    r.check(c.sampleWindow(dt, 1000, first, count), "a reversed pair still yields a window");
    r.check(first == 200 && count == 300, "and the same one");
}

void testWindowClampsToTheRecord(JTestReport& r) {
    JCursorModel c;
    c.setXEnabled(true);
    size_t first = 0, count = 0;

    // Both cursors dragged beyond the end of the record.
    c.setX(-1.0, 99.0);
    r.check(c.sampleWindow(1.0e-5, 1000, first, count), "an over-wide window is still valid");
    r.check(first == 0 && count == 1000, "and clamps to the whole record");

    c.setXEnabled(false);
    r.check(!c.sampleWindow(1.0e-5, 1000, first, count),
            "disabled cursors define no window, so measurements use the whole record");

    c.setXEnabled(true);
    r.check(!c.sampleWindow(1.0e-5, 0, first, count), "an empty record yields no window");
    r.check(!c.sampleWindow(0.0, 1000, first, count), "a zero sample interval yields no window");
}

void testHandleMovement(JTestReport& r) {
    JCursorModel c;
    c.setX(0.0, 0.0);
    c.moveHandle(JCursorModel::JHandle::X2, 4.0e-3);
    r.check(c.x2() == 4.0e-3, "moving a handle moves only that cursor");
    r.check(c.x1() == 0.0,    "and leaves the other alone");

    c.moveHandle(JCursorModel::JHandle::None, 9.0);
    r.check(c.x1() == 0.0 && c.x2() == 4.0e-3, "moving no handle changes nothing");
}

} // namespace

int main() {
    JTestReport r("JCursorModel");
    testDeltas(r);
    testDegenerateFrequency(r);
    testWindowIsOrderIndependent(r);
    testWindowClampsToTheRecord(r);
    testHandleMovement(r);
    return r.result();
}
