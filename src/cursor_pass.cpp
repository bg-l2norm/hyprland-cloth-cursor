// SPDX-License-Identifier: MIT
#include "cursor_pass.hpp"

#include <hyprland/src/render/Renderer.hpp>

#include <array>
#include <cmath>
#include <utility>

namespace clothcursor {

CursorPassElement::CursorPassElement(RenderData data) : m_data(std::move(data)) {}

std::vector<UP<IPassElement>> CursorPassElement::draw() {
    Mat3x3 transform = Mat3x3::identity();
    transform.translate(m_data.box.pos());
    transform.translate(m_data.hotspot);
    transform.rotate(m_data.transform.angle);
    transform.multiply(Mat3x3{std::array<float, 9>{1.F, static_cast<float>(std::tan(m_data.transform.bend)), 0.F, 0.F, 1.F, 0.F, 0.F, 0.F, 1.F}});
    transform.scale({m_data.transform.stretchX, m_data.transform.stretchY});
    transform.translate(-m_data.hotspot);
    transform.translate(-m_data.box.pos());

    Mat3x3 projection = g_pHyprRenderer->m_renderData.targetProjection.copy().multiply(transform);
    std::swap(g_pHyprRenderer->m_renderData.targetProjection, projection);
    CTexPassElement::SRenderData renderData;
    renderData.tex = m_data.texture;
    renderData.box = m_data.box;
    g_pHyprRenderer->draw(renderData, g_pHyprRenderer->m_renderData.damage);
    std::swap(g_pHyprRenderer->m_renderData.targetProjection, projection);
    return {};
}

bool CursorPassElement::needsLiveBlur() {
    return false;
}

bool CursorPassElement::needsPrecomputeBlur() {
    return false;
}

std::optional<CBox> CursorPassElement::boundingBox() {
    if (m_data.monitorScale <= 0.F)
        return std::nullopt;
    return m_data.bounds.copy().scale(1.F / m_data.monitorScale).round();
}

CRegion CursorPassElement::opaqueRegion() {
    return {};
}

void CursorPassElement::discard() {}

const char* CursorPassElement::passName() {
    return "CClothCursorPassElement";
}

ePassElementType CursorPassElement::type() {
    return EK_CUSTOM;
}

} // namespace clothcursor
