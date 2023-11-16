/*----------------------------------------------------------------------------------------------------------
 *  Copyright (c) Peter Bjorklund. All rights reserved. https://github.com/piot/conclave-client-cli
 *  Licensed under the MIT License. See LICENSE in the project root for license information.
 *--------------------------------------------------------------------------------------------------------*/
#include <clash/clash.h>
#include <clash/response.h>
#include <clog/clog.h>
#include <clog/console.h>
#include <conclave-client-udp/client.h>
#include <conclave-client/debug.h>
#include <conclave-client/realize_debug.h>
#include <errno.h>
#include <flood/out_stream.h>
#include <guise-client-udp/client.h>
#include <guise-client-udp/read_secret.h>
#include <imprint/default_setup.h>
#include <inttypes.h>
#include <redline/edit.h>
#include <signal.h>

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
    ClvClientUdp clvClient;
    bool hasStartedConclave;
    uint8_t lastShownPingResponseVersion;
    uint8_t lastShownRoomCreateVersion;
    Clog log;
} App;

typedef struct RoomCreateCmd {
    int verbose;
    const char* filename;
} RoomCreateCmd;

typedef struct PingCmd {
    int verbose;
    int knowledge;
} PingCmd;

static void onRoomCreate(void* _self, const void* _data, ClashResponse* response)
{
    App* self = (App*)_self;
    const RoomCreateCmd* data = (const RoomCreateCmd*)_data;
    clashResponseWritecf(response, 3, "room create: (app:%s) '", self->secret);
    clashResponseWritecf(response, 1, "%s", data->filename);
    clashResponseResetColor(response);
    clashResponseWritef(response, "'");
    clashResponseWritecf(response, 18, " verbose:%d\n", data->verbose);

    ClvSerializeRoomCreateOptions createRoom;
    createRoom.applicationId = 1;
    createRoom.maxNumberOfPlayers = 8;
    createRoom.flags = 0;
    createRoom.name = "secret room";

    clvClientUdpCreateRoom(&self->clvClient, &createRoom);
}

static void onState(void* _self, const void* data, ClashResponse* response)
{
    (void)data;
    (void)response;

    App* self = (App*)_self;
    if (!self->hasStartedConclave) {
        clashResponseWritecf(response, 4, "conclave not started yet\n");
        return;
    }
    printf("realize state: %s target: %s\n",
        clvClientRealizeStateToString(self->clvClient.conclaveClient.state),
        clvClientRealizeStateToString(self->clvClient.conclaveClient.targetState));
    printf("state: %s\n", clvClientStateToString(self->clvClient.conclaveClient.client.state));
}

static void onPing(void* _self, const void* _data, ClashResponse* response)
{
    const PingCmd* data = (const PingCmd*)_data;
    (void)response;

    App* self = (App*)_self;
    if (!self->hasStartedConclave) {
        clashResponseWritecf(response, 4, "conclave not started yet\n");
        return;
    }

    clvClientRealizePing(&self->clvClient.conclaveClient, (uint64_t)data->knowledge);
}

static ClashOption roomCreateOptions[]
    = { { "name", 'n', "the file name to store capture to", ClashTypeString | ClashTypeArg,
            "somefile.swamp-capture", offsetof(RoomCreateCmd, filename) },
          { "verbose", 'v', "enable detailed output", ClashTypeFlag, "",
              offsetof(RoomCreateCmd, verbose) } };

static ClashCommand roomCommands[] = {
    { "create", "Create a room", sizeof(struct RoomCreateCmd), roomCreateOptions,
        sizeof(roomCreateOptions) / sizeof(roomCreateOptions[0]), 0, 0, (ClashFn)onRoomCreate },
};

static ClashOption pingOptions[] = {
    { "knowledge", 'k', "how much knowledge (simulation tick ID) that the client has",
        (ClashOptionType)ClashTypeInt | ClashTypeArg, "0", offsetof(PingCmd, knowledge) },
    { "verbose", 'v', "enable detailed output", ClashTypeFlag, "", offsetof(PingCmd, verbose) }
};

static ClashCommand mainCommands[] = {
    { "room", "room commands", 0, 0, 0, roomCommands,
        sizeof(roomCommands) / sizeof(roomCommands[0]), 0 },
    { "state", "show state on conclave client", 0, 0, 0, 0, 0, onState },
    { "ping", "ping the conclave server", sizeof(PingCmd), pingOptions,
        sizeof(pingOptions) / sizeof(pingOptions[0]), 0, 0, onPing },
};

static ClashDefinition commands = { mainCommands, sizeof(mainCommands) / sizeof(mainCommands[0]) };

static void outputChangesIfAny(App* app, RedlineEdit* edit)
{
    ClvClientRealize* conclaveClientRealize = &app->clvClient.conclaveClient;
    ClvClient* conclaveClient = &conclaveClientRealize->client;

    if (conclaveClient->pingResponseOptionsVersion != app->lastShownPingResponseVersion) {
        redlineEditRemove(edit);
        app->lastShownPingResponseVersion = conclaveClient->pingResponseOptionsVersion;
        const ClvSerializePingResponseOptions* pingResponse = &conclaveClient->pingResponseOptions;
        printf("--- room info updated ---\n");
        for (size_t i = 0; i < pingResponse->roomInfo.memberCount; ++i) {
            if (i == pingResponse->roomInfo.indexOfOwner) {
                printf("\xF0\x9F\x91\x91"); // Crown
            } else {
                printf(" ");
            }

            printf("\xF0\x9F\x91\xA4"); // Bust

            printf(" userID: %" PRIX64 "\n", pingResponse->roomInfo.members[i]);
        }
        drawPrompt(edit);
        redlineEditBringback(edit);
    }

    if (conclaveClient->roomCreateVersion != app->lastShownRoomCreateVersion) {
        app->lastShownRoomCreateVersion = conclaveClient->roomCreateVersion;
        redlineEditRemove(edit);
        printf("--- Room Create Done ---\n");
        printf("\xF0\x9F\x8F\xA0 roomID: %d, connectionToRoom: %d\n", conclaveClient->mainRoomId,
            conclaveClient->roomConnectionIndex);
        drawPrompt(edit);
        redlineEditBringback(edit);
    }
}

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

    Clog clvClientUdpLog;
    clvClientUdpLog.config = &g_clog;
    clvClientUdpLog.constantPrefix = "clvClientUdp";

    const char* conclaveHost = "127.0.0.1";
    const uint16_t conclavePort = 27003;

    App app;
    app.secret = "working";
    app.hasStartedConclave = false;
    app.lastShownPingResponseVersion = 0;
    app.lastShownRoomCreateVersion = 0;
    app.log.config = &g_clog;
    app.log.constantPrefix = "app";

    while (!g_quit) {
        MonotonicTimeMs now = monotonicTimeMsNow();
        guiseClientUdpUpdate(&guiseClient, now);
        if (!app.hasStartedConclave && guiseClient.guiseClient.state == GuiseClientStateLoggedIn) {
            CLOG_INFO("conclave init")
            clvClientUdpInit(&app.clvClient, conclaveHost, conclavePort,
                guiseClient.guiseClient.mainUserSessionId, clvClientUdpLog);
            app.hasStartedConclave = true;
        }
        if (app.hasStartedConclave) {
            clvClientUdpUpdate(&app.clvClient, now);
            outputChangesIfAny(&app, &edit);
        }
        int result = redlineEditUpdate(&edit);
        if (result == -1) {
            printf("\n");
            const char* textInput = redlineEditLine(&edit);
            if (tc_str_equal(textInput, "quit")) {
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

                if (outStream.pos > 0) {
                    fputs((const char*)outStream.octets, stdout);
                }
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
