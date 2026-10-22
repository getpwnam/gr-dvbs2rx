# Finite-input receiver startup

The receiver must distinguish a detected header from settled gain and carrier
estimates. These corrections address the high-SNR investigation in
[getpwnam/wavescope#1274](https://github.com/getpwnam/wavescope/issues/1274).
They preserve BCH validity and every attempted-frame/error counter, do not append
IQ padding, and do not reduce the configured coarse-estimation period.

## Header correlation

The differential SOF/PLSC metric is bounded by the RMS differential-input
magnitude when that magnitude exceeds one. Large AGC transients can no longer
turn unrelated symbols into arbitrarily strong header detections.

The divisor never falls below one: weak headers are not amplified into
detections while the upstream AGC is still settling. The existing unlocked
and locked thresholds remain 30 and 25. Zero and nonfinite differential
energy produce a zero metric. The normalization uses double precision for
the square root/division to avoid subnormal reciprocal overflow.

## Frame-local carrier bootstrap

The periodic coarse estimator can be unavailable both on initial acquisition
and after a tagged upstream rotator update resets its averaging window.
Waiting for that period before correcting already detected frames corrupts
otherwise decodable initial payloads.

A phase-unwrapped, least-squares slope over the decoded PLHEADER provides a
frame-local coarse prior during those intervals. It does not advance or
reset the periodic accumulator. It enables feed-forward correction of the
current payload. A successful payload/pilot fine estimate can also start
feedback after two observed headers instead of waiting for a coarse average.
The absolute feedback coordinate still targets the following physical frame.
Lock loss and rotator-reference changes discard cached state; reacquisition
uses fresh header/payload evidence rather than the pre-loss estimate.

Pilot phase availability is tracked separately from periodic coarse state.
This permits pilot phase resets during a bootstrap-corrected frame without
claiming the periodic estimator has converged.

## Pilotless APSK

QPSK and 8PSK already use fourth/eighth-power payload spectra to disambiguate
header-to-header carrier cycles. The same mechanism now uses the ring
symmetries of 16APSK (twelfth power) and 32APSK (sixteenth power). All symbols
are normalized by their magnitude before the power operation.

The 32,768-point transform supplies only the carrier-cycle ambiguity; header
phases still supply the fine estimate. The coarse prior resolves the wider
modulation-order alias. Adjacent physical frames are required, and weak or
nonfinite spectra do not replace the existing estimator. APSK's peak-power
gate accounts for zero-padding of short payloads. QPSK/8PSK gates are unchanged.

## Frontend boundary

These native changes cannot repair arbitrary amplitude distortion. WaveScope
also replaces a long-delay DC blocker ahead of its AGC with a causal DC notch,
still before channel translation. Moving ADC DC removal after translation is
not equivalent and is not a supported workaround.

Native CTest coverage includes gain/tiny-input correlation, weak-header
acquisition, clean/noisy APSK frequency estimates, noise-only rejection, and
non-mutating header bootstrap. Whole-receiver acceptance additionally requires
the WaveScope unchanged-input corpus and native-backed carrier/first-frame
tests; CTest alone is not a qualification claim.

## BCH validity before transport reassembly

Improved acquisition also exposes additional uncorrectable frames near the FEC
threshold. A valid BBHEADER CRC does not validate the DATAFIELD: the header can
survive while the payload contains bit errors. Passing those bytes into MPEG-TS
reassembly can contaminate otherwise useful whole-window output.

The deheader now honors upstream `bch_frame_status` tags. A frame marked invalid
is counted and dropped before packet reassembly, and any partial packet is
cleared so bytes cannot be joined across the loss. Malformed, duplicate or
misaligned status tags are warned about and rejected. Untagged input retains
the existing header/packet CRC checks; a valid status never bypasses those checks.

Every BCH attempt and failure remains in the decoder counters. Deheader
processed/drop counters also retain these rejected frames. Packet/error counters
describe packets actually emitted, as before. This strengthens the inputs to
transport recovery; it does not relax WaveScope's zero-TS-error acceptance gate,
erase failed observations, or introduce a startup warmup discard. BBFRAME export
retains its separate validity policy.
