#include "wsjs.hpp"
#include "usb2snes.hpp"
#include "apclient.hpp"
#include <stdio.h>
#include <unistd.h>
#include <algorithm>
#include <emscripten.h>
#include <emscripten/html5.h>
#include <emscripten/bind.h>
#include "uuid.h"


#include GAME_H


/* This is the entry point for our cpp ap client.
 *
 * Short program flow explanation:
 *   it is possible to connect to AP (/connect) and Game (starting SNI/QUsb2Snes) in any order.
 *   events from one will check state of the other and act accordingly
 *   AP event handling is set in connect_ap() below
 *   Game event handling is set in create_game() below, which will be called the first time a SNES (emu) is connected
 */

/* ABSOLUTELY REQUIRED TODO:
 * implement status update in APClient
 * test snex9x-rr + lua bridge + SNI <- SNI bug reading wrong address
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


void set_status_color(const std::string& field, const std::string& color)
{
    EM_ASM({
        document.getElementById(UTF8ToString($0)).style.color = UTF8ToString($1);
    }, field.c_str(), color.c_str());
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

EM_BOOL step(double time, void* userData)
{
    // we run code that acts on elapsed time in the main loop
    // TODO: use async timers instead
    if (snes) snes->poll();
    if (ap) ap->poll();
    if (game) game->poll();
    return EM_TRUE;
}

void connect_ap(std::string uri="")
{
    // read or generate uuid, required by AP
    std::string uuid = get_uuid();

    if (ap) delete ap;
    ap = nullptr;
    if (!uri.empty() && uri.find("ws://") != 0 && uri.find("wss://") != 0) uri = "ws://"+uri;

    printf("Connecting to AP...\n");
    if (uri.empty()) ap = new APClient(uuid, GAME::Name);
    else ap = new APClient(uuid, GAME::Name, uri);

    // clear game's cache. read below on socket_connected_handler
    if (game) game->clear_cache();

    // TODO: ap->load_data_package(...); // from cache
    ap_sync_queued = false;
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
    ap->set_room_info_handler([](){
        // compare seeds and error out if it's the wrong one, and then (try to) connect with games's slot
        if (!game || game->get_seed().empty() || game->get_slot().empty())
            printf("Waiting for game ...\n");
        else if (strncmp(game->get_seed().c_str(), ap->get_seed().c_str(), GAME::MAX_SEED_LENGTH) != 0)
            bad_seed(ap->get_seed(), game->get_seed());
        else
            ap->ConnectSlot(game->get_slot());
    });
    ap->set_slot_connected_handler([](){
        set_status_color("ap", "#00ff00");
    });
    ap->set_slot_disconnected_handler([](){
        set_status_color("ap", "#ffff00");
    });
    ap->set_slot_refused_handler([](const std::list<std::string>& errors){
        set_status_color("ap", "#ffff00");
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
            printf("  #%d: %s (%d) from %s - %s\n",
                   item.index, itemname.c_str(), item.item,
                   sender.c_str(), location.c_str());
            game->send_item(item.index, item.item, sender, location);
        }
    });
    ap->set_data_package_changed_handler([](const json& data) {
        // TODO: dump data to a cache
    });
    ap->set_print_handler([](const std::string& msg) {
        printf("%s\n", msg.c_str());
    });
    ap->set_print_json_handler([](const std::list<APClient::TextNode>& msg) {
        printf("%s\n", ap->render_json(msg).c_str());
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
                ap->ConnectSlot(game->get_slot());
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
    game->set_locations_checked_handler([](std::list<int> locations) {
        if (ap) ap->LocationChecks(locations);
    });
    game->set_locations_scouted_handler([](std::list<int> locations) {
        if (ap) ap->LocationScouts(locations);
    });
}

void disconnect_ap()
{
    if (ap) delete ap;
    ap = nullptr;
    set_status_color("ap", "#777777");
}

void start()
{
    // read or generate uuid, required by AP
    std::string uuid = get_uuid();
    printf("UUID: %s\n", uuid.c_str());
#if 0
    if (auto_connect_ap) {
        connect_ap(last_host);
    }
#endif

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
    emscripten_request_animation_frame_loop(step, 0);
}

void on_command(const std::string& command)
{
    if (command == "/connect") {
        connect_ap();
    } else if (command.find("/connect ") == 0) {
        connect_ap(command.substr(9));
    } else if (command == "/disconnect") {
        disconnect_ap();
    } else if (command == "/sync") {
        if (game) game->clear_cache();
        if (ap) ap->Sync();
    } else if (command.find("/") == 0) {
        printf("Unknown command: %s\n", command.c_str());
    } else if (!ap) {
        printf("AP not connected\n");
    } else {
        ap->Say(command);
    }
}

int main(int argc, char** argv)
{
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

EMSCRIPTEN_BINDINGS(main) {
    emscripten::function("start", &start);
    emscripten::function("on_command", &on_command); // TODO: use stdin instead?
}
