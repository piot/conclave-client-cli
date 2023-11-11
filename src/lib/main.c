/*----------------------------------------------------------------------------------------------------------
 *  Copyright (c) Peter Bjorklund. All rights reserved. https://github.com/piot/conclave-client-cli
 *  Licensed under the MIT License. See LICENSE in the project root for license information.
 *--------------------------------------------------------------------------------------------------------*/
#include <clash/clash.h>
#include <clash/response.h>
#include <clog/clog.h>
#include <clog/console.h>
#include <errno.h>
#include <flood/out_stream.h>
#include <guise-client-udp/client.h>
#include <guise-client-udp/read_secret.h>
#include <imprint/default_setup.h>
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

typedef struct App {
    const char* secret;
} App;

typedef struct RoomCreateCmd {
    int verbose;
    const char* filename;
} RoomCreateCmd;

static void onRoomCreate(App* self, const RoomCreateCmd* data, ClashResponse* response)
{
    clashResponseWritecf(response, 3, "\nroom create: (app:%s) '", self->secret);
    clashResponseWritecf(response, 1, "%s", data->filename);
    clashResponseResetColor(response);
    clashResponseWritef(response, "'");
    clashResponseWritecf(response, 18, " verbose:%d\n", data->verbose);
}

static ClashOption recordStartOptions[]
    = { { "name", 'n', "the file name to store capture to", ClashTypeString | ClashTypeArg,
            "somefile.swamp-capture", offsetof(RoomCreateCmd, filename) },
          { "verbose", 'v', "enable detailed output", ClashTypeFlag, "",
              offsetof(RoomCreateCmd, verbose) } };

static ClashCommand roomCommands[] = {
    { "create", "Create a room", sizeof(struct RoomCreateCmd), recordStartOptions,
        sizeof(recordStartOptions) / sizeof(recordStartOptions[0]), 0, 0, (ClashFn)onRoomCreate },
};

static ClashCommand mainCommands[] = { { "room", "room commands", 0, 0, 0, roomCommands,
    sizeof(roomCommands) / sizeof(roomCommands[0]), 0 } };

static ClashDefinition commands = { mainCommands, sizeof(mainCommands) / sizeof(mainCommands[0]) };

int main(void)
{
    g_clog.log = clog_console;
    g_clog.level = CLOG_TYPE_VERBOSE;

    signal(SIGINT, interruptHandler);

    GuiseClientUdpSecret guiseSecret;
    guiseClientUdpReadSecret(&guiseSecret);

    ImprintDefaultSetup imprint;
    imprintDefaultSetupInit(&imprint, 128 * 1024);

    GuiseClientUdp guiseClient;
    guiseClientUdpInit(&guiseClient, 0, "127.0.0.1", 27004, &guiseSecret);

    RedlineEdit edit;

    redlineEditInit(&edit);

    drawPrompt(&edit);

    FldOutStream outStream;
    uint8_t buf[1024];
    fldOutStreamInit(&outStream, buf, 1024);

    App app;
    app.secret = "working";

    while (!g_quit) {
        MonotonicTimeMs now = monotonicTimeMsNow();
        guiseClientUdpUpdate(&guiseClient, now);
        int result = redlineEditUpdate(&edit);
        if (result == -1) {
            printf("\nCommand is done!\n");
            drawPrompt(&edit);
            const char* textInput = redlineEditLine(&edit);
            if (tc_str_equal(textInput, "quit")) {
                printf("\n");
                break;
            } else if (tc_str_equal(textInput, "help")) {
                outStream.p = outStream.octets;
                outStream.pos = 0;
                clashUsageToStream(&commands, &outStream);
                puts((const char*)outStream.octets);
                outStream.p = outStream.octets;
                outStream.pos = 0;
            } else {
                outStream.p = outStream.octets;
                outStream.pos = 0;
                int parseResult = clashParseString(&commands, textInput, &app, &outStream);
                if (parseResult < 0) {
                    printf("unknown command %d\n", parseResult);
                }

                puts((const char*)outStream.octets);
                outStream.p = outStream.octets;
                outStream.pos = 0;
            }
            redlineEditClear(&edit);
            drawPrompt(&edit);
            redlineEditReset(&edit);
        }
        sleepMs(16);
    }

    redlineEditClose(&edit);

    return 0;
}
