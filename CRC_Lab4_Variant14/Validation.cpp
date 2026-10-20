#include "Validation.h"
#include "Crc16.h"
#include <algorithm>
#include <iostream>
#include <random>
#include <stdexcept>
#include <vector>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

// Independent reference: long division in GF(2) on an explicit bit array.
// Unlike the production routines this has no 16-bit shift register or table.
std::uint16_t divideBits(std::vector<unsigned> bits, bool augment) {
    if (augment) bits.resize(bits.size() + 16, 0);
    constexpr unsigned generator[17] = {1,0,0,0,1,0,0,0,0,0,0,1,0,0,0,0,1};
    for (std::size_t i = 0; i + 16 < bits.size(); ++i) {
        if (bits[i]) {
            for (std::size_t j = 0; j < 17; ++j) bits[i + j] ^= generator[j];
        }
    }
    std::uint16_t result = 0;
    const std::size_t start = bits.size() > 16 ? bits.size() - 16 : 0;
    for (std::size_t i = start; i < bits.size(); ++i)
        result = static_cast<std::uint16_t>((result << 1) | bits[i]);
    return result;
}

std::vector<unsigned> toBits(const std::vector<std::uint8_t>& bytes, bool lsbFirst) {
    std::vector<unsigned> result;
    for (auto byte : bytes)
        for (unsigned i = 0; i < 8; ++i)
            result.push_back((byte >> (lsbFirst ? i : 7 - i)) & 1u);
    return result;
}
}

void runValidation() {
    using namespace crc16;
    const Table direct = makeDirectTable(), mirror = makeReflectedTable();
    const std::vector<std::uint8_t> check{'1','2','3','4','5','6','7','8','9'};
    require(sequential(check.data(), check.size()) == 0x31C3, "XMODEM check failed");
    require(reflectedSequential(check.data(), check.size()) == 0x2189, "KERMIT check failed");
    require(ccittFalse(check.data(), check.size()) == 0x29B1, "CCITT-FALSE check failed");
    require(reflect16(Polynomial) == ReflectedPolynomial, "Polynomial reflection failed");
    for (unsigned i = 0; i < 256; ++i)
        require(mirror[reflect8(static_cast<std::uint8_t>(i))] == reflect16(direct[i]),
            "Table reflection failed");

    auto validate = [&](const std::vector<std::uint8_t>& bytes) {
        const auto* p = bytes.data();
        const auto n = bytes.size();
        const auto normal = divideBits(toBits(bytes, false), true);
        const auto reflected = reflect16(divideBits(toBits(bytes, true), true));
        require(sequential(p,n) == normal, "Sequential/reference mismatch");
        require(tableDirect(p,n,direct) == normal, "Direct table/reference mismatch");
        require(reflectedSequential(p,n) == reflected, "Reflected/reference mismatch");
        require(reflectedTable(p,n,mirror) == reflected, "Reflected table/reference mismatch");
        auto codeword = toBits(bytes, false);
        for (int i = 15; i >= 0; --i) codeword.push_back((normal >> i) & 1u);
        require(divideBits(codeword, false) == 0, "Nonzero codeword residue");
    };
    validate({}); validate(check);
    validate(std::vector<std::uint8_t>(MessageBytes, 0));
    validate(std::vector<std::uint8_t>(MessageBytes, 255));
    for (unsigned i = 0; i < 256; ++i) validate({static_cast<std::uint8_t>(i)});
    std::mt19937 rng(14);
    for (unsigned test = 0; test < 200; ++test) {
        // Includes non-125-byte inputs for boundary coverage of the core functions.
        std::vector<std::uint8_t> bytes(test % 2 == 0 ? MessageBytes : test);
        for (auto& byte : bytes) byte = static_cast<std::uint8_t>(rng() & 255u);
        validate(bytes);
    }
    std::vector<std::uint8_t> bytes(MessageBytes);
    for (auto& byte : bytes) byte = static_cast<std::uint8_t>(rng() & 255u);
    const auto original = sequential(bytes.data(), bytes.size());
    for (std::size_t bit = 0; bit < MessageBits; ++bit) {
        bytes[bit / 8] ^= static_cast<std::uint8_t>(1u << (7 - bit % 8));
        require(sequential(bytes.data(), bytes.size()) != original, "Single-bit error missed");
        bytes[bit / 8] ^= static_cast<std::uint8_t>(1u << (7 - bit % 8));
    }
    std::cout << "Перевірки пройдено: еталонне ділення, 460 повідомлень, таблиці,\n"
        "контрольні значення стандартів, нульовий залишок кодового слова\n"
        "та всі 1000 одиничних спотворень одного повідомлення.\n";
}
