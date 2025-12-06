#pragma once

#include <string>
#include <random>
#include <sstream>
#include <iomanip>

namespace MoleculaEntity {

class UuidGenerator {
public:
    [[nodiscard]] static std::string generate() {
        static std::random_device rd;
        static std::mt19937_64 gen(rd());
        static std::uniform_int_distribution<uint64_t> dis;

        uint64_t ab = dis(gen);
        uint64_t cd = dis(gen);

        // Set version 4
        ab = (ab & 0xFFFFFFFFFFFF0FFFULL) | 0x0000000000004000ULL;
        // Set variant
        cd = (cd & 0x3FFFFFFFFFFFFFFFULL) | 0x8000000000000000ULL;

        std::ostringstream ss;
        ss << std::hex << std::setfill('0');
        ss << std::setw(8) << ((ab >> 32) & 0xFFFFFFFF);
        ss << '-';
        ss << std::setw(4) << ((ab >> 16) & 0xFFFF);
        ss << '-';
        ss << std::setw(4) << (ab & 0xFFFF);
        ss << '-';
        ss << std::setw(4) << ((cd >> 48) & 0xFFFF);
        ss << '-';
        ss << std::setw(12) << (cd & 0xFFFFFFFFFFFFULL);

        return ss.str();
    }
};

} // namespace MoleculaEntity
