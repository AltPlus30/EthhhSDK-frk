#include "button_prompt_pixels.h"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); return 1; } } while (0)

int main(int argc, char** argv)
{
    using namespace eth::prompts;
    unsigned w, h;
    std::vector<uint32_t> pixels;
    // Short, offset palette, BGRA alpha, bottom/right origin, two rows.
    std::vector<uint8_t> tga(18, 0);
    tga[1] = 1; tga[2] = 1; tga[3] = 7; tga[5] = 2; tga[7] = 32;
    tga[12] = 2; tga[14] = 2; tga[16] = 8; tga[17] = 0x10;
    tga.insert(tga.end(), {1,2,3,4, 5,6,7,8, 7,8,8,7});
    CHECK(DecodeTga(tga.data(), tga.size(), w, h, pixels));
    CHECK(w == 2 && h == 2);
    CHECK(pixels == std::vector<uint32_t>({0x04030201,0x08070605,0x08070605,0x04030201}));
    for (size_t length = 0; length < tga.size(); ++length)
        CHECK(!DecodeTga(tga.data(), length, w, h, pixels));
    tga.back() = 9; // A palette index outside a short palette must be rejected.
    CHECK(!DecodeTga(tga.data(), tga.size(), w, h, pixels));
    tga.assign(18, 0);
    tga[2] = 2; tga[12] = 1; tga[14] = 2; tga[16] = 24; tga[17] = 0x20;
    tga.insert(tga.end(), {1,2,3,4,5,6});
    CHECK(DecodeTga(tga.data(), tga.size(), w, h, pixels));
    CHECK(pixels == std::vector<uint32_t>({0xff030201,0xff060504}));
    std::vector<Region> regions{{1,0,0,32,24,34}, {15,0,0,32,24,34}};
    CHECK(ValidateRegions(256, 96, regions));
    CHECK(!ValidateRegions(256, 96, {}));
    auto invalid = regions;
    invalid[1].code = 1; CHECK(!ValidateRegions(256, 96, invalid));
    invalid = regions; invalid[0].code = 0; CHECK(!ValidateRegions(256, 96, invalid));
    invalid[0].code = 31; CHECK(!ValidateRegions(256, 96, invalid));
    invalid = regions; invalid[0].width = 129; CHECK(!ValidateRegions(256, 96, invalid));
    invalid = regions; invalid[0].advance = 0; CHECK(!ValidateRegions(256, 96, invalid));
    invalid = regions; invalid[0].x = 250; CHECK(!ValidateRegions(256, 96, invalid));
    invalid[0].x = UINT32_MAX; CHECK(!ValidateRegions(256, 96, invalid));
    invalid = regions; invalid[0].y = 90; CHECK(!ValidateRegions(256, 96, invalid));
    // Custom system: a different rectangle/advance for any non-newline code.
    CHECK(ValidateRegions(64, 64, {{30,1,2,40,16,42}}));
    // Optional actual font TGA inputs exercise the production decoder without
    // storing game assets in this repository.
    for (int i = 1; i < argc; ++i) {
        std::ifstream file(argv[i], std::ios::binary);
        CHECK(file.good());
        std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)), {});
        CHECK(DecodeTga(bytes.data(), bytes.size(), w, h, pixels));
        std::printf("Decoded %s: %ux%u\n", argv[i], w, h);
    }
    std::puts("Button prompt decoder/region validation checks passed");
    return 0;
}
