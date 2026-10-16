
#ifndef _r_vss_r_build_info_h
#define _r_vss_r_build_info_h

#include "r_utils/r_macro.h"
#include <cstdint>
#include <string>

namespace r_vss
{

// Which build of Revere this is, for the About box, the local API (/version,
// /cameras) and support. The version, build number and revision are fixed when
// CMake configures; the build time and the dirty flag are stamped on every build.

// git describe without the leading v ("0.0.31", "0.0.31-4-gabc1234"), with
// "-dirty" appended when the tree had uncommitted changes at build time.
R_API std::string revere_version();

// Commits up to the built revision (git rev-list --count HEAD); 0 if unknown.
R_API int64_t revere_build_number();

// When this build was made, ISO 8601 UTC ("2026-10-15T20:01:02Z").
R_API std::string revere_build_time();

// Short git hash of the built revision, or "unknown".
R_API std::string revere_git_revision();

// One line for support: "Revere 0.0.31 (build 273) · built 2026-10-15T20:01:02Z · abc1234".
R_API std::string revere_build_summary();

}

#endif
