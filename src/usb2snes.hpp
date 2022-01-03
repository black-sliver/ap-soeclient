#ifndef _USB2SNES_H
#define _USB2SNES_H

#include <wswrap.hpp>
#include <string>
#include <queue>
#include <nlohmann/json.hpp>
#include <functional>
#include <time.h>
#include <algorithm>


#define USB2SNES_DEBUG


class USB2SNES {
protected:
    typedef nlohmann::json json;
    typedef wswrap::WS WS;

public:
    USB2SNES(const std::string& uri="ws://localhost:8080")
    {
        _uri = uri;
        connect_socket();
    }

    virtual ~USB2SNES()
    {
        if (_state == State::SNES_CONNECTED) {
            _state = State::SOCKET_CONNECTED;
            if (_hOnSnesDisconnected) _hOnSnesDisconnected();
        }
        if (_state >= State::SOCKET_CONNECTED) {
            _state = State::DISCONNECTED;
            if (_hOnSocketDisconnected) _hOnSocketDisconnected();
        }
        _state = State::DISCONNECTED;
        delete _ws;
        _ws = nullptr;
    }
    
    enum class State {
        DISCONNECTED,
        SOCKET_CONNECTING,
        SOCKET_CONNECTED,
        SNES_LIST,
        SNES_CONNECTING,
        SNES_CONNECTED,
    };

    State get_state() const { return _state; }

    void read_memory(uint32_t addr, uint32_t len, std::function<void(const std::string&)> callback)
    {
        if (len == 0) {
            warn("read_memory with len=0 requested. Fix your code.");
            if (!callback) return;
        }
        char saddr[9]; snprintf(saddr, sizeof(saddr), "%06X", (unsigned)mapaddr(addr));
        char slen[9];  snprintf(slen,  sizeof(slen),  "%X",   (unsigned)len);
        
        send(json{
            {"Opcode", "GetAddress"},
            {"Space", "SNES"},
            {"Operands", {saddr, slen}}
        }, len, callback);
    }
    
    bool write_memory(uint32_t addr, const std::string& data)
    {
        if (_state < State::SNES_CONNECTED) return false;
        if (data.length() == 0) {
            warn("write_memory with len=0 requested. Fix your code.");
            return true;
        }
        // TODO: return false if not connected
        char saddr[9]; snprintf(saddr, sizeof(saddr), "%06X", (unsigned)mapaddr(addr));
        char slen[9];  snprintf(slen,  sizeof(slen),  "%X",   (unsigned)data.length());
        send(json{
            {"Opcode", "PutAddress"},
            {"Space", "SNES"},
            {"Operands", {saddr, slen}}
        }, data);
        return true;
    }

    void poll()
    {
        if (_ws) _ws->poll();
        if (_state < State::SOCKET_CONNECTED) { // TODO: == DISCONNECTED ?
            auto t = now();
            if (t - _lastSocketConnect > _socketReconnectInterval) {
                if (_state != State::DISCONNECTED)
                    log("Connect timed out. Retrying.");
                else
                    log("Reconnecting to service");
                connect_socket();
            }
        }
        else if (_state == State::SOCKET_CONNECTED) {
            auto t = now();
            if (t - _lastSnesConnect > 1000) {
                connect_snes();
            }
        }
    }

    bool idle()
    {
        return (_state == State::SNES_CONNECTED && _txQueue.empty());
    }

    void set_socket_connected_handler(std::function<void(void)> f)
    {
        _hOnSocketConnected = f;
    }

    void set_socket_disconnected_handler(std::function<void(void)> f)
    {
        _hOnSocketDisconnected = f;
    }

    void set_snes_connected_handler(std::function<void(void)> f)
    {
        _hOnSnesConnected = f;
    }

    void set_snes_disconnected_handler(std::function<void(void)> f)
    {
        _hOnSnesDisconnected = f;
    }

private:
    void log(const char* msg)
    {
        printf("USB2SNES: %s\n", msg);
    }
    void warn(const char* msg)
    {
        fprintf(stderr, "USB2SNES: %s\n", msg);
    }
    void debug(const char* msg)
    {
#ifdef USB2SNES_DEBUG
        log(msg);
#endif
    }

    uint32_t mapaddr(uint32_t addr)
    {
        if (addr>>16 == 0x7e || addr>>16==0x7f) return 0xF50000 + (addr&0xffff);
        // TODO: SRAM
        // ROM // TODO: hirom/lorom
        if (addr>=0x800000) return addr-0x800000;
        return addr;
    }

    void onopen()
    {
        debug("onopen()");
        log("Service connected");
        _state = State::SOCKET_CONNECTED;
        std::string appname = "SoEAPClient";
        std::string appid;
        const char idchars[] = "0123456789abcdef";
        for (int i=0; i<4; i++) appid += idchars[std::rand()%strlen(idchars)];
        send(json{
            { "Opcode", "Name" },
            { "Space", "SNES" },
            { "Operands", {appname+" "+appid}}
        }, TxItem::NO_RESPONSE);
        if (_hOnSocketConnected) _hOnSocketConnected();
        //_socketReconnectInterval = 1500; // moved to onSnesConnected part
        connect_snes();
    }

    void onclose()
    {
        debug("onclose()");
        while (!_txQueue.empty()) _txQueue.pop(); // TODO: run error handlers?
        _rxBuffer.clear();
        if (_state == State::SNES_CONNECTED) {
            _state = State::SOCKET_CONNECTED;
            if (_hOnSnesDisconnected) _hOnSnesDisconnected();
        }
        if (_state > State::SOCKET_CONNECTING) {
            log("Service disconnected");
            _state = State::DISCONNECTED;
            if (_hOnSocketDisconnected) _hOnSocketDisconnected();
        }
        _state = State::DISCONNECTED;
        delete _ws;
        _ws = nullptr;
    }

    void onmessage(const std::string& s)
    {
        while (!_txQueue.empty() && _txQueue.front().sent && _txQueue.front().expectedResponse == TxItem::NO_RESPONSE)
        {
            if (_txQueue.front().responseCallback)
                _txQueue.front().responseCallback("");
            _txQueue.pop();
        }
        if (s.empty()) return;
        if (_txQueue.empty() || !_txQueue.front().sent) {
            log("Unexpected data received");
        } else if (_txQueue.front().expectedResponse == TxItem::JSON_RESPONSE) {
            if (!_rxBuffer.empty()) {
                log("WARNING: Dumping data from rx buffer");
                _rxBuffer.clear();
            }
            json j = json::parse(s);
            debug(("received "+j.dump()).c_str());
            if (_state == State::SNES_LIST) {
                // reply to DeviceList
                if (j["Results"].is_array() && j["Results"].size()>0) {
                    // TODO: add logic to try not just the first device, or let user select
                    _state = State::SNES_CONNECTING;
                    log("Attaching...");
                    send({
                        {"Opcode", "Attach"},
                        {"Space", "SNES"},
                        {"Operands", {j["Results"][0]}}
                    }, TxItem::NO_RESPONSE);
                    send({
                        { "Opcode", "Info" },
                        { "Space", "SNES" }
                    }, TxItem::JSON_RESPONSE);
                } else {
                    _state = State::SOCKET_CONNECTED;
                }
            }
            else if (_state == State::SNES_CONNECTING) {
                // reply to Info
                if (j["Results"].is_array() && j["Results"].size()>0) {
                    log("SNES connected");
                    _state = State::SNES_CONNECTED;
                    _socketReconnectInterval = 1500;
                    if (_hOnSnesConnected) _hOnSnesConnected();
                } else {
                    log("Error connecting to snes");
                    _state = State::SOCKET_CONNECTED;
                }
            } else {
                log("Unexpected response");
            }
            _txQueue.pop();
        } else {
            _rxBuffer += s;
            size_t expected = _txQueue.front().expectedResponse;
            if (_rxBuffer.length() >= expected) {
                if (_txQueue.front().responseCallback) {
                    _txQueue.front().responseCallback(_rxBuffer.substr(0, expected));
                }
                _rxBuffer = _rxBuffer.substr(expected);
                _txQueue.pop();
            } else {
                debug("Partial binary response");
            }
        }
        while (!_txQueue.empty() && _txQueue.front().sent && _txQueue.front().expectedResponse == TxItem::NO_RESPONSE)
        {
            if (_txQueue.front().responseCallback)
                _txQueue.front().responseCallback("");
            _txQueue.pop();
        }
    }

    void onerror()
    {
        debug("onerror()");
    }

    struct TxItem {
        std::string data;
        size_t expectedResponse;
        std::function<void(const std::string&)> responseCallback;
        bool binary;
        bool sent;
        
        static constexpr size_t NO_RESPONSE = (size_t)0;
        static constexpr size_t JSON_RESPONSE = (size_t)-1;
        
        TxItem(const std::string& data, size_t expectedResponse=NO_RESPONSE, std::function<void(const std::string&)> responseCallback=nullptr, bool binary=false)
            : data(data), expectedResponse(expectedResponse),
              responseCallback(responseCallback), binary(binary), sent(false)
        {
        }
    };

    void send(const TxItem& item)
    {
        _txQueue.push(item);
        if (item.binary)
            _ws->send_binary(item.data);
        else
            _ws->send_text(item.data);
        _txQueue.back().sent = true;
    }

    void send(const std::string& data, size_t expectedResponse, std::function<void(const std::string&)> responseCallback=nullptr)
    {
        send(TxItem{data, expectedResponse, responseCallback});
    }

    void send(const json& data, size_t expectedResponse, std::function<void(const std::string&)> responseCallback=nullptr)
    {
        send(TxItem{data.dump(), expectedResponse, responseCallback});
    }

    void send(const json& data, const std::string& bytes)
    {
        debug(("send: " + data.dump() + " [" + std::to_string(data.dump().length()) +"]").c_str());
        printf("USB2SNES: send: ");
        for (char c: bytes) {
            uint8_t b = (uint8_t)c;
            printf("%02x ", (unsigned)b);
        }
        printf("[%d]\n", (int)bytes.length());
        send(TxItem{data.dump(), TxItem::NO_RESPONSE, nullptr});
        send(TxItem{bytes, TxItem::NO_RESPONSE, nullptr, true});
    }

    void connect_socket()
    {
        delete _ws;
        _ws = nullptr;
        if (_uri.empty()) {
            _state = State::DISCONNECTED;
            return;
        }
        _state = State::SOCKET_CONNECTING;
        while (!_txQueue.empty()) _txQueue.pop(); // TODO: run error handlers?
        _rxBuffer.clear();
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
    
    void connect_snes()
    {
        if (_state == State::SOCKET_CONNECTED) {
            _state = State::SNES_LIST;
            send(json{
                { "Opcode", "DeviceList" },
                { "Space", "SNES" }
            }, TxItem::JSON_RESPONSE);
            _lastSnesConnect = now();
        }
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
    WS* _ws = nullptr;
    State _state = State::DISCONNECTED;
    std::queue<TxItem> _txQueue;
    std::string _rxBuffer;
    std::function<void(void)> _hOnSocketConnected = nullptr;
    std::function<void(void)> _hOnSocketDisconnected = nullptr;
    std::function<void(void)> _hOnSnesConnected = nullptr;
    std::function<void(void)> _hOnSnesDisconnected = nullptr;
    unsigned long _lastSocketConnect;
    unsigned long _lastSnesConnect;
    unsigned long _socketReconnectInterval = 1500;
    
};

#endif // _USB2SNES_H
