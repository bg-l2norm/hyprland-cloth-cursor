#pragma once

#include <algorithm>
#include <cmath>

namespace clothcursor {

struct Vec2 {
    double x = 0.0;
    double y = 0.0;

    constexpr Vec2 operator+(const Vec2& o) const { return {x + o.x, y + o.y}; }
    constexpr Vec2 operator-(const Vec2& o) const { return {x - o.x, y - o.y}; }
    constexpr Vec2 operator*(double s) const { return {x * s, y * s}; }
    constexpr Vec2 operator/(double s) const { return {x / s, y / s}; }
    constexpr Vec2& operator+=(const Vec2& o) { x += o.x; y += o.y; return *this; }
    constexpr Vec2& operator*=(double s) { x *= s; y *= s; return *this; }
};

struct PhysicsParams {
    double stiffness = 230.0;
    double damping = 26.0;
    double maxLag = 36.0;
    double maxVelocity = 2600.0;
    double maxDt = 1.0 / 30.0;
    double integrationStep = 1.0 / 240.0;
    double settleDistance = 0.035;
    double settleVelocity = 0.35;
    double maxAngle = 0.20;      // radians
    double maxBend = 0.12;       // skew angle in radians
    double maxStretch = 1.12;
    double minCrossStretch = 0.96;
};

struct PhysicsState {
    Vec2 body{};
    Vec2 velocity{};
    bool initialized = false;
    bool settled = true;
};

struct VisualTransform {
    double angle = 0.0;
    double bend = 0.0;
    double stretchX = 1.0;
    double stretchY = 1.0;
};

struct OrientedBox {
    Vec2 topLeft{};
    Vec2 size{};
    Vec2 hotspot{};
    VisualTransform transform{};
};

struct Bounds {
    Vec2 min{};
    Vec2 max{};
};

[[nodiscard]] double length(Vec2 v);
[[nodiscard]] bool finite(const PhysicsState& state);
void reset(PhysicsState& state, Vec2 target);
void update(PhysicsState& state, Vec2 target, double dt, const PhysicsParams& params = {});
[[nodiscard]] VisualTransform deriveVisual(const PhysicsState& state, Vec2 target, const PhysicsParams& params = {});
[[nodiscard]] OrientedBox anchorAtHotspot(Vec2 logicalSize, Vec2 hotspot, Vec2 target, VisualTransform transform);
[[nodiscard]] Vec2 transformedHotspot(const OrientedBox& box, Vec2 originalSize, Vec2 hotspot);
[[nodiscard]] Bounds conservativeBounds(const OrientedBox& box);

} // namespace clothcursor
