// Exact fractions of a millisecond.
//
// tools/interp/timeline.py holds every sample time as a Python Fraction so no
// rounding creeps into the plan. At 60 frames a second one step is 50/3 ms,
// which no binary float holds exactly, and the checks add the durations up and
// expect the total to equal end_ms on the nose. This is the same arithmetic in
// C++: a normalized numerator and denominator over 64-bit integers, with
// 128-bit intermediates so a multiply cannot overflow on the way.
#pragma once

#include <cstdint>
#include <numeric>
#include <stdexcept>

namespace swchess::interp {

class Fraction {
  public:
    Fraction() = default;
    Fraction(std::int64_t whole) : numerator_(whole), denominator_(1) {}
    Fraction(std::int64_t numerator, std::int64_t denominator) {
        if (denominator == 0) {
            throw std::runtime_error("a fraction cannot have a denominator of zero");
        }
        numerator_ = numerator;
        denominator_ = denominator;
        normalize();
    }

    std::int64_t numerator() const { return numerator_; }
    std::int64_t denominator() const { return denominator_; }

    double toDouble() const {
        return static_cast<double>(numerator_) / static_cast<double>(denominator_);
    }

    Fraction operator+(const Fraction& other) const {
        return make(cross(numerator_, other.denominator_) + cross(other.numerator_, denominator_),
                    cross(denominator_, other.denominator_));
    }
    Fraction operator-(const Fraction& other) const {
        return make(cross(numerator_, other.denominator_) - cross(other.numerator_, denominator_),
                    cross(denominator_, other.denominator_));
    }
    Fraction operator*(const Fraction& other) const {
        return make(cross(numerator_, other.numerator_), cross(denominator_, other.denominator_));
    }
    Fraction operator/(const Fraction& other) const {
        if (other.numerator_ == 0) {
            throw std::runtime_error("cannot divide a fraction by zero");
        }
        return make(cross(numerator_, other.denominator_), cross(denominator_, other.numerator_));
    }

    bool operator<(const Fraction& other) const {
        return cross(numerator_, other.denominator_) < cross(other.numerator_, denominator_);
    }
    bool operator<=(const Fraction& other) const { return !(other < *this); }
    bool operator>(const Fraction& other) const { return other < *this; }
    bool operator>=(const Fraction& other) const { return !(*this < other); }
    bool operator==(const Fraction& other) const {
        return numerator_ == other.numerator_ && denominator_ == other.denominator_;
    }
    bool operator!=(const Fraction& other) const { return !(*this == other); }

    // The smallest whole number that is not below this fraction.
    std::int64_t ceiling() const {
        std::int64_t whole = numerator_ / denominator_;
        if (numerator_ % denominator_ > 0) {
            ++whole;
        }
        return whole;
    }

    // Rounds to six decimal places, half away from zero, the way
    // tools/interp/pipeline.py's _round6 rounds a time before writing it.
    Fraction roundTo6() const {
        Fraction scaled = *this * Fraction(1000000, 1);
        __int128 doubled = static_cast<__int128>(scaled.numerator_) * 2 + scaled.denominator_;
        __int128 divisor = static_cast<__int128>(scaled.denominator_) * 2;
        // Python's // floors, and every time here is at or above zero.
        __int128 whole = doubled / divisor;
        if ((doubled % divisor != 0) && ((doubled < 0) != (divisor < 0))) {
            --whole;
        }
        return make(whole, 1000000);
    }

  private:
    static __int128 cross(std::int64_t a, std::int64_t b) {
        return static_cast<__int128>(a) * static_cast<__int128>(b);
    }

    static Fraction make(__int128 numerator, __int128 denominator) {
        Fraction out;
        if (denominator < 0) {
            numerator = -numerator;
            denominator = -denominator;
        }
        __int128 a = numerator < 0 ? -numerator : numerator;
        __int128 b = denominator;
        while (b != 0) {
            __int128 t = a % b;
            a = b;
            b = t;
        }
        if (a > 1) {
            numerator /= a;
            denominator /= a;
        }
        if (numerator > INT64_MAX || numerator < INT64_MIN || denominator > INT64_MAX) {
            throw std::runtime_error("a fraction grew past what 64 bits hold");
        }
        out.numerator_ = static_cast<std::int64_t>(numerator);
        out.denominator_ = static_cast<std::int64_t>(denominator);
        return out;
    }

    void normalize() {
        *this = make(numerator_, denominator_);
    }

    std::int64_t numerator_ = 0;
    std::int64_t denominator_ = 1;
};

}  // namespace swchess::interp
