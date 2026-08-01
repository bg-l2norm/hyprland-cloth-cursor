set(TESTED_HYPRLAND_COMMITS
    "a0136d8c04687bb36eb8a28eb9d1ff92aea99704"
    "5c9377c15f85c50648f35ca5a213754f95b93ca0"
)
set(HYPRLAND_VERSION_HEADER "/usr/include/hyprland/src/version.h" CACHE FILEPATH "Installed Hyprland version header")

if(NOT EXISTS "${HYPRLAND_VERSION_HEADER}")
    message(FATAL_ERROR "Installed Hyprland version header not found: ${HYPRLAND_VERSION_HEADER}")
endif()

file(STRINGS "${HYPRLAND_VERSION_HEADER}" HYPRLAND_HASH_LINE REGEX "^#define GIT_COMMIT_HASH")
set(HYPRLAND_COMMIT_TESTED FALSE)
foreach(TESTED_COMMIT IN LISTS TESTED_HYPRLAND_COMMITS)
    if(HYPRLAND_HASH_LINE MATCHES "\"${TESTED_COMMIT}\"")
        set(HYPRLAND_COMMIT_TESTED TRUE)
        set(MATCHED_HYPRLAND_COMMIT "${TESTED_COMMIT}")
        break()
    endif()
endforeach()

if(NOT HYPRLAND_COMMIT_TESTED)
    message(WARNING
        "Cloth Cursor has only been tested with Hyprland commits ${TESTED_HYPRLAND_COMMITS}. "
        "Building against the installed headers (${HYPRLAND_HASH_LINE}); runtime ABI and hook checks will still protect the compositor."
    )
else()
    message(STATUS "Verified tested Hyprland header commit ${MATCHED_HYPRLAND_COMMIT}")
endif()
