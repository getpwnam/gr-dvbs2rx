/* -*- c++ -*- */
/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "apsk.hh"
#include <boost/test/unit_test.hpp>
#include <gnuradio/gr_complex.h>
#include <cmath>

namespace gr {
namespace dvbs2rx {

BOOST_AUTO_TEST_CASE(test_16apsk_4_5_reference_mapping)
{
    AmplitudePhaseShiftKeying<16, gr_complex, int8_t> apsk(2.75);
    const float r2 = std::sqrt(4.0 / (std::pow(1.0 / 2.75, 2) + 3.0));
    const float r1 = r2 / 2.75;
    const gr_complex expected[] = {
        std::polar(r2, float(M_PI / 4.0)),
        std::polar(r2, float(-M_PI / 4.0)),
        std::polar(r2, float(3.0 * M_PI / 4.0)),
        std::polar(r2, float(-3.0 * M_PI / 4.0)),
        std::polar(r2, float(M_PI / 12.0)),
        std::polar(r2, float(-M_PI / 12.0)),
        std::polar(r2, float(11.0 * M_PI / 12.0)),
        std::polar(r2, float(-11.0 * M_PI / 12.0)),
        std::polar(r2, float(5.0 * M_PI / 12.0)),
        std::polar(r2, float(-5.0 * M_PI / 12.0)),
        std::polar(r2, float(7.0 * M_PI / 12.0)),
        std::polar(r2, float(-7.0 * M_PI / 12.0)),
        std::polar(r1, float(M_PI / 4.0)),
        std::polar(r1, float(-M_PI / 4.0)),
        std::polar(r1, float(3.0 * M_PI / 4.0)),
        std::polar(r1, float(-3.0 * M_PI / 4.0)),
    };

    for (unsigned int index = 0; index < 16; index++) {
        int8_t bits[4];
        for (unsigned int bit = 0; bit < 4; bit++)
            bits[bit] = index & (1U << (3 - bit)) ? -1 : 1;

        const gr_complex symbol = apsk.map(bits);
        BOOST_CHECK_SMALL(symbol.real() - expected[index].real(), 1e-5f);
        BOOST_CHECK_SMALL(symbol.imag() - expected[index].imag(), 1e-5f);

        int8_t hard[4];
        int8_t soft[4];
        apsk.hard(hard, symbol);
        apsk.soft(soft, symbol, 8.0f);
        for (unsigned int bit = 0; bit < 4; bit++) {
            BOOST_CHECK_EQUAL(hard[bit], bits[bit]);
            BOOST_CHECK((soft[bit] < 0) == (bits[bit] < 0));
        }
    }
}

BOOST_AUTO_TEST_CASE(test_32apsk_3_4_reference_mapping)
{
    AmplitudePhaseShiftKeying<32, gr_complex, int8_t> apsk(5.27, 2.84);
    const float r3 = std::sqrt(8.0 / (std::pow(1.0 / 5.27, 2) +
                                      3.0 * std::pow(2.84 / 5.27, 2) + 4.0));
    const float r2 = r3 * 2.84 / 5.27;
    const float r1 = r3 / 5.27;
    const gr_complex expected[] = {
        std::polar(r2, float(M_PI / 4.0)),
        std::polar(r2, float(5.0 * M_PI / 12.0)),
        std::polar(r2, float(-M_PI / 4.0)),
        std::polar(r2, float(-5.0 * M_PI / 12.0)),
        std::polar(r2, float(3.0 * M_PI / 4.0)),
        std::polar(r2, float(7.0 * M_PI / 12.0)),
        std::polar(r2, float(-3.0 * M_PI / 4.0)),
        std::polar(r2, float(-7.0 * M_PI / 12.0)),
        std::polar(r3, float(M_PI / 8.0)),
        std::polar(r3, float(3.0 * M_PI / 8.0)),
        std::polar(r3, float(-M_PI / 4.0)),
        std::polar(r3, float(-M_PI / 2.0)),
        std::polar(r3, float(3.0 * M_PI / 4.0)),
        std::polar(r3, float(M_PI / 2.0)),
        std::polar(r3, float(-7.0 * M_PI / 8.0)),
        std::polar(r3, float(-5.0 * M_PI / 8.0)),
        std::polar(r2, float(M_PI / 12.0)),
        std::polar(r1, float(M_PI / 4.0)),
        std::polar(r2, float(-M_PI / 12.0)),
        std::polar(r1, float(-M_PI / 4.0)),
        std::polar(r2, float(11.0 * M_PI / 12.0)),
        std::polar(r1, float(3.0 * M_PI / 4.0)),
        std::polar(r2, float(-11.0 * M_PI / 12.0)),
        std::polar(r1, float(-3.0 * M_PI / 4.0)),
        std::polar(r3, 0.0f),
        std::polar(r3, float(M_PI / 4.0)),
        std::polar(r3, float(-M_PI / 8.0)),
        std::polar(r3, float(-3.0 * M_PI / 8.0)),
        std::polar(r3, float(7.0 * M_PI / 8.0)),
        std::polar(r3, float(5.0 * M_PI / 8.0)),
        std::polar(r3, float(M_PI)),
        std::polar(r3, float(-3.0 * M_PI / 4.0)),
    };

    for (unsigned int index = 0; index < 32; index++) {
        int8_t bits[5];
        for (unsigned int bit = 0; bit < 5; bit++)
            bits[bit] = index & (1U << (4 - bit)) ? -1 : 1;

        const gr_complex symbol = apsk.map(bits);
        BOOST_CHECK_SMALL(symbol.real() - expected[index].real(), 1e-5f);
        BOOST_CHECK_SMALL(symbol.imag() - expected[index].imag(), 1e-5f);

        int8_t hard[5];
        int8_t soft[5];
        apsk.hard(hard, symbol);
        apsk.soft(soft, symbol, 8.0f);
        for (unsigned int bit = 0; bit < 5; bit++) {
            BOOST_CHECK_EQUAL(hard[bit], bits[bit]);
            BOOST_CHECK((soft[bit] < 0) == (bits[bit] < 0));
        }
    }
}

} // namespace dvbs2rx
} // namespace gr