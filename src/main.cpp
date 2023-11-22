#include <apclient.hpp>
#include "usb2snes.hpp"
#include <apuuid.hpp>
#include <stdio.h>
#include <inttypes.h>
#include <unistd.h>
#include <algorithm>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <emscripten/html5.h>
#include <emscripten/bind.h>
#else
#define EM_BOOL bool
#define EM_TRUE true
#define EM_FALSE false
#if !defined WIN32 && !defined _WIN32
#include <poll.h>
#endif
#endif
#include <math.h>
#include <limits>

#if defined(WIN32) && !defined(PRId64 )
#define PRId64 "I64d"
#endif

#include GAME_H

#define VERSION_TUPLE {0,3,9}


/* This is the entry point for our cpp ap client.
 *
 * Short program flow explanation:
 *   it is possible to connect to AP (/connect) and Game (starting SNI/QUsb2Snes) in any order.
 *   events from one will check state of the other and act accordingly
 *   AP event handling is set in connect_ap() below
 *   Game event handling is set in create_game() below, which will be called the first time a SNES (emu) is connected
 */

/* TODO:
 * seed/slot should come from a copy in RAM (not ROM) in future to support more EMUs
 * test that changing ROM (seed/slot) always invalidates (reset()) game
 * above may not be true if the lua connector is kept running while changing ROM
 * invalid item index (> locations.size()) should generate an error and maybe set index to current index +1
 */


using nlohmann::json;


APClient* ap = nullptr;
bool ap_sync_queued = false;
USB2SNES* snes = nullptr;
Game* game = nullptr;
std::string password;
bool ap_connect_sent = false; // TODO: move to APClient::State ?
double deathtime = -1;
bool is_https = false; // set to true if the context only supports wss://
bool is_wss = false; // set to true if the connection specifically asked for wss://
bool is_ws = false; // set to true if the connection specifically asked for ws://
unsigned connect_error_count = 0;


#if __cplusplus < 201500L
decltype(APClient::DEFAULT_URI) constexpr APClient::DEFAULT_URI;  // c++14 needs a proper declaration
decltype(USB2SNES::DEFAULT_URI) constexpr USB2SNES::DEFAULT_URI;
decltype(USB2SNES::LEGACY_URI) constexpr USB2SNES::LEGACY_URI;
#endif

#ifdef __EMSCRIPTEN__
#define VIRTUAL_HOME_DIR "/settings"
#define OLD_DATAPACKAGE_CACHE "/settings/datapackage.json"
#define UUID_FILE "/settings/uuid"
#define CERT_STORE "" // not required in a browser context
#else
#define OLD_DATAPACKAGE_CACHE "datapackage.json"
#define UUID_FILE "uuid" // TODO: place in %appdata%
#define CERT_STORE "cacert.pem"
#endif


bool isEqual(double a, double b)
{
    return fabs(a - b) < std::numeric_limits<double>::epsilon() * fmax(fabs(a), fabs(b));
}


void set_status_color(const std::string& field, const std::string& color)
{

#ifdef __EMSCRIPTEN__
    EM_ASM({
        document.getElementById(UTF8ToString($0)).style.color = UTF8ToString($1);
    }, field.c_str(), color.c_str());
#else
#endif
}

void bad_seed(const std::string& apSeed, const std::string& gameSeed)
{
    printf("AP seed \"%s\" does not match game seed \"%s\". Did you connect to the wrong server?\n",
        apSeed.c_str(), gameSeed.c_str());
}

void bad_slot(const std::string& gameSlot)
{
    printf("Game slot \"%s\" invalid. Did you connect to the wrong server?\n",
        gameSlot.c_str());
}

void disconnect_ap()
{
    if (ap) delete ap;
    ap = nullptr;
    set_status_color("ap", "#777777");
}

void connect_ap(std::string uri="")
{
    // read or generate uuid, required by AP
    std::string uuid = ap_get_uuid(UUID_FILE);

    if (ap) delete ap;
    ap = nullptr;

    is_ws = uri.rfind("ws://", 0) == 0;
    is_wss = uri.rfind("wss://", 0) == 0;

    #ifdef __EMSCRIPTEN__
    if (is_https && is_ws) {
        EM_ASM({
            throw 'WS not supported';
        });
    } else if (is_https && !is_wss) {
        uri = "wss://" + uri; // only wss supported
    }
    #endif

    printf("Connecting to AP...\n");
    ap = new APClient(uuid, GAME::Name, uri.empty() ? APClient::DEFAULT_URI : uri, CERT_STORE);

    // clear game's cache. read below on socket_connected_handler
    if (game) game->clear_cache();

    // load DataPackage cache
    try {
        ap->set_data_package_from_file(OLD_DATAPACKAGE_CACHE);
    } catch (std::exception) { /* ignore */ }

    // set state and callbacks
    ap_sync_queued = false;
    connect_error_count = 0;
    set_status_color("ap", "#ff0000");
    ap->set_socket_connected_handler([](){
        // if the socket (re)connects we actually don't know the server's state. clear game's cache to not desync
        // TODO: in future set game's location cache from AP's checked_locations instead
        if (game) game->clear_cache();
        set_status_color("ap", "#ffff00");
    });
    ap->set_socket_disconnected_handler([](){
        set_status_color("ap", "#ff0000");
    });
    ap->set_socket_error_handler([](const std::string& error) {
        connect_error_count++;
        #ifdef __EMSCRIPTEN__
        set_status_color("ap", "#ff0000");
        if (is_https && !is_wss) {
            if (connect_error_count == 2) {
                printf("Error: could not connect to AP server! "
                       "Please check if the room is active and the port is correct.\n"
                       "You have to use the http:// version of ap-soeclient for non-SSL rooms.\n");
            } else if (connect_error_count > 2) {
                disconnect_ap();
            }
        }
        #else
        if (!error.empty() && error != "Unknown")
            printf("%s\n", error.c_str());
        #endif
    });
    ap->set_room_info_handler([](){
        // compare seeds and error out if it's the wrong one, and then (try to) connect with games's slot
        if (!game || game->get_seed().empty() || game->get_slot().empty())
            printf("Waiting for game ...\n");
        else if (strncmp(game->get_seed().c_str(), ap->get_seed().c_str(), GAME::MAX_SEED_LENGTH) != 0)
            bad_seed(ap->get_seed(), game->get_seed());
        else {
            std::list<std::string> tags;
            if (game->want_deathlink()) tags.push_back("DeathLink");
            ap->ConnectSlot(game->get_slot(), password, game->get_items_handling(), tags, VERSION_TUPLE);
            ap_connect_sent = true; // TODO: move to APClient::State ?
        }
    });
    ap->set_slot_connected_handler([](const json&){
        game->set_deathlink(game->want_deathlink());
        set_status_color("ap", "#00ff00");
    });
    ap->set_slot_disconnected_handler([](){
        set_status_color("ap", "#ffff00");
        ap_connect_sent = false;
    });
    ap->set_slot_refused_handler([](const std::list<std::string>& errors){
        set_status_color("ap", "#ffff00");
        ap_connect_sent = false;
        if (std::find(errors.begin(), errors.end(), "InvalidSlot") != errors.end()) {
            bad_slot(game?game->get_slot():"");
        } else {
            printf("AP: Connection refused:");
            for (const auto& error: errors) printf(" %s", error.c_str());
            printf("\n");
        }
    });
    ap->set_items_received_handler([](const std::list<APClient::NetworkItem>& items) {
        if (!ap->is_data_package_valid()) {
            // NOTE: this should not happen since we ask for data package before connecting
            if (!ap_sync_queued) ap->Sync();
            ap_sync_queued = true;
            return;
        }
        for (const auto& item: items) {
            std::string itemname = ap->get_item_name(item.item);
            std::string sender = ap->get_player_alias(item.player);
            std::string location = ap->get_location_name(item.location);
            printf("  #%d: %s (%" PRId64 ") from %s - %s\n",
                   item.index, itemname.c_str(), item.item,
                   sender.c_str(), location.c_str());
            game->send_item(item.index, item.item, sender, location);
        }
    });
    ap->set_data_package_changed_handler([](const json& data) {
        #ifdef __EMSCRIPTEN__
        EM_ASM(
            FS.syncfs(function (err) {});
        );
        #endif
    });
    ap->set_print_handler([](const std::string& msg) {
        printf("%s\n", msg.c_str());
    });
    ap->set_print_json_handler([](const std::list<APClient::TextNode>& msg) {
        printf("%s\n", ap->render_json(msg, APClient::RenderFormat::ANSI).c_str());
    });
    ap->set_bounced_handler([](const json& cmd) {
        if (game->want_deathlink()) {
            auto tagsIt = cmd.find("tags");
            auto dataIt = cmd.find("data");
            if (tagsIt != cmd.end() && tagsIt->is_array()
                    && std::find(tagsIt->begin(), tagsIt->end(), "DeathLink") != tagsIt->end())
            {
                if (dataIt != cmd.end() && dataIt->is_object()) {
                    json data = *dataIt;
                    printf("Received deathlink...\n");
                    if (data["time"].is_number() && isEqual(data["time"].get<double>(), deathtime)) {
                        deathtime = -1;
                    } else if (game) {
                        game->send_death();
                        printf("Died by the hands of %s: %s\n",
                            data["source"].is_string() ? data["source"].get<std::string>().c_str() : "???",
                            data["cause"].is_string() ? data["cause"].get<std::string>().c_str() : "???");
                    }
                } else {
                    printf("Bad deathlink packet!\n");
                }
            }
        }
    });
}

void create_game()
{
    if (game) delete game;
    game = nullptr;
    
    printf("Instantiating \"%s\" game...\n", GAME::Name);
    game = new GAME(snes);
    set_status_color("game", "#ff0000");
    game->set_game_started_handler([]() {
        game->clear_cache(); // is this good enough?
        set_status_color("game", "#ffff00");
        if (ap && ap->get_state() > APClient::State::ROOM_INFO) {
            // compare seed & slot and disconnect if they do not match
            if (!game->get_seed().empty() &&
                    strncmp(game->get_seed().c_str(), ap->get_seed().c_str(), GAME::MAX_SEED_LENGTH) != 0)
            {
                bad_seed(ap->get_seed(), game->get_seed());
                ap->reset();
                return;
            }
            else if (game->get_slot() != ap->get_slot())
            {
                printf("Slot changed, disconnecting.\n");
                ap->reset();
                return;
            }
            else if (game->get_deathlink() != game->want_deathlink()) {
                std::list<std::string> tags;
                game->set_deathlink(game->want_deathlink());
                if (game->get_deathlink()) tags.push_back("DeathLink");
                ap->ConnectUpdate(false, 0, true, {"DeathLink"});
            }
        }
        if (ap && ap->get_state() == APClient::State::ROOM_INFO) {
            if (!game->get_seed().empty() &&
                    strncmp(game->get_seed().c_str(), ap->get_seed().c_str(), GAME::MAX_SEED_LENGTH) != 0)
            {
                bad_seed(ap->get_seed(), game->get_seed());
                return;
            }
            else
            {
                std::list<std::string> tags;
                game->set_deathlink(game->want_deathlink());
                if (game->get_deathlink()) tags.push_back("DeathLink");
                ap->ConnectSlot(game->get_slot(), password, game->get_items_handling(), tags, VERSION_TUPLE);
                ap_connect_sent = true; // TODO: move to APClient::State ?
            }
        }
    });
    game->set_game_stopped_handler([]() {
        set_status_color("game", "#ff0000");
    });
    game->set_game_joined_handler([]() {
        set_status_color("game", "#00ff00");
        // TODO: cache in Game instead of ap->sync()?
        if (ap && ap->get_state() == APClient::State::SLOT_CONNECTED) {
            ap->Sync();
        }
    });
    game->set_game_left_handler([]() {
        set_status_color("game", "#ffff00");
    });
    game->set_game_finished_handler([]() {
        set_status_color("game", "#00ff00");
        printf("Game finished!\n");
        if (ap) ap->StatusUpdate(APClient::ClientStatus::GOAL);
    });
    game->set_locations_checked_handler([](std::list<int64_t> locations) {
        if (ap) ap->LocationChecks(locations);
    });
    game->set_locations_scouted_handler([](std::list<int64_t> locations) {
        if (ap) ap->LocationScouts(locations);
    });
    game->set_death_handler([]() {
        if (!ap) return;
        deathtime = ap->get_server_time();
        json data{
            {"time", deathtime},
            {"cause", "Evermore."},
            {"source", ap->get_slot()},
        };
        ap->Bounce(data, {}, {}, {"DeathLink"});
    });
}

bool read_command(std::string& cmd)
{
#if defined __EMSCRIPTEN__
    return false;
#elif defined WIN32 || defined _WIN32
    static std::string cmd_buf;
    HANDLE hStdin = GetStdHandle(STD_INPUT_HANDLE);
    DWORD n = 0;
    INPUT_RECORD rec;
    if (!PeekConsoleInputA(hStdin, &rec, 1, &n) || n < 1) return false;
    if (rec.EventType != KEY_EVENT) {
        // drop non-key-event
        ReadConsoleInput(hStdin, &rec, 1, &n);
        return false;
    }
    // read unicode from console buffer
    WCHAR wBuf[256];
    wBuf[255] = 0;
    n = 255;
    if (!ReadConsoleW(hStdin, wBuf, n, &n, NULL)) {
        return false;
    }
    // convert to utf8
    int mbSize = WideCharToMultiByte(CP_UTF8, 0, wBuf, n, NULL, 0, NULL, NULL);
    if (mbSize == 0) {
        return false;
    }
    char *mbBuf = new char[mbSize+1];
    mbBuf[mbSize] = 0;
    WideCharToMultiByte(CP_UTF8, 0, wBuf, n, mbBuf, mbSize, NULL, NULL);
    cmd_buf += mbBuf;
    delete[] mbBuf;
    while (!cmd_buf.empty() && (cmd_buf[0] == 0x08 || cmd_buf[0] == '\r' || cmd_buf[0] == '\n' || cmd_buf[0] == 127))
        cmd_buf = cmd_buf.substr(1);
    for (size_t i=1; i<cmd_buf.length(); i++) {
        if (cmd_buf[i] != 0x08) continue;
        // backspace
        if ((cmd_buf[i-1] & 0b10000000) == 0) {
            cmd_buf.erase(i-1, 2);
            i--;
        } else if ((cmd_buf[i-2] & 0b11100000) == 0b11000000) {
            cmd_buf.erase(i-2, 3);
            i-=2;
        } else if ((cmd_buf[i-3] & 0b11110000) == 0b11100000) {
            cmd_buf.erase(i-3, 4);
            i-=3;
        } else if ((cmd_buf[i-4] & 0b11111000) == 0b11110000) {
            cmd_buf.erase(i-4, 5);
            i-=4;
        }
    }
    WCHAR *wBuf2 = new WCHAR[cmd_buf.length() + 1];
    wBuf2[0] = 0;
    MultiByteToWideChar(CP_UTF8, 0, cmd_buf.c_str(), cmd_buf.length() + 1, wBuf2, cmd_buf.length() + 1);
    wprintf(L"\r                                        \r%ls", wBuf2);
    delete[] wBuf2;
    size_t p = cmd_buf.find('\r');
    if (p != cmd_buf.npos) {
        cmd = cmd_buf.substr(0, p);
        cmd_buf = cmd_buf.substr(p+1);
        printf("\n");
    }
    while (!cmd.empty() && (cmd.back() == '\n' || cmd.back() == '\r')) cmd.pop_back();
    return !cmd.empty();
#else
    struct pollfd fd = { STDIN_FILENO, POLLIN, 0 };
    int res = poll(&fd, 1, 5);
    if (res && fd.revents) {
        cmd.resize(1024);
        if (fgets((char*)cmd.data(), cmd.size(), stdin)) {
            cmd.resize(strlen(cmd.data()));
            while (!cmd.empty() && (cmd.back() == '\n' || cmd.back() == '\r')) cmd.pop_back();
            return !cmd.empty();
        }
    }
    return false;
#endif
}

void on_command(const std::string& command)
{
    if (command == "/help") {
        printf("Available commands:\n"
               "  /connect [addr[:port]] - connect to AP server\n"
               "  /disconnect - disconnect from AP server\n"
               "  /force-send - send missing items to game, ignoring locks\n"
               "  /force-resend - resend all items to game\n"
               "  /sync - resync items/locations with AP server\n");
    } else if (command == "/connect") {
        connect_ap();
    } else if (command.find("/connect ") == 0) {
        connect_ap(command.substr(9));
    } else if (command == "/disconnect") {
        disconnect_ap();
    } else if (command == "/sync") {
        if (game) game->clear_cache();
        if (ap) ap->Sync();
    } else if (command == "/force-send") {
        if (!game) printf("Can't force-send if game is not running.\n");
        else if (!game->force_send()) printf("Game does not support force-send.\n");
    } else if (command == "/force-resend") {
        if (!game) printf("Can't force-resend if game is not running.\n");
        else if (!game->force_resend()) printf("Game does not support force-resend.\n");
    } else if (command.find("/") == 0) {
        printf("Unknown command: %s\n", command.c_str());
    } else if (!ap || ap->get_state() < APClient::State::SOCKET_CONNECTED) {
        printf("AP not connected. Can't send chat message.\n");
        if (command.length() >= 2 && command[1] == '/') {
            printf("Did you mean \"%s\"?\n", command.c_str()+1);
        } else if (command.substr(0, 7) == "connect") {
            auto p = command[7] ? 7 : command.npos;
            while (p != command.npos && command[p] == ' ') p++;
            printf("Did you mean \"/connect%s%s\"?\n",
                    p!=command.npos ? " " : "", p!=command.npos ? command.substr(p).c_str() : "");
        }
    } else {
        ap->Say(command);
    }
}

EM_BOOL step(double time, void* userData)
{
    // we run code that acts on elapsed time in the main loop
    // TODO: use async timers (for JS) instead and get rid of step() alltogether
    if (snes) snes->poll();
    if (ap) ap->poll();
    if (game) game->poll();

    // parse stdin
    std::string cmd;
    if (read_command(cmd)) {
        on_command(cmd);
    }

    return EM_TRUE;
}
EM_BOOL interval_step(double time)
{
    return step(time, nullptr);
}

void start()
{
#ifndef __EMSCRIPTEN__ // HTML GUI has its own log
    // TODO: create log and redirect stdout
#endif
    // read or generate uuid, required by AP
    printf("UUID: %s\n", ap_get_uuid(UUID_FILE).c_str());

    printf("Connecting to SNES...\n");
    set_status_color("snes", "#ff0000");
    snes = new USB2SNES();
    snes->set_socket_connected_handler([](){
        set_status_color("snes", "#ffff00");
    });
    snes->set_socket_disconnected_handler([](){
        set_status_color("snes", "#ff0000");
        // NOTE: we don't destroy game here since we can't cancel pending SNES callbacks (yet)
        if (game) game->reset();
        set_status_color("game", "#777777");
    });
    snes->set_snes_connected_handler([](){
        set_status_color("snes", "#00ff00");
        if (!game) create_game();
    });
    snes->set_snes_disconnected_handler([](){
        set_status_color("snes", "#ffff00");
        // NOTE: we don't destroy game here since we can't cancel pending SNES callbacks (yet)
        if (game) game->reset();
        set_status_color("game", "#777777");
    });

    printf("Running mainloop...\n");
    printf("use /connect [<host>] to connect to an AP server\n");
#ifdef __EMSCRIPTEN__
    // auto-connect to ap server given by #server=...
    EM_ASM({
        // TODO: use argv and set connect_ap instead?
        if (Module.apServer) Module.on_command('/connect '+Module.apServer);
    });
    //emscripten_request_animation_frame_loop(step, 0);
    EM_ASM({
        setInterval(function(){Module.step(0);}, 100);
    });
#else
    while (step(0, nullptr));
#endif
}

int main(int argc, char** argv)
{
#ifdef __EMSCRIPTEN__
    is_https = EM_ASM_INT({
        return (document.location.protocol == 'https:');
    });
#endif
#if defined WIN32 || defined _WIN32
    DWORD mode = 0;
    if (GetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), &mode)) {
        mode &= ~(ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT | ENABLE_MOUSE_INPUT);
        SetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), mode);
    }
#endif
#ifdef VIRTUAL_HOME_DIR
    // override home. used to default to persistent storage with emscripten
    setenv("HOME", VIRTUAL_HOME_DIR, true);
#endif
#ifdef USE_IDBFS
    // mount persistant storage, then run app
    EM_ASM({
        FS.mkdir('/settings');
        FS.mount(IDBFS, {}, '/settings');
        FS.syncfs(true, function(err) {
            Module.start()
        });
    });
#else
    start();
#endif

    return 0;
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_BINDINGS(main) {
    emscripten::function("start", &start);
    emscripten::function("step", &interval_step);
    emscripten::function("on_command", &on_command); // TODO: use stdin instead?
}
#endif
