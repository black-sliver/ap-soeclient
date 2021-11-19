# AP SoE Client

This is the [Archipelago Multiworld](https://github.com/ArchipelagoMW/Archipelago)
client for the Secret of Evermore world.
This is still in beta, but should mostly work.

## How does it work

It compiles to WASM+JS and runs in your browser.

## How to use it

You can build it (or download a release) and host it on `http://localhost:8000`
using `serve.py`. Or visit an URL that hosts it.

The page will automatically connect to your SNI if running.
Use `/connect <host>` command to connect to an AP server.

## How to build it

see `build.sh`

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
* don't enable force-send on connect, instead add /force or something
  * when reconnect the first item may be sent while another one is in transit
* add text colors
* https-http-wss-ws notice or switch-over
* some text clean-up
* store last log
