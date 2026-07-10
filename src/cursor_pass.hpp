// SPDX-License-Identifier: MIT
#pragma once

#include "physics.hpp"

#include <hyprland/src/render/Texture.hpp>
#include <hyprland/src/render/pass/PassElement.hpp>

#include <optional>

namespace clothcursor {

class CursorPassElement final : public IPassElement {
  public:
    struct RenderData {
        SP<Render::ITexture> texture;
        CBox box;
        CBox bounds;
        Vector2D hotspot;
        VisualTransform transform;
        float monitorScale = 1.F;
    };

    explicit CursorPassElement(RenderData data);
    ~CursorPassElement() override = default;

    std::vector<UP<IPassElement>> draw() override;
    bool needsLiveBlur() override;
    bool needsPrecomputeBlur() override;
    std::optional<CBox> boundingBox() override;
    CRegion opaqueRegion() override;
    void discard() override;
    const char* passName() override;
    ePassElementType type() override;

  private:
    RenderData m_data;
};

} // namespace clothcursor
