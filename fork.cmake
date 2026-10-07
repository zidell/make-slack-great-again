# This fork's releases (src/app/update/fork_release.h): scripts/fork-release.*
# configure MSGA_FORK_RELEASE (the N of <MSGA_VERSION>.N) and MSGA_UPDATES;
# a local build is <MSGA_VERSION>.0 and never checks for updates.
set(MSGA_FORK_RELEASE 0 CACHE STRING "This fork's release on top of MSGA_VERSION (0: a local build)")
option(MSGA_UPDATES "Check the fork's GitHub releases for updates" OFF)
add_compile_definitions(MSGA_FORK_RELEASE=${MSGA_FORK_RELEASE})
if(MSGA_UPDATES)
    add_compile_definitions(MSGA_UPDATES)
endif()
