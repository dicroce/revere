#ifndef __utils_h
#define __utils_h

#include <memory>
#include <functional>
#include <vector>
#include <string>

void install_terminate();
void handle_terminate();

template<typename PT>
class raii final
{
public:
    template<typename DL>
    raii(PT* p, DL dl) : p(p, dl) {}
    raii(raii&&) = default;
    raii(const raii&) = delete;
    ~raii() = default;
    raii& operator=(const raii&) = delete;
    operator PT*() {return p.get();}
private:
    std::unique_ptr<PT, std::function<void(PT*)>> p;
};

std::vector<std::string> regular_files_in_dir(const std::string& dir);
bool ends_with(const std::string& a, const std::string& b);

#endif
