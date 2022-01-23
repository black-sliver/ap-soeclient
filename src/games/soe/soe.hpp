#ifndef _GAMES_SOE_SOE_H
#define _GAMES_SOE_SOE_H


#include "../game.hpp"
#include "../../usb2snes.hpp"
#include <map>
#include <string.h>

#ifdef GAME
#error multiple games defined
#endif
#define GAME SoE


class SoE : public Game
{
public:
    static constexpr auto Name = "Secret of Evermore";
    static constexpr int MAX_SEED_LENGTH = 32;

    SoE(USB2SNES* snes)
        : Game(snes)
    {
    }

    virtual ~SoE()
    {
    }

protected:
    virtual const std::map<uint32_t, std::map<uint8_t, unsigned> > get_bit_locations() const override
    {
        return _bitLocations;
    }

    virtual void read_seed_and_slot(std::function<void(const std::string&, const std::string&)> callback) override
    {
        if (_snes->get_state() != USB2SNES::State::SNES_CONNECTED) callback("","");
        _snes->read_memory(CART_HEADER_LOC, CART_HEADER_LEN,
                [this,callback](const std::string& res)
        {
            if (memcmp(res.c_str(), CART_HEADER, CART_HEADER_LEN)) {
                callback("","");
            } else {
                _snes->read_memory(AP_SECTION_LOC, AP_SECTION_LEN,
                        [callback](const std::string& res)
                {
                    std::string seed = res.substr(0,32);
                    callback(seed.c_str(), res.c_str()+32); // c_str() trims at NUL byte
                });
            }
        });
    }

    virtual void read_joined(std::function<void(bool)> callback) override
    {
        // 22ab&0x40 is set when entering the jungle after intro cutscene/fight,
        // even if the intro/fight is skipped.
        // 22ab&0x30 is unused and cleared to 0.
        // We need to check bits for 1 and anothers for 0 to detect garbage/all 1/all 0.
        // Below check is still a 1 in in 8 chance to accept garbage as joined.
        if (_snes->get_state() != USB2SNES::State::SNES_CONNECTED) callback(false);
        _snes->read_memory(0x7e22ab, 1, [callback](const std::string& res) {
            callback(((uint8_t)res[0] & 0x70) == 0x40);
        });
    }

    virtual void read_finished(std::function<void(bool)> callback) override
    {
        // NOTE: this is *not* the check used for speedruns. speedrun time is when carltron has 0 HP
        // this flag is set when inside outro (after the dialog with Sydney)
        // this function will only be called when in-game detection succeeded
        _snes->read_memory(0x7e22f1, 1, [callback](const std::string& res) {
            callback((uint8_t)res[0] & 0x40);
        });
    }

    virtual int get_location_base() const override
    {
        return 64000;
    }

    virtual void send_item(int index, int id, const std::string& sender, const std::string& location) override
    {
        id -= get_location_base();
        _receivedItems[index] = id;
        if (index > _lastItemIndex) _lastItemIndex = index;
    }

    virtual void poll() override
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
                uint16_t expect = (uint8_t)res[1];
                expect <<= 8; expect |= (uint8_t)res[0];
                auto expectedIndex = (int)expect;
                bool pending = _lockVersion > 1 ? res[9] : res[2];
                if (expectedIndex <= _lastItemIndex && !pending && (_ignoreSendLock || (res[7] == 0 && res[8] == 0))) {
                    const auto it = _receivedItems.find(expectedIndex);
                    if (it == _receivedItems.end()) {
                        printf("ERROR: received bad items from server\n");
                        return;
                    }
                    _ignoreSendLock = false;
                    const auto itemIt = _items.find(it->second);
                    uint16_t index = (uint16_t)expectedIndex;
                    uint16_t amount = itemIt->second.first;
                    uint16_t itemid = itemIt->second.second;
                    // index, amount, id
                    uint8_t buf[] = {
                        (uint8_t)(index &0xff), (uint8_t)((index >>8)&0xff),
                        (uint8_t)(amount&0xff), (uint8_t)((amount>>8)&0xff),
                        (uint8_t)(itemid&0xff), (uint8_t)((itemid>>8)&0xff),
                        1 // start script (for v0.39.2)
                    };
                    _snes->write_memory(0x7e2578, std::string((const char*)buf, _lockVersion > 1 ? 7 : 6));
                    if (_lockVersion < 2) {
                        // start script (for v0.39.1)
                        char start[] = {1};
                        _snes->write_memory(0x7e2577, std::string(start, 1));
                    }
                }
            });
        
        }
    }
    
    virtual bool force_send() override
    {
        // ignore part of the receive lock for the next item
        _ignoreSendLock = true;
        return true;
    }
    
    virtual void clear_cache() override
    {
        Game::clear_cache();
        _receivedItems.clear();
        _lastItemIndex = -1;
    }

    virtual int get_items_handling() const override
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
    uint32_t _gameVersion = 0; // not implemented yet. 0 means unknown
    uint8_t _lockVersion = 0; // 0:unknown, 1:v0.39.1, 2:v0.39.2
    static constexpr auto CART_HEADER = "SECRET OF EVERMORE   \x31\x02\x0c\x03\x01\x33\x00";
    static constexpr size_t CART_HEADER_LOC = 0xFFC0;
    static constexpr size_t CART_HEADER_LEN = 28;
    static constexpr size_t AP_SECTION_LOC = 0x3d0040; // $fd0040; TODO: move to SRAM
    static constexpr size_t AP_SECTION_LEN = 64;
};

#endif // _GAMES_SOE_SOE_H
