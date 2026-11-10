#include <signal.h>
#include <stdio.h>
#if defined(_WIN32)
#include <windows.h>
#else
#include <unistd.h>
#endif

#include "miipc.h"

static volatile int g_app_stop_requested;

static void app_sleep_ms(unsigned int milliseconds)
{
#if defined(_WIN32)
    Sleep(milliseconds);
#else
    usleep(milliseconds * 1000u);
#endif
}

static void app_signal_handler(int sig)
{
    (void)sig;
    g_app_stop_requested = 1;
    miipc_request_stop();
}

int main(void)
{
    miipc_config_t config;
    int ret;

    miipc_default_config(&config);
    config.install_signal_handlers = 0;

    signal(SIGINT, app_signal_handler);
    signal(SIGTERM, app_signal_handler);

    ret = miipc_start(&config);
    if (ret != 0) {
        printf("APP start libMiIpc failed ret=%d\n", ret);
    }

    if (miipc_is_started()) {
        while (!g_app_stop_requested && miipc_is_started()) {
            app_sleep_ms(100);
        }
        miipc_stop();
    }

    return ret == 0 ? 0 : 1;
}
