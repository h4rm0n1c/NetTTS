#include "app_cli.hpp"

#include <shellapi.h>
#include <windows.h>
#include <cstdlib>

#include "log.hpp"

void app_cli_ensure_console(AppCliContext& ctx){
    if (!ctx.console_attached){
        log_attach_console();
        ctx.console_attached = true;
        ctx.result.console_attached = true;
    }
}

AppCliResult parse_app_cli(const AppCliHooks* hooks){
    AppCliResult result{};
    result.headless        = false;
    result.run_server      = false;
    result.help_requested  = false;
    result.verbose_logging = false;
    result.exit_requested  = false;
    result.exit_code       = 0;
    result.console_attached= false;
    result.host            = L"127.0.0.1";
    result.port            = 5555;
    result.device_index    = -1;
    result.posn_poll_ms    = 0;
    result.has_log_path    = false;
    result.log_path.clear();

    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) return result;

    bool console_attached = false;
    AppCliContext ctx{result, console_attached};

    for (int i = 1; i < argc; ++i){
        std::wstring arg = argv[i];

        if (arg == L"--help" || arg == L"-h" || arg == L"/?"){
            result.help_requested = true;
        } else if (arg == L"--headless" || arg == L"--verbose"){
            result.headless = true;
            result.verbose_logging = true;
        } else if (arg == L"--runserver" || arg == L"--startserver"){
            result.run_server = true;
        } else if (arg == L"--log" && i + 1 < argc){
            result.has_log_path = true;
            result.log_path = argv[++i];
        } else if (arg == L"--host" && i + 1 < argc){
            result.host = argv[++i];
        } else if (arg == L"--port" && i + 1 < argc){
            result.port = _wtoi(argv[++i]);
        } else if (arg == L"--devnum" && i + 1 < argc){
            result.device_index = _wtoi(argv[++i]);
        } else if (arg == L"--posn-ms" && i + 1 < argc){
            result.posn_poll_ms = _wtoi(argv[++i]);
        } else if (hooks && hooks->handle_option &&
                   hooks->handle_option(arg, i, argc, argv, ctx, hooks->user_data)){
            if (result.exit_requested) break;
        }
    }

    if (result.headless){
        result.verbose_logging = true;
    }

    if ((result.headless || result.help_requested) && !console_attached){
        app_cli_ensure_console(ctx);
    }

    LocalFree(argv);

    result.console_attached = console_attached;
    return result;
}
