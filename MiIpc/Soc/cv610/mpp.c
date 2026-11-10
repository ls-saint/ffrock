#include "cv610warp.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define MI_CV610_MPP_READY_FILE "/tmp/mppd"

static volatile sig_atomic_t g_mpp_running = 1;

static void mpp_signal_handler(int sig)
{
    (void)sig;
    g_mpp_running = 0;
}

static void mpp_install_signals(void)
{
    signal(SIGINT, mpp_signal_handler);
    signal(SIGTERM, mpp_signal_handler);
    signal(SIGHUP, mpp_signal_handler);
    signal(SIGQUIT, mpp_signal_handler);
    signal(SIGABRT, mpp_signal_handler);
    signal(SIGPIPE, SIG_IGN);
}

static int mpp_write_ready_file(void)
{
    FILE* fp = fopen(MI_CV610_MPP_READY_FILE, "w");

    if (fp == NULL) {
        return -1;
    }
    fprintf(fp, "%ld\n", (long)getpid());
    fclose(fp);
    return 0;
}

int main(int argc, char* argv[])
{
    hi_s32 ret;
    int tick = 0;

    (void)argc;
    (void)argv;

    unlink(MI_CV610_MPP_READY_FILE);
    mpp_install_signals();

    ret = sample_venc_normal();
    if (ret != HI_SUCCESS) {
        printf("CV610 mpp start failed ret=%#x\n", ret);
        return 1;
    }

    if (mpp_write_ready_file() != 0) {
        printf("CV610 mpp write ready file failed: %s\n", MI_CV610_MPP_READY_FILE);
        AppMppExit();
        return 1;
    }

    printf("CV610 mpp ready\n");
    fflush(stdout);
    while (g_mpp_running) {
        sleep(1);
        if (++tick >= 30) {
            tick = 0;
            printf("CV610 mpp running\n");
            fflush(stdout);
        }
    }

    unlink(MI_CV610_MPP_READY_FILE);
    AppMppExit();
    printf("CV610 mpp exit\n");
    return 0;
}
