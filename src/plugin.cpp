#include "runtime.hpp"

#include <hyprland/src/plugins/PluginAPI.hpp>

#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {
constexpr std::string_view kExpectedCommit = "a0136d8c04687bb36eb8a28eb9d1ff92aea99704";
constexpr std::string_view kExpectedAbi = "a0136d8c04687bb36eb8a28eb9d1ff92aea99704_aq_0.12_hu_0.13_hg_0.5_hc_0.1_hlg_0.6";
std::unique_ptr<clothcursor::Runtime> runtime;
}

APICALL EXPORT std::string PLUGIN_API_VERSION() {
    return HYPRLAND_API_VERSION;
}

APICALL EXPORT PLUGIN_DESCRIPTION_INFO PLUGIN_INIT(HANDLE handle) {
    const std::string serverAbi = __hyprland_api_get_hash();
    const std::string clientAbi = __hyprland_api_get_client_hash();
    const auto version = HyprlandAPI::getHyprlandVersion(handle);

    if (version.hash != kExpectedCommit || serverAbi != kExpectedAbi || clientAbi != kExpectedAbi || serverAbi != clientAbi)
        throw std::runtime_error("[clothcursor] exact Hyprland ABI mismatch; refusing initialization");

    runtime = std::make_unique<clothcursor::Runtime>(handle);
    runtime->start();
    return {"clothcursor", "Spring-smoothed cloth cursor with click squish", "bg-l2norm", "0.1.0"};
}

APICALL EXPORT void PLUGIN_EXIT() {
    if (runtime)
        runtime->disable();
    runtime.reset();
}
