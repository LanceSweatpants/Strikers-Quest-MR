#include "port/guest_validation.h"
#include <vector>
#include <stdio.h>
#include <limits>

static std::vector<unsigned char> fixture(bool v2)
{
    std::vector<unsigned char> b;
    auto word = [&](uint32_t v) {
        for (int i = 0; i < 4; i++) b.push_back((unsigned char)(v >> (i * 8)));
    };
    auto zero = [&](size_t n) { b.resize(b.size() + n, 0); };
    const char* magic = v2 ? "GPK2" : "GPK1";
    b.insert(b.end(), magic, magic + 4);
    word(v2 ? 2 : 1); word(1); word(1); word(1); word(1);
    word(0x3f800000); zero(32);
    if (v2) word(1);
    word(1); word(UINT32_MAX); zero(12); word(1);
    for (int i = 0; i < 16; i++) word(i % 5 == 0 ? 0x3f800000 : 0);
    if (v2) {
        word(0);
        for (int i = 0; i < 16; i++) word(i % 5 == 0 ? 0x3f800000 : 0);
    }
    word(1); word(33); zero(36); // odd texture payload plus alignment
    word(1); word(1); word(3); zero(52); zero(8);
    zero(48); word(1); zero(28); zero(4); // one animation plus hasTrans/padding
    return b;
}

int main()
{
    for (bool v2 : {false, true}) {
        auto b = fixture(v2);
        if (!portguest::valid_pack(b.data(), b.size())) return 1;
        for (size_t n = 0; n < b.size(); n++)
            if (portguest::valid_pack(b.data(), n)) return 2;
        for (size_t off : {8u, 12u, 16u, 20u}) {
            auto bad = b;
            for (size_t i = 0; i < 4; i++) bad[off + i] = 255;
            if (portguest::valid_pack(bad.data(), bad.size())) return 3;
        }
        b.push_back(0);
        if (portguest::valid_pack(b.data(), b.size())) return 4;
    }
    if (portguest::valid_stat(std::numeric_limits<float>::infinity(), false, 100)
        || portguest::valid_stat(std::numeric_limits<float>::quiet_NaN(), false, 100)
        || portguest::valid_stat(-1, false, 100)
        || portguest::valid_stat(0, true, 10)
        || portguest::valid_stat(1.1f, false, 1)
        || !portguest::valid_stat(0.3f, true, 10)
        || !portguest::valid_stat(0, false, 100)) return 5;
    puts("guest validation: truncations, counts and stat bounds passed");
    return 0;
}
