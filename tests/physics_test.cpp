#include "physics.hpp"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>

using namespace clothcursor;

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

bool near(double a, double b, double epsilon) {
    return std::abs(a - b) <= epsilon;
}

PhysicsState simulate(double hz, double seconds, Vec2 start, Vec2 target) {
    PhysicsState state;
    reset(state, start);
    const int frames = static_cast<int>(hz * seconds);
    for (int i = 0; i < frames; ++i)
        update(state, target, 1.0 / hz);
    return state;
}

void testStationaryConvergence() {
    auto state = simulate(120.0, 2.0, {0, 0}, {100, 50});
    require(state.settled, "spring should settle");
    require(near(state.body.x, 100.0, 1e-9) && near(state.body.y, 50.0, 1e-9),
            "settled body should snap exactly to target");
}

void testFrameRateIndependence() {
    const auto at60 = simulate(60.0, 0.18, {0, 0}, {80, -30});
    const auto at144 = simulate(144.0, 0.18, {0, 0}, {80, -30});
    require(length(at60.body - at144.body) < 1.5,
            "60 Hz and 144 Hz trajectories should remain close");
}

void testJumpAndDtClamps() {
    PhysicsState state;
    reset(state, {0, 0});
    update(state, {100000, -100000}, 8.0);
    require(finite(state), "large jump/frame stall must stay finite");
    require(length(state.body - Vec2{100000, -100000}) <= PhysicsParams{}.maxLag + 1e-6,
            "visual body lag must be clamped");
}

void testInvalidInputResetsSafely() {
    PhysicsState state;
    reset(state, {5, 6});
    state.velocity = {20, 20};
    update(state, {9, 10}, std::numeric_limits<double>::quiet_NaN());
    require(state.settled && near(state.body.x, 9, 0) && near(state.body.y, 10, 0),
            "invalid dt must fail safe to target");
}

void testHotspotAnchoring() {
    const Vec2 size{100, 70};
    const Vec2 hotspot{7, 11};
    const Vec2 target{412.25, 227.75};
    const VisualTransform transform{.angle = 0.17, .bend = 0.10, .stretchX = 1.11, .stretchY = 0.965};
    const auto box = anchorAtHotspot(size, hotspot, target, transform);
    const auto reconstructed = transformedHotspot(box, size, hotspot);
    require(length(reconstructed - target) < 1e-9,
            "transformed cursor hotspot must remain exact");
}

void testBoundsContainRotatedCorners() {
    const auto box = anchorAtHotspot({100, 50}, {8, 10}, {48, 30}, {.angle = 0.2, .bend = 0.08});
    const auto bounds = conservativeBounds(box);
    require(bounds.min.x < bounds.max.x && bounds.min.y < bounds.max.y,
            "bounds must be non-empty");
    require(std::isfinite(bounds.min.x) && std::isfinite(bounds.max.y),
            "bounds must be finite");
    require((bounds.max.x - bounds.min.x) >= 100.0,
            "rotation should conservatively expand horizontal bounds");
}

void testVisualClamps() {
    PhysicsState state;
    state.initialized = true;
    state.settled = false;
    state.body = {10000, -10000};
    state.velocity = {100000, -100000};
    const auto visual = deriveVisual(state, {0, 0});
    const PhysicsParams params;
    require(std::abs(visual.angle) <= params.maxAngle,
            "rotation must be clamped");
    require(std::abs(visual.bend) <= params.maxBend,
            "bend must be clamped");
    require(visual.stretchX <= params.maxStretch && visual.stretchY >= params.minCrossStretch,
            "stretch must be clamped");
}

} // namespace

int main() {
    testStationaryConvergence();
    testFrameRateIndependence();
    testJumpAndDtClamps();
    testInvalidInputResetsSafely();
    testHotspotAnchoring();
    testBoundsContainRotatedCorners();
    testVisualClamps();
    std::cout << "physics tests passed\n";
    return 0;
}
