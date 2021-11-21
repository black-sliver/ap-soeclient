#ifndef _GAMES_GAME_H
#define _GAMES_GAME_H


#include "../usb2snes.hpp"
#include <stdint.h>
#include <functional>
#include <string>
#include <list>
#include <map>

#if 0
#define debug(fmt, ...) printf ("Game: " fmt "\n", __VA_ARGS__);
#else
#define debug(...) do {} while(0)
#endif


class Game // TODO: rename to SnesGame
{
public:
    struct bitaddr {
        // generic helper to define a list of location bits
        uint32_t val;
        bitaddr(uint32_t addr, uint8_t mask) {
            val = addr<<8 | mask;
        }
        uint32_t addr() const { return val>>8; }
        uint8_t mask() const { return (uint8_t)(val & 0xff); }
        bool operator==(const bitaddr& other) const {
            return val == other.val;
        }
        bool operator<(const bitaddr& other) const {
            return val < other.val;
        }
    };
    
    enum class State {
        STOPPED, // game not running at all
        RUNNING, // game running but not playing
        JOINED, // player playing
        FINISHED, // finished (player maybe still playing)
    };

    Game(USB2SNES* snes)
        : _snes(snes)
    {
        _lastLocationPoll = now() - LOCATION_POLL_INTERVAL;
    }

    virtual ~Game()
    {
        // TODO: cancel all pending callbacks in _snes
        reset();
    }

    virtual void reset()
    {
        // either this or the destructor should be called if the connection is lost
        clear_cache();
        invalidate_pending_read();
        _seed.clear();
        _slot.clear();
        if (_state != State::STOPPED) {
            _state = State::STOPPED;
            if (_hOnGameStopped) _hOnGameStopped();
        }
    }

    virtual void clear_cache() {
        _cache.clear();
        if (_state == State::FINISHED) _state = State::JOINED;
    }

    virtual void poll()
    {
        // NOTE: if reads are queued, we need to double-check a lot of
        //       state in the callbacks. Not polling while busy actually
        //       reduce the amount of state we have to check, since we
        //       can't change state randomly, but better safe than sorry,
        //       so we leave all checks in.
        // NOTE: there is still potential to trigger checks when ram is
        //       filled with garbage since we read seed/slot from rom.
        if (!_snes->idle()) return;
        auto t = now();
        if (_state == State::STOPPED) {
            // TODO: different interval for timeout and read complete,
            //       or better detect if read was cancelled
            if (t - _lastStartedCheck >= STARTED_CHECK_INTERVAL && 
                    _snes->get_state() == USB2SNES::State::SNES_CONNECTED) {
                read_seed_and_slot([this](const std::string& seed, const std::string& slot){
                    if (!seed.empty() && !slot.empty()) {
                        if (seed == _seed && slot == _slot && _state >= State::RUNNING)
                            return; // already done
                        _seed = seed;
                        _slot = slot;
                        _state = State::RUNNING;
                        if (_hOnGameStarted) _hOnGameStarted();
                        log(("running, seed \"" + _seed + "\", slot \""+ _slot + "\"").c_str());
                    }
                });
                _lastStartedCheck = t;
            }
        }
        else if (_state == State::RUNNING) {
            // TODO: different interval for timeout and read complete,
            //       or better detect if read was cancelled
            if (t - _lastJoinedCheck >= JOINED_CHECK_INTERVAL &&
                    _snes->get_state() == USB2SNES::State::SNES_CONNECTED) {
                read_seed_and_slot([this](const std::string& seed, const std::string& slot) {
                    invalidate_pending_read();
                    if (seed != _seed || slot != _slot) {
                        _state = State::STOPPED;
                        _seed.clear();
                        _slot.clear();
                        invalidate_pending_read();
                        log("stopped, game, slot or seed changed");
                        if (_hOnGameStopped) _hOnGameStopped();
                    } else {
                        read_joined([this](bool res) {
                            invalidate_pending_read();
                            if (res && _state < State::JOINED) {
                                log("joined/loaded save");
                                _state = State::JOINED;
                                on_game_joined();
                                if (_hOnGameJoined) _hOnGameJoined();
                            }
                        });
                    }
                });
                _lastJoinedCheck = t;
            }
        }
        else {
            // TODO: different interval for timeout and read complete,
            //       or better detect if read was cancelled
            if (t - _lastLocationPoll >= LOCATION_POLL_INTERVAL && 
                    _snes->get_state() == USB2SNES::State::SNES_CONNECTED) {
                if (_addressCache.empty()) makeAddressCache();
                // read "joined" detection
                read_joined([this](bool res) {
                    invalidate_pending_read();
                    if (!res && _state > State::RUNNING) {
                        _state = State::RUNNING;
                        // TODO: cancel all pending reads?
                        if (_hOnGameLeft) _hOnGameLeft();
                    } else if (_state < State::JOINED) {
                        return; // already detected reset/rom change
                    } else {
                        _readBufferValid = res;
                    }
                });
                // read all values of interest to a buffer
                for (const auto& pair: _addressCache) {
                    auto addr = pair.first;
                    auto len = pair.second;
                    _snes->read_memory(addr, len, [this,addr,len](const std::string& data) {
                        for (uint32_t i=0; i<len; i++) {
                            _readBuffer[addr+i] = (uint8_t)data[i];
                        }
                    });
                }
                // read "finished" detection
                read_finished([this](bool res) {
                    _finishedBuffer = res;
                });
                // verify rom by reading seed and slot
                read_seed_and_slot([this](const std::string& seed, const std::string& slot) {
                    if (!seed.empty() && seed == _seed && !slot.empty() && slot == _slot) return; // OK
                    invalidate_pending_read();
                    if (_state == State::JOINED) {
                        _state = State::RUNNING;
                        if (_hOnGameLeft) _hOnGameLeft();
                    }
                    if (_state == State::RUNNING) {
                        _state = State::STOPPED;
                        log("stopped, game, slot or seed changed");
                        if (_hOnGameStopped) _hOnGameStopped();
                    }
                });
                // read "joined" detection again, and apply values
                read_joined([this](bool res) {
                    if (_state < State::JOINED) return;
                    if (!res) {
                        invalidate_pending_read();
                        _state = State::RUNNING;
                        if (_hOnGameLeft) _hOnGameLeft();
                    } else if (_readBufferValid) {
                        std::list<int> locationsChecked;
                        std::list<int> locationsScouted;
                        for (const auto& pair: _readBuffer) {
                            const auto& bits = get_bit_locations();
                            auto cacheIt = _cache.find(pair.first);
                            if (cacheIt == _cache.end() || cacheIt->second != pair.second)
                            {
                                debug("Value at $%06x changed to 0x%02hhx", pair.first, pair.second);
                                uint8_t old = (cacheIt == _cache.end()) ? 0 : cacheIt->second;
                                auto bitsIt = bits.find(pair.first);
                                if (bitsIt != bits.end()) {
                                    for (const auto& bit: bitsIt->second) {
                                        if ((pair.second & bit.first) && !(old & bit.first)) {
                                            int locId = get_location_base() + bit.second;
                                            char msg[128];
                                            snprintf(msg, sizeof(msg),
                                                     "Looted location (#%u) %d",
                                                     bit.second, locId);
                                            log(msg);
                                            locationsChecked.push_back(locId);
                                        }
                                    }
                                }
                                if (cacheIt == _cache.end()) _cache[pair.first] = pair.second;
                                else cacheIt->second = pair.second;
                            }
                        }
                        if (!locationsChecked.empty() && _hOnLocationsChecked)
                            _hOnLocationsChecked(locationsChecked);
                        if (!locationsScouted.empty() && _hOnLocationsScouted)
                            _hOnLocationsScouted(locationsScouted);
                        if (_finishedBuffer && _state != State::FINISHED) {
                            _state = State::FINISHED;
                            if (_hOnGameFinished) _hOnGameFinished();
                        }
                        invalidate_pending_read();
                    }
                });
                _lastLocationPoll = t;
            }
        }
    }

    void set_game_started_handler(std::function<void(void)> f)
    {
        _hOnGameStarted = f;
    }

    void set_game_stopped_handler(std::function<void(void)> f)
    {
        _hOnGameStopped = f;
    }

    void set_game_joined_handler(std::function<void(void)> f)
    {
        _hOnGameJoined = f;
    }

    void set_game_left_handler(std::function<void(void)> f)
    {
        _hOnGameLeft = f;
    }

    void set_locations_checked_handler(std::function<void(std::list<int>)> f)
    {
        _hOnLocationsChecked = f;
    }

    void set_locations_scouted_handler(std::function<void(std::list<int>)> f)
    {
        _hOnLocationsScouted = f;
    }

    void set_game_finished_handler(std::function<void(void)> f)
    {
        _hOnGameFinished = f;
    }

    State get_state() const {
        return _state;
    }

    const std::string& get_slot() const {
        return _slot;
    }

    const std::string& get_seed() const {
        return _seed;
    }

    virtual void send_item(int index, int id, const std::string& sender, const std::string& location) = 0;

    virtual bool force_send() { return false; }

protected:    
    void log(const char* s)
    {
        printf("Game: %s\n", s);
    }

    static unsigned long now()
    {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        unsigned long ms = (unsigned long)ts.tv_sec * 1000;
        ms += (unsigned long)ts.tv_nsec / 1000000;
        return ms;
    }
    
    virtual void on_game_joined() {} // override this to handle game join

    virtual const std::map<uint32_t, std::map<uint8_t, unsigned> > get_bit_locations() const = 0;
    virtual void read_seed_and_slot(std::function<void(const std::string&, const std::string&)> callback) = 0;
    virtual void read_joined(std::function<void(bool)> callback) = 0;
    virtual void read_finished(std::function<void(bool)> callback) = 0;
    virtual int get_location_base() const = 0;
    
    USB2SNES* _snes = nullptr;
    
private:
    void makeAddressCache()
    {
        _addressCache.clear();
        for (const auto& pair: get_bit_locations()) {
            _addressCache[pair.first] = 1;
        }
        // merge continuous reads
        auto prev = _addressCache.rbegin();
        for (auto it = prev; it != _addressCache.rend(); ++it)
        {
            if (prev != it && it->first >= prev->first-3) { // allow 2B hole per 1B data
                //printf("merging $%06x:%u .. $%06x:%u", it->first, it->second, prev->first, prev->second);
                it->second = prev->second + (prev->first - it->first);
                prev->second = 0;
                //printf("-> $%06x:%u\n", it->first, it->second);
            }
            prev = it;
        }
        // clean up zero reads
        for (auto it = _addressCache.begin(); it != _addressCache.end();)
        {
            if (it->second == 0) it = _addressCache.erase(it);
            else ++it;
        }
    }

    void invalidate_pending_read()
    {
        _readBuffer.clear();
        _finishedBuffer = false;
        _readBufferValid = false;
    }

    unsigned long _lastLocationPoll = 0;
    unsigned long _lastStartedCheck = 0;
    unsigned long _lastJoinedCheck = 0;
    static constexpr unsigned long LOCATION_POLL_INTERVAL = 1000;
    static constexpr unsigned long STARTED_CHECK_INTERVAL = 1000;
    static constexpr unsigned long JOINED_CHECK_INTERVAL = 1000;
    State _state = State::STOPPED;
    std::string _seed;
    std::string _slot;
    std::map<uint32_t, uint32_t> _addressCache;
    std::map<uint32_t, uint8_t> _readBuffer;
    bool _finishedBuffer = false;
    bool _readBufferValid = false;
    std::map<uint32_t, uint8_t> _cache;
    std::function<void(void)> _hOnGameStarted = nullptr;
    std::function<void(void)> _hOnGameStopped = nullptr;
    std::function<void(void)> _hOnGameJoined = nullptr;
    std::function<void(void)> _hOnGameLeft = nullptr;
    std::function<void(void)> _hOnGameFinished = nullptr;
    std::function<void(std::list<int>)> _hOnLocationsChecked = nullptr;
    std::function<void(std::list<int>)> _hOnLocationsScouted = nullptr;
};

#endif // _GAMES_GAME_H
