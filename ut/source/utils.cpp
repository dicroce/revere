
#include "utils.h"
#include "framework.h"
#include "r_utils/r_std_utils.h"
#include <algorithm>
#include <iterator>

using namespace std;
using namespace r_fakey;

shared_ptr<r_fake_camera> _create_fc(int port)
{
    auto fr = r_utils::r_std_utils::get_env("FAKEY_ROOT");

    auto server_root = (fr.empty())?string("."):fr;

    auto file_names = rtf_regular_files_in_dir(server_root);

    vector<string> media_file_names;
    copy_if(begin(file_names), end(file_names), back_inserter(media_file_names),
        [](const string& file_name){return rtf_ends_with(file_name, ".mp4") || rtf_ends_with(file_name, ".mkv");});

    return make_shared<r_fake_camera>(server_root, media_file_names, port);
}

shared_ptr<r_fake_camera> _create_fc(int port, const string& username, const string& password)
{
    auto fr = r_utils::r_std_utils::get_env("FAKEY_ROOT");

    auto server_root = (fr.empty())?string("."):fr;

    auto file_names = rtf_regular_files_in_dir(server_root);

    vector<string> media_file_names;
    copy_if(begin(file_names), end(file_names), back_inserter(media_file_names),
        [](const string& file_name){return rtf_ends_with(file_name, ".mp4") || rtf_ends_with(file_name, ".mkv");});

    return make_shared<r_fake_camera>(server_root, media_file_names, port, username, password);
}
