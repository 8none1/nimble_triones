#include <string>
#include <unordered_map>

// NB lower case names

std::unordered_map<std::string, uint> mode_map = {
    {"seven color cross fade", 37},
    {"red gradual change", 38},
    {"green gradual change", 39},
    {"blue gradual change", 40},
    {"yellow gradual change", 41},
    {"cyan gradual change", 42},
    {"purple gradual change", 43},
    {"white gradual change", 44},
    {"red green cross fade", 45},
    {"red blue cross fade", 46},
    {"green blue cross fade", 47},
    {"seven colour strobe flash", 48},
    {"red strobe flash", 49},
    {"green strobe flash", 50},
    {"blue strobe flash", 51},
    {"yellow strobe flash", 52},
    {"cyan strobe flash", 53},
    {"purple strobe flash", 54},
    {"white strobe flash", 55},
    {"seven color jumping change", 56},
    // # 65 : 0x41: Looks like this might be solid colour as set by remote control, you cant set this it only shows in the status.
    {"rgb fade", 97},
    {"rgb cycle", 98}
};

uint8_t get_mode_id(std::string mode_name) {
    return mode_map[mode_name];
}