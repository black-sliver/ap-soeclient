#ifndef _UUID_H
#define _UUID_H


#include <stdint.h>
#include <stdio.h>
#include <string>


#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <emscripten/html5.h>
#define UUID_FILE "/settings/uuid"
#else
#define UUID_FILE "uuid" // TODO: place in %appdata%
#include <stdlib.h>
#include <time.h>
#endif


static void init_rng()
{
    #ifdef __EMSCRIPTEN__
    /* js crypto needs no init from C */
    #else
    srand ((unsigned int) time (NULL));
    #endif
}

static uint8_t rand_byte()
{
    #ifdef __EMSCRIPTEN__
    return (uint8_t)EM_ASM_INT({
        var buf = new Uint8Array(1);
        crypto.getRandomValues(buf);
        return buf[0];
    });
    #else
    return rand();
    #endif
}

static void make_uuid(char* out)
{
    for (uint8_t i=0; i<16; i++) {
        sprintf(out + 2*i, "%02hhx", rand_byte());
    }
}

static std::string get_uuid()
{
    char uuid[33]; uuid[32] = 0;
#if defined USE_IDBFS || !defined __EMSCRIPTEN__
    std::string uuidFile = UUID_FILE;
    //uuidFile += "." + GAME::Name; // different UUIDs for different games?
    FILE* f = fopen(uuidFile.c_str(), "rb");
    size_t n = 0;
    if (f) {
        n = fread(uuid, 1, 32, f);
        fclose(f);
    }
    if (!f || n < 32) {
        init_rng();
        make_uuid(uuid);
        f = fopen(UUID_FILE, "wb");
        if (f) {
            n = fwrite(uuid, 1, 32, f);
            fclose(f);
            #ifdef __EMSCRIPTEN__
            EM_ASM(
                FS.syncfs(function (err) {});
            );
            #endif
        }
        if (!f || n < 32) {
            printf("Could not write persistant UUID!\n");
        }
    }
    f = nullptr;
#else
    #error TODO: implement localStorage API
#endif
    return uuid;
}

#endif // _UUID_H
