#ifndef PORT_GUEST_VALIDATION_H
#define PORT_GUEST_VALIDATION_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>

// No engine dependencies: the exact production parser checks can run on the host.
namespace portguest
{
static const size_t kMaxPackBytes = 256u * 1024u * 1024u;

inline bool finite_float(float value)
{
    uint32_t bits;
    memcpy(&bits, &value, sizeof bits);
    return (bits & 0x7f800000u) != 0x7f800000u;
}

inline bool valid_stat(float value, bool positive, float maximum)
{
    return finite_float(value) && (positive ? value > 0 : value >= 0) && value <= maximum;
}

struct PackReader
{
    const unsigned char* data;
    size_t size;
    size_t offset;
    bool skip(size_t count)
    {
        if (count > size - offset)
            return false;
        offset += count;
        return true;
    }
    bool word(uint32_t& value)
    {
        if (!skip(4))
            return false;
        const unsigned char* p = data + offset - 4;
        value = (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
        return true;
    }
    bool floats(size_t count)
    {
        if (count > (size - offset) / 4)
            return false;
        for (size_t i = 0; i < count; i++)
        {
            uint32_t bits;
            if (!word(bits) || (bits & 0x7f800000u) == 0x7f800000u)
                return false;
        }
        return true;
    }
    bool padded(size_t count)
    {
        return skip(count) && skip((4 - (count & 3)) & 3);
    }
};

inline bool valid_pack(const unsigned char* data, size_t size)
{
    if (size < 60 || size > kMaxPackBytes)
        return false;
    const bool v2 = memcmp(data, "GPK2", 4) == 0;
    if (!v2 && memcmp(data, "GPK1", 4) != 0)
        return false;
    PackReader r = {data, size, 4};
    uint32_t version, nodes, packets, textures, anims;
    if (!r.word(version) || version != (v2 ? 2u : 1u) || !r.word(nodes) || !r.word(packets)
        || !r.word(textures) || !r.word(anims) || !r.floats(1) || !r.skip(32))
        return false;
    if (nodes == 0 || nodes > 160 || packets > 4096 || textures > 4096 || anims > 4096)
        return false;
    uint32_t tmp;
    // r73 fix: the pack's mode bits since r69 (1 retarget, 2 flies, 4 floats): up to 7. This said "> 1"
    // and turned away every flying / floating Pokemon (16 of the 70: Gastly, Scyther, Charizard ...).
    if (v2 && (!r.word(tmp) || tmp > 7))
        return false;
    for (uint32_t i = 0; i < nodes; i++)
    {
        uint32_t parent;
        if (!r.word(tmp) || !r.word(parent) || (parent != UINT32_MAX && parent >= i)
            || !r.floats(3) || !r.word(tmp) || tmp > 1 || !r.floats(16))
            return false;
        if (v2 && (!r.word(tmp) || !r.floats(16)))
            return false;
    }
    for (uint32_t i = 0; i < textures; i++)
    {
        uint32_t bytes;
        if (!r.word(tmp) || !r.word(bytes) || bytes < 32 || !r.padded(bytes))
            return false;
    }
    size_t vertexTotal = 0;
    for (uint32_t i = 0; i < packets; i++)
    {
        uint32_t vertices, indices;
        if (!r.word(tmp) || !r.word(vertices) || !r.word(indices))
            return false;
        // r73 fix: the vertex count's top four bits are the packet's flags (r69: 0x80000000 soft, the
        // gas puffs; r73: 0x40000000 flame + its frame in 0x30000000), not part of the count
        vertices &= 0x0FFFFFFFu;
        if (vertices > 65536 || indices > 65535 || (indices != 0 && vertices == 0))
            return false;
        vertexTotal += vertices;
        // Bound CPU skinning and the persistent expanded vertex arrays too.
        if (vertexTotal > 1000000 || vertices > (r.size - r.offset) / 52)
            return false;
        for (uint32_t j = 0; j < vertices; j++)
            if (!r.floats(8) || !r.skip(4) || !r.floats(4))
                return false;
        if (!r.padded((size_t)indices * 2))
            return false;
    }
    for (uint32_t i = 0; i < anims; i++)
    {
        uint32_t keys;
        if (!r.skip(48) || !r.word(keys) || keys == 0 || keys > (r.size - r.offset) / (nodes * 28u))
            return false;
        if (!r.floats((size_t)keys * nodes * 7) || !r.padded(nodes))
            return false;
    }
    return r.offset == r.size;
}
}
#endif
