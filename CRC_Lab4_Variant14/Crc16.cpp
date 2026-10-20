#include "Crc16.h"

namespace crc16 {
std::uint8_t reflect8(std::uint8_t value) {
    std::uint8_t result = 0;
    for (unsigned i = 0; i < 8; ++i) {
        result = static_cast<std::uint8_t>((result << 1) | (value & 1u));
        value >>= 1;
    }
    return result;
}

std::uint16_t reflect16(std::uint16_t value) {
    return static_cast<std::uint16_t>((static_cast<unsigned>(reflect8(
        static_cast<std::uint8_t>(value))) << 8) | reflect8(static_cast<std::uint8_t>(value >> 8)));
}

Table makeDirectTable() {
    Table table{};
    for (unsigned i = 0; i < table.size(); ++i) {
        auto reg = static_cast<std::uint16_t>(i << 8);
        for (unsigned bit = 0; bit < 8; ++bit) {
            const bool top = (reg & 0x8000u) != 0;
            reg = static_cast<std::uint16_t>(reg << 1);
            if (top) reg ^= Polynomial;
        }
        table[i] = reg;
    }
    return table;
}

Table makeReflectedTable() {
    Table table{};
    for (unsigned i = 0; i < table.size(); ++i) {
        auto reg = static_cast<std::uint16_t>(i);
        for (unsigned bit = 0; bit < 8; ++bit) {
            const bool low = (reg & 1u) != 0;
            reg >>= 1;
            if (low) reg ^= ReflectedPolynomial;
        }
        table[i] = reg;
    }
    return table;
}

std::uint16_t sequential(const std::uint8_t* data, std::size_t size) {
    std::uint16_t reg = 0;
    for (std::size_t i = 0; i < size; ++i) {
        for (int bit = 7; bit >= 0; --bit) {
            const bool top = (reg & 0x8000u) != 0;
            reg = static_cast<std::uint16_t>((reg << 1) | ((data[i] >> bit) & 1u));
            if (top) reg ^= Polynomial;
        }
    }
    // Virtual zeros: no temporary copy or padded input buffer is needed.
    for (unsigned bit = 0; bit < 16; ++bit) {
        const bool top = (reg & 0x8000u) != 0;
        reg = static_cast<std::uint16_t>(reg << 1);
        if (top) reg ^= Polynomial;
    }
    return reg;
}

std::uint16_t tableDirect(const std::uint8_t* data, std::size_t size, const Table& table) {
    std::uint16_t reg = 0;
    for (std::size_t i = 0; i < size; ++i) {
        const unsigned index = ((reg >> 8) ^ data[i]) & 0xFFu;
        reg = static_cast<std::uint16_t>((reg << 8) ^ table[index]);
    }
    return reg;
}

std::uint16_t reflectedSequential(const std::uint8_t* data, std::size_t size) {
    std::uint16_t reg = 0;
    for (std::size_t i = 0; i < size; ++i) {
        reg ^= data[i];
        for (unsigned bit = 0; bit < 8; ++bit) {
            const bool low = (reg & 1u) != 0;
            reg >>= 1;
            if (low) reg ^= ReflectedPolynomial;
        }
    }
    return reg;
}

std::uint16_t reflectedTable(const std::uint8_t* data, std::size_t size, const Table& table) {
    std::uint16_t reg = 0;
    for (std::size_t i = 0; i < size; ++i) {
        const unsigned index = (reg ^ data[i]) & 0xFFu;
        reg = static_cast<std::uint16_t>((reg >> 8) ^ table[index]);
    }
    return reg;
}

std::uint16_t ccittFalse(const std::uint8_t* data, std::size_t size) {
    std::uint16_t reg = 0xFFFF;
    for (std::size_t i = 0; i < size; ++i) {
        reg ^= static_cast<std::uint16_t>(static_cast<unsigned>(data[i]) << 8);
        for (unsigned bit = 0; bit < 8; ++bit) {
            const bool top = (reg & 0x8000u) != 0;
            reg = static_cast<std::uint16_t>(reg << 1);
            if (top) reg ^= Polynomial;
        }
    }
    return reg;
}
}
