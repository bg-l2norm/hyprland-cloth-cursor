#include "runtime.hpp"

#include "cursor_pass.hpp"

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/event/EventBus.hpp>
#include <hyprland/src/managers/PointerManager.hpp>
#include <hyprland/src/render/Renderer.hpp>

#include <algorithm>
#include <bit>
#include <cmath>
#include <dlfcn.h>
#include <exception>
#include <stdexcept>
#include <string_view>
#include <type_traits>
#include <utility>

namespace clothcursor {
namespace {
constexpr double kDamagePadding = 3.0;
constexpr std::string_view kCursorRenderSymbol =
    "_ZN15CPointerManager24renderSoftwareCursorsForEN9Hyprutils6Memory14CSharedPointerI8CMonitorEERKNSt6chrono10time_pointINS5_3_V212steady_clockENS5_8durationIlSt5ratioILl1ELl1000000000EEEEEERNS0_4Math7CRegionESt8optionalINSG_8Vector2DEEb";

std::string_view actionFrom(const std::string& request) {
    const auto first = request.find_first_not_of(" \t");
    if (first == std::string::npos)
        return "status";
    const auto separator = request.find_first_of(" \t", first);
    if (separator == std::string::npos) {
        const std::string_view only{request.data() + first, request.size() - first};
        return only == "clothcursor" ? std::string_view{"status"} : only;
    }
    const auto action = request.find_first_not_of(" \t", separator);
    if (action == std::string::npos)
        return "status";
    const auto end = request.find_first_of(" \t", action);
    return std::string_view{request}.substr(action, end == std::string::npos ? request.size() - action : end - action);
}

bool intersects(Bounds first, Bounds second) noexcept {
    return first.min.x < second.max.x && first.max.x > second.min.x && first.min.y < second.max.y && first.max.y > second.min.y;
}

bool valid(Bounds bounds) noexcept {
    return std::isfinite(bounds.min.x) && std::isfinite(bounds.min.y) && std::isfinite(bounds.max.x) && std::isfinite(bounds.max.y) &&
        bounds.max.x > bounds.min.x && bounds.max.y > bounds.min.y;
}

Bounds joined(Bounds first, Bounds second, double padding) noexcept {
    return {
        .min = {std::min(first.min.x, second.min.x) - padding, std::min(first.min.y, second.min.y) - padding},
        .max = {std::max(first.max.x, second.max.x) + padding, std::max(first.max.y, second.max.y) + padding},
    };
}

template <typename T>
void* memberAddress(T pointer) {
    struct Representation {
        std::uintptr_t pointer;
        std::ptrdiff_t adjustment;
    };
    static_assert(std::is_member_function_pointer_v<T>);
    static_assert(sizeof(T) == sizeof(Representation));
    const auto representation = std::bit_cast<Representation>(pointer);
    if ((representation.pointer & 1U) != 0U)
        return nullptr;
    return reinterpret_cast<void*>(representation.pointer);
}
} // namespace

Runtime::Runtime(HANDLE handle) noexcept : m_handle(handle) {}

Runtime::~Runtime() {
    disable();
    if (m_command)
        HyprlandAPI::unregisterHyprCtlCommand(m_handle, m_command);
    if (s_instance == this)
        s_instance = nullptr;
}

void Runtime::start() {
    if (s_instance && s_instance != this)
        throw std::runtime_error("[clothcursor] another runtime is already active");
    s_instance = this;
    m_command = HyprlandAPI::registerHyprCtlCommand(m_handle, SHyprCtlCommand{
        .name = "clothcursor",
        .exact = false,
        .fn = [this](eHyprCtlOutputFormat format, std::string request) { return command(format, request); },
    });
    if (!m_command)
        throw std::runtime_error("[clothcursor] failed to register hyprctl command");
}

bool Runtime::installHook(std::string& error) noexcept {
    void* address = memberAddress(&CPointerManager::renderSoftwareCursorsFor);
    Dl_info info{};
    if (!address || !dladdr(address, &info) || !info.dli_sname || std::string_view{info.dli_sname} != kCursorRenderSymbol) {
        error = "Hyprland cursor-render symbol mismatch; effect was not enabled and the stock cursor remains active";
        return false;
    }

    m_cursorHook = HyprlandAPI::createFunctionHook(m_handle, address, reinterpret_cast<void*>(&Runtime::hookCursorRender));
    if (!m_cursorHook || !m_cursorHook->hook()) {
        error = "failed to activate cursor-render hook; effect was not enabled and the stock cursor remains active";
        removeHook();
        return false;
    }
    return true;
}

void Runtime::removeHook() noexcept {
    if (!m_cursorHook)
        return;
    HyprlandAPI::removeFunctionHook(m_handle, m_cursorHook);
    m_cursorHook = nullptr;
}

bool Runtime::enable(std::string& error) noexcept {
    if (m_enabled)
        return true;
    if (!g_pHyprRenderer || !g_pPointerManager || !g_pCompositor) {
        error = "Hyprland cursor/render state unavailable";
        return false;
    }
    if (g_pHyprRenderer->type() != Render::IHyprRenderer::RT_GL) {
        error = "OpenGL renderer required; native cursor left untouched";
        return false;
    }

    try {
        const Vector2D pointer = g_pPointerManager->position();
        m_target = {pointer.x, pointer.y};
        reset(m_spring, m_target);
        m_lastStep = Clock::now();
        m_lastBounds = conservativeBounds(currentTransform());
        m_haveBounds = valid(m_lastBounds);

        if (!installHook(error))
            return false;

        m_mouseListener = Event::bus()->m_events.input.mouse.move.listen([this](Vector2D position, Event::SCallbackInfo&) { onMouseMove(position); });
        m_mouseButtonListener = Event::bus()->m_events.input.mouse.button.listen(
            [this](IPointer::SButtonEvent event, Event::SCallbackInfo&) { onMouseButton(event); });
        m_cursorListener = g_pPointerManager->m_events.cursorChanged.listen([this] { onCursorChanged(); });
        m_monitorAddedListener = Event::bus()->m_events.monitor.added.listen([this](PHLMONITOR monitor) { onMonitorAdded(monitor); });

        m_enabled = true;
        g_pPointerManager->lockSoftwareAll();
        m_softwareLocked = true;
        damageCurrentAndNative();
        return true;
    } catch (const std::exception& exception) {
        error = exception.what();
    } catch (...) {
        error = "callback or hook registration failed";
    }

    disable();
    return false;
}

void Runtime::clearListeners() noexcept {
    m_mouseListener.reset();
    m_mouseButtonListener.reset();
    m_cursorListener.reset();
    m_monitorAddedListener.reset();
}

void Runtime::disable() noexcept {
    const bool wasEnabled = std::exchange(m_enabled, false);

    if (g_pHyprRenderer)
        g_pHyprRenderer->m_renderPass.removeAllOfType("CClothCursorPassElement");
    if (wasEnabled && m_haveBounds)
        damage(m_lastBounds);

    clearListeners();
    if (m_softwareLocked && g_pPointerManager) {
        g_pPointerManager->unlockSoftwareAll();
        m_softwareLocked = false;
    }
    removeHook();

    if (wasEnabled && g_pHyprRenderer && g_pPointerManager)
        g_pHyprRenderer->damageBox(g_pPointerManager->getCursorBoxGlobal().expand(kDamagePadding));

    m_haveBounds = false;
    m_spring = {};
    m_pressedButtonCount = 0;
    m_pressAmount = 0.0;
    m_pressVelocity = 0.0;
}

bool Runtime::enabled() const noexcept {
    return m_enabled;
}

void Runtime::hookCursorRender(CPointerManager* self, PHLMONITOR monitor, const Time::steady_tp& now, CRegion& damage,
                               std::optional<Vector2D> overridePos, bool forceRender) {
    auto* runtime = s_instance;
    if (!runtime || !runtime->m_cursorHook)
        return;
    const auto original = reinterpret_cast<CursorRenderFn>(runtime->m_cursorHook->m_original);
    if (!runtime->m_enabled) {
        original(self, monitor, now, damage, overridePos, forceRender);
        return;
    }
    ++runtime->m_cursorHookCalls;
    if (!runtime->renderCursor(self, monitor, now, overridePos, forceRender)) {
        ++runtime->m_fallbackCalls;
        original(self, monitor, now, damage, overridePos, forceRender);
    }
}

bool Runtime::renderCursor(CPointerManager* pointers, PHLMONITOR monitor, const Time::steady_tp& now, std::optional<Vector2D> overridePos,
                           bool forceRender) noexcept {
    if (!pointers || !monitor) {
        ++m_renderRejects;
        ++m_invalidState;
        ++m_missingContext;
        return false;
    }

    const auto& image = pointers->currentCursorImage();
    if (!image.pBuffer && !image.surface) {
        ++m_renderRejects;
        ++m_missingImage;
        return false;
    }
    if (!std::isfinite(image.size.x) || !std::isfinite(image.size.y) || image.size.x <= 0.0 || image.size.y <= 0.0) {
        ++m_emptyImage;
        return true;
    }

    // Our lock means the normal composited frame already contains this cursor.
    // A forced screencopy cursor pass would duplicate it.
    if (forceRender)
        return true;

    if (!std::isfinite(image.scale) || image.scale <= 0.F) {
        ++m_renderRejects;
        ++m_invalidState;
        ++m_invalidScale;
        return false;
    }

    Vector2D pointer = overridePos.value_or(pointers->position());
    if (!std::isfinite(pointer.x) || !std::isfinite(pointer.y)) {
        ++m_renderRejects;
        ++m_invalidState;
        ++m_invalidPointer;
        return false;
    }
    m_target = {pointer.x, pointer.y};

    bool advanced = false;
    if (now > m_lastStep) {
        const double dt = std::chrono::duration<double>(now - m_lastStep).count();
        update(m_spring, m_target, dt);
        updatePress(dt);
        m_lastStep = now;
        advanced = true;
    }

    const OrientedBox transformed = currentTransform();
    const Bounds transformedBounds = conservativeBounds(transformed);
    if (!valid(transformedBounds)) {
        ++m_renderRejects;
        ++m_invalidState;
        ++m_invalidBounds;
        return false;
    }

    if (advanced) {
        const Bounds previous = m_haveBounds ? m_lastBounds : transformedBounds;
        m_lastBounds = transformedBounds;
        m_haveBounds = true;
        const double pressTarget = m_pressedButtonCount > 0 ? 1.0 : 0.0;
        const bool pressSettled = std::abs(m_pressAmount - pressTarget) < 0.001 && std::abs(m_pressVelocity) < 0.01;
        if (!m_spring.settled || !pressSettled) {
            // Damage both the lagged trajectory and the authoritative pointer.
            // Either output can drive the simulation while crossing monitors.
            damage(joined(previous, transformedBounds, kDamagePadding));
            if (g_pHyprRenderer)
                g_pHyprRenderer->damageBox(CBox{pointer.x, pointer.y, 1, 1}.expand(kDamagePadding));
            g_pCompositor->scheduleFrameForMonitor(monitor);
        }
    }

    const Bounds monitorBounds{.min = {monitor->m_position.x, monitor->m_position.y},
                               .max = {monitor->m_position.x + monitor->m_size.x, monitor->m_position.y + monitor->m_size.y}};
    if (!intersects(transformedBounds, monitorBounds))
        return true;

    auto texture = pointers->getCurrentCursorTexture();
    if (!texture) {
        ++m_renderRejects;
        ++m_missingTexture;
        return false;
    }

    const Vec2 logicalSize{image.size.x / image.scale, image.size.y / image.scale};
    const Vec2 renderedHotspot = visualHotspot();
    CBox box{(renderedHotspot.x - image.hotspot.x - monitor->m_position.x) * monitor->m_scale,
             (renderedHotspot.y - image.hotspot.y - monitor->m_position.y) * monitor->m_scale,
             logicalSize.x * monitor->m_scale,
             logicalSize.y * monitor->m_scale};
    box.x = std::round(box.x);
    box.y = std::round(box.y);

    CBox bounds{(transformedBounds.min.x - monitor->m_position.x) * monitor->m_scale,
                (transformedBounds.min.y - monitor->m_position.y) * monitor->m_scale,
                (transformedBounds.max.x - transformedBounds.min.x) * monitor->m_scale,
                (transformedBounds.max.y - transformedBounds.min.y) * monitor->m_scale};

    g_pHyprRenderer->m_renderPass.add(makeUnique<CursorPassElement>(CursorPassElement::RenderData{
        .texture = std::move(texture),
        .box = box,
        .bounds = bounds,
        .hotspot = Vector2D{image.hotspot.x * monitor->m_scale, image.hotspot.y * monitor->m_scale},
        .transform = currentVisual(),
        .monitorScale = monitor->m_scale,
    }));
    ++m_passesQueued;

    if (image.surface) {
        const auto surface = image.surface.lock();
        if (surface && surface->resource())
            surface->resource()->frame(now);
    }

    return true;
}

void Runtime::damage(Bounds bounds) noexcept {
    if (!g_pHyprRenderer || !valid(bounds))
        return;
    g_pHyprRenderer->damageBox(CBox{bounds.min.x, bounds.min.y, bounds.max.x - bounds.min.x, bounds.max.y - bounds.min.y});
}

void Runtime::damageCurrentAndNative() noexcept {
    if (m_haveBounds)
        damage(m_lastBounds);
    if (g_pHyprRenderer && g_pPointerManager)
        g_pHyprRenderer->damageBox(g_pPointerManager->getCursorBoxGlobal().expand(kDamagePadding));
}

OrientedBox Runtime::currentTransform() const noexcept {
    if (!g_pPointerManager)
        return {};
    const auto& image = g_pPointerManager->currentCursorImage();
    if (!std::isfinite(image.scale) || image.scale <= 0.F)
        return {};
    return anchorAtHotspot({image.size.x / image.scale, image.size.y / image.scale}, {image.hotspot.x, image.hotspot.y}, visualHotspot(), currentVisual());
}

VisualTransform Runtime::currentVisual() const noexcept {
    VisualTransform visual = deriveVisual(m_spring, m_target);
    const double compression = 1.0 - std::clamp(m_pressAmount, 0.0, 1.0) * 0.21;
    visual.stretchX *= compression;
    visual.stretchY *= compression;
    return visual;
}

Vec2 Runtime::visualHotspot() const noexcept {
    return m_spring.initialized && finite(m_spring) ? m_spring.body : m_target;
}

void Runtime::updatePress(double dt) noexcept {
    const double target = m_pressedButtonCount > 0 ? 1.0 : 0.0;
    if (!std::isfinite(dt) || dt <= 0.0) {
        m_pressAmount = target;
        m_pressVelocity = 0.0;
        return;
    }

    double remaining = std::min(dt, 1.0 / 30.0);
    while (remaining > 1e-9) {
        const double step = std::min(remaining, 1.0 / 240.0);
        const double acceleration = (target - m_pressAmount) * 420.0 - m_pressVelocity * 32.0;
        m_pressVelocity += acceleration * step;
        m_pressAmount += m_pressVelocity * step;
        remaining -= step;
    }
    m_pressAmount = std::clamp(m_pressAmount, 0.0, 1.0);
    if (std::abs(m_pressAmount - target) < 0.002 && std::abs(m_pressVelocity) < 0.02) {
        m_pressAmount = target;
        m_pressVelocity = 0.0;
    }
}

void Runtime::onMouseMove(Vector2D position) noexcept {
    if (!m_enabled || !g_pPointerManager)
        return;
    if (!std::isfinite(position.x) || !std::isfinite(position.y))
        return;

    const Bounds oldBounds = m_haveBounds ? m_lastBounds : Bounds{};
    m_target = {position.x, position.y};
    const Bounds next = conservativeBounds(currentTransform());
    if (m_haveBounds && valid(next))
        damage(joined(oldBounds, next, kDamagePadding));
    else if (valid(next))
        damage(next);
    if (g_pHyprRenderer)
        g_pHyprRenderer->damageBox(CBox{position.x, position.y, 1, 1}.expand(kDamagePadding));
    if (g_pCompositor) {
        const auto owner = g_pCompositor->getMonitorFromVector(position);
        if (owner)
            g_pCompositor->scheduleFrameForMonitor(owner);
    }
}

void Runtime::onMouseButton(IPointer::SButtonEvent event) noexcept {
    if (!m_enabled || !event.mouse)
        return;
    if (event.state == WL_POINTER_BUTTON_STATE_PRESSED)
        ++m_pressedButtonCount;
    else if (event.state == WL_POINTER_BUTTON_STATE_RELEASED)
        m_pressedButtonCount = std::max(0, m_pressedButtonCount - 1);
    if (m_haveBounds)
        damage(m_lastBounds);
}

void Runtime::onCursorChanged() noexcept {
    if (!m_enabled)
        return;
    damageCurrentAndNative();
    const Bounds next = conservativeBounds(currentTransform());
    if (valid(next))
        damage(next);
}

void Runtime::onMonitorAdded(PHLMONITOR monitor) noexcept {
    if (m_enabled && m_softwareLocked && g_pPointerManager && monitor)
        g_pPointerManager->lockSoftwareForMonitor(monitor);
}

std::string Runtime::command(eHyprCtlOutputFormat format, const std::string& request) {
    const auto action = actionFrom(request);
    if (action == "enable" || (action == "toggle" && !enabled())) {
        std::string error;
        if (!enable(error))
            return format == FORMAT_JSON ? "{\"enabled\":false,\"error\":\"" + error + "\"}\n" : "clothcursor: error: " + error + "\n";
    } else if (action == "disable" || (action == "toggle" && enabled())) {
        disable();
    } else if (action != "status") {
        return format == FORMAT_JSON ? "{\"error\":\"usage: clothcursor [status|enable|disable|toggle]\"}\n" :
                                       "usage: hyprctl clothcursor [status|enable|disable|toggle]\n";
    }

    if (format == FORMAT_JSON)
        return std::string{"{\"enabled\":"} + (enabled() ? "true" : "false") + ",\"cursor_hook_calls\":" + std::to_string(m_cursorHookCalls) +
            ",\"passes_queued\":" + std::to_string(m_passesQueued) + ",\"render_rejects\":" + std::to_string(m_renderRejects) +
            ",\"fallback_calls\":" + std::to_string(m_fallbackCalls) + ",\"missing_image\":" + std::to_string(m_missingImage) +
            ",\"empty_image\":" + std::to_string(m_emptyImage) +
            ",\"missing_texture\":" + std::to_string(m_missingTexture) + ",\"invalid_state\":" + std::to_string(m_invalidState) +
            ",\"missing_context\":" + std::to_string(m_missingContext) + ",\"invalid_scale\":" + std::to_string(m_invalidScale) +
            ",\"invalid_pointer\":" + std::to_string(m_invalidPointer) + ",\"invalid_bounds\":" + std::to_string(m_invalidBounds) +
            ",\"target_x\":" + std::to_string(m_target.x) + ",\"target_y\":" + std::to_string(m_target.y) +
            ",\"body_x\":" + std::to_string(m_spring.body.x) + ",\"body_y\":" + std::to_string(m_spring.body.y) +
            ",\"spring_settled\":" + (m_spring.settled ? "true" : "false") + "}\n";
    return std::string{"clothcursor: "} + (enabled() ? "enabled" : "disabled") + " (cursor hooks " + std::to_string(m_cursorHookCalls) + ", passes " +
        std::to_string(m_passesQueued) + ")\n";
}

} // namespace clothcursor
