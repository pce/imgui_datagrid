#include "utils.hpp"
#include <cstdlib>

namespace fs = std::filesystem;

void OpenFile(const fs::path& filePath)
{
#ifdef _WIN32
    const auto command = "start \"\" \"" + filePath.string() + "\"";
#elif __APPLE__
    const auto command = "open \"" + filePath.string() + "\"";
#else
    const auto command = "xdg-open \"" + filePath.string() + "\"";
#endif

    std::system(command.c_str());
}
