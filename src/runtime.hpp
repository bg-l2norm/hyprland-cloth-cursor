#pragma once

#include "physics.hpp"
#include "hyprland_compat.hpp"

#include <hyprland/src/SharedDefs.hpp>
#include <hyprland/src/devices/IPointer.hpp>
#include <hyprland/src/helpers/signal/Signal.hpp>
#include <hyprland/src/helpers/time/Time.hpp>
#include <hyprland/src/plugins/HookSystem.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>

namespace clothcursor {

class Runtime {
  public:
    explicit Runtime(HANDLE handle) noexcept;
    ~Runtime();

    Runtime(const Runtime&) = delete;
    Runtime& operator=(const Runtime&) = delete;

    void start();
    bool enable(std::string& error) noexcept;
    void disable() noexcept;
    [[nodiscard]] bool enabled() const noexcept;
    [[nodiscard]] std::string command(eHyprCtlOutputFormat format, const std::string& request);

  private:
    using Clock = std::chrono::steady_clock;
    using CursorRenderFn = void (*)(HyprPointerManager*, PHLMONITOR, const Time::steady_tp&, CRegion&, std::optional<Vector2D>, bool);

    static void hookCursorRender(HyprPointerManager* self, PHLMONITOR monitor, const Time::steady_tp& now, CRegion& damage,
                                 std::optional<Vector2D> overridePos, bool forceRender);

    bool installHook(std::string& error) noexcept;
    void removeHook() noexcept;
    [[nodiscard]] bool renderCursor(HyprPointerManager* pointers, PHLMONITOR monitor, const Time::steady_tp& now,
                                    std::optional<Vector2D> overridePos, bool forceRender) noexcept;
    void onMouseMove(Vector2D position) noexcept;
    void onMouseButton(IPointer::SButtonEvent event) noexcept;
    void onCursorChanged() noexcept;
    void onMonitorAdded(PHLMONITOR monitor) noexcept;
    void damage(Bounds bounds) noexcept;
    void damageCurrentAndNative() noexcept;
    OrientedBox currentTransform() const noexcept;
    VisualTransform currentVisual() const noexcept;
    Vec2 visualHotspot() const noexcept;
    void updatePress(double dt) noexcept;
    void clearListeners() noexcept;

    inline static Runtime* s_instance = nullptr;

    HANDLE m_handle = nullptr;
    bool m_enabled = false;
    bool m_haveBounds = false;
    bool m_softwareLocked = false;
    std::uint64_t m_cursorHookCalls = 0;
    std::uint64_t m_ownerOutputHookCalls = 0;
    std::uint64_t m_nonOwnerOutputHookCalls = 0;
    std::uint64_t m_passesQueued = 0;
    std::uint64_t m_renderRejects = 0;
    std::uint64_t m_fallbackCalls = 0;
    std::uint64_t m_missingImage = 0;
    std::uint64_t m_emptyImage = 0;
    std::uint64_t m_missingTexture = 0;
    std::uint64_t m_invalidState = 0;
    std::uint64_t m_missingContext = 0;
    std::uint64_t m_invalidScale = 0;
    std::uint64_t m_invalidPointer = 0;
    std::uint64_t m_invalidBounds = 0;
    int m_pressedButtonCount = 0;
    double m_pressAmount = 0.0;
    double m_pressVelocity = 0.0;
    Vec2 m_target{};
    PhysicsState m_spring{};
    Bounds m_lastBounds{};
    Clock::time_point m_lastStep{};

    CFunctionHook* m_cursorHook = nullptr;
    SP<SHyprCtlCommand> m_command;
    CHyprSignalListener m_mouseListener;
    CHyprSignalListener m_mouseButtonListener;
    CHyprSignalListener m_cursorListener;
    CHyprSignalListener m_monitorAddedListener;
};

} // namespace clothcursor
