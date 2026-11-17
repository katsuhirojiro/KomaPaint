#pragma once

#include "PointerInput.h"

#include <QAbstractNativeEventFilter>
#include <QObject>
#include <QPointer>
#include <functional>
#include <vector>

class QWindow;

namespace komapaint::input {

struct WinTabDiagnostics {
    bool dllPresent = false;
    bool contextOpen = false;
    bool enabled = false;
    int deviceCount = 0;
    std::string interfaceName;
    PointerCapabilities capabilities{};
};

// Dynamic wintab32.dll backend for Windows (optional, zero build/driver lock-in).
// Off Windows, compiles cleanly as an unavailable stub.
class WinTabBackend final : public QObject, public QAbstractNativeEventFilter {
    Q_OBJECT
public:
    using PacketCallback = std::function<void(const RawPointerEvent&)>;
    using ProximityCallback = std::function<void(std::uint64_t deviceId, bool enter, std::uint64_t timestampNs)>;

    explicit WinTabBackend(QObject* parent = nullptr);
    ~WinTabBackend() override;

    static WinTabDiagnostics probe();

    bool attachWindow(QWindow* window);
    void detachWindow();
    void setEnabled(bool enabled);
    bool isEnabled() const noexcept { return m_enabled; }
    bool isContextOpen() const noexcept;
    bool isStylusInProximity() const noexcept { return m_inProximity; }

    void setPacketCallback(PacketCallback cb) { m_packetCallback = std::move(cb); }
    void setProximityCallback(ProximityCallback cb) { m_proximityCallback = std::move(cb); }

    WinTabDiagnostics diagnostics() const;

    bool nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result) override;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
    QPointer<QWindow> m_window;
    bool m_enabled = false;
    bool m_inProximity = false;
    PacketCallback m_packetCallback;
    ProximityCallback m_proximityCallback;
};

} // namespace komapaint::input
