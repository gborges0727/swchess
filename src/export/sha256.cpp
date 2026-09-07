#include "export/sha256.h"

#include <cstdio>
#include <cstring>
#include <stdexcept>

namespace swchess::exporter {
namespace {

const std::uint32_t kRoundConstants[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u,
    0x923f82a4u, 0xab1c5ed5u, 0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
    0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u, 0xe49b69c1u, 0xefbe4786u,
    0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u,
    0x06ca6351u, 0x14292967u, 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
    0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u, 0xa2bfe8a1u, 0xa81a664bu,
    0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au,
    0x5b9cca4fu, 0x682e6ff3u, 0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
    0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u};

std::uint32_t rotateRight(std::uint32_t value, int bits) {
    return (value >> bits) | (value << (32 - bits));
}

// Holds the running hash between blocks so a large file can stream through.
struct Sha256State {
    std::uint32_t hash[8] = {0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
                             0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u};
    std::uint8_t buffer[64] = {};
    std::size_t buffered = 0;
    std::uint64_t total = 0;

    void compress(const std::uint8_t* block) {
        std::uint32_t w[64];
        for (int i = 0; i < 16; ++i) {
            w[i] = (static_cast<std::uint32_t>(block[i * 4]) << 24) |
                   (static_cast<std::uint32_t>(block[i * 4 + 1]) << 16) |
                   (static_cast<std::uint32_t>(block[i * 4 + 2]) << 8) |
                   static_cast<std::uint32_t>(block[i * 4 + 3]);
        }
        for (int i = 16; i < 64; ++i) {
            std::uint32_t s0 = rotateRight(w[i - 15], 7) ^ rotateRight(w[i - 15], 18) ^
                               (w[i - 15] >> 3);
            std::uint32_t s1 = rotateRight(w[i - 2], 17) ^ rotateRight(w[i - 2], 19) ^
                               (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        std::uint32_t a = hash[0], b = hash[1], c = hash[2], d = hash[3];
        std::uint32_t e = hash[4], f = hash[5], g = hash[6], h = hash[7];
        for (int i = 0; i < 64; ++i) {
            std::uint32_t s1 = rotateRight(e, 6) ^ rotateRight(e, 11) ^ rotateRight(e, 25);
            std::uint32_t choice = (e & f) ^ (~e & g);
            std::uint32_t temp1 = h + s1 + choice + kRoundConstants[i] + w[i];
            std::uint32_t s0 = rotateRight(a, 2) ^ rotateRight(a, 13) ^ rotateRight(a, 22);
            std::uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
            std::uint32_t temp2 = s0 + majority;
            h = g; g = f; f = e; e = d + temp1;
            d = c; c = b; b = a; a = temp1 + temp2;
        }
        hash[0] += a; hash[1] += b; hash[2] += c; hash[3] += d;
        hash[4] += e; hash[5] += f; hash[6] += g; hash[7] += h;
    }

    void update(const std::uint8_t* data, std::size_t size) {
        total += size;
        while (size > 0) {
            std::size_t room = 64 - buffered;
            std::size_t take = size < room ? size : room;
            std::memcpy(buffer + buffered, data, take);
            buffered += take;
            data += take;
            size -= take;
            if (buffered == 64) {
                compress(buffer);
                buffered = 0;
            }
        }
    }

    std::string finish() {
        std::uint64_t bits = total * 8;
        std::uint8_t one = 0x80;
        update(&one, 1);
        total -= 1;
        std::uint8_t zero = 0;
        while (buffered != 56) {
            update(&zero, 1);
            total -= 1;
        }
        std::uint8_t tail[8];
        for (int i = 0; i < 8; ++i) {
            tail[i] = static_cast<std::uint8_t>(bits >> (56 - i * 8));
        }
        update(tail, 8);

        static const char* digits = "0123456789abcdef";
        std::string out;
        out.reserve(64);
        for (int i = 0; i < 8; ++i) {
            for (int shift = 28; shift >= 0; shift -= 4) {
                out += digits[(hash[i] >> shift) & 0xF];
            }
        }
        return out;
    }
};

}  // namespace

std::string sha256Hex(const std::uint8_t* data, std::size_t size) {
    Sha256State state;
    state.update(data, size);
    return state.finish();
}

std::string sha256File(const std::string& path) {
    std::FILE* file = std::fopen(path.c_str(), "rb");
    if (file == nullptr) {
        throw std::runtime_error("cannot open " + path);
    }
    Sha256State state;
    std::vector<std::uint8_t> block(1u << 20);
    while (true) {
        std::size_t got = std::fread(block.data(), 1, block.size(), file);
        if (got > 0) {
            state.update(block.data(), got);
        }
        if (got < block.size()) {
            break;
        }
    }
    bool failed = std::ferror(file) != 0;
    std::fclose(file);
    if (failed) {
        throw std::runtime_error("cannot read " + path);
    }
    return state.finish();
}

}  // namespace swchess::exporter
