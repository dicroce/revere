
#ifndef __revere_ut_utils_h
#define __revere_ut_utils_h

#include <memory>
#include "r_fakey/r_fake_camera.h"

std::shared_ptr<r_fakey::r_fake_camera> _create_fc(int port);
std::shared_ptr<r_fakey::r_fake_camera> _create_fc(int port, const std::string& username, const std::string& password);

#endif
