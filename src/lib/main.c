/*----------------------------------------------------------------------------------------------------------
 *  Copyright (c) Peter Bjorklund. All rights reserved. https://github.com/piot/conclave-client-cli
 *  Licensed under the MIT License. See LICENSE in the project root for license information.
 *--------------------------------------------------------------------------------------------------------*/
#include <clog/clog.h>
#include <clog/console.h>
#include <errno.h>
#include <redline/edit.h>

clog_config g_clog;

static int g_quit = 0;

static void interruptHandler(int sig)
{
    (void)sig;

    g_quit = 1;
}

#include <time.h>
static void sleepMs(size_t milliseconds)
{
    struct timespec ts;
    ts.tv_sec = milliseconds / 1000;
    ts.tv_nsec = (milliseconds % 1000) * 1000000;

    int err = nanosleep(&ts, &ts);
    if (err != 0) {
        CLOG_ERROR("NOT WORKING:%d", errno)
    }
}

static void drawPrompt(RedlineEdit* edit)
{
    redlineEditPrompt(edit, "conclave> ");
}

int main(void)
{
    g_clog.log = clog_console;
    g_clog.level = CLOG_TYPE_VERBOSE;

    signal(SIGINT, interruptHandler);

    RedlineEdit edit;

    redlineEditInit(&edit);

    drawPrompt(&edit);

    while (!g_quit) {
        int result = redlineEditUpdate(&edit);
        if (result == -1) {
            printf("\nCommand is done!\n");
            drawPrompt(&edit);
            const char* textInput = redlineEditLine(&edit);
            if (tc_str_equal(textInput, "quit")) {
                printf("\n");
                break;
            } else if (tc_str_equal(textInput, "connect")) {
            }
            redlineEditClear(&edit);
            redlineEditReset(&edit);
        }
        sleepMs(16);
    }

    redlineEditClose(&edit);

    return 0;
}
