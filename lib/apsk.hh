/* -*- c++ -*- */
/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef APSK_HH
#define APSK_HH

#include "modulation.hh"
#include <cstdint>
#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <limits>
#include <type_traits>

template <int NUM, typename TYPE, typename CODE>
struct AmplitudePhaseShiftKeying;

template <typename TYPE, typename CODE>
struct AmplitudePhaseShiftKeying<16, TYPE, CODE> : public Modulation<TYPE, CODE> {
    static const int NUM = 16;
    static const int BITS = 4;
    typedef TYPE complex_type;
    typedef typename TYPE::value_type value_type;
    typedef CODE code_type;

    explicit AmplitudePhaseShiftKeying(value_type outer_inner_ratio = 2.75)
    {
        const value_type r2 = std::sqrt(
            4.0 / (1.0 / (outer_inner_ratio * outer_inner_ratio) + 3.0));
        const value_type r1 = r2 / outer_inner_ratio;
        const value_type pi = std::acos(value_type(-1));

        d_points = { std::polar(r2, pi / 4),
                     std::polar(r2, -pi / 4),
                     std::polar(r2, 3 * pi / 4),
                     std::polar(r2, -3 * pi / 4),
                     std::polar(r2, pi / 12),
                     std::polar(r2, -pi / 12),
                     std::polar(r2, 11 * pi / 12),
                     std::polar(r2, -11 * pi / 12),
                     std::polar(r2, 5 * pi / 12),
                     std::polar(r2, -5 * pi / 12),
                     std::polar(r2, 7 * pi / 12),
                     std::polar(r2, -7 * pi / 12),
                     std::polar(r1, pi / 4),
                     std::polar(r1, -pi / 4),
                     std::polar(r1, 3 * pi / 4),
                     std::polar(r1, -3 * pi / 4) };
    }

    int bits() override { return BITS; }

    void hard(code_type* bits, complex_type symbol) override
    {
        const size_t index = nearest(symbol);
        for (int bit = 0; bit < BITS; bit++)
            bits[bit] = index & (1U << (BITS - bit - 1)) ? code_type(-1) : code_type(1);
    }

    void soft(code_type* bits, complex_type symbol, value_type precision) override
    {
        for (int bit = 0; bit < BITS; bit++) {
            value_type distance_zero = std::numeric_limits<value_type>::max();
            value_type distance_one = std::numeric_limits<value_type>::max();
            const size_t mask = 1U << (BITS - bit - 1);
            for (size_t index = 0; index < d_points.size(); index++) {
                const value_type distance = std::norm(symbol - d_points[index]);
                value_type& best = index & mask ? distance_one : distance_zero;
                best = std::min(best, distance);
            }
            bits[bit] = quantize(precision * (distance_one - distance_zero));
        }
    }

    complex_type map(code_type* bits) override
    {
        size_t index = 0;
        for (int bit = 0; bit < BITS; bit++)
            index = (index << 1) | (bits[bit] < 0);
        return d_points[index];
    }

private:
    static code_type quantize(value_type value)
    {
        if (std::is_integral<code_type>::value)
            value = std::nearbyint(value);
        if (std::is_same<code_type, int8_t>::value)
            value = std::min<value_type>(std::max<value_type>(value, -128), 127);
        return value;
    }

    size_t nearest(complex_type symbol) const
    {
        size_t nearest_index = 0;
        value_type nearest_distance = std::numeric_limits<value_type>::max();
        for (size_t index = 0; index < d_points.size(); index++) {
            const value_type distance = std::norm(symbol - d_points[index]);
            if (distance < nearest_distance) {
                nearest_index = index;
                nearest_distance = distance;
            }
        }
        return nearest_index;
    }

    std::array<complex_type, NUM> d_points;
};

template <typename TYPE, typename CODE>
struct AmplitudePhaseShiftKeying<32, TYPE, CODE> : public Modulation<TYPE, CODE> {
    static const int NUM = 32;
    static const int BITS = 5;
    typedef TYPE complex_type;
    typedef typename TYPE::value_type value_type;
    typedef CODE code_type;

    explicit AmplitudePhaseShiftKeying(value_type outer_inner_ratio = 5.27,
                                       value_type middle_inner_ratio = 2.84)
    {
        const value_type r3 = std::sqrt(
            8.0 / (1.0 / (outer_inner_ratio * outer_inner_ratio) +
                   3.0 * std::pow(middle_inner_ratio / outer_inner_ratio, 2) + 4.0));
        const value_type r1 = r3 / outer_inner_ratio;
        const value_type r2 = r1 * middle_inner_ratio;
        const value_type pi = std::acos(value_type(-1));

        d_points = { std::polar(r2, pi / 4),
                     std::polar(r2, 5 * pi / 12),
                     std::polar(r2, -pi / 4),
                     std::polar(r2, -5 * pi / 12),
                     std::polar(r2, 3 * pi / 4),
                     std::polar(r2, 7 * pi / 12),
                     std::polar(r2, -3 * pi / 4),
                     std::polar(r2, -7 * pi / 12),
                     std::polar(r3, pi / 8),
                     std::polar(r3, 3 * pi / 8),
                     std::polar(r3, -pi / 4),
                     std::polar(r3, -pi / 2),
                     std::polar(r3, 3 * pi / 4),
                     std::polar(r3, pi / 2),
                     std::polar(r3, -7 * pi / 8),
                     std::polar(r3, -5 * pi / 8),
                     std::polar(r2, pi / 12),
                     std::polar(r1, pi / 4),
                     std::polar(r2, -pi / 12),
                     std::polar(r1, -pi / 4),
                     std::polar(r2, 11 * pi / 12),
                     std::polar(r1, 3 * pi / 4),
                     std::polar(r2, -11 * pi / 12),
                     std::polar(r1, -3 * pi / 4),
                     std::polar(r3, value_type(0)),
                     std::polar(r3, pi / 4),
                     std::polar(r3, -pi / 8),
                     std::polar(r3, -3 * pi / 8),
                     std::polar(r3, 7 * pi / 8),
                     std::polar(r3, 5 * pi / 8),
                     std::polar(r3, pi),
                     std::polar(r3, -3 * pi / 4) };
    }

    int bits() override { return BITS; }

    void hard(code_type* bits, complex_type symbol) override
    {
        const size_t index = nearest(symbol);
        for (int bit = 0; bit < BITS; bit++)
            bits[bit] = index & (1U << (BITS - bit - 1)) ? code_type(-1) : code_type(1);
    }

    void soft(code_type* bits, complex_type symbol, value_type precision) override
    {
        for (int bit = 0; bit < BITS; bit++) {
            value_type distance_zero = std::numeric_limits<value_type>::max();
            value_type distance_one = std::numeric_limits<value_type>::max();
            const size_t mask = 1U << (BITS - bit - 1);
            for (size_t index = 0; index < d_points.size(); index++) {
                const value_type distance = std::norm(symbol - d_points[index]);
                value_type& best = index & mask ? distance_one : distance_zero;
                best = std::min(best, distance);
            }
            bits[bit] = quantize(precision * (distance_one - distance_zero));
        }
    }

    complex_type map(code_type* bits) override
    {
        size_t index = 0;
        for (int bit = 0; bit < BITS; bit++)
            index = (index << 1) | (bits[bit] < 0);
        return d_points[index];
    }

private:
    static code_type quantize(value_type value)
    {
        if (std::is_integral<code_type>::value)
            value = std::nearbyint(value);
        if (std::is_same<code_type, int8_t>::value)
            value = std::min<value_type>(std::max<value_type>(value, -128), 127);
        return value;
    }

    size_t nearest(complex_type symbol) const
    {
        size_t nearest_index = 0;
        value_type nearest_distance = std::numeric_limits<value_type>::max();
        for (size_t index = 0; index < d_points.size(); index++) {
            const value_type distance = std::norm(symbol - d_points[index]);
            if (distance < nearest_distance) {
                nearest_index = index;
                nearest_distance = distance;
            }
        }
        return nearest_index;
    }

    std::array<complex_type, NUM> d_points;
};

#endif