#include "WinTabBackend.h"

#include <QCoreApplication>
#include <QWindow>

#include <chrono>
#include <cmath>
#include <cstring>

#if defined(Q_OS_WIN)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

namespace komapaint::input {

#if defined(Q_OS_WIN)
namespace {

using HCTX = HANDLE;
using WTPKT = DWORD;
using FIX32 = DWORD;

constexpr UINT kWtiInterface = 1;
constexpr UINT kWtiCursors = 200;
constexpr UINT kWtiDevices = 100;
constexpr UINT kWtiDefSysCtx = 4;

constexpr UINT kIfcWintabId = 1;
constexpr UINT kIfcNDevices = 4;
constexpr UINT kIfcNCursors = 5;

constexpr UINT kDvcName = 1;
constexpr UINT kDvcPktData = 7;
constexpr UINT kDvcX = 12;
constexpr UINT kDvcY = 13;
constexpr UINT kDvcNPressure = 15;
constexpr UINT kDvcTPressure = 16;
constexpr UINT kDvcOrientation = 17;
constexpr UINT kDvcRotation = 18;

constexpr UINT kCsrType = 2;
constexpr UINT kCsrPhysId = 4;

constexpr WTPKT kPkContext = 0x0001;
constexpr WTPKT kPkStatus = 0x0002;
constexpr WTPKT kPkTime = 0x0004;
constexpr WTPKT kPkChanged = 0x0008;
constexpr WTPKT kPkSerialNumber = 0x0010;
constexpr WTPKT kPkCursor = 0x0020;
constexpr WTPKT kPkButtons = 0x0040;
constexpr WTPKT kPkX = 0x0080;
constexpr WTPKT kPkY = 0x0100;
constexpr WTPKT kPkZ = 0x0200;
constexpr WTPKT kPkNormalPressure = 0x0400;
constexpr WTPKT kPkTangentPressure = 0x0800;
constexpr WTPKT kPkOrientation = 0x1000;
constexpr WTPKT kPkRotation = 0x2000;

constexpr UINT kCxoSystem = 0x0001;
constexpr UINT kCxoPen = 0x0002;
constexpr UINT kCxoMessages = 0x0004;

constexpr UINT kTpsInvert = 0x0002;
constexpr UINT kTpsProximity = 0x0001;

constexpr UINT kWtPacket = 0x7FF0;
constexpr UINT kWtCtxOverlap = 0x7FF3;
constexpr UINT kWtProximity = 0x7FF5;
constexpr UINT kWtInfoChange = 0x7FF6;

#pragma pack(push, 1)
struct WtAxis {
    LONG axMin;
    LONG axMax;
    UINT axUnits;
    FIX32 axResolution;
};

struct WtOrientation {
    int orAzimuth;
    int orAltitude;
    int orTwist;
};

struct WtRotation {
    int roPitch;
    int roRoll;
    int roYaw;
};

struct WtLogContextW {
    WCHAR lcName[40];
    UINT lcOptions;
    UINT lcStatus;
    UINT lcLocks;
    UINT lcMsgBase;
    UINT lcDevice;
    UINT lcPktRate;
    WTPKT lcPktData;
    WTPKT lcPktMode;
    WTPKT lcMoveMask;
    DWORD lcBtnDnMask;
    DWORD lcBtnUpMask;
    LONG lcInOrgX;
    LONG lcInOrgY;
    LONG lcInOrgZ;
    LONG lcInExtX;
    LONG lcInExtY;
    LONG lcInExtZ;
    LONG lcOutOrgX;
    LONG lcOutOrgY;
    LONG lcOutOrgZ;
    LONG lcOutExtX;
    LONG lcOutExtY;
    LONG lcOutExtZ;
    FIX32 lcSensX;
    FIX32 lcSensY;
    FIX32 lcSensZ;
    BOOL lcSysMode;
    int lcSysOrgX;
    int lcSysOrgY;
    int lcSysExtX;
    int lcSysExtY;
    FIX32 lcSysSensX;
    FIX32 lcSysSensY;
};
#pragma pack(pop)

using FnWTInfoW = UINT(WINAPI*)(UINT, UINT, LPVOID);
using FnWTOpenW = HCTX(WINAPI*)(HWND, WtLogContextW*, BOOL);
using FnWTClose = BOOL(WINAPI*)(HCTX);
using FnWTPacketsGet = int(WINAPI*)(HCTX, int, LPVOID);
using FnWTEnable = BOOL(WINAPI*)(HCTX, BOOL);
using FnWTOverlap = BOOL(WINAPI*)(HCTX, BOOL);
using FnWTQueueSizeSet = BOOL(WINAPI*)(HCTX, int);

struct WinTabApi {
    HMODULE module = nullptr;
    FnWTInfoW wtInfoW = nullptr;
    FnWTOpenW wtOpenW = nullptr;
    FnWTClose wtClose = nullptr;
    FnWTPacketsGet wtPacketsGet = nullptr;
    FnWTEnable wtEnable = nullptr;
    FnWTOverlap wtOverlap = nullptr;
    FnWTQueueSizeSet wtQueueSizeSet = nullptr;

    bool load() noexcept
    {
        if (module) return wtInfoW && wtOpenW && wtClose && wtPacketsGet;
        module = LoadLibraryW(L"wintab32.dll");
        if (!module) return false;
        wtInfoW = reinterpret_cast<FnWTInfoW>(GetProcAddress(module, "WTInfoW"));
        wtOpenW = reinterpret_cast<FnWTOpenW>(GetProcAddress(module, "WTOpenW"));
        wtClose = reinterpret_cast<FnWTClose>(GetProcAddress(module, "WTClose"));
        wtPacketsGet = reinterpret_cast<FnWTPacketsGet>(GetProcAddress(module, "WTPacketsGet"));
        wtEnable = reinterpret_cast<FnWTEnable>(GetProcAddress(module, "WTEnable"));
        wtOverlap = reinterpret_cast<FnWTOverlap>(GetProcAddress(module, "WTOverlap"));
        wtQueueSizeSet = reinterpret_cast<FnWTQueueSizeSet>(GetProcAddress(module, "WTQueueSizeSet"));
        if (!wtInfoW || !wtOpenW || !wtClose || !wtPacketsGet) {
            unload();
            return false;
        }
        return true;
    }

    void unload() noexcept
    {
        if (module) {
            FreeLibrary(module);
            module = nullptr;
        }
        wtInfoW = nullptr;
        wtOpenW = nullptr;
        wtClose = nullptr;
        wtPacketsGet = nullptr;
        wtEnable = nullptr;
        wtOverlap = nullptr;
        wtQueueSizeSet = nullptr;
    }
};

std::uint64_t steadyNowNs() noexcept
{
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
}

std::size_t packetByteSize(WTPKT mask) noexcept
{
    std::size_t bytes = 0;
    if (mask & kPkContext) bytes += sizeof(HCTX);
    if (mask & kPkStatus) bytes += sizeof(UINT);
    if (mask & kPkTime) bytes += sizeof(DWORD);
    if (mask & kPkChanged) bytes += sizeof(WTPKT);
    if (mask & kPkSerialNumber) bytes += sizeof(UINT);
    if (mask & kPkCursor) bytes += sizeof(UINT);
    if (mask & kPkButtons) bytes += sizeof(DWORD);
    if (mask & kPkX) bytes += sizeof(LONG);
    if (mask & kPkY) bytes += sizeof(LONG);
    if (mask & kPkZ) bytes += sizeof(LONG);
    if (mask & kPkNormalPressure) bytes += sizeof(UINT);
    if (mask & kPkTangentPressure) bytes += sizeof(UINT);
    if (mask & kPkOrientation) bytes += sizeof(WtOrientation);
    if (mask & kPkRotation) bytes += sizeof(WtRotation);
    return bytes;
}

template <typename T>
T readField(const std::uint8_t*& ptr) noexcept
{
    T value{};
    std::memcpy(&value, ptr, sizeof(T));
    ptr += sizeof(T);
    return value;
}

} // namespace
#endif

struct WinTabBackend::Impl {
#if defined(Q_OS_WIN)
    WinTabApi api;
    HCTX ctx = nullptr;
    HWND hwnd = nullptr;
    WTPKT activePktData = 0;
    std::size_t pktSize = 0;
    WtAxis pressureAxis{0, 1023, 0, 0};
    WtAxis tangentAxis{0, 1023, 0, 0};
    WtAxis orientAxes[3]{};
    bool hasOrientAxes = false;
    bool hasRotation = false;
    PointerCapabilities caps{};
    std::vector<std::uint8_t> packetScratch;
    std::uint32_t prevButtons = 0;
    bool filterInstalled = false;
    std::uint64_t activeDeviceId = 0x57544142ULL; // 'WTAB'
    std::string deviceName = "WinTab Tablet";
#endif
};

WinTabBackend::WinTabBackend(QObject* parent)
    : QObject(parent),
      m_impl(std::make_unique<Impl>())
{
}

WinTabBackend::~WinTabBackend()
{
    detachWindow();
#if defined(Q_OS_WIN)
    m_impl->api.unload();
#endif
}

WinTabDiagnostics WinTabBackend::probe()
{
    WinTabDiagnostics diag;
#if defined(Q_OS_WIN)
    WinTabApi api;
    if (!api.load()) return diag;
    diag.dllPresent = true;

    WCHAR idBuf[128]{};
    if (api.wtInfoW(kWtiInterface, kIfcWintabId, idBuf) > 0) {
        diag.interfaceName = QString::fromWCharArray(idBuf).toStdString();
    }
    UINT ndev = 0;
    if (api.wtInfoW(kWtiInterface, kIfcNDevices, &ndev) > 0) {
        diag.deviceCount = static_cast<int>(ndev);
    }
    WTPKT supported = 0;
    api.wtInfoW(kWtiDevices, kDvcPktData, &supported);
    diag.capabilities.hasPressure = (supported & kPkNormalPressure) != 0;
    diag.capabilities.hasTangentialPressure = (supported & kPkTangentPressure) != 0;
    diag.capabilities.hasTilt = (supported & kPkOrientation) != 0;
    diag.capabilities.hasRotation = (supported & kPkRotation) != 0;
    diag.capabilities.hasHover = true;
    diag.capabilities.hasBarrelButtons = (supported & kPkButtons) != 0;
    diag.capabilities.hasEraser = true;
    api.unload();
#endif
    return diag;
}

bool WinTabBackend::attachWindow(QWindow* window)
{
    if (m_window == window && isContextOpen()) return true;
    detachWindow();
    m_window = window;
    if (!m_window || !m_enabled) return false;

#if defined(Q_OS_WIN)
    if (!m_impl->api.load()) return false;
    HWND hwnd = reinterpret_cast<HWND>(m_window->winId());
    if (!hwnd) return false;

    WtLogContextW lc{};
    if (!m_impl->api.wtInfoW(kWtiDefSysCtx, 0, &lc)) return false;

    WTPKT supported = 0;
    m_impl->api.wtInfoW(kWtiDevices, kDvcPktData, &supported);
    const WTPKT desired = kPkContext | kPkStatus | kPkTime | kPkCursor | kPkButtons |
                          kPkX | kPkY | kPkNormalPressure | kPkTangentPressure |
                          kPkOrientation | kPkRotation;
    WTPKT pktMask = desired & (supported | kPkContext | kPkStatus | kPkTime | kPkCursor | kPkButtons | kPkX | kPkY);

    lc.lcOptions |= kCxoMessages | kCxoPen | kCxoSystem;
    lc.lcPktData = pktMask;
    lc.lcPktMode = 0; // absolute
    lc.lcMoveMask = pktMask;
    lc.lcBtnDnMask = 0xFFFFFFFFUL;
    lc.lcBtnUpMask = 0xFFFFFFFFUL;

    HCTX ctx = m_impl->api.wtOpenW(hwnd, &lc, TRUE);
    if (!ctx) return false;

    if (m_impl->api.wtQueueSizeSet) {
        m_impl->api.wtQueueSizeSet(ctx, 64);
    }

    m_impl->ctx = ctx;
    m_impl->hwnd = hwnd;
    m_impl->activePktData = lc.lcPktData;
    m_impl->pktSize = packetByteSize(lc.lcPktData);
    m_impl->packetScratch.resize(m_impl->pktSize * 64);

    if (lc.lcPktData & kPkNormalPressure) {
        m_impl->api.wtInfoW(kWtiDevices, kDvcNPressure, &m_impl->pressureAxis);
    }
    if (lc.lcPktData & kPkTangentPressure) {
        m_impl->api.wtInfoW(kWtiDevices, kDvcTPressure, &m_impl->tangentAxis);
    }
    m_impl->hasOrientAxes = (lc.lcPktData & kPkOrientation) &&
        m_impl->api.wtInfoW(kWtiDevices, kDvcOrientation, m_impl->orientAxes) > 0;
    m_impl->hasRotation = (lc.lcPktData & kPkRotation) != 0;

    WCHAR devName[128]{};
    if (m_impl->api.wtInfoW(kWtiDevices, kDvcName, devName) > 0) {
        m_impl->deviceName = QString::fromWCharArray(devName).toStdString();
    }

    m_impl->caps = {};
    m_impl->caps.hasPressure = (lc.lcPktData & kPkNormalPressure) != 0 &&
                               (m_impl->pressureAxis.axMax > m_impl->pressureAxis.axMin);
    m_impl->caps.hasTilt = m_impl->hasOrientAxes;
    m_impl->caps.hasRotation = m_impl->hasRotation || m_impl->hasOrientAxes;
    m_impl->caps.hasTangentialPressure = (lc.lcPktData & kPkTangentPressure) != 0 &&
                                         (m_impl->tangentAxis.axMax > m_impl->tangentAxis.axMin);
    m_impl->caps.hasHover = true;
    m_impl->caps.hasBarrelButtons = (lc.lcPktData & kPkButtons) != 0;
    m_impl->caps.hasEraser = true;
    m_impl->caps.supportsCoalescing = true;

    if (!m_impl->filterInstalled && QCoreApplication::instance()) {
        QCoreApplication::instance()->installNativeEventFilter(this);
        m_impl->filterInstalled = true;
    }
    return true;
#else
    return false;
#endif
}

void WinTabBackend::detachWindow()
{
#if defined(Q_OS_WIN)
    if (m_impl->filterInstalled && QCoreApplication::instance()) {
        QCoreApplication::instance()->removeNativeEventFilter(this);
        m_impl->filterInstalled = false;
    }
    if (m_impl->ctx && m_impl->api.wtClose) {
        m_impl->api.wtClose(m_impl->ctx);
        m_impl->ctx = nullptr;
    }
    m_impl->hwnd = nullptr;
    m_impl->prevButtons = 0;
#endif
    m_inProximity = false;
}

void WinTabBackend::setEnabled(bool enabled)
{
    if (m_enabled == enabled) return;
    m_enabled = enabled;
    if (m_enabled) {
        if (m_window) attachWindow(m_window);
    } else {
        detachWindow();
    }
}

bool WinTabBackend::isContextOpen() const noexcept
{
#if defined(Q_OS_WIN)
    return m_impl->ctx != nullptr;
#else
    return false;
#endif
}

WinTabDiagnostics WinTabBackend::diagnostics() const
{
    WinTabDiagnostics diag = probe();
    diag.enabled = m_enabled;
    diag.contextOpen = isContextOpen();
#if defined(Q_OS_WIN)
    if (diag.contextOpen) diag.capabilities = m_impl->caps;
#endif
    return diag;
}

bool WinTabBackend::nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result)
{
#if defined(Q_OS_WIN)
    if (!m_enabled || !m_impl->ctx || !m_window || eventType != "windows_generic_MSG") {
        return false;
    }
    MSG* msg = static_cast<MSG*>(message);
    if (!msg) return false;

    if (msg->message == kWtProximity) {
        const bool enter = LOWORD(msg->lParam) != 0;
        m_inProximity = enter;
        if (!enter) m_impl->prevButtons = 0;
        if (m_proximityCallback) {
            m_proximityCallback(m_impl->activeDeviceId, enter, steadyNowNs());
        }
        return false;
    }

    if (msg->message == kWtCtxOverlap && m_impl->api.wtOverlap) {
        if (msg->hwnd == m_impl->hwnd) {
            m_impl->api.wtOverlap(m_impl->ctx, TRUE);
        }
        return false;
    }

    if (msg->message == kWtInfoChange) {
        if (m_window) attachWindow(m_window);
        return false;
    }

    if (msg->message == kWtPacket && reinterpret_cast<HCTX>(msg->lParam) == m_impl->ctx) {
        const int count = m_impl->api.wtPacketsGet(m_impl->ctx, 64, m_impl->packetScratch.data());
        if (count <= 0 || m_impl->pktSize == 0) return false;

        const double dpr = m_window->devicePixelRatio() > 0.0 ? m_window->devicePixelRatio() : 1.0;
        const WTPKT mask = m_impl->activePktData;
        for (int i = 0; i < count; ++i) {
            const std::uint8_t* ptr = m_impl->packetScratch.data() + static_cast<std::size_t>(i) * m_impl->pktSize;
            UINT status = 0;
            DWORD pktTime = 0;
            UINT cursor = 0;
            DWORD buttons = 0;
            LONG px = 0, py = 0;
            UINT nPress = 0, tPress = 0;
            WtOrientation orient{};
            WtRotation rot{};

            if (mask & kPkContext) (void)readField<HCTX>(ptr);
            if (mask & kPkStatus) status = readField<UINT>(ptr);
            if (mask & kPkTime) pktTime = readField<DWORD>(ptr);
            if (mask & kPkChanged) (void)readField<WTPKT>(ptr);
            if (mask & kPkSerialNumber) (void)readField<UINT>(ptr);
            if (mask & kPkCursor) cursor = readField<UINT>(ptr);
            if (mask & kPkButtons) buttons = readField<DWORD>(ptr);
            if (mask & kPkX) px = readField<LONG>(ptr);
            if (mask & kPkY) py = readField<LONG>(ptr);
            if (mask & kPkZ) (void)readField<LONG>(ptr);
            if (mask & kPkNormalPressure) nPress = readField<UINT>(ptr);
            if (mask & kPkTangentPressure) tPress = readField<UINT>(ptr);
            if (mask & kPkOrientation) orient = readField<WtOrientation>(ptr);
            if (mask & kPkRotation) rot = readField<WtRotation>(ptr);

            POINT clientPt{px, py};
            ScreenToClient(m_impl->hwnd, &clientPt);

            RawPointerEvent ev;
            ev.deviceId = m_impl->activeDeviceId;
            ev.pointerId = ev.deviceId + cursor;
            ev.timestampNs = pktTime > 0 ? static_cast<std::uint64_t>(pktTime) * 1'000'000ULL : steadyNowNs();
            ev.backend = InputBackend::WinTab;
            ev.viewX = static_cast<double>(clientPt.x) / dpr;
            ev.viewY = static_cast<double>(clientPt.y) / dpr;
            ev.capabilities = m_impl->caps;
            ev.inProximity = (status & kTpsProximity) != 0 || m_inProximity;

            bool isEraser = (status & kTpsInvert) != 0;
            UINT csrType = 0;
            if (!isEraser && m_impl->api.wtInfoW(kWtiCursors + cursor, kCsrType, &csrType) > 0) {
                isEraser = ((csrType & 0x0F06U) == 0x0802U);
            }
            ev.pointerType = isEraser ? PointerType::Eraser : PointerType::Pen;

            if (m_impl->caps.hasPressure) {
                const double span = static_cast<double>(m_impl->pressureAxis.axMax - m_impl->pressureAxis.axMin);
                ev.pressure = span > 0.0
                    ? static_cast<double>(static_cast<LONG>(nPress) - m_impl->pressureAxis.axMin) / span
                    : 0.0;
            }
            if (m_impl->caps.hasTangentialPressure) {
                const double span = static_cast<double>(m_impl->tangentAxis.axMax - m_impl->tangentAxis.axMin);
                ev.tangentialPressure = span > 0.0
                    ? static_cast<double>(static_cast<LONG>(tPress) - m_impl->tangentAxis.axMin) / span
                    : 0.0;
            }
            if (m_impl->hasOrientAxes) {
                constexpr double kPi = 3.14159265358979323846;
                const double azMax = static_cast<double>(m_impl->orientAxes[0].axMax > 0 ? m_impl->orientAxes[0].axMax : 3600);
                const double altMax = static_cast<double>(m_impl->orientAxes[1].axMax > 0 ? m_impl->orientAxes[1].axMax : 900);
                const double azRad = (static_cast<double>(orient.orAzimuth) / azMax) * 2.0 * kPi;
                const double altDeg = 90.0 - std::min(90.0, (std::abs(static_cast<double>(orient.orAltitude)) / altMax) * 90.0);
                ev.tiltX = altDeg * std::sin(azRad);
                ev.tiltY = -altDeg * std::cos(azRad);
                if (m_impl->orientAxes[2].axMax > 0) {
                    ev.rotationDegrees = (static_cast<double>(orient.orTwist) /
                                          static_cast<double>(m_impl->orientAxes[2].axMax)) * 360.0;
                }
            }
            if (m_impl->hasRotation) {
                ev.rotationDegrees = static_cast<double>(rot.roYaw) / 10.0;
            }

            std::uint32_t mappedBtns = NoButton;
            if (buttons & 0x01U) mappedBtns |= PrimaryButton;
            if (buttons & 0x02U) mappedBtns |= SecondaryButton;
            if (buttons & 0x04U) mappedBtns |= MiddleButton;
            if (buttons & 0x08U) mappedBtns |= Barrel3Button;
            ev.buttons = mappedBtns;

            const bool tipNow = (buttons & 0x01U) != 0;
            const bool tipPrev = (m_impl->prevButtons & 0x01U) != 0;
            if (tipNow && !tipPrev) ev.phase = PointerPhase::Down;
            else if (tipNow && tipPrev) ev.phase = PointerPhase::Move;
            else if (!tipNow && tipPrev) ev.phase = PointerPhase::Up;
            else ev.phase = PointerPhase::Hover;
            m_impl->prevButtons = buttons;

            if (m_packetCallback) m_packetCallback(ev);
        }
        if (result) *result = 0;
        return true;
    }

    // Suppress Win32 synthesized mouse messages while WinTab stylus is in proximity.
    if (m_inProximity &&
        (msg->message == WM_MOUSEMOVE || msg->message == WM_LBUTTONDOWN || msg->message == WM_LBUTTONUP ||
         msg->message == WM_RBUTTONDOWN || msg->message == WM_RBUTTONUP)) {
        const LPARAM extra = GetMessageExtraInfo();
        const bool isWinTabOrPen = (static_cast<ULONG_PTR>(extra) & 0xFFFFFF00ULL) == 0xFF515700ULL ||
                                   (m_impl->prevButtons != 0);
        if (isWinTabOrPen) {
            if (result) *result = 0;
            return true;
        }
    }
#else
    Q_UNUSED(eventType);
    Q_UNUSED(message);
    Q_UNUSED(result);
#endif
    return false;
}

} // namespace komapaint::input
