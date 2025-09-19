#pragma once

#include <string>
#include <windows.h>

struct AppCliResult {
    bool headless;
    bool run_server;
    bool help_requested;
    bool verbose_logging;
    bool exit_requested;
    int  exit_code;
    bool console_attached;

    std::wstring host;
    int          port;
    int          device_index;
    int          posn_poll_ms;

    std::wstring log_path;
    bool         has_log_path;
};

struct AppCliContext {
    AppCliResult& result;
    bool&         console_attached;
};

struct AppCliHooks {
    bool (*handle_option)(const std::wstring& option,
                          int&                index,
                          int                 argc,
                          wchar_t**           argv,
                          AppCliContext&      ctx,
                          void*               user_data);
    void* user_data;
};

void app_cli_ensure_console(AppCliContext& ctx);

AppCliResult parse_app_cli(const AppCliHooks* hooks = nullptr);
