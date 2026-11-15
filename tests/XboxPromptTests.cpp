#include "xbox_prompt_pixels.h"
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
    CHECK(IconForCode(0) == -1 && IconForCode(31) == -1);
    CHECK(IconForCode(15) == IconForCode(1));
    CHECK(IconForCode(16) == IconForCode(3));
    // Supplied rage ordering differs from the game's token ordering.
    CHECK(IconForCode(17) == 13 && IconForCode(18) == 15);
    CHECK(IconForCode(19) == 16 && IconForCode(20) == 14);
    // Optional actual font TGA inputs exercise the production decoder without
    // storing game assets in this repository.
    for (int i = 1; i < argc; ++i) {
        std::ifstream file(argv[i], std::ios::binary);
        CHECK(file.good());
        std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)), {});
        CHECK(DecodeTga(bytes.data(), bytes.size(), w, h, pixels));
        std::printf("Decoded %s: %ux%u\n", argv[i], w, h);
    }
    std::puts("Xbox prompt decoder/mapping checks passed");
    return 0;
}
