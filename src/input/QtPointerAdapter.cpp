#include "QtPointerAdapter.h"

#include <QHoverEvent>
#include <QInputDevice>
#include <QMouseEvent>
#include <QPointingDevice>
#include <QTabletEvent>
#include <QTouchEvent>

#include <algorithm>
#include <chrono>
#include <mutex>

namespace komapaint::input {
namespace {

std::uint64_t steadyNowNs() noexcept
{
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
}

std::uint64_t eventTimestampNs(quint64 qtTimestampMs) noexcept
{
    constexpr std::uint64_t reanchorGapNs = 1'000;
    static std::mutex s_mutex;
    static bool s_hasQtAnchor = false;
    static quint64 s_qtAnchorMs = 0;
    static std::uint64_t s_wallAnchorNs = 0;
    static std::uint64_t s_lastOutNs = 0;

    const std::uint64_t nowNs = steadyNowNs();
    std::lock_guard<std::mutex> lock(s_mutex);

    if (qtTimestampMs > 0) {
        if (!s_hasQtAnchor || qtTimestampMs < s_qtAnchorMs) {
            s_hasQtAnchor = true;
            s_qtAnchorMs = qtTimestampMs;
            s_wallAnchorNs = std::max(nowNs, s_lastOutNs + reanchorGapNs);
        }
        std::uint64_t candidate =
            s_wallAnchorNs + static_cast<std::uint64_t>(qtTimestampMs - s_qtAnchorMs) * 1'000'000ULL;
        // Re-anchor if the Qt clock drifted behind real wall time (e.g. after long waits).
        if (candidate + 50'000'000ULL < nowNs) {
            s_qtAnchorMs = qtTimestampMs;
            s_wallAnchorNs = std::max(nowNs, s_lastOutNs + reanchorGapNs);
            candidate = s_wallAnchorNs;
        }
        if (candidate < s_lastOutNs) {
            candidate = s_lastOutNs;
        }
        s_lastOutNs = candidate;
        return candidate;
    }

    const std::uint64_t out = std::max(nowNs, s_lastOutNs);
    s_lastOutNs = out;
    return out;
}

std::uint32_t translateButtons(Qt::MouseButtons qtButtons) noexcept
{
    std::uint32_t mask = NoButton;
    if (qtButtons.testFlag(Qt::LeftButton)) mask |= PrimaryButton;
    if (qtButtons.testFlag(Qt::RightButton)) mask |= SecondaryButton;
    if (qtButtons.testFlag(Qt::MiddleButton)) mask |= MiddleButton;
    if (qtButtons.testFlag(Qt::ExtraButton1) || qtButtons.testFlag(Qt::BackButton)) mask |= Barrel3Button;
    return mask;
}

PointerType translatePointerType(QPointingDevice::PointerType pt,
                                 QInputDevice::DeviceType dt) noexcept
{
    switch (pt) {
    case QPointingDevice::PointerType::Pen: return PointerType::Pen;
    case QPointingDevice::PointerType::Eraser: return PointerType::Eraser;
    case QPointingDevice::PointerType::Finger: return PointerType::Touch;
    case QPointingDevice::PointerType::Cursor:
        if (dt == QInputDevice::DeviceType::Stylus || dt == QInputDevice::DeviceType::Airbrush) {
            return PointerType::Pen;
        }
        return PointerType::Mouse;
    case QPointingDevice::PointerType::Generic:
    case QPointingDevice::PointerType::Unknown:
    default:
        break;
    }

    switch (dt) {
    case QInputDevice::DeviceType::Stylus:
    case QInputDevice::DeviceType::Airbrush:
        return PointerType::Pen;
    case QInputDevice::DeviceType::TouchScreen:
        return PointerType::Touch;
    case QInputDevice::DeviceType::Mouse:
    case QInputDevice::DeviceType::TouchPad:
    case QInputDevice::DeviceType::Puck:
        return PointerType::Mouse;
    default:
        return PointerType::Unknown;
    }
}

std::uint64_t deviceIdentifier(const QInputDevice* device) noexcept
{
    if (!device) return 0;
    if (const auto* pd = qobject_cast<const QPointingDevice*>(device)) {
        const qint64 num = pd->uniqueId().numericId();
        if (num != 0 && num != -1) return static_cast<std::uint64_t>(num);
    }
    return static_cast<std::uint64_t>(device->systemId()) ^
           (reinterpret_cast<std::uintptr_t>(device) & 0xFFFFFFFFULL);
}

} // namespace

InputBackend QtPointerAdapter::defaultQtStylusBackend() noexcept
{
#if defined(Q_OS_WIN)
    return InputBackend::WindowsInk;
#elif defined(Q_OS_MACOS)
    return InputBackend::MacOSTablet;
#elif defined(Q_OS_ANDROID)
    return InputBackend::AndroidStylus;
#else
    return InputBackend::QtNative;
#endif
}

PointerCapabilities QtPointerAdapter::capabilitiesFromDevice(const QInputDevice* device) noexcept
{
    PointerCapabilities caps;
    if (!device) return caps;

    const auto qtCaps = device->capabilities();
    caps.hasPressure = qtCaps.testFlag(QInputDevice::Capability::Pressure);
    caps.hasTilt = qtCaps.testFlag(QInputDevice::Capability::XTilt) ||
                   qtCaps.testFlag(QInputDevice::Capability::YTilt);
    caps.hasRotation = qtCaps.testFlag(QInputDevice::Capability::Rotation);
    caps.hasTangentialPressure = qtCaps.testFlag(QInputDevice::Capability::TangentialPressure);
    caps.hasHover = qtCaps.testFlag(QInputDevice::Capability::Hover);

    if (const auto* pd = qobject_cast<const QPointingDevice*>(device)) {
        caps.hasEraser = (pd->pointerType() == QPointingDevice::PointerType::Eraser) ||
                         (pd->type() == QInputDevice::DeviceType::Stylus);
        caps.hasBarrelButtons = (pd->buttonCount() > 1) ||
                                (pd->type() == QInputDevice::DeviceType::Stylus);
    }
    caps.minPressure = 0.0;
    caps.maxPressure = 1.0;
    return caps;
}

PointerDeviceInfo QtPointerAdapter::fromInputDevice(const QInputDevice* device)
{
    PointerDeviceInfo info;
    if (!device) return info;

    info.deviceId = deviceIdentifier(device);
    info.name = device->name().toStdString();
    info.vendor = device->seatName().toStdString();
    info.capabilities = capabilitiesFromDevice(device);
    info.connected = true;

    const auto* pd = qobject_cast<const QPointingDevice*>(device);
    const auto pt = pd ? pd->pointerType() : QPointingDevice::PointerType::Unknown;
    info.primaryType = translatePointerType(pt, device->type());
    info.backend = (info.primaryType == PointerType::Pen || info.primaryType == PointerType::Eraser)
        ? defaultQtStylusBackend()
        : InputBackend::QtNative;
    return info;
}

std::vector<PointerDeviceInfo> QtPointerAdapter::enumerateDevices()
{
    std::vector<PointerDeviceInfo> list;
    const auto devs = QInputDevice::devices();
    list.reserve(static_cast<std::size_t>(devs.size()));
    for (const QInputDevice* dev : devs) {
        if (!dev) continue;
        if (dev->type() == QInputDevice::DeviceType::Keyboard) continue;
        list.push_back(fromInputDevice(dev));
    }
    return list;
}

RawPointerEvent QtPointerAdapter::fromTabletEvent(const QTabletEvent* event)
{
    RawPointerEvent raw;
    if (!event) return raw;

    const QPointingDevice* dev = event->pointingDevice();
    raw.deviceId = deviceIdentifier(dev);
    raw.pointerId = raw.deviceId != 0 ? raw.deviceId : 1;
    raw.timestampNs = eventTimestampNs(event->timestamp());
    raw.pointerType = (event->pointerType() == QPointingDevice::PointerType::Eraser)
        ? PointerType::Eraser
        : PointerType::Pen;
    raw.backend = defaultQtStylusBackend();
    raw.viewX = event->position().x();
    raw.viewY = event->position().y();
    raw.capabilities = capabilitiesFromDevice(dev);

    // Note: some Windows Ink / macOS / Android tablet drivers send QTabletEvent
    // with real pressure even if QPointingDevice didn't pre-populate Pressure bit
    // in synthetic/headless environments; only trust explicit capability OR non-mouse
    // tablet pressure > 0 when device reports Pressure capability. Never fabricate tilt/rotation.
    if (dev && dev->capabilities().testFlag(QInputDevice::Capability::Pressure)) {
        raw.capabilities.hasPressure = true;
    }
    raw.capabilities.hasHover = true;
    raw.capabilities.hasBarrelButtons = true;
    if (event->pointerType() == QPointingDevice::PointerType::Eraser) {
        raw.capabilities.hasEraser = true;
    }

    raw.pressure = raw.capabilities.hasPressure ? event->pressure() : 0.0;
    raw.tiltX = raw.capabilities.hasTilt ? event->xTilt() : 0.0;
    raw.tiltY = raw.capabilities.hasTilt ? event->yTilt() : 0.0;
    raw.rotationDegrees = raw.capabilities.hasRotation ? event->rotation() : 0.0;
    raw.tangentialPressure = raw.capabilities.hasTangentialPressure ? event->tangentialPressure() : 0.0;
    raw.buttons = translateButtons(event->buttons());

    switch (event->type()) {
    case QEvent::TabletPress:
        raw.phase = PointerPhase::Down;
        raw.inProximity = true;
        break;
    case QEvent::TabletMove:
        raw.phase = (event->buttons().testFlag(Qt::LeftButton) || event->pressure() > 0.0)
            ? PointerPhase::Move
            : PointerPhase::Hover;
        raw.inProximity = true;
        break;
    case QEvent::TabletRelease:
        raw.phase = PointerPhase::Up;
        raw.inProximity = true;
        break;
    case QEvent::TabletEnterProximity:
        raw.phase = PointerPhase::Enter;
        raw.inProximity = true;
        break;
    case QEvent::TabletLeaveProximity:
        raw.phase = PointerPhase::Leave;
        raw.inProximity = false;
        break;
    default:
        raw.phase = PointerPhase::Move;
        break;
    }
    return raw;
}

RawPointerEvent QtPointerAdapter::fromMouseEvent(const QMouseEvent* event, PointerPhase phase)
{
    RawPointerEvent raw;
    if (!event) return raw;

    const QPointingDevice* dev = event->pointingDevice();
    raw.deviceId = deviceIdentifier(dev);
    raw.pointerId = raw.deviceId != 0 ? raw.deviceId : 100;
    raw.timestampNs = eventTimestampNs(event->timestamp());
    raw.pointerType = PointerType::Mouse;
    raw.phase = phase;
    raw.backend = InputBackend::QtNative;
    raw.viewX = event->position().x();
    raw.viewY = event->position().y();
    raw.buttons = translateButtons(event->buttons());
    if (phase == PointerPhase::Down && event->button() == Qt::LeftButton) {
        raw.buttons |= PrimaryButton;
    }
    raw.synthesizedMouse = (event->source() != Qt::MouseEventNotSynthesized);
    raw.capabilities = capabilitiesFromDevice(dev);
    raw.capabilities.hasHover = true;
    // Plain mice report no pressure hardware; PressureCalibration uses explicit mouseFallbackPressure.
    raw.capabilities.hasPressure = false;
    raw.pressure = 0.0;
    return raw;
}

RawPointerEvent QtPointerAdapter::fromHoverEvent(const QHoverEvent* event, PointerPhase phase)
{
    RawPointerEvent raw;
    if (!event) return raw;

    const QPointingDevice* dev = event->pointingDevice();
    raw.deviceId = deviceIdentifier(dev);
    raw.pointerId = raw.deviceId != 0 ? raw.deviceId : 100;
    raw.timestampNs = eventTimestampNs(event->timestamp());
    const auto pt = dev ? dev->pointerType() : QPointingDevice::PointerType::Generic;
    const auto dt = dev ? dev->type() : QInputDevice::DeviceType::Mouse;
    raw.pointerType = translatePointerType(pt, dt);
    if (raw.pointerType == PointerType::Unknown) raw.pointerType = PointerType::Mouse;
    raw.phase = phase;
    raw.backend = (raw.pointerType == PointerType::Pen || raw.pointerType == PointerType::Eraser)
        ? defaultQtStylusBackend()
        : InputBackend::QtNative;
    raw.viewX = event->position().x();
    raw.viewY = event->position().y();
    raw.inProximity = (phase != PointerPhase::Leave);
    raw.capabilities = capabilitiesFromDevice(dev);
    raw.capabilities.hasHover = true;
    return raw;
}

void QtPointerAdapter::fromTouchEvent(const QTouchEvent* event, std::vector<RawPointerEvent>& out)
{
    out.clear();
    if (!event) return;

    const QPointingDevice* dev = event->pointingDevice();
    const std::uint64_t devId = deviceIdentifier(dev);
    const std::uint64_t tsNs = eventTimestampNs(event->timestamp());
    const auto baseCaps = capabilitiesFromDevice(dev);

    for (const QEventPoint& pt : event->points()) {
        RawPointerEvent raw;
        raw.deviceId = devId;
        raw.pointerId = static_cast<std::uint64_t>(pt.id()) + 10000ULL;
        raw.timestampNs = pt.timestamp() > 0 ? eventTimestampNs(pt.timestamp()) : tsNs;

        const QPointingDevice* ptDev = pt.device() ? pt.device() : dev;
        const auto ptType = ptDev ? ptDev->pointerType() : QPointingDevice::PointerType::Finger;
        const auto dt = ptDev ? ptDev->type() : QInputDevice::DeviceType::TouchScreen;
        raw.pointerType = translatePointerType(ptType, dt);
        if (raw.pointerType == PointerType::Unknown) raw.pointerType = PointerType::Touch;

        raw.backend = (raw.pointerType == PointerType::Pen || raw.pointerType == PointerType::Eraser)
            ? defaultQtStylusBackend()
            : InputBackend::QtNative;
        raw.viewX = pt.position().x();
        raw.viewY = pt.position().y();
        raw.capabilities = ptDev ? capabilitiesFromDevice(ptDev) : baseCaps;
        raw.pressure = raw.capabilities.hasPressure ? pt.pressure() : 0.0;
        raw.rotationDegrees = raw.capabilities.hasRotation ? pt.rotation() : 0.0;
        raw.inProximity = (pt.state() != QEventPoint::State::Released);
        raw.buttons = (pt.state() != QEventPoint::State::Released) ? PrimaryButton : NoButton;

        switch (pt.state()) {
        case QEventPoint::State::Pressed:
            raw.phase = PointerPhase::Down;
            break;
        case QEventPoint::State::Updated:
        case QEventPoint::State::Stationary:
            raw.phase = PointerPhase::Move;
            break;
        case QEventPoint::State::Released:
            raw.phase = PointerPhase::Up;
            break;
        default:
            raw.phase = PointerPhase::Move;
            break;
        }
        out.push_back(raw);
    }
}

} // namespace komapaint::input
