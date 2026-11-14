#pragma once

// The binaural convolution: two FIRs (left and right ear) over the same input, for a run of
// outputs at once. Each coefficient is loaded once per run and used for every output of it,
// and each output keeps its own running sum (no horizontal sums), so this is far cheaper than
// one dot product per output and ear.
namespace dsp
{
// left[m] = sum over j < taps of irLeft[j] * x[j + m], right[] likewise, for m < n.
// The IRs are stored time-reversed, so x[0 .. taps - 1] is the window of the first output.
// x must be readable up to x[taps + 8 * ceil (n / 8) - 2]: runs are computed in groups of
// 8 outputs and the extra results are dropped. Every output is summed over j in order, the
// same way whatever n is, so splitting a run never changes the result.
using ConvolveTwo = void (*) (const float* irLeft, const float* irRight, int taps, const float* x,
                              float* left, float* right, int n) noexcept;

// The fastest version this CPU runs (AVX2 + FMA on most x64 CPUs, SSE2 on the rest, plain
// C++ elsewhere, which the compiler vectorises). Picks once; call from prepare, not the
// audio thread.
ConvolveTwo bestConvolveTwo();

// Every version this CPU can run, for tests.
struct NamedConvolveTwo
{
    const char* name;
    ConvolveTwo convolve;
};

int availableConvolveTwo (NamedConvolveTwo* out, int capacity);
} // namespace dsp
