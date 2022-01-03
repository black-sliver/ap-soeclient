/* Copyright (c) 2021 black-sliver

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#ifndef _APCLIENT_HPP
#define _APCLIENT_HPP

#include <wswrap.hpp>
#include <string>
#include <list>
#include <set>
#include <nlohmann/json.hpp>
#include <valijson/adapters/nlohmann_json_adapter.hpp>
#include <valijson/schema.hpp>
#include <valijson/schema_parser.hpp>
#include <valijson/validator.hpp>


#define APCLIENT_DEBUG


class APClient {
protected:
    typedef nlohmann::json json;
    typedef valijson::adapters::NlohmannJsonAdapter JsonSchemaAdapter;
    typedef wswrap::WS WS;

public:
    APClient(const std::string& uuid, const std::string& game, const std::string& uri="ws://localhost:38281")
    {
        // fix up URI (add ws:// and default port if none is given)
        // TODO: move this to the front-end once we have wss:// and ws://
        //       or multiple rooms on the same port
        auto p = uri.find("://");
        if (p == uri.npos) {
            _uri = "ws://" + uri;
            p = 2;
        } else {
            _uri = uri;
        }
        auto pColon = _uri.find(":", p+3);
        auto pSlash = _uri.find("/", p+3);
        if (pColon == _uri.npos || (pSlash != _uri.npos && pColon > pSlash)) {
            auto tmp = _uri.substr(0, pSlash) + ":38281";
            if (pSlash != _uri.npos) tmp += _uri.substr(pSlash);
            _uri = tmp;
        }
        _uuid = uuid;
        _game = game;
        _dataPackage = {
            {"version", -1},
            {"games", json(json::value_t::object)},
        };
        valijson::SchemaParser parser;
        parser.populateSchema(JsonSchemaAdapter(_packetSchemaJson), _packetSchema);
        connect_socket();
    }

    virtual ~APClient()
    {
        delete _ws;
        _ws = nullptr;
    }

    enum class State {
        DISCONNECTED,
        SOCKET_CONNECTING,
        SOCKET_CONNECTED,
        ROOM_INFO,
        SLOT_CONNECTED,
    };

    enum class ClientStatus : int {
        UNKNOWN = 0,
        READY = 10,
        PLAYING = 20,
        GOAL = 30,
    };

    enum class RenderFormat {
        TEXT,
        HTML,
        ANSI,
    };

    struct NetworkItem {
        int item;
        int location;
        int player;
        int index = -1; // to sync items, not actually part of NetworkItem
    };

    struct NetworkPlayer {
        int team;
        int slot;
        std::string alias;
        std::string name;
    };

    struct TextNode {
        std::string type;
        std::string color;
        std::string text;
        bool found = false;
    };

    void set_socket_connected_handler(std::function<void(void)> f)
    {
        _hOnSocketConnected = f;
    }

    void set_socket_disconnected_handler(std::function<void(void)> f)
    {
        _hOnSocketDisconnected = f;
    }

    void set_slot_connected_handler(std::function<void(void)> f)
    {
        _hOnSlotConnected = f;
    }

    void set_slot_refused_handler(std::function<void(const std::list<std::string>&)> f)
    {
        _hOnSlotRefused = f;
    }

    void set_slot_disconnected_handler(std::function<void(void)> f)
    {
        _hOnSlotDisconnected = f;
    }

    void set_room_info_handler(std::function<void(void)> f)
    {
        _hOnRoomInfo = f;
    }

    void set_items_received_handler(std::function<void(const std::list<NetworkItem>&)> f)
    {
        _hOnItemsReceived = f;
    }

    void set_location_info_handler(std::function<void(const std::list<NetworkItem>&)> f)
    {
        _hOnLocationInfo = f;
    }

    void set_data_package_changed_handler(std::function<void(const json&)> f)
    {
        _hOnDataPackageChanged = f;
    }

    void set_print_handler(std::function<void(const std::string&)> f)
    {
        _hOnPrint = f;
    }

    void set_print_json_handler(std::function<void(const std::list<TextNode>&)> f)
    {
        _hOnPrintJson = f;
    }

    void set_bounced_handler(std::function<void(const json&)> f)
    {
        _hOnBounced = f;
    }

    void set_data_package(const json& data)
    {
        // only apply from cache if not updated and it looks valid
        if (!_dataPackageValid && data.find("games") != data.end()) {
            _dataPackage = data;
            for (auto gamepair: _dataPackage["games"].items()) {
                const auto& gamedata = gamepair.value();
                _dataPackage["games"][gamepair.key()] = gamedata;
                for (auto pair: gamedata["item_name_to_id"].items()) {
                    _items[pair.value().get<int>()] = pair.key();
                }
                for (auto pair: gamedata["location_name_to_id"].items()) {
                    _locations[pair.value().get<int>()] = pair.key();
                }
            }
        }
    }

    std::string get_player_alias(int slot)
    {
        if (slot == 0) return "Server";
        for (const auto& player: _players) {
            if (player.team == _team && player.slot == slot) {
                return player.alias;
            }
        }
        return "Unknown";
    }

    std::string get_location_name(int code)
    {
        auto it = _locations.find(code);
        if (it != _locations.end()) return it->second;
        return "Unknown";
    }

    std::string get_item_name(int code)
    {
        auto it = _items.find(code);
        if (it != _items.end()) return it->second;
        return "Unknown";
    }

    std::string render_json(const std::list<TextNode>& msg, RenderFormat fmt=RenderFormat::TEXT)
    {
        // TODO: implement RenderFormat::HTML
        if (fmt == RenderFormat::HTML)
            throw std::invalid_argument("AP::render_json(..., HTML) not implemented");
        std::string out;
        bool colorIsSet = false;
        for (const auto& node: msg) {
            std::string color;
            std::string text;
            if (fmt != RenderFormat::TEXT) color = node.color;
            if (node.type == "player_id") {
                int id = std::stoi(node.text);
                if (color.empty() && id == _slotnr) color = "magenta";
                else if (color.empty()) color = "yellow";
                text = get_player_alias(id);
            } else if (node.type == "item_id") {
                int id = std::stoi(node.text);
                if (color.empty() && node.found) color = "green";
                else if (color.empty()) color = "cyan";
                text = get_item_name(id);
            } else if (node.type == "location_id") {
                int id = std::stoi(node.text);
                if (color.empty()) color = "blue";
                text = get_location_name(id);
            } else {
                text = node.text;
            }
            if (fmt == RenderFormat::ANSI) {
                if (color.empty() && colorIsSet) {
                    out += color2ansi(""); // reset color
                    colorIsSet = false;
                }
                else if (!color.empty()) {
                    out += color2ansi(color);
                    colorIsSet = true;
                }
                deansify(text);
                out += text;
            } else {
                out += text;
            }
        }
        if (fmt == RenderFormat::ANSI && colorIsSet) out += color2ansi("");
        return out;
    }

    bool LocationChecks(std::list<int> locations)
    {
        // returns true if checks were sent or queued
        if (_state == State::SLOT_CONNECTED) {
            auto packet = json{{
                {"cmd", "LocationChecks"},
                {"locations", locations},
            }};
            debug("> " + packet[0]["cmd"].get<std::string>() + ": " + packet.dump());
            _ws->send(packet.dump());
        } else {
            _checkQueue.insert(locations.begin(), locations.end());
            // FIXME: this needs to be sent at some point
        }
        return true;
    }

    bool LocationScouts(std::list<int> locations)
    {
        // returns true if scouts were sent or queued
        if (_state == State::SLOT_CONNECTED) {
            auto packet = json{{
                {"cmd", "LocationScouts"},
                {"locations", locations},
            }};
            debug("> " + packet[0]["cmd"].get<std::string>() + ": " + packet.dump());
            _ws->send(packet.dump());
        } else {
            _scoutQueue.insert(locations.begin(), locations.end());
            // FIXME: this needs to be sent at some point
        }
        return true;
    }

    bool StatusUpdate(ClientStatus status)
    {
        // returns true if status update was sent or queued
        if (_state == State::SLOT_CONNECTED) {
            auto packet = json{{
                {"cmd", "StatusUpdate"},
                {"status", status},
            }};
            debug("> " + packet[0]["cmd"].get<std::string>() + ": " + packet.dump());
            _ws->send(packet.dump());
            return true;
        }
        _clientStatus = status;
        return false;
    }

    bool ConnectSlot(const std::string& name, const std::string& password="", int ver_ma=0, int ver_mi=2, int ver_build=0)
    {
        if (_state < State::SOCKET_CONNECTED) return false;
        _slot = name;
        debug("Connecting slot...");
        auto packet = json{{
            {"cmd", "Connect"},
            {"game", _game},
            {"uuid", _uuid},
            {"name", name},
            {"password", password},
            {"version", {{"major", ver_ma}, {"minor", ver_mi}, {"build", ver_build}, {"class", "Version"}}},
            {"tags", json::array()},
        }};
        debug("> " + packet[0]["cmd"].get<std::string>() + ": " + packet.dump());
        _ws->send(packet.dump());
        return true;
    }

    bool Sync()
    {
        if (_state < State::SLOT_CONNECTED) return false;
        auto packet = json{{
            {"cmd", "Sync"},
        }};
        debug("> " + packet[0]["cmd"].get<std::string>() + ": " + packet.dump());
        _ws->send(packet.dump());
        return true;
    }

    bool GetDataPackage(const std::list<std::string>& exclude = {})
    {
        if (_state < State::ROOM_INFO) return false;
        auto packet = json{{
            {"cmd", "GetDataPackage"},
            {"exclusions", exclude},
        }};
        debug("> " + packet[0]["cmd"].get<std::string>() + ": " + packet.dump());
        _ws->send(packet.dump());
        return true;
    }
    
    bool Bounce(const json& data, std::list<std::string> games = {},
                std::list<int> slots = {}, std::list<std::string> tags = {})
    {
        if (_state < State::ROOM_INFO) return false; // or SLOT_CONNECTED?
        auto packet = json{{
            {"cmd", "Bounce"},
            {"data", data},
        }};
        if (!games.empty()) packet["games"] = games;
        if (!slots.empty()) packet["slots"] = slots;
        if (!tags.empty()) packet["tags"] = tags;
#ifdef APCLIENT_DEBUG
        const size_t maxDumpLen = 512;
        auto dump = packet[0].dump().substr(0, maxDumpLen);
        if (dump.size() > maxDumpLen-3) dump = dump.substr(0, maxDumpLen-3) + "...";
        debug("> " + packet[0]["cmd"].get<std::string>() + ": " + dump);
#endif
        _ws->send(packet.dump());
        return true;
    }
    
    bool Say(const std::string& text)
    {
        if (_state < State::ROOM_INFO) return false; // or SLOT_CONNECTED?
        auto packet = json{{
            {"cmd", "Say"},
            {"text", text},
        }};
        debug("> " + packet[0]["cmd"].get<std::string>() + ": " + packet.dump());
        _ws->send(packet.dump());
        return true;
    }

    State get_state() const
    {
        return _state;
    }

    const std::string& get_seed() const
    {
        return _seed;
    }

    const std::string& get_slot() const
    {
        return _slot;
    }

    bool is_data_package_valid() const
    {
        // returns true if cached texts are valid
        // if not, get_location_name() and get_item_name() will return "Unknown"
        return _dataPackageValid;
    }

    void poll()
    {
        if (_ws) _ws->poll();
        if (_state < State::SOCKET_CONNECTED) {
            auto t = now();
            if (t - _lastSocketConnect > _socketReconnectInterval) {
                if (_state != State::DISCONNECTED)
                    log("Connect timed out. Retrying.");
                else
                    log("Reconnecting to server");
                connect_socket();
            }
        }
    }

    void reset()
    {
        _checkQueue.clear();
        _scoutQueue.clear();
        _clientStatus = ClientStatus::UNKNOWN;
        _seed.clear();
        _slot.clear();
        _team = -1;
        _slotnr = -1;
        _players.clear();
        delete _ws;
        _ws = nullptr;
        _state = State::DISCONNECTED;
    }

private:
    void log(const char* msg)
    {
        printf("APClient: %s\n", msg);
    }
    void log(const std::string& msg)
    {
        log(msg.c_str());
    }
    void debug(const char* msg)
    {
#ifdef APCLIENT_DEBUG
        log(msg);
#else
        (void)msg;
#endif
    }
    void debug(const std::string& msg)
    {
        debug(msg.c_str());
    }

    void onopen()
    {
        debug("onopen()");
        log("Server connected");
        _state = State::SOCKET_CONNECTED;
        if (_hOnSocketConnected) _hOnSocketConnected();
        _socketReconnectInterval = 1500;
    }

    void onclose()
    {
        debug("onclose()");
        if (_state > State::SOCKET_CONNECTING) {
            log("Server disconnected");
            _state = State::DISCONNECTED;
            if (_hOnSocketDisconnected) _hOnSocketDisconnected();
        }
        _state = State::DISCONNECTED;
        delete _ws;
        _ws = nullptr;
        _seed = "";
    }

    void onmessage(const std::string& s)
    {
        try {
            json packet = json::parse(s);
            valijson::Validator validator;
            JsonSchemaAdapter packetAdapter(packet);
            if (!validator.validate(_packetSchema, packetAdapter, nullptr)) {
                throw std::runtime_error("Packet validation failed");
            }
            for (auto& command: packet) {
                std::string cmd = command["cmd"];
                JsonSchemaAdapter commandAdapter(command);
#ifdef APCLIENT_DEBUG
                const size_t maxDumpLen = 512;
                auto dump = command.dump().substr(0, maxDumpLen);
                if (dump.size() > maxDumpLen-3) dump = dump.substr(0, maxDumpLen-3) + "...";
                debug("< " + cmd + ": " + dump);
#endif
                // TODO: validate command schema to get a useful error message
                if (cmd == "RoomInfo") {
                    _seed = command["seed_name"];
                    if (_state < State::ROOM_INFO) _state = State::ROOM_INFO;
                    if (_hOnRoomInfo) _hOnRoomInfo();
                    
                    // check if cached data package is already valid
                    // we are nice and check and query individual games
                    _dataPackageValid = true;
                    std::list<std::string> exclude;
                    for (auto itV: command["datapackage_versions"].items()) {
                        if (!itV.value().is_number()) continue;
                        int v = itV.value().get<int>();
                        if (v < 1) {
                            // 0 means don't cache
                            _dataPackageValid = false;
                            continue;
                        }
                        auto itDp = _dataPackage["games"].find(itV.key());
                        if (itDp == _dataPackage["games"].end()) {
                            // new game
                            _dataPackageValid = false;
                            continue;
                        }
                        if ((*itDp)["version"] != v) {
                            // different version
                            _dataPackageValid = false;
                            continue;
                        }
                        // ok, cache valid
                        exclude.push_back(itV.key());
                    }
                    if (!_dataPackageValid) GetDataPackage(exclude);
                    else debug("DataPackage up to date");
                }
                else if (cmd == "ConnectionRefused") {
                    if (_hOnSlotRefused) {
                        std::list<std::string> errors;
                        for (const auto& error: command["errors"])
                            errors.push_back(error);
                        _hOnSlotRefused(errors);
                    }
                }
                else if (cmd == "Connected") {
                    _state = State::SLOT_CONNECTED;
                    if (_hOnSlotConnected) _hOnSlotConnected();
                    _team = command["team"];
                    _slotnr = command["slot"];
                    _players.clear();
                    for (auto& player: command["players"]) {
                        _players.push_back({
                            player["team"].get<int>(),
                            player["slot"].get<int>(),
                            player["alias"].get<std::string>(),
                            player["name"].get<std::string>(),
                        });
                    }
                }
                else if (cmd == "ReceivedItems") {
                    std::list<NetworkItem> items;
                    int index = command["index"].get<int>();
                    for (const auto& item: command["items"]) {
                        items.push_back({
                            item["item"].get<int>(),
                            item["location"].get<int>(),
                            item["player"].get<int>(),
                            index++,
                        });
                    }
                    if (_hOnItemsReceived) _hOnItemsReceived(items);
                }
                else if (cmd == "LocationInfo") {
                    std::list<NetworkItem> items;
                    for (const auto& item: command["locations"]) {
                        items.push_back({
                            item["item"].get<int>(),
                            item["location"].get<int>(),
                            item["player"].get<int>(),
                        });
                    }
                    if (_hOnLocationInfo) _hOnLocationInfo(items);
                }
                else if (cmd == "RoomUpdate") {
                    // ignored at the moment
                }
                else if (cmd == "DataPackage") {
                    auto data = _dataPackage;
                    if (!data["games"].is_object())
                        data["games"] = json(json::value_t::object);
                    for (auto gamepair: command["data"]["games"].items())
                        data["games"][gamepair.key()] = gamepair.value();
                    data["version"] = command["data"]["version"];
                    _dataPackageValid = false;
                    set_data_package(data);
                    _dataPackageValid = true;
                    if (_hOnDataPackageChanged) _hOnDataPackageChanged(_dataPackage);
                }
                else if (cmd == "Print") {
                    if (_hOnPrint) _hOnPrint(command["text"].get<std::string>());
                }
                else if (cmd == "PrintJSON") {
                    std::list<TextNode> msg;
                    for (const auto& part: command["data"]) {
                        auto itType = part.find("type");
                        auto itColor = part.find("color");
                        auto itText = part.find("text");
                        auto itFound = part.find("found");
                        msg.push_back({
                            itType == part.end() ? "" : itType->get<std::string>(),
                            itColor == part.end() ? "" : itColor->get<std::string>(),
                            itText == part.end() ? "" : itText->get<std::string>(),
                            itFound == part.end() ? false : itFound->get<bool>(),
                        });
                    }
                    if (_hOnPrintJson) _hOnPrintJson(msg);
                }
                else if (cmd == "Bounced") {
                    if (_hOnBounced) _hOnBounced(command["data"]);
                }
                else {
                    debug("unhandled cmd");
                }
            }
        } catch (std::exception& ex) {
            log((std::string("onmessage() error: ") + ex.what()).c_str());
        }
    }

    void onerror()
    {
        debug("onerror()");
    }

    void connect_socket()
    {
        delete _ws;
        if (_uri.empty()) {
            _ws = nullptr;
            _state = State::DISCONNECTED;
            return;
        }
        _state = State::SOCKET_CONNECTING;
        _ws = new WS(_uri,
                [this]() { onopen(); },
                [this]() { onclose(); },
                [this](const std::string& s) { onmessage(s); },
                [this]() { onerror(); }
        );
        _lastSocketConnect = now();
        _socketReconnectInterval *= 2;
        // NOTE: browsers have a very badly implemented connection rate limit
        // alternatively we could always wait for onclose() to get the actual
        // allowed rate once we are over it
        unsigned long maxReconnectInterval = std::max(15000UL, _ws->get_ok_connect_interval());
        if (_socketReconnectInterval > maxReconnectInterval) _socketReconnectInterval = maxReconnectInterval;
    }

    std::string color2ansi(const std::string& color)
    {
        // convert color to ansi color command
        if (color == "red") return "\x1b[31m";
        if (color == "green") return "\x1b[32m";
        if (color == "yellow") return "\x1b[33m";
        if (color == "blue") return "\x1b[34m";
        if (color == "magenta") return "\x1b[35m";
        if (color == "cyan") return "\x1b[36m";
        return "\x1b[0m";
    }

    void deansify(std::string& text)
    {
        // disable ansi commands in text by replacing ESC by space
        std::replace(text.begin(), text.end(), '\x1b', ' ');
    }

    static unsigned long now()
    {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        unsigned long ms = (unsigned long)ts.tv_sec * 1000;
        ms += (unsigned long)ts.tv_nsec / 1000000;
        return ms;
    }

    std::string _uri;
    std::string _game;
    std::string _uuid;
    WS* _ws = nullptr;
    State _state = State::DISCONNECTED;

    std::function<void(void)> _hOnSocketConnected = nullptr;
    std::function<void(void)> _hOnSocketDisconnected = nullptr;
    std::function<void(void)> _hOnSlotConnected = nullptr;
    std::function<void(void)> _hOnSlotDisconnected = nullptr;
    std::function<void(const std::list<std::string>&)> _hOnSlotRefused = nullptr;
    std::function<void(void)> _hOnRoomInfo = nullptr;
    std::function<void(const std::list<NetworkItem>&)> _hOnItemsReceived = nullptr;
    std::function<void(const std::list<NetworkItem>&)> _hOnLocationInfo = nullptr;
    std::function<void(const json&)> _hOnDataPackageChanged = nullptr;
    std::function<void(const std::string&)> _hOnPrint = nullptr;
    std::function<void(const std::list<TextNode>&)> _hOnPrintJson = nullptr;
    std::function<void(const json&)> _hOnBounced = nullptr;

    unsigned long _lastSocketConnect;
    unsigned long _socketReconnectInterval = 1500;
    std::set<int> _checkQueue;
    std::set<int> _scoutQueue;
    ClientStatus _clientStatus = ClientStatus::UNKNOWN;
    std::string _seed;
    std::string _slot; // currently connected slot, if any
    int _team = -1;
    int _slotnr = -1;
    std::list<NetworkPlayer> _players;
    std::map<int, std::string> _locations;
    std::map<int, std::string> _items;
    bool _dataPackageValid = false;
    json _dataPackage;

    const json _packetSchemaJson = R"({
        "type": "array",
        "items": {
            "type": "object",
            "properties": {
                "cmd": { "type": "string" }
            },
            "required": [ "cmd" ]
        }
    })"_json;
    valijson::Schema _packetSchema;
};

#endif // _APCLIENT_HPP
