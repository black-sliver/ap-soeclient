# AP SoE Client

This is the [Archipelago Multiworld](https://github.com/ArchipelagoMW/Archipelago) client for the Secret of Evermore
world.

## How does it work

It compiles to WASM+JS and runs in your browser. URL where it's gonna live is yet to be determined.

## How to use it

Until it's hosted publicly, you can build it (or download a release) and host it on `http://localhost:8000`
using `serve.py`.

The page will automatically connect to your SNI if running. Use `/connect <host>` command to connect to the AP server.

## How to build it

see `build.sh`
