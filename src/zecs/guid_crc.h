#pragma once

#include <cstdint>

namespace zecs
{

// The text that the GUID of a type is made from: the name of this function
// as the compiler prints it. The name of T is a part of it. So two types
// have the same text only if they have the same name.
template<typename T>
constexpr const char* guid_name() noexcept
{
#ifdef _MSC_VER
    return __FUNCSIG__;
#else
    return __PRETTY_FUNCTION__;
#endif
}

// CRC-32 of a text. It is slow, bit by bit, and needs no table:
// it runs only when the compiler builds the program.
consteval uint32_t guid_crc32(const char* text)
{
    // This is the usual CRC-32, the same as in zip and png.
    // The text is one long number, and the CRC is what is left when this
    // number is divided by a fixed number, the polynomial. The division
    // uses XOR in place of subtraction.
    // The start value and the last XOR are a part of the standard.
    uint32_t crc = 0xffffffff;
    for (; *text != 0; ++text)
    {
        // Put the next 8 bits of the text into the low bits.
        crc ^= static_cast<unsigned char>(*text);

        // One step of the division for each of these bits: take the low
        // bit out, and if it was 1, subtract the polynomial.
        // 0xedb88320 is the polynomial with its bits in the opposite
        // order, because the bits go out from the low end.
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ ((crc & 1) ? 0xedb88320u : 0u);
    }

    return crc ^ 0xffffffff;
}

} // namespace zecs
