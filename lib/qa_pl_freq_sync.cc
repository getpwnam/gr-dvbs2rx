/* -*- c++ -*- */
/*
 * Copyright (c) 2021 Igor Freire.
 *
 * This file is part of gr-dvbs2rx.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "apsk.hh"
#include "pi2_bpsk.h"
#include "pl_defs.h"
#include "pl_freq_sync.h"
#include "pl_signaling.h"
#include <gnuradio/expj.h>
#include <boost/test/data/test_case.hpp>
#include <boost/test/unit_test.hpp>
#include <limits>
#include <random>

namespace bdata = boost::unit_test::data;
namespace tt = boost::test_tools;

namespace gr {
namespace dvbs2rx {

BOOST_DATA_TEST_CASE(test_qpsk_payload_frequency,
                     bdata::make({ 8100, 32400 }) *
                         bdata::make({ -0.01, -0.0006, -0.00021, 0.0, 0.0006, 0.01 }) *
                         bdata::make({ 4.1, 8.0, 12.0 }),
                     length,
                     offset,
                     snr)
{
    freq_sync synchronizer(20, 0);
    std::mt19937 generator(902);
    std::normal_distribution<float> noise(0, std::sqrt(0.5 / std::pow(10.0, snr / 10)));
    volk::vector<gr_complex> payload(length);
    for (int index = 0; index < length; ++index) {
        const auto symbol = gr_expj(M_PI / 4 + (generator() % 4) * M_PI / 2);
        // Preserve the seeded waveform regardless of argument evaluation order.
        const float imag_noise = noise(generator);
        const float real_noise = noise(generator);
        payload[index] = symbol * gr_expj(2 * M_PI * offset * index) +
                         gr_complex(real_noise, imag_noise);
    }
    double estimate = 0;
    BOOST_REQUIRE(
        synchronizer.estimate_psk_payload_frequency(payload.data(), length, estimate));
    BOOST_CHECK_SMALL(estimate - offset, 1.0 / (4 * (length + 90)));
}

BOOST_AUTO_TEST_CASE(test_qpsk_payload_frequency_rejects_noise_and_invalid_input)
{
    freq_sync synchronizer(20, 0);
    volk::vector<gr_complex> payload(32400, gr_complex(0, 0));
    double estimate = 123;
    BOOST_CHECK(!synchronizer.estimate_psk_payload_frequency(
        payload.data(), payload.size(), estimate));
    BOOST_CHECK_EQUAL(estimate, 123);
    std::mt19937 generator(903);
    std::normal_distribution<float> noise(0, 1);
    for (auto& symbol : payload) {
        const float imag_noise = noise(generator);
        const float real_noise = noise(generator);
        symbol = gr_complex(real_noise, imag_noise);
    }
    BOOST_CHECK(!synchronizer.estimate_psk_payload_frequency(
        payload.data(), payload.size(), estimate));
    payload[0] = gr_complex(std::numeric_limits<float>::quiet_NaN(), 0);
    BOOST_CHECK(!synchronizer.estimate_psk_payload_frequency(
        payload.data(), payload.size(), estimate));
    payload[0] = gr_complex(std::numeric_limits<float>::infinity(), 0);
    BOOST_CHECK(!synchronizer.estimate_psk_payload_frequency(
        payload.data(), payload.size(), estimate));
    BOOST_CHECK_EQUAL(estimate, 123);
}

BOOST_DATA_TEST_CASE(test_8psk_payload_frequency,
                     bdata::make({ 5400, 21600 }) *
                         bdata::make({ -0.04, -0.0006, -0.00021, 0.0, 0.0006, 0.04 }) *
                         bdata::make({ 8.5, 9.0, 12.0 }),
                     length,
                     offset,
                     snr)
{
    freq_sync synchronizer(20, 0);
    std::mt19937 generator(902);
    std::normal_distribution<float> noise(0, std::sqrt(0.5 / std::pow(10.0, snr / 10)));
    volk::vector<gr_complex> payload(length);
    for (int index = 0; index < length; ++index) {
        const auto symbol = gr_expj((generator() % 8) * M_PI / 4);
        const float imag_noise = noise(generator);
        const float real_noise = noise(generator);
        payload[index] = symbol * gr_expj(2 * M_PI * offset * index) +
                         gr_complex(real_noise, imag_noise);
    }
    double estimate = 0;
    BOOST_REQUIRE(
        synchronizer.estimate_psk_payload_frequency(payload.data(), length, estimate, 8));
    BOOST_CHECK_SMALL(estimate - offset, 1.0 / (4 * (length + 90)));
}

BOOST_DATA_TEST_CASE(test_8psk_payload_frequency_rejects_noise_and_invalid_input,
                     bdata::make({ 5400, 21600 }) * bdata::make({ 1, 2, 3, 4, 5 }),
                     length,
                     seed)
{
    freq_sync synchronizer(20, 0);
    volk::vector<gr_complex> payload(length, gr_complex(0, 0));
    double estimate = 123;
    BOOST_CHECK(!synchronizer.estimate_psk_payload_frequency(
        payload.data(), payload.size(), estimate, 8));
    std::mt19937 generator(seed);
    std::normal_distribution<float> noise(0, 1);
    for (auto& symbol : payload) {
        const float imag_noise = noise(generator);
        const float real_noise = noise(generator);
        symbol = gr_complex(real_noise, imag_noise);
    }
    BOOST_CHECK(!synchronizer.estimate_psk_payload_frequency(
        payload.data(), payload.size(), estimate, 8));
    payload[0] = gr_complex(std::numeric_limits<float>::quiet_NaN(), 0);
    BOOST_CHECK(!synchronizer.estimate_psk_payload_frequency(
        payload.data(), payload.size(), estimate, 8));
    payload[0] = gr_complex(std::numeric_limits<float>::infinity(), 0);
    BOOST_CHECK(!synchronizer.estimate_psk_payload_frequency(
        payload.data(), payload.size(), estimate, 8));
    BOOST_CHECK(!synchronizer.estimate_psk_payload_frequency(
        payload.data(), payload.size(), estimate, 16));
    BOOST_CHECK_EQUAL(estimate, 123);
}

BOOST_DATA_TEST_CASE(test_apsk_payload_frequency,
                     bdata::make({ 4, 5 }) * bdata::make({ false, true }) *
                         bdata::make({ -0.01, -0.00001, 0.0, 0.00001, 0.01 }) *
                         bdata::make({ 30.0, 100.0 }),
                     bits_per_symbol,
                     short_frame,
                     offset,
                     snr)
{
    const unsigned int length = (short_frame ? 16200 : 64800) / bits_per_symbol;
    const unsigned int order = bits_per_symbol == 4 ? 12 : 16;
    AmplitudePhaseShiftKeying<16, gr_complex, int8_t> apsk16(2.85);
    AmplitudePhaseShiftKeying<32, gr_complex, int8_t> apsk32(5.27, 2.84);
    std::mt19937 generator(1274);
    std::normal_distribution<float> noise(0, std::sqrt(0.5 / std::pow(10.0, snr / 10)));
    volk::vector<gr_complex> payload(length);
    for (unsigned int index = 0; index < length; ++index) {
        int8_t bits[5];
        for (auto& bit : bits)
            bit = generator() & 1 ? -1 : 1;
        const auto symbol = bits_per_symbol == 4 ? apsk16.map(bits) : apsk32.map(bits);
        const float real_noise = noise(generator);
        const float imag_noise = noise(generator);
        payload[index] = symbol * gr_expj(2 * M_PI * offset * index) +
                         gr_complex(real_noise, imag_noise);
    }
    freq_sync synchronizer(20, 0);
    double estimate = 0;
    BOOST_REQUIRE(synchronizer.estimate_psk_payload_frequency(
        payload.data(), length, estimate, order));
    BOOST_CHECK_SMALL(estimate - offset, 1.0 / (4 * (length + 90)));
}

BOOST_DATA_TEST_CASE(test_apsk_payload_frequency_rejects_noise,
                     bdata::make({ 3240, 4050, 12960, 16200 }) * bdata::make({ 12, 16 }) *
                         bdata::make({ 1, 2, 3, 4, 5 }),
                     length,
                     order,
                     seed)
{
    freq_sync synchronizer(20, 0);
    volk::vector<gr_complex> payload(length, gr_complex(0, 0));
    double estimate = 123;
    BOOST_CHECK(!synchronizer.estimate_psk_payload_frequency(
        payload.data(), length, estimate, order));
    std::mt19937 generator(seed);
    std::normal_distribution<float> noise(0, 1);
    for (auto& symbol : payload) {
        const float real_noise = noise(generator);
        const float imag_noise = noise(generator);
        symbol = gr_complex(real_noise, imag_noise);
    }
    BOOST_CHECK(!synchronizer.estimate_psk_payload_frequency(
        payload.data(), length, estimate, order));
    payload[0] = gr_complex(std::numeric_limits<float>::quiet_NaN(), 0);
    BOOST_CHECK(!synchronizer.estimate_psk_payload_frequency(
        payload.data(), length, estimate, order));
    BOOST_CHECK_EQUAL(estimate, 123);
}

/* Add frequency and phase offsets to an input complex symbol buffer */
void rotate(gr_complex* out,
            const gr_complex* in,
            float freq_offset,
            float phase_0,
            unsigned int n_symbols)
{
    const gr_complex phase_inc = gr_expj(2 * M_PI * freq_offset);
    gr_complex phase = gr_expj(phase_0);
    volk_32fc_s32fc_x2_rotator_32fc(out, in, phase_inc, &phase, n_symbols);
}

/* Fixture */
struct F {
    F() : plheader(PLHEADER_LEN)
    {
        // Frequency synchronizer object
        unsigned int period = 1;
        int debug_level = 0;
        p_freq_sync = new freq_sync(period, debug_level);

        // Noiseless PLHEADER for testing
        plsc_encoder plsc_mapper;
        gr_complex* ptr = plheader.data();
        map_bpsk(sof_big_endian, ptr, SOF_LEN);
        uint8_t modcod = 21;
        bool short_fecframe = true;
        bool has_pilots = false;
        plsc_mapper.encode(ptr + SOF_LEN, modcod, short_fecframe, has_pilots);
    }
    ~F() { delete p_freq_sync; }

    freq_sync* p_freq_sync;
    volk::vector<gr_complex> plheader;
};

BOOST_DATA_TEST_CASE_F(F,
                       test_header_bootstrap_preserves_periodic_estimator,
                       bdata::make({ -0.3, -0.00001, 0.0, 0.00001, 0.3 }),
                       offset)
{
    freq_sync synchronizer(4, 0);
    volk::vector<gr_complex> rotated(PLHEADER_LEN);
    rotate(rotated.data(), plheader.data(), offset, 2.1, PLHEADER_LEN);
    for (unsigned int frame = 0; frame < 4; ++frame) {
        BOOST_CHECK_SMALL(
            synchronizer.estimate_plheader_frequency(rotated.data(), (21 << 2) | 2) -
                offset,
            1e-6);
        BOOST_CHECK_EQUAL(synchronizer.estimate_coarse(rotated.data(), false),
                          frame == 3);
    }
    BOOST_CHECK_SMALL(synchronizer.get_coarse_foffset() - offset, 1e-6);
}

BOOST_FIXTURE_TEST_CASE(test_frequency_reset_discards_partial_window, F)
{
    for (const double offset : { -0.3, 0.3 }) {
        freq_sync synchronizer(4, 0);
        volk::vector<gr_complex> rotated(PLHEADER_LEN);
        rotate(rotated.data(), plheader.data(), 0.0002, 0, PLHEADER_LEN);
        for (unsigned int frame = 0; frame < 6; ++frame)
            BOOST_CHECK_EQUAL(synchronizer.estimate_coarse(rotated.data(), false),
                              frame == 3);
        BOOST_REQUIRE(synchronizer.is_coarse_corrected());
        synchronizer.reset();
        BOOST_CHECK(!synchronizer.is_coarse_corrected());
        BOOST_CHECK_EQUAL(synchronizer.get_coarse_foffset(), 0);
        BOOST_CHECK_EQUAL(synchronizer.get_fine_foffset(), 0);
        BOOST_CHECK(!synchronizer.has_fine_foffset_est());
        rotate(rotated.data(), plheader.data(), offset, 0, PLHEADER_LEN);
        for (unsigned int frame = 0; frame < 4; ++frame)
            BOOST_CHECK_EQUAL(synchronizer.estimate_coarse(rotated.data(), false),
                              frame == 3);
        BOOST_CHECK_SMALL(synchronizer.get_coarse_foffset() - offset, 1e-6);
    }
}


BOOST_DATA_TEST_CASE_F(F,
                       test_coarse_freq_est_unit_period,
                       bdata::make({ -0.23, -0.13, 0.03, 0.19, 0.25 }) *
                           bdata::make({ false, true }),
                       freq_offset,
                       use_full_plheader)
{
    // Add frequency offset and a non-zero initial phase
    float phase_0 = M_PI;
    volk::vector<gr_complex> rotated(PLHEADER_LEN);
    rotate(rotated.data(), plheader.data(), freq_offset, phase_0, PLHEADER_LEN);

    // Coarse frequency offset estimate
    uint8_t plsc = (21 << 2) | (1 << 1); // ignored when use_full_plheader=false
    bool new_est = p_freq_sync->estimate_coarse(rotated.data(), use_full_plheader, plsc);
    float freq_offset_est = p_freq_sync->get_coarse_foffset();

    BOOST_CHECK_EQUAL(new_est, true);
    BOOST_CHECK_CLOSE(freq_offset_est, freq_offset, 1e-4);

    // All the tested frequency offset values are above the fine frequency
    // offset estimation range. Hence, the frequency synchronizer object should
    // not enter the "coarse corrected" state.
    BOOST_CHECK_EQUAL(p_freq_sync->is_coarse_corrected(), false);
}

BOOST_DATA_TEST_CASE_F(F,
                       test_coarse_freq_est_non_unit_period,
                       bdata::make({ -0.23, -0.13, 0.03, 0.19, 0.25 }) *
                           bdata::make({ false, true }),
                       freq_offset,
                       use_full_plheader)
{
    // Recreate the frequency synchronizer with an estimation period of two
    delete p_freq_sync;
    unsigned int period = 2;
    int debug_level = 0;
    p_freq_sync = new freq_sync(period, debug_level);

    // Process two rotated PLHEADERs affected by a common frequency offset and
    // initial phase. Assume the interval between them corresponds to the
    // maximum PLFRAME length.
    volk::vector<gr_complex> rotated1(PLHEADER_LEN);
    volk::vector<gr_complex> rotated2(PLHEADER_LEN);
    float phase = M_PI;
    rotate(rotated1.data(), plheader.data(), freq_offset, phase, PLHEADER_LEN);
    phase += (MAX_PLFRAME_LEN) * (2 * M_PI * freq_offset);
    rotate(rotated2.data(), plheader.data(), freq_offset, phase, PLHEADER_LEN);

    // First coarse frequency offset estimate.
    //
    // Since period=2, the first call should not produce a new estimate.
    uint8_t plsc = (21 << 2) | (1 << 1); // ignored when use_full_plheader=false
    bool new_est = p_freq_sync->estimate_coarse(rotated1.data(), use_full_plheader, plsc);
    BOOST_CHECK_EQUAL(new_est, false);

    // The second call should produce a new estimate.
    new_est = p_freq_sync->estimate_coarse(rotated2.data(), use_full_plheader, plsc);
    BOOST_CHECK_EQUAL(new_est, true);

    // The estimate should be the average of the two realizations
    float freq_offset_est = p_freq_sync->get_coarse_foffset();
    BOOST_CHECK_CLOSE(freq_offset_est, freq_offset, 1e-4);

    // Try another round of two frames with a completely different frequency
    // offset to make sure the internal state is properly reset on every period
    float freq_offset2 = -freq_offset;
    volk::vector<gr_complex> rotated3(PLHEADER_LEN);
    volk::vector<gr_complex> rotated4(PLHEADER_LEN);
    phase += (MAX_PLFRAME_LEN) * (2 * M_PI * freq_offset2);
    rotate(rotated3.data(), plheader.data(), freq_offset2, phase, PLHEADER_LEN);
    phase += (MAX_PLFRAME_LEN) * (2 * M_PI * freq_offset2);
    rotate(rotated4.data(), plheader.data(), freq_offset2, phase, PLHEADER_LEN);
    new_est = p_freq_sync->estimate_coarse(rotated3.data(), use_full_plheader, plsc);
    BOOST_CHECK_EQUAL(new_est, false);
    new_est = p_freq_sync->estimate_coarse(rotated4.data(), use_full_plheader, plsc);
    BOOST_CHECK_EQUAL(new_est, true);
    freq_offset_est = p_freq_sync->get_coarse_foffset();
    BOOST_CHECK_CLOSE(freq_offset_est, freq_offset2, 1e-4);
}

BOOST_DATA_TEST_CASE_F(F,
                       test_coarse_corrected_state,
                       bdata::make({ -3.26e-4, -1e-4, -1e-5, 1e-5, 1e-4, 3.26e-4 }) *
                           bdata::make({ false, true }),
                       freq_offset,
                       use_full_plheader)
{
    // This test focuses on frequency offset values within the fine frequency
    // offset estimation range.
    BOOST_TEST(abs(freq_offset) < fine_foffset_corr_range);

    // Add frequency offset and a non-zero initial phase
    float phase_0 = M_PI;
    volk::vector<gr_complex> rotated(PLHEADER_LEN);
    rotate(rotated.data(), plheader.data(), freq_offset, phase_0, PLHEADER_LEN);

    // Coarse frequency offset estimate
    uint8_t plsc = (21 << 2) | (1 << 1); // ignored when use_full_plheader=false
    bool new_est = p_freq_sync->estimate_coarse(rotated.data(), use_full_plheader, plsc);
    float freq_offset_est = p_freq_sync->get_coarse_foffset();

    // Once the frequency offset falls within the fine frequency offset
    // estimation range, the frequency synchronizer infers that it has achieved
    // the "coarse corrected" state
    BOOST_CHECK_EQUAL(new_est, true);
    BOOST_CHECK_EQUAL(p_freq_sync->is_coarse_corrected(), true);

    // The coarse estimation performance is a little worse for low frequency
    // offset values. Consider an error tolerance of 0.5%.
    BOOST_CHECK_CLOSE(freq_offset_est, freq_offset, 5e-1);
}

BOOST_DATA_TEST_CASE_F(
    F,
    test_sof_phase_est,
    bdata::make(
        { (-M_PI / 2), (-3 * M_PI / 4), (M_PI / 2), (3 * M_PI / 4), (M_PI - 1e-5) }),
    phase_0)
{
    // Add a non-zero initial phase
    float freq_offset = 0;
    volk::vector<gr_complex> rotated(PLHEADER_LEN);
    rotate(rotated.data(), plheader.data(), freq_offset, phase_0, PLHEADER_LEN);

    // Estimate the SOF phase
    float phase_0_est = p_freq_sync->estimate_sof_phase(rotated.data());
    BOOST_CHECK_CLOSE(phase_0_est, phase_0, 1e-4);
}

BOOST_DATA_TEST_CASE_F(
    F,
    test_plheader_phase_est,
    bdata::make(
        { (-M_PI / 2), (-3 * M_PI / 4), (M_PI / 2), (3 * M_PI / 4), (M_PI - 1e-5) }),
    phase_0)
{
    // Add a non-zero initial phase
    float freq_offset = 0;
    volk::vector<gr_complex> rotated(PLHEADER_LEN);
    rotate(rotated.data(), plheader.data(), freq_offset, phase_0, PLHEADER_LEN);

    // To estimate the full PLHEADER phase, the underlying PLSC must be
    // known. The test PLHEADER has modcod=21 and short_fecframe=1:
    uint8_t plsc = (21 << 2) | (1 << 1);

    // Estimate the PLHEADER phase
    float phase_0_est = p_freq_sync->estimate_plheader_phase(rotated.data(), plsc);
    BOOST_CHECK_CLOSE(phase_0_est, phase_0, 1e-4);

    // If the codeword informed to `estimate_plheader_phase()` is wrong (e.g.,
    // after PLSC decoding error), the correctness of the phase estimate is not
    // guaranteed. For an initial phase offset theta and zero frequency offset,
    // the modulation removal process implements:
    //
    // exp(j*theta) * sym * conj(expected_sym) = exp(j*theta),
    //
    // since the symbols have unitary energy. Hence, after summing 90 samples of
    // exp(j*theta) and taking the angle of that, we obtain theta.
    //
    // Now, when the informed PLSC does not match the actual PLSC of the
    // incoming PLHEADER, the modulation removal fails. Since the PLHEADER
    // symbols are always exp(+-j*pi/4) or exp(+-j*3*pi/4), the modulation
    // removal leads to exp(j*theta) multiplied by a factor of +-1 or
    // +-j1. Still, because of the Reed-Muller codeword structure, the differing
    // symbols sometimes can cancel each other out, in which case the phase
    // estimate can still be right, although with less averaging.
    //
    // For example, if theta=pi/2, the sum of 90 perfectly modulation-removed
    // symbols would become 90*exp(j*pi/2), which is equal to j90. Ultimately,
    // angle(j*90) yields pi/2, as expected. In contrast, if the informed
    // codeword is wrong in, say, 32 positions, the total sum could reduce to
    // j*26, assuming the 32 unmatched symbols are canceled out by 32 positions
    // of the supplied codeword that match relative to the actual codeword. As a
    // result, the sum becomes j*26, instead of j*90. In other words, the
    // codeword mismatch disturbs the energy accumulation that is useful to
    // overcome noise. Nevertheless, in this case, the PLHEADER phase estimate
    // could still be reasonable.
    //
    // The real problem is when not all mismatched bits of the informed PLSC
    // cancel with the actual PLSC. As far as I can tell, this happens for at
    // least one codeword in the Reed-Muller code set. For example, for the
    // given PLSC, it happens for (plsc - 2), as follows:
    float phase_0_est_2 = p_freq_sync->estimate_plheader_phase(rotated.data(), plsc - 2);
    BOOST_TEST(phase_0_est_2 != phase_0, tt::tolerance(0.1));
}


BOOST_DATA_TEST_CASE_F(
    F,
    test_pilot_phase_est,
    bdata::make(
        { (-M_PI / 2), (-3 * M_PI / 4), (M_PI / 2), (3 * M_PI / 4), (M_PI - 1e-5) }),
    phase_0)
{
    volk::vector<gr_complex> pilot_blk(PILOT_BLK_LEN, { SQRT2_2, SQRT2_2 });

    // Add frequency offset and a non-zero initial phase
    volk::vector<gr_complex> rotated(PILOT_BLK_LEN);
    float freq_offset = 0;
    rotate(rotated.data(), pilot_blk.data(), freq_offset, phase_0, PILOT_BLK_LEN);

    // Estimate the phase of the pilot block
    int i_blk = 0;
    float phase_0_est = p_freq_sync->estimate_pilot_phase(rotated.data(), i_blk);

    // Check
    BOOST_CHECK_CLOSE(phase_0_est, phase_0, 1e-3);
}


BOOST_DATA_TEST_CASE_F(
    F,
    test_fine_freq_est_pilot_mode,
    bdata::make(
        { (-M_PI / 2), (-3 * M_PI / 4), (M_PI / 2), (3 * M_PI / 4), (M_PI - 1e-5) }) *
        bdata::make({ -3.26e-4, -1e-4, -1e-5, 1e-5, 1e-4, 3.26e-4 }),
    phase_0,
    freq_offset)
{
    // First and foremost, the fine frequency offset estimator only works when
    // the frequency offset is below an upper limit:
    BOOST_TEST(abs(freq_offset) < fine_foffset_corr_range);

    // Generate a PLFRAME with 60 slots and 3 pilot blocks
    uint8_t n_slots = 60;
    uint8_t n_pilot_blks = 3;
    uint32_t plframe_len =
        PLHEADER_LEN + (n_slots * SLOT_LEN) + (n_pilot_blks * PILOT_BLK_LEN);
    volk::vector<gr_complex> pilot_blk(PILOT_BLK_LEN, { SQRT2_2, SQRT2_2 });
    volk::vector<gr_complex> plframe(plframe_len);
    memcpy(plframe.data(), plheader.data(), PLHEADER_LEN * sizeof(gr_complex));
    for (int i = 0; i < n_pilot_blks; i++) {
        gr_complex* p_pilot =
            plframe.data() + PLHEADER_LEN + ((i + 1) * PILOT_BLK_PERIOD) - PILOT_BLK_LEN;
        memcpy(p_pilot, pilot_blk.data(), PILOT_BLK_LEN * sizeof(gr_complex));
    }

    // Add frequency and phase offset
    volk::vector<gr_complex> rot_plframe(plframe_len);
    rotate(rot_plframe.data(), plframe.data(), freq_offset, phase_0, plframe_len);

    // The synchronizer object records when the first fine frequency offset
    // estimate becomes available internally. At this point, it should be false.
    BOOST_CHECK_EQUAL(p_freq_sync->has_fine_foffset_est(), false);

    // Fine frequency offset estimate
    uint8_t plsc = (21 << 2) | (1 << 1); // test PLHEADER info
    const gr_complex* p_rot_plheader = rot_plframe.data();
    const gr_complex* p_rot_payload = rot_plframe.data() + PLHEADER_LEN;
    p_freq_sync->estimate_fine_pilot_mode(
        p_rot_plheader, p_rot_payload, n_pilot_blks, plsc);

    // Check
    float freq_offset_est = p_freq_sync->get_fine_foffset();
    BOOST_CHECK_EQUAL(p_freq_sync->has_fine_foffset_est(), true);
    BOOST_CHECK_CLOSE(freq_offset_est, freq_offset, 1e-3);
}

BOOST_DATA_TEST_CASE_F(
    F,
    test_fine_freq_est_pilotless_mode,
    bdata::make(
        { (-M_PI / 2), (-3 * M_PI / 4), (M_PI / 2), (3 * M_PI / 4), (M_PI - 1e-5) }) *
        bdata::make({ -1e-5, -1e-6, 1e-5, 1e-6 }),
    phase_0,
    freq_offset)
{
    // Generate a full PLFRAME with 360 slots, plus an extra PLHEADER
    uint16_t plframe_len = PLHEADER_LEN + (360 * SLOT_LEN);
    uint16_t total_len = plframe_len + PLHEADER_LEN;
    volk::vector<gr_complex> syms(total_len);
    memcpy(syms.data(), plheader.data(), PLHEADER_LEN * sizeof(gr_complex));
    memcpy(syms.data() + plframe_len, plheader.data(), PLHEADER_LEN * sizeof(gr_complex));

    // Add frequency and phase offset
    volk::vector<gr_complex> rot_syms(total_len);
    rotate(rot_syms.data(), syms.data(), freq_offset, phase_0, total_len);

    // Estimate the two PLHEADER phases
    uint8_t plsc = (21 << 2) | (1 << 1); // test PLHEADER info
    float phase_1 = p_freq_sync->estimate_plheader_phase(rot_syms.data(), plsc);
    float phase_2 =
        p_freq_sync->estimate_plheader_phase(rot_syms.data() + plframe_len, plsc);

    // The fine estimation should only be executed after the residual frequency offset
    // (indicated by the coarse estimator) falls within an acceptable range. The caller
    // should make sure that the frequency synchronizer is at least in coarse-corrected
    // state before calling the pilotless fine estimator.
    bool use_full_plheader = true;
    p_freq_sync->estimate_coarse(rot_syms.data(), use_full_plheader, plsc);
    double coarse_foffset = p_freq_sync->get_coarse_foffset();
    BOOST_CHECK_EQUAL(p_freq_sync->is_coarse_corrected(), true);

    // Now, compute the fine frequency offset estimate
    bool new_est = p_freq_sync->estimate_fine_pilotless_mode(
        phase_1, phase_2, plframe_len, coarse_foffset);
    BOOST_CHECK_EQUAL(new_est, true);
    BOOST_CHECK_EQUAL(p_freq_sync->has_fine_foffset_est(), true);

    // Check the estimate
    float freq_offset_est = p_freq_sync->get_fine_foffset();
    BOOST_CHECK_CLOSE(freq_offset_est, freq_offset, 1e-2);
}

BOOST_DATA_TEST_CASE_F(
    F,
    test_derotate_plheader_open_loop,
    bdata::make(
        { (-M_PI / 2), (-3 * M_PI / 4), (M_PI / 2), (3 * M_PI / 4), (M_PI - 1e-5) }) *
        bdata::make({ -0.23, -0.13, 0.03, 0.19, 0.25 }),
    phase_0,
    freq_offset)
{
    // Add frequency offset and a non-zero initial phase
    volk::vector<gr_complex> rotated(PLHEADER_LEN);
    rotate(rotated.data(), plheader.data(), freq_offset, phase_0, PLHEADER_LEN);

    // Estimate the coarse frequency offset before attempting the
    // derotation. The derotation routine uses the latest available frequency
    // offset estimate (kept internally).
    bool use_full_plheader = false;
    p_freq_sync->estimate_coarse(rotated.data(), use_full_plheader);

    // Derotate the PLHEADER
    bool open_loop = true;
    p_freq_sync->derotate_plheader(rotated.data(), open_loop);

    // Check the derotated result
    const gr_complex* derotated = p_freq_sync->get_plheader();
    for (int i = 0; i < PLHEADER_LEN; i++) {
        BOOST_CHECK_CLOSE(plheader[i].real(), derotated[i].real(), 1e-2);
        BOOST_CHECK_CLOSE(plheader[i].imag(), derotated[i].imag(), 1e-2);
    }
}

BOOST_DATA_TEST_CASE_F(
    F,
    test_derotate_plheader_closed_loop,
    bdata::make(
        { (-M_PI / 2), (-3 * M_PI / 4), (M_PI / 2), (3 * M_PI / 4), (M_PI - 1e-5) }),
    phase_0)
{
    // In closed-loop, the external frequency correction block should eventually converge
    // to an accurate correction, leaving a negligible residual frequency offset. Thus,
    // add a non-zero initial phase and assume zero frequency offset.
    float freq_offset = 0;
    volk::vector<gr_complex> rotated(PLHEADER_LEN);
    rotate(rotated.data(), plheader.data(), freq_offset, phase_0, PLHEADER_LEN);

    // Derotate the PLHEADER
    p_freq_sync->derotate_plheader(rotated.data());

    // Check the derotated result
    const gr_complex* derotated = p_freq_sync->get_plheader();
    for (int i = 0; i < PLHEADER_LEN; i++) {
        BOOST_CHECK_CLOSE(plheader[i].real(), derotated[i].real(), 1e-2);
        BOOST_CHECK_CLOSE(plheader[i].imag(), derotated[i].imag(), 1e-2);
    }
}


} // namespace dvbs2rx
} // namespace gr
