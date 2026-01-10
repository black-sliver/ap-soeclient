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
#define GAME SoE


class SoE final : public Game
{
public:
    static constexpr auto Name = "Secret of Evermore";
    static constexpr int MAX_SEED_LENGTH = 32;

    explicit SoE(USB2SNES* snes)
        : Game(snes)
    {
    }

    ~SoE() override = default;

protected:
    [[nodiscard]] const std::map<uint32_t, std::map<uint8_t, unsigned> > get_bit_locations() const override
    {
        return _bitLocations;
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
        // 22ab&0x40 is set when entering the jungle after intro cutscene/fight,
        // even if the intro/fight is skipped.
        // 22ab&0x30 is unused and cleared to 0.
        // We need to check bits for 1 and another for 0 to detect garbage/all 1/all 0.
        // Below check is still a 1 in 8 chance to accept garbage as joined.
        if (_snes->get_state() != USB2SNES::State::SNES_CONNECTED) callback(false);
        _snes->read_memory(0x7e22ab, 1, [callback](const std::string& res) {
            callback((static_cast<uint8_t>(res[0]) & 0x70) == 0x40);
        });
    }

    void read_finished(std::function<void(bool)> callback) override
    {
        // NOTE: this is *not* the check used for speedruns. speedrun time is when carltron has 0 HP
        // this flag is set when inside outro (after the dialog with Sydney)
        // this function will only be called when in-game detection succeeded
        _snes->read_memory(0x7e22f1, 1, [callback](const std::string& res) {
            callback(static_cast<uint8_t>(res[0]) & 0x40);
        });
    }

    [[nodiscard]] int64_t get_location_base() const override
    {
        return 64000;
    }

    void send_item(const int index, int64_t id, const std::string&, const std::string&) override
    {
        id -= get_location_base();
        _receivedItems[index] = static_cast<int>(id);
        if (index > _lastItemIndex) _lastItemIndex = index;
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
            auto t = now();
            if (t - _lastSent < 2000) return; // only send an item every 2sec
            _lastSent = t;
            // TODO: if (_lockVersion > 2) data is written to SRAM
            // NOTE: by default scripts can not clear SRAM. That's why this
            //       solution was avoided in the first version.
            _snes->read_memory(0x7e2575, 10, [this](const std::string& res) { // read expected_index and receive busy
                if (res.size() < 10) {
                    printf("ERROR: could not read send-item state from game\n");
                    return;
                }
                // FIXME: if `this` gets destroyed without _snes getting
                //        destroyed then code below is a bad memory access.
                //        We need to cancel this callback on delete.
                if (get_state() != State::JOINED) return; // changed state during Game::poll()
                uint16_t expect = static_cast<uint8_t>(res[1]);
                expect <<= 8;
                expect |= static_cast<uint8_t>(res[0]);
                const auto expectedIndex = _ignoreSendIndex ? 0 : static_cast<int>(expect);
                const bool pending = _lockVersion > 1 ? res[9] : res[2];
                if (expectedIndex <= _lastItemIndex && !pending && (_ignoreSendLock || (res[7] == 0 && res[8] == 0))) {
                    const auto it = _receivedItems.find(expectedIndex);
                    if (it == _receivedItems.end()) {
                        printf("ERROR: received bad items from server\n");
                        return;
                    }
                    _ignoreSendLock = false;
                    const auto itemIt = _items.find(it->second);
                    const auto index = static_cast<uint16_t>(expectedIndex);
                    const uint16_t amount = itemIt->second.first;
                    const uint16_t itemid = itemIt->second.second;
                    // index, amount, id
                    const uint8_t buf[] = {
                        static_cast<uint8_t>(index & 0xff), static_cast<uint8_t>((index >> 8) & 0xff),
                        static_cast<uint8_t>(amount & 0xff), static_cast<uint8_t>((amount >> 8) & 0xff),
                        static_cast<uint8_t>(itemid & 0xff), static_cast<uint8_t>((itemid >> 8) & 0xff),
                        1 // start script (for v0.39.2)
                    };
                    if (_ignoreSendIndex) {
                        const auto newIndex = static_cast<unsigned>(expectedIndex);
                        const uint8_t overrideIndexBuf[] = {
                            static_cast<uint8_t>(newIndex & 0xff), static_cast<uint8_t>((newIndex >> 8) & 0xff)
                        };
                        _snes->write_memory(
                            0x7e2575,
                            std::string(reinterpret_cast<const char *>(overrideIndexBuf), sizeof(overrideIndexBuf))
                        );
                        _ignoreSendIndex = false;
                    }
                    _snes->write_memory(
                        0x7e2578,
                        std::string(reinterpret_cast<const char *>(buf), _lockVersion > 1 ? 7 : 6)
                    );
                    if (_lockVersion < 2) {
                        // start script (for v0.39.1)
                        constexpr char start[] = {1};
                        _snes->write_memory(0x7e2577, std::string(start, 1));
                    }
                }
            });
            if (_deathlink) _snes->read_memory(BOY_HP_LOC, 4, [this](const std::string& res) {
                uint16_t hp = static_cast<uint8_t>(res[1]);
                hp <<= 8;
                hp |= static_cast<uint8_t>(res[0]);
                uint16_t extra = static_cast<uint8_t>(res[3]);
                extra <<= 8;
                extra |= static_cast<uint8_t>(res[2]);
                if (hp == 0) {
                    if (!_dead && extra != 0) { // checking extra values to not detect soft reset
                        _dead = true;
                        if (now() - _lastDeathlinkDeath > 5000) {
                            // if we die within 5 seconds of deathlink completion,
                            // don't send deathlink for it
                            if (_hOnDeath) _hOnDeath();
                        }
                        log("You died.");
                    }
                } else {
                    _dead = false;
                }
            });
            if (_deathlink && _deathQueued) _snes->read_memory(EFFECT_LOC, 2, [this](const std::string& res) {
                if (res[0] == 0 && res[1] == 0) { // no effect scheduled (done or idle)
                    if (_deathSent) {
                        _deathSent = false;
                        _deathQueued = false;
                    } else {
                        _snes->write_memory(EFFECT_LOC, std::string("\x01\x00", 2));
                        _deathSent = true;
                    }
                    _lastDeathlinkDeath = now(); // started or done
                } else if (_deathSent) {
                    _lastDeathlinkDeath = now(); // waiting to trigger
                }
            });
        } else {
            // don't participate in deathlink while not in-game
            _dead = true;
            _deathQueued = false;
            _deathSent = false;
        }
    }

    bool force_send() override
    {
        _ignoreSendLock = true;
        return true;
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
        _receivedItems.clear();
        _lastItemIndex = -1;
    }

    [[nodiscard]] int get_items_handling() const override
    {
        return 0b101; // local items, remote inventory
    }

private:
    static std::map<uint32_t, std::map<uint8_t, unsigned> > _bitLocations;
    static std::map<int, std::pair<uint16_t, uint16_t> > _items;
    std::map<int, int> _receivedItems;
    int _lastItemIndex = -1;
    unsigned long _lastSent = 0;
    bool _ignoreSendLock = false;
    bool _ignoreSendIndex = false;
    uint8_t _lockVersion = 0; // 0:unknown, 1:v0.39.1, 2:v0.39.2
    bool _dead = true;
    bool _deathQueued = false;
    bool _deathSent = false;
    unsigned long _lastDeathlinkDeath = 0;

    static constexpr auto CART_HEADER = "SECRET OF EVERMORE   \x31\x02\x0c\x03\x01\x33\x00";
    static constexpr size_t CART_HEADER_LOC = 0xFFC0;
    static constexpr size_t CART_HEADER_LEN = 28;
    static constexpr size_t AP_SECTION_LOC = 0x3d0040; // $fd0040; TODO: move to WRAM
    static constexpr size_t AP_SECTION_LEN = 64;
    static constexpr size_t GAME_FLAGS_LOC = 0x3d000c; // $fd000c; TODO: move to WRAM
    static constexpr size_t BOY_HP_LOC = 0x7E4EB3;
    static constexpr size_t EFFECT_LOC = 0xa07ffe;
};
