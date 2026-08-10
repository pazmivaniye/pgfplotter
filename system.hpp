#ifndef PGFPLOTTER_SYSTEM_HPP
#define PGFPLOTTER_SYSTEM_HPP

#include <string>
#include <vector>

namespace pgfplotter
{
    void system_call(const std::string& file, const std::vector<std::string>&
        args = {});
    void split_path(const std::string& path, std::string& dir, std::string&
        name);
}

#endif
