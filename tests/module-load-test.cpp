#include <obs.h>
#include <util/base.h>

#include <cstdarg>
#include <cstdio>
#include <filesystem>
#include <vector>
#include <windows.h>

namespace {
void logMessage(int level, const char *format, va_list arguments, void *)
{
    std::vfprintf(level >= LOG_WARNING ? stderr : stdout, format, arguments);
    std::fputc('\n', level >= LOG_WARNING ? stderr : stdout);
}
}

int main(int argc, char **argv)
{
    if (argc != 3) {
        std::fprintf(stderr, "usage: module-load-test <plugin-dll> <data-directory>\n");
        return 2;
    }
    base_set_log_handler(logMessage, nullptr);
    const auto plugin = std::filesystem::absolute(argv[1]);
    const auto data = std::filesystem::absolute(argv[2]);
    if (!std::filesystem::is_regular_file(plugin) || !std::filesystem::is_directory(data)) {
        std::fprintf(stderr, "plugin or data path is missing\n");
        return 2;
    }
    // OBS has already loaded its frontend and Qt libraries before discovering
    // third-party modules. Reproduce that host state in this small harness.
    std::vector<HMODULE> hostLibraries;
    for (const auto *name : {"obs-frontend-api.dll", "Qt6Core.dll", "Qt6Gui.dll",
                             "Qt6Widgets.dll", "Qt6Network.dll"}) {
        HMODULE library = LoadLibraryA(name);
        if (!library) {
            std::fprintf(stderr, "host dependency failed to load: %s (%lu)\n", name, GetLastError());
            for (auto loaded : hostLibraries)
                FreeLibrary(loaded);
            return 7;
        }
        hostLibraries.push_back(library);
    }
    if (!obs_startup("en-US", nullptr, nullptr)) {
        std::fprintf(stderr, "obs_startup failed\n");
        return 3;
    }
    int result = 0;
    obs_module_t *module = nullptr;
    const int opened = obs_open_module(&module, plugin.string().c_str(), data.string().c_str());
    if (opened != MODULE_SUCCESS) {
        std::fprintf(stderr, "obs_open_module failed: %d\n", opened);
        result = 4;
    } else if (!obs_init_module(module)) {
        std::fprintf(stderr, "obs_init_module failed\n");
        result = 5;
    } else if ((obs_get_source_output_flags("excelprotocol_fx") & (OBS_SOURCE_VIDEO | OBS_SOURCE_AUDIO))
               != (OBS_SOURCE_VIDEO | OBS_SOURCE_AUDIO)) {
        std::fprintf(stderr, "excelprotocol_fx audio/video source was not registered\n");
        result = 6;
    } else {
        std::puts("ExcelProtocol FX loaded and registered its OBS audio/video source.");
    }
    obs_shutdown();
    for (auto iterator = hostLibraries.rbegin(); iterator != hostLibraries.rend(); ++iterator)
        FreeLibrary(*iterator);
    return result;
}
