#pragma once

#include "PointerInput.h"

#include <QList>

class QHoverEvent;
class QInputDevice;
class QMouseEvent;
class QPointingDevice;
class QTabletEvent;
class QTouchEvent;

namespace komapaint::input {

class QtPointerAdapter {
public:
    static InputBackend defaultQtStylusBackend() noexcept;

    static PointerDeviceInfo fromInputDevice(const QInputDevice* device);
    static PointerCapabilities capabilitiesFromDevice(const QInputDevice* device) noexcept;
    static std::vector<PointerDeviceInfo> enumerateDevices();

    static RawPointerEvent fromTabletEvent(const QTabletEvent* event);
    static RawPointerEvent fromMouseEvent(const QMouseEvent* event, PointerPhase phase);
    static RawPointerEvent fromHoverEvent(const QHoverEvent* event, PointerPhase phase);

    // Converts all points of a touch event into raw samples (zero heap allocation
    // when caller reuses a reserved vector).
    static void fromTouchEvent(const QTouchEvent* event, std::vector<RawPointerEvent>& out);
};

} // namespace komapaint::input
