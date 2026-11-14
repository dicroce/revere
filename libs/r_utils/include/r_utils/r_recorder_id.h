
#ifndef r_utils_r_recorder_id_h
#define r_utils_r_recorder_id_h

#include "r_utils/r_macro.h"
#include <string>

namespace r_utils
{

// The recorder id: a random UUID made the first time Revere runs with a data
// directory (the "top dir") and kept there, so it stays the same across
// restarts, upgrades, address changes and renames. It identifies this
// recorder to the programs that talk to it (a client on the home network, a
// plugin, the cloud plugin reporting it upstream), so they can tell one
// recorder from another without relying on an address or a name the user can
// change. It's an identifier, not a secret: it proves nothing about who is
// asking or answering.
//
// Moving the data directory to another machine moves the id with it, which is
// right: the recordings, cameras and settings move too.
namespace r_recorder_id
{

// <top_dir>/recorder_id
R_API std::string path(const std::string& top_dir);

// Whether s is a UUID as this writes them: 36 characters, lower-case hex in
// 8-4-4-4-12 groups.
R_API bool well_formed(const std::string& s);

// The id kept in top_dir, made and kept first if there's none (or the file
// doesn't hold one). Empty if it can't be kept: an id that changed with every
// run would be worse than none. Never throws.
R_API std::string get_or_create(const std::string& top_dir);

}

}

#endif
