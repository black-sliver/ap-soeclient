#include "som.hpp"
#include <cstdint>
#include <utility>


static std::map<uint32_t, std::vector<std::pair<uint8_t, unsigned> > >
makeByteLocations(const std::vector<std::pair<std::pair<uint8_t, uint8_t>, unsigned>>& locations)
{
    // flag, value -> id => addr -> {value, id}
    std::map<uint32_t, std::vector<std::pair<uint8_t, unsigned> > > res;
    for (const auto& pair: locations) {
        const uint32_t addr = 0x7ecf00u + pair.first.first;
        const uint8_t value = pair.first.second;
        const unsigned id = pair.second;
        res[addr].emplace_back(std::make_pair(value, id));
    }
    return res;
}

std::map<uint32_t, std::vector<std::pair<uint8_t, unsigned> > > SoM::_byteLocations = makeByteLocations({
#include "locations.inc"
});

std::map<int, std::pair<uint16_t, uint16_t> > SoM::_items = {
#include "items.inc"
};
