#pragma once

#if __has_include(<hyprland/src/pointer/PointerManager.hpp>)
#include <hyprland/src/pointer/PointerManager.hpp>
#include <hyprland/src/state/MonitorState.hpp>

#define CLOTHCURSOR_NAMESPACED_POINTER_MANAGER 1

namespace clothcursor {
using HyprPointerManager = Pointer::CPointerManager;

inline HyprPointerManager* pointerManager() noexcept {
    return Pointer::mgr().get();
}

inline PHLMONITOR monitorFromVector(Vector2D position) noexcept {
    return State::monitorState() ? State::monitorState()->query().vec(position).run() : PHLMONITOR{};
}

inline void scheduleMonitorFrame(PHLMONITOR monitor) noexcept {
    if (monitor)
        monitor->scheduleFrame();
}
} // namespace clothcursor
#else
#include <hyprland/src/managers/PointerManager.hpp>
#include <hyprland/src/Compositor.hpp>

#define CLOTHCURSOR_NAMESPACED_POINTER_MANAGER 0

namespace clothcursor {
using HyprPointerManager = CPointerManager;

inline HyprPointerManager* pointerManager() noexcept {
    return g_pPointerManager.get();
}

inline PHLMONITOR monitorFromVector(Vector2D position) noexcept {
    return g_pCompositor ? g_pCompositor->getMonitorFromVector(position) : PHLMONITOR{};
}

inline void scheduleMonitorFrame(PHLMONITOR monitor) noexcept {
    if (g_pCompositor && monitor)
        g_pCompositor->scheduleFrameForMonitor(monitor);
}
} // namespace clothcursor
#endif