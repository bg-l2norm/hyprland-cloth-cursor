#include "physics.hpp"

#include <array>
#include <limits>

namespace clothcursor {
namespace {

constexpr double kEpsilon = 1e-12;

Vec2 rotate(Vec2 p, double angle) {
    const double c = std::cos(angle);
    const double s = std::sin(angle);
    return {p.x * c - p.y * s, p.x * s + p.y * c};
}

Vec2 deform(Vec2 p, VisualTransform transform) {
    p.x *= transform.stretchX;
    p.y *= transform.stretchY;
    p.x += std::tan(transform.bend) * p.y;
    return rotate(p, transform.angle);
}

Vec2 clampMagnitude(Vec2 value, double limit) {
    const double magnitude = length(value);
    if (magnitude <= limit || magnitude <= kEpsilon)
        return value;
    return value * (limit / magnitude);
}

} // namespace

double length(Vec2 v) {
    return std::hypot(v.x, v.y);
}

bool finite(const PhysicsState& state) {
    return std::isfinite(state.body.x) && std::isfinite(state.body.y) &&
           std::isfinite(state.velocity.x) && std::isfinite(state.velocity.y);
}

void reset(PhysicsState& state, Vec2 target) {
    state.body = target;
    state.velocity = {};
    state.initialized = true;
    state.settled = true;
}

void update(PhysicsState& state, Vec2 target, double dt, const PhysicsParams& params) {
    if (!state.initialized || !std::isfinite(dt) || dt <= 0.0 ||
        !std::isfinite(target.x) || !std::isfinite(target.y)) {
        reset(state, target);
        return;
    }

    double remaining = std::min(dt, params.maxDt);
    const double hMax = std::max(params.integrationStep, 1e-5);

    while (remaining > kEpsilon) {
        const double h = std::min(remaining, hMax);
        const Vec2 displacement = target - state.body;
        const Vec2 acceleration = displacement * params.stiffness - state.velocity * params.damping;
        state.velocity += acceleration * h;
        state.velocity = clampMagnitude(state.velocity, params.maxVelocity);
        state.body += state.velocity * h;

        const Vec2 lag = state.body - target;
        if (length(lag) > params.maxLag)
            state.body = target + clampMagnitude(lag, params.maxLag);
        remaining -= h;
    }

    if (!finite(state)) {
        reset(state, target);
        return;
    }

    state.settled = length(state.body - target) <= params.settleDistance &&
                    length(state.velocity) <= params.settleVelocity;
    if (state.settled) {
        state.body = target;
        state.velocity = {};
    }
}

VisualTransform deriveVisual(const PhysicsState& state, Vec2 target, const PhysicsParams& params) {
    if (!state.initialized || !finite(state))
        return {};

    const Vec2 lag = state.body - target;
    const double lagMagnitude = std::min(length(lag), params.maxLag);
    const double speed = std::min(length(state.velocity), params.maxVelocity);

    // Deformation itself stays anchored at the position supplied by the
    // renderer. Runtime may deliberately supply its smoothed visual anchor.
    const double angleSignal = (-lag.x * 0.011) + (state.velocity.x * -0.00022);
    const double stretchSignal = lagMagnitude * 0.0021 + speed * 0.000018;

    VisualTransform result;
    result.angle = std::clamp(angleSignal, -params.maxAngle, params.maxAngle);
    result.bend = std::clamp(angleSignal * 0.55, -params.maxBend, params.maxBend);
    result.stretchX = std::clamp(1.0 + stretchSignal, 1.0, params.maxStretch);
    result.stretchY = std::clamp(1.0 - (result.stretchX - 1.0) * 0.34,
                                 params.minCrossStretch, 1.0);
    return result;
}

OrientedBox anchorAtHotspot(Vec2 logicalSize, Vec2 hotspot, Vec2 target, VisualTransform transform) {
    return {
        .topLeft = target - hotspot,
        .size = logicalSize,
        .hotspot = hotspot,
        .transform = transform,
    };
}

Vec2 transformedHotspot(const OrientedBox& box, Vec2 originalSize, Vec2 hotspot) {
    (void)originalSize;
    (void)hotspot;
    return box.topLeft + box.hotspot;
}

Bounds conservativeBounds(const OrientedBox& box) {
    const Vec2 target = box.topLeft + box.hotspot;
    const std::array<Vec2, 4> localCorners{{
        {-box.hotspot.x, -box.hotspot.y},
        {box.size.x - box.hotspot.x, -box.hotspot.y},
        {box.size.x - box.hotspot.x, box.size.y - box.hotspot.y},
        {-box.hotspot.x, box.size.y - box.hotspot.y},
    }};

    Bounds result{
        .min = {std::numeric_limits<double>::infinity(), std::numeric_limits<double>::infinity()},
        .max = {-std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity()},
    };

    for (const auto& corner : localCorners) {
        const Vec2 p = target + deform(corner, box.transform);
        result.min.x = std::min(result.min.x, p.x);
        result.min.y = std::min(result.min.y, p.y);
        result.max.x = std::max(result.max.x, p.x);
        result.max.y = std::max(result.max.y, p.y);
    }
    return result;
}

} // namespace clothcursor
