#include "Crc16.h"
#include "Validation.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <limits>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#ifdef _WIN32
#define NOMINMAX
#include <Windows.h>
#endif

namespace {
using Clock = std::chrono::steady_clock;
volatile std::uint64_t benchmarkSink = 0;
struct Context { crc16::Table direct; crc16::Table reflected; };
using Function = std::uint16_t (*)(const crc16::Message&, const Context&);
struct Algorithm { const char* name; Function run; bool usesTable; };

const std::array<Algorithm, 5> algorithms{{
    {"Простий послідовний", [](const crc16::Message& m, const Context&) {
        return crc16::sequential(m.data(), m.size()); }, false},
    {"Прямий табличний", [](const crc16::Message& m, const Context& c) {
        return crc16::tableDirect(m.data(), m.size(), c.direct); }, true},
    {"Дзеркальний послідовний", [](const crc16::Message& m, const Context&) {
        return crc16::reflectedSequential(m.data(), m.size()); }, false},
    {"Дзеркальний табличний", [](const crc16::Message& m, const Context& c) {
        return crc16::reflectedTable(m.data(), m.size(), c.reflected); }, true},
    {"CRC-16/CCITT-FALSE", [](const crc16::Message& m, const Context&) {
        return crc16::ccittFalse(m.data(), m.size()); }, false}
}};

std::string hex16(std::uint16_t value) {
    std::ostringstream out;
    out << "0x" << std::uppercase << std::hex << std::setw(4) << std::setfill('0') << value;
    return out.str();
}

crc16::Message generate(std::mt19937& rng) {
    crc16::Message result{};
    // Each of 1000 bits is sampled separately, then packed into 125 bytes.
    // mt19937 output and masking give reproducibility for a supplied seed.
    for (auto& byte : result)
        for (unsigned bit = 0; bit < 8; ++bit)
            byte = static_cast<std::uint8_t>((byte << 1) | (rng() & 1u));
    return result;
}

void showMessage(const crc16::Message& m, const Context& context) {
    std::cout << "\nВхідне повідомлення: K = 1000 біт = 125 байтів\n";
    for (std::size_t byte = 0; byte < m.size(); ++byte) {
        for (int bit = 7; bit >= 0; --bit) std::cout << ((m[byte] >> bit) & 1u);
        if ((byte + 1) % 10 == 0 || byte + 1 == m.size()) std::cout << '\n';
    }
    std::cout << "\nКонтрольні суми для цього повідомлення:\n";
    for (const auto& a : algorithms) {
        const auto result = a.run(m, context);
        std::cout << a.name << ": " << hex16(result) << "  (";
        for (int bit = 15; bit >= 0; --bit) std::cout << ((result >> bit) & 1u);
        std::cout << ")\n";
    }
    std::cout << "Пряма пара має однакові CRC, дзеркальна пара також має однакові CRC.\n"
        "Між парами CRC можуть відрізнятися: порядок бітів у байті різний.\n"
        "CCITT-FALSE додатково відрізняється початковим регістром 0xFFFF.\n";
}

double measureTable(bool reflected) {
    constexpr unsigned repeats = 2000;
    std::uint64_t checksum = 0;
    const auto start = Clock::now();
    for (unsigned i = 0; i < repeats; ++i) {
        const auto table = reflected ? crc16::makeReflectedTable() : crc16::makeDirectTable();
        checksum += table[i % table.size()];
    }
    const auto end = Clock::now();
    benchmarkSink = checksum;
    return std::chrono::duration<double, std::micro>(end-start).count() / repeats;
}

void benchmark(std::size_t count, unsigned rounds, std::mt19937& rng, const Context& context) {
    std::vector<crc16::Message> messages(count);
    for (auto& m : messages) m = generate(rng);
    // Validate every benchmark input OUTSIDE timed regions.
    for (const auto& m : messages) {
        if (algorithms[0].run(m,context) != algorithms[1].run(m,context) ||
            algorithms[2].run(m,context) != algorithms[3].run(m,context))
            throw std::runtime_error("CRC mismatch on benchmark data");
    }
    for (const auto& a : algorithms) {
        std::uint64_t checksum = 0;
        for (std::size_t i = 0; i < std::min<std::size_t>(count, 500); ++i)
            checksum += a.run(messages[i], context);
        benchmarkSink = checksum;
    }
    std::array<std::vector<double>, 5> measurements;
    std::array<std::uint64_t, 5> checksums{};
    std::cout << "\nЕксперимент: " << count << " випадкових повідомлень, " << rounds
        << " раундів; " << count * rounds << " обчислень кожним алгоритмом.\n"
        "Дані для всіх алгоритмів однакові. Генерація, перевірки, друк\n"
        "і побудова таблиць не входять до часу обчислення CRC.\n";
    for (unsigned round = 0; round < rounds; ++round) {
        // Rotate the order to reduce systematic first/last algorithm bias.
        for (std::size_t step = 0; step < algorithms.size(); ++step) {
            const std::size_t index = (round + step) % algorithms.size();
            const auto function = algorithms[index].run;
            std::uint64_t checksum = 0;
            const auto start = Clock::now();
            for (const auto& m : messages) checksum += function(m, context);
            const auto end = Clock::now();
            benchmarkSink = checksum; // Observable result prevents dead-code removal.
            checksums[index] = checksum;
            measurements[index].push_back(std::chrono::duration<double, std::micro>(end-start).count()
                / static_cast<double>(count));
        }
    }
    const double directSetup = measureTable(false), reflectedSetup = measureTable(true);
    double base = 0;
    for (double t : measurements[0]) base += t;
    base /= rounds;
    std::cout << std::fixed << std::setprecision(4);
    double fastestTime = std::numeric_limits<double>::max();
    std::size_t fastest = 0;
    for (std::size_t i = 0; i < algorithms.size(); ++i) {
        double mean = 0;
        for (double t : measurements[i]) mean += t;
        mean /= rounds;
        const auto bounds = std::minmax_element(measurements[i].begin(), measurements[i].end());
        std::cout << '\n' << algorithms[i].name << '\n'
            << "  Середній час одного CRC: " << mean << " мкс\n"
            << "  Мін./макс. середній час раунду: " << *bounds.first << " / "
            << *bounds.second << " мкс\n"
            << "  Сумарний час усіх раундів: " << mean * static_cast<double>(count) * rounds / 1000.0
            << " мс\n" << "  Відношення часу простого алгоритму до цього: " << base / mean << '\n';
        if (algorithms[i].usesTable) {
            const double setup = i == 1 ? directSetup : reflectedSetup;
            std::cout << "  Окремо побудова таблиці (середнє з 2000): " << setup << " мкс\n"
                << "  Час/CRC з амортизацією побудови на " << count * rounds << " викликів: "
                << mean + setup / (static_cast<double>(count) * rounds) << " мкс\n";
        }
        std::cout << "  Контроль накопичення результатів: " << checksums[i] << '\n';
        if (mean < fastestTime) { fastestTime = mean; fastest = i; }
    }
    std::cout << "\nНайменший середній час у цьому запуску: " << algorithms[fastest].name
        << " (" << fastestTime << " мкс).\n"
        "Час залежить від комп'ютера, оптимізації та навантаження ОС.\n";
}

void showTheory() {
    std::cout << "\nРЕСУРСНА СКЛАДНІСТЬ\n"
        "K — кількість бітів; W=16; B=K/8=125; таблиця має 256 елементів.\n"
        "Простий послідовний: K+W = 1016 бітових кроків; час Theta(K+W).\n"
        "Прямий табличний: B = 125 табличних кроків; час Theta(K/8).\n"
        "Дзеркальний послідовний: K = 1000 бітових кроків; час Theta(K).\n"
        "Дзеркальний табличний: B = 125 табличних кроків; час Theta(K/8).\n"
        "CCITT-FALSE: K = 1000 бітових кроків; час Theta(K).\n"
        "За фіксованих W і розміру байта всі п'ять алгоритмів мають Theta(K).\n"
        "Бітовий і табличний кроки мають різний склад операцій.\n"
        "Одна таблиця: 256*2 = 512 байтів; побудова: 256*8 = 2048 кроків.\n"
        "Дві таблиці програми: 1024 байти. Регістр одного алгоритму: 2 байти.\n"
        "Додаткова пам'ять кожного алгоритму за фіксованих W і байта — O(1);\n"
        "послідовні не мають таблиць; вхід (125 байтів) враховується окремо.\n"
        "Тестовий набір із E повідомлень займає 125*E байтів даних.\n"
        "CRC працює в GF(2): додавання і віднімання — XOR, переносів немає.\n"
        "Для прямої пари R(x) = M(x)*x^16 mod G(x).\n"
        "Кодове слово M(x)*x^16 + R(x) ділиться на G(x) без залишку.\n"
        "G(x) виявляє одиничні помилки, усі непарні кількості помилкових\n"
        "бітів (має множник x+1), усі пакети помилок довжиною до 16 бітів.\n"
        "Не всі багатобітові помилки виявляються; CRC не є криптозахистом.\n";
}

unsigned long long parseNumber(const std::string& value) {
    if (value.empty() || value.find_first_not_of("0123456789") != std::string::npos)
        throw std::runtime_error("Очікується невід'ємне ціле число.");
    return std::stoull(value);
}
}

int main(int argc, char* argv[]) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif
    try {
        std::size_t experiments = 10000;
        unsigned rounds = 5;
        std::uint32_t seed = std::random_device{}();
        bool testsOnly = false;
        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i];
            if (arg == "--test") { testsOnly = true; continue; }
            if (arg == "--help") {
                std::cout << "CRC_Lab4_Variant14 [--experiments 10000] [--rounds 5] [--seed 14] [--test]\n"
                    "Без параметрів: перевірки, повідомлення, CRC, експерименти та аналіз.\n"
                    "--test: виконати лише перевірки.\n";
                return 0;
            }
            if (arg != "--experiments" && arg != "--rounds" && arg != "--seed")
                throw std::runtime_error("Невідомий параметр. Скористайтеся --help.");
            if (++i >= argc) throw std::runtime_error("Після параметра потрібне число.");
            const auto value = parseNumber(argv[i]);
            if (arg == "--experiments") {
                if (value < 1 || value > 1000000) throw std::runtime_error("Кількість експериментів: 1..1000000.");
                experiments = static_cast<std::size_t>(value);
            } else if (arg == "--rounds") {
                if (value < 1 || value > 100) throw std::runtime_error("Кількість раундів: 1..100.");
                rounds = static_cast<unsigned>(value);
            } else {
                if (value > 0xFFFFFFFFull) throw std::runtime_error("Seed: 0..4294967295.");
                seed = static_cast<std::uint32_t>(value);
            }
        }
        std::cout << "Лабораторна робота 4. Алгоритми CRC. Варіант 14\n"
            "G(x) = x^16 + x^12 + x^5 + 1\n"
            "Повний поліном: 0x11021; без старшого біта: 0x1021; дзеркальний: 0x8408.\n";
#ifdef _DEBUG
        std::cout << "УВАГА: конфігурація Debug. Для вимірювання часу виберіть Release | x64.\n";
#endif
        runValidation();
        if (testsOnly) return 0;
        std::cout << "Seed генератора: " << seed << '\n';
        const Context context{crc16::makeDirectTable(), crc16::makeReflectedTable()};
        std::mt19937 rng(seed);
        showMessage(generate(rng), context);
        benchmark(experiments, rounds, rng, context);
        showTheory();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Помилка: " << error.what() << '\n';
        return 1;
    }
}
