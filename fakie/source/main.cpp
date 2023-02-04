
#include "r_fakey/r_fake_camera.h"
#include "r_utils/r_file.h"
#ifdef IS_WINDOWS
#pragma warning( push )
#pragma warning( disable : 4244 )
#endif
#include <gst/gst.h>
#ifdef IS_WINDOWS
#pragma warning( pop )
#endif
#include <memory>
#include <functional>
#include <algorithm>
#include <iterator>
#include "r_utils/r_std_utils.h"
#include "utils.h"
#include "bad_guy.h"

using namespace std;
using namespace r_fakey;

std::shared_ptr<r_fake_camera> fake_camera;

void handle_sigint(int d)
{
    if(fake_camera)
        fake_camera->quit();
}

int main(int argc, char* argv[])
{
    install_terminate();

    signal(SIGINT, handle_sigint);

    gst_init(&argc, &argv);

    auto fr = r_utils::r_std_utils::get_env("FAKEY_ROOT");

    auto server_root = (fr.empty())?string("."):fr;

    auto file_names = regular_files_in_dir(server_root);

    vector<string> media_file_names;
    copy_if(begin(file_names), end(file_names), back_inserter(media_file_names),
        [](const string& file_name){return ends_with(file_name, ".mp4") || ends_with(file_name, ".mkv");});

    fake_camera = make_shared<r_fake_camera>(server_root, media_file_names, 554);
    
    fake_camera->start();

    return 0;
}
