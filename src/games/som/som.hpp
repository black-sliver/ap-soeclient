#pragma once

#include <cinttypes>
#include <cstdint>
#include <cstring>
#include <map>
#include "../game.hpp"
#include "../../usb2snes.hpp"

#ifdef GAME
#error multiple games defined
#endif
#define GAME SoM


class SoM final : public Game
{
public:
    static constexpr auto Name = "Secret of Mana";
    static constexpr int MAX_SEED_LENGTH = 32;

    explicit SoM(USB2SNES* snes)
        : Game(snes)
    {
    }

    ~SoM() override = default;

protected:
    enum class Goal : uint8_t {
        Unknown = 0,
        ManaBeast = 1,
        MTR = 2,
    };

    [[nodiscard]] const std::map<uint32_t, std::map<uint8_t, unsigned> >& get_bit_locations() const override
    {
        static const std::map<uint32_t, std::map<uint8_t, unsigned> > empty;
        return empty; // SoMR doesn't have bit locations, only value locations
    }

    [[nodiscard]] const std::map<uint32_t, std::vector<std::pair<uint8_t, unsigned> > >& get_byte_locations() const override
    {
        return _byteLocations;
    }

    void read_seed_and_slot(std::function<void(const std::string&, const std::string&, unsigned flags)> callback) override
    {
        if (_snes->get_state() != USB2SNES::State::SNES_CONNECTED) callback("","",0);
        _snes->read_memory(CART_HEADER_LOC, CART_HEADER_LEN,
                [this,callback](const std::string& res)
        {
            if (memcmp(res.c_str(), CART_HEADER, CART_HEADER_LEN) != 0) {
                callback("","",0);
            } else {
                _snes->read_memory(GAME_FLAGS_LOC, 1,
                        [this,callback](const std::string& res)
                {
                    const auto b = static_cast<uint8_t>(res[0]);
                    unsigned flags = 0;
                    if (b & 0x80) flags |= FLAG_WANT_DEATHLINK;
                    _snes->read_memory(AP_SECTION_LOC, AP_SECTION_LEN,
                            [callback, flags](const std::string& res)
                    {
                        const std::string seed = res.substr(0,32);
                        callback(seed.c_str(), res.c_str()+32, flags); // NOLINT(*-redundant-string-cstr) // trim at \0
                    });
                });
            }
        });
    }

    void read_joined(std::function<void(bool)> callback) override
    {
        // if at least one of the character flags is `1` and the other ones are `0` or `1`, that means we started.
        // however in menu, not everything is memset to zero, so read the flag 0x89 as well,
        // which should be == 0 when valid and (!= 0 in menu) and hope that's good enough.
        // FIXME: a breakpoint in load SRAM will still send out items, because some flags are > 0x89
        if (_snes->get_state() != USB2SNES::State::SNES_CONNECTED) callback(false);
        _charactersValid = false;
        _snes->read_memory(0x7ecf0c, 3, [this](const std::string& res) {
            _charactersValid = res.length() == 3
                && (res[0] == 1 || res[1] == 1 || res[2] == 1)
                && (res[0] == 0 || res[0] == 1)
                && (res[1] == 0 || res[1] == 1)
                && (res[2] == 0 || res[2] == 1);
        });

        _snes->read_memory(0x7ecf89, 1, [this, callback](const std::string& res) {
            if (_snes->get_state() != USB2SNES::State::SNES_CONNECTED) callback(false);
            const bool ok = res.length() == 1 && res[0] == 0;
            callback(_charactersValid && ok);
        });

#if 0
        // could also check this, but won't reset in menu
        _snes->read_memory(0x7ecfc1, 1, [this, callback](const std::string& res) {
            if (_snes->get_state() != USB2SNES::State::SNES_CONNECTED) callback(false);
            const bool ok = res.length() == 1 && res[0] >= 1 && res[0] <= 15;
            callback(_charactersValid && ok);
        });
#endif
    }

    void read_finished(std::function<void(bool)> callback) override
    {
        // TODO: xmas guy
        if (_goal == Goal::MTR) {
            // read total mana power (0x46). 15 = done
            _snes->read_memory(0x7ecffc, 1, [callback](const std::string& res) {
                callback(res[0] == 15);
            });
        } else if (_goal == Goal::ManaBeast) {
            // read mana fort state (0x4E). 9 = done
            _snes->read_memory(0x7ecf4e, 1, [callback](const std::string& res) {
                callback(res[0] == 9);
            });
        }
    }

    [[nodiscard]] int64_t get_location_base() const override
    {
        return 0;
    }

    void set_locations(const std::set<int64_t>& checked, const std::set<int64_t>& missing) override
    {
        const int64_t manaTree = get_location_base() + 30;
        const int64_t dreadSlime = get_location_base() + 6;
        _goal = (checked.empty() && missing.empty()) ? Goal::Unknown :
                (!checked.count(manaTree) && !missing.count(manaTree)) ? Goal::MTR :
                (checked.count(dreadSlime) || missing.count(dreadSlime)) ? Goal::ManaBeast :
                Goal::Unknown;
        debug("Set goal: %d", static_cast<int>(_goal));
    }

    void send_item(const int index, int64_t id, const std::string&, const std::string&) override
    {
        if (id == -1) { // AP Nothing
            id = 0; // SoMR Nothing
        } else {
            id -= get_location_base();
        }
        _receivedItems[index] = static_cast<int>(id);
        if (index > _lastItemIndex) _lastItemIndex = index;
        _lastSent = now() - 2001; // try to send immediately
    }

    void send_death() override
    {
        if (_deathlink)
            _deathQueued = true;
    }

    void poll() override
    {
        // NOTE: see NOTE in Game::poll()
        if (!_snes->idle()) return;
        Game::poll();
        // send out item(s) if possible
        if (get_state() == State::JOINED) {
            const auto t = now();
            if (t - _lastSent < 2000) return; // only try to send an item every 2sec
            _lastSent = t;
            // 1. check if an event is still queued
            // (1a. TODO: if not, check if scripts are busy?)
            // 2. if not, check the next send index
            // 3. if correct, queue event
            _snes->read_memory(0x7e0443, 2, [this](const std::string& res) {
                // read expected_index and receive busy
                if (res.size() < 2) {
                    printf("ERROR: could not read send-item state from game\n");
                    return;
                }
                if (memcmp(res.c_str(), "\x00", 2) != 0) return; // busy
                // FIXME: if `this` gets destroyed without _snes getting
                //        destroyed then code below is a bad memory access.
                //        We need to cancel this callback on delete.
                _snes->read_memory(0x7ecf88, 3, [this](const std::string &res) {
                    // read expected_index and receive busy
                    if (res.size() != 3) {
                        printf("ERROR: could not read item index from game\n");
                        return;
                    }
                    if (get_state() != State::JOINED) return; // changed state during Game::poll()
                    uint16_t expect = static_cast<uint8_t>(res[2]); // hi
                    expect <<= 8;
                    expect |= static_cast<uint8_t>(res[0]); // lo
                    if (res[1] != 0) {
                        printf("ERROR: invalid next item index. Are in the menu?\n");
                        return;
                    }
                    const auto expectedIndex = _ignoreSendIndex ? 0 : static_cast<int>(expect);
                    if (expectedIndex <= _lastItemIndex) {
                        const auto it = _receivedItems.find(expectedIndex);
                        if (it == _receivedItems.end()) {
                            printf("ERROR: received bad items from server\n");
                            return;
                        }
                        const auto itemid = static_cast<uint16_t>(it->second);
                        // index, amount, id
                        const uint8_t buf[] = {
                            static_cast<uint8_t>(itemid & 0xff),
                            static_cast<uint8_t>((itemid >> 8) & 0xff),
                            1,
                        };
                        if (_ignoreSendIndex) {
                            const auto newIndex = static_cast<unsigned>(expectedIndex);
                            const uint8_t overrideIndexBuf[] = {
                                static_cast<uint8_t>(newIndex & 0xff), // lo
                                0,
                                static_cast<uint8_t>((newIndex >> 8) & 0xff), // hi
                            };
                            _snes->write_memory(
                                0x7ecf88,
                                std::string(reinterpret_cast<const char *>(overrideIndexBuf), sizeof(overrideIndexBuf))
                            );
                            _ignoreSendIndex = false;
                        }
                        _snes->write_memory(
                            0x7e0441, // NOTE: this was 7eff00 originally, but that appears to be in use
                            std::string(reinterpret_cast<const char *>(buf), sizeof(buf))
                        );
                    }
                });
            });

            // TODO: deathlink
            (void)_lastDeathlinkDeath;
        } else {
            // don't participate in deathlink while not in-game
            _dead = true;
            _deathQueued = false;
            _deathSent = false;
        }
    }

    bool force_resend() override
    {
        // overwrite expected item index to be 0
        _ignoreSendIndex = true;
        return true;
    }

    void clear_cache() override
    {
        Game::clear_cache();
        _goal = Goal::Unknown;
        _receivedItems.clear();
        _lastItemIndex = -1;
    }

    [[nodiscard]] int get_items_handling() const override
    {
        return 0b111; // full remote
    }

private:
    static std::map<uint32_t, std::vector<std::pair<uint8_t, unsigned> > > _byteLocations;
    static std::map<int, std::pair<uint16_t, uint16_t> > _items;
    std::map<int, int> _receivedItems;
    bool _charactersValid = false;
    int _lastItemIndex = -1;
    unsigned long _lastSent = 0;
    bool _ignoreSendIndex = false;
    bool _dead = true; // NOTE: deathlink not implemented yet
    bool _deathQueued = false;
    bool _deathSent = false;
    unsigned long _lastDeathlinkDeath = 0;
    Goal _goal = Goal::Unknown;

    static constexpr auto CART_HEADER = "Secret of MANA       \x21\x02\x0b\x03\x01\xc3\x00";
    static constexpr size_t CART_HEADER_LOC = 0xFFC0;
    static constexpr size_t CART_HEADER_LEN = 28;
    static constexpr size_t AP_SECTION_LOC = 0x3d0040; // $fd0040; TODO: move to WRAM
    static constexpr size_t AP_SECTION_LEN = 64;
    static constexpr size_t GAME_FLAGS_LOC = 0x3d000c; // $fd000c; TODO: move to WRAM; TODO: implement this
};
