#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace crc16 {
constexpr std::uint16_t Polynomial = 0x1021;
constexpr std::uint16_t ReflectedPolynomial = 0x8408;
constexpr std::size_t MessageBits = 1000;
constexpr std::size_t MessageBytes = MessageBits / 8;
using Message = std::array<std::uint8_t, MessageBytes>;
using Table = std::array<std::uint16_t, 256>;

std::uint8_t reflect8(std::uint8_t value);
std::uint16_t reflect16(std::uint16_t value);
Table makeDirectTable();
Table makeReflectedTable();

// Literal division of M(x)*x^16: bits enter the low end of the register,
// then 16 zero bits are processed. init=0, xorout=0, MSB first.
std::uint16_t sequential(const std::uint8_t* data, std::size_t size);
// Equivalent byte recurrence; multiplication by x^16 is already implicit.
std::uint16_t tableDirect(const std::uint8_t* data, std::size_t size, const Table& table);
// LSB first within EACH byte; bytes remain in their original order.
// Right shifts, reflected polynomial, implicit augmentation.
std::uint16_t reflectedSequential(const std::uint8_t* data, std::size_t size);
std::uint16_t reflectedTable(const std::uint8_t* data, std::size_t size, const Table& table);
// Standard parameters from table 4.1: init=FFFF, refin=false,
// refout=false, xorout=0000; check("123456789")=29B1.
std::uint16_t ccittFalse(const std::uint8_t* data, std::size_t size);
}
