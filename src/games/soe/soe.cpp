#include "soe.hpp"
#include <utility>
#include <inttypes.h>

static std::map<uint32_t, std::map<uint8_t, unsigned> >
makeBitLocations(const std::map<Game::bitaddr, unsigned> locations)
{
    std::map<uint32_t, std::map<uint8_t, unsigned> > res;
    for (const auto& pair: locations) {
        res[pair.first.addr()][pair.first.mask()] = pair.second;
    }
    return res;
}

std::map<uint32_t, std::map<uint8_t, unsigned> > SoE::_bitLocations = makeBitLocations({
#include "locations.inc"
});

std::map<int, std::pair<uint16_t, uint16_t> > SoE::_items = {
#include "items.inc"
};
