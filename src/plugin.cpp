#include "runtime.hpp"

#include <hyprland/src/plugins/PluginAPI.hpp>

#include <memory>
#include <algorithm>
#include <array>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {
struct TestedBuild {
    std::string_view commit;
    std::string_view abi;
};
constexpr std::array kTestedBuilds{
    TestedBuild{"a0136d8c04687bb36eb8a28eb9d1ff92aea99704", "a0136d8c04687bb36eb8a28eb9d1ff92aea99704_aq_0.12_hu_0.13_hg_0.5_hc_0.1_hlg_0.6"},
    TestedBuild{"5c9377c15f85c50648f35ca5a213754f95b93ca0", "5c9377c15f85c50648f35ca5a213754f95b93ca0_aq_0.14_hu_0.14_hg_0.5_hc_0.1_hlg_0.6"},
    TestedBuild{"efb50993780079460b0cbed1363e2166a2de1d9f", "efb50993780079460b0cbed1363e2166a2de1d9f_aq_0.14_hu_0.14_hg_0.5_hc_0.1_hlg_0.6"},
};
std::unique_ptr<clothcursor::Runtime> runtime;
}

APICALL EXPORT std::string PLUGIN_API_VERSION() {
    return HYPRLAND_API_VERSION;
}

APICALL EXPORT PLUGIN_DESCRIPTION_INFO PLUGIN_INIT(HANDLE handle) {
    const std::string serverAbi = __hyprland_api_get_hash();
    const std::string clientAbi = __hyprland_api_get_client_hash();
    const auto version = HyprlandAPI::getHyprlandVersion(handle);

    if (serverAbi != clientAbi)
        throw std::runtime_error("[clothcursor] runtime ABI differs from the headers used to build the plugin; refusing initialization");
    const bool tested = std::ranges::any_of(kTestedBuilds, [&](const TestedBuild& build) { return version.hash == build.commit && serverAbi == build.abi; });
    if (!tested)
        std::cerr << "[clothcursor] WARN: running on an untested Hyprland build; guarded runtime checks remain enabled\n";

    runtime = std::make_unique<clothcursor::Runtime>(handle);
    runtime->start();
    return {"clothcursor", "Spring-smoothed cloth cursor with click squish", "bg-l2norm", "0.2.1"};
}

APICALL EXPORT void PLUGIN_EXIT() {
    if (runtime)
        runtime->disable();
    runtime.reset();
}
