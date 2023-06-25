# AP SoE Client

This is the [Archipelago Multiworld](https://github.com/ArchipelagoMW/Archipelago)
client for the Secret of Evermore world.

## How does it work

It compiles to WASM+JS and runs in your browser.

## How to use it

You can build it (or download a release) and host it on `http://localhost:8000`
using `serve.py`. Or visit an URL that hosts it.

The page will automatically connect to your SNI if running.
Use `/connect <host>` command to connect to an AP server.

The game can very rarely desync receiving items. When everything is green,
the client receives items, but the game does not, while in a room/scene that
should be able to receive items, use `/force-send` command to ignore the lock.

## How to build it

see `build-*.sh`

## Local storage

* The client will store a random ID in local storage to be able to replace the
  previous connection from the same client (crash or lost connectivity).
  This ID is only shared between the client and the AP server `/connect`ed to.
* The client stores a cache of item and location names to reduce traffic.
* The client may store the last 24hrs of output to a log.
* The http server hosting the client does not store any of that.
* The local storage can be cleared at any time through browser features.

## TODO

* make Goal stick.
  At the moment
  * connecting to AP host clears the cached Goal status, so
  * be connected to AP host while the outro is running
  * or connect to AP host with the outro still running
* some text clean-up

## Attribution

Check [the source](https://github.com/black-sliver/ap-soeclient/) for all referenced projects.

Binary distributions may include or link to

* [OpenSSL](https://github.com/openssl/openssl)
* [ASIO](https://github.com/chriskohlhoff/asio)
* [WebSocketPP](https://github.com/zaphoyd/websocketpp)
* [Curl's CA Extract](https://curl.se/docs/caextract.html)
* [nlohmann::json](https://github.com/nlohmann/json)
* [valijson](https://github.com/tristanpenman/valijson)
