set(EXPECTED_HYPRLAND_COMMIT "a0136d8c04687bb36eb8a28eb9d1ff92aea99704")
set(HYPRLAND_VERSION_HEADER "/usr/include/hyprland/src/version.h" CACHE FILEPATH "Installed Hyprland version header")

if(NOT EXISTS "${HYPRLAND_VERSION_HEADER}")
    message(FATAL_ERROR "Installed Hyprland version header not found: ${HYPRLAND_VERSION_HEADER}")
endif()

file(STRINGS "${HYPRLAND_VERSION_HEADER}" HYPRLAND_HASH_LINE REGEX "^#define GIT_COMMIT_HASH")
if(NOT HYPRLAND_HASH_LINE MATCHES "\"${EXPECTED_HYPRLAND_COMMIT}\"")
    message(WARNING
        "Cloth Cursor has only been tested with Hyprland commit ${EXPECTED_HYPRLAND_COMMIT}. "
        "Building against the installed headers (${HYPRLAND_HASH_LINE}); runtime ABI and hook checks will still protect the compositor."
    )
else()
    message(STATUS "Verified tested Hyprland header commit ${EXPECTED_HYPRLAND_COMMIT}")
endif()
