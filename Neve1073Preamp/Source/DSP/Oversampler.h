#pragma once

#include <array>
#include <cstddef>

namespace nv73::dsp
{

/**
 * Single 1st-order allpass section in the polyphase half-band branch:
 *   H_ap(w) = (a + w) / (1 + a * w),  where w = z^-1 at the lower sample rate.
 * Difference equation:
 *   y[n] = a * (x[n] - y[n-1]) + x[n-1]
 */
struct AllpassSection
{
    double a { 0.0 };
    double xPrev { 0.0 };
    double yPrev { 0.0 };

    void reset() noexcept
    {
        xPrev = 0.0;
        yPrev = 0.0;
    }

    [[nodiscard]] inline double process (double x) noexcept
    {
        const double y = a * (x - yPrev) + xPrev;
        xPrev = x;
        yPrev = y;
        return y;
    }
};

/**
 * 2x Polyphase Elliptic Half-Band Stage (NCoeffs total allpass sections split across 2 branches).
 * Branch 0 processes even polyphase path; Branch 1 processes odd polyphase path (with z^-1).
 */
template <std::size_t NumCoeffs>
class HalfBand2xStage
{
    static_assert (NumCoeffs % 2 == 0, "NumCoeffs must be even.");
    static constexpr std::size_t kPerBranch = NumCoeffs / 2;

public:
    explicit HalfBand2xStage (const std::array<double, NumCoeffs>& sortedCoeffs) noexcept
    {
        for (std::size_t i = 0; i < kPerBranch; ++i)
        {
            upBranch0_[i].a   = sortedCoeffs[2 * i];
            upBranch1_[i].a   = sortedCoeffs[2 * i + 1];
            downBranch0_[i].a = sortedCoeffs[2 * i];
            downBranch1_[i].a = sortedCoeffs[2 * i + 1];
        }
    }

    void reset() noexcept
    {
        for (std::size_t i = 0; i < kPerBranch; ++i)
        {
            upBranch0_[i].reset();
            upBranch1_[i].reset();
            downBranch0_[i].reset();
            downBranch1_[i].reset();
        }
    }

    /** Upsample 1 sample -> 2 samples [out0, out1] with exact unity DC gain. */
    inline void upsample (double in, double& out0, double& out1) noexcept
    {
        double u0 = in;
        double u1 = in;
        for (std::size_t i = 0; i < kPerBranch; ++i)
        {
            u0 = upBranch0_[i].process (u0);
            u1 = upBranch1_[i].process (u1);
        }
        out0 = u0;
        out1 = u1;
    }

    /** Downsample 2 samples [in0, in1] -> 1 sample with >103 dB alias rejection. */
    [[nodiscard]] inline double downsample (double in0, double in1) noexcept
    {
        double d0 = in1; // Branch 0 filters odd polyphase sample v[2n+1]
        double d1 = in0; // Branch 1 filters even polyphase sample v[2n]
        for (std::size_t i = 0; i < kPerBranch; ++i)
        {
            d0 = downBranch0_[i].process (d0);
            d1 = downBranch1_[i].process (d1);
        }
        return 0.5 * (d0 + d1);
    }

private:
    std::array<AllpassSection, kPerBranch> upBranch0_ {};
    std::array<AllpassSection, kPerBranch> upBranch1_ {};
    std::array<AllpassSection, kPerBranch> downBranch0_ {};
    std::array<AllpassSection, kPerBranch> downBranch1_ {};
};

/**
 * Zero-allocation per-sample 1x / 2x / 4x polyphase oversampler.
 * - Stage 1 (1x <-> 2x): 17th-order Cauer elliptic half-band (> 103 dB stopband, tbw = 0.045)
 * - Stage 2 (2x <-> 4x): 9th-order Cauer elliptic half-band  (> 113 dB stopband, tbw = 0.240)
 */
class Oversampler
{
public:
    enum class Factor : int
    {
        x1 = 1,
        x2 = 2,
        x4 = 4
    };

    Oversampler() noexcept
        : stage1_ (kStage1Coeffs),
          stage2_ (kStage2Coeffs)
    {
    }

    void reset() noexcept
    {
        stage1_.reset();
        stage2_.reset();
    }

    template <typename SampleProcessor>
    [[nodiscard]] inline double processSample (double input, Factor factor, SampleProcessor&& fn) noexcept
    {
        if (factor == Factor::x1)
            return fn (input);

        if (factor == Factor::x2)
        {
            double s0 = 0.0, s1 = 0.0;
            stage1_.upsample (input, s0, s1);
            s0 = fn (s0);
            s1 = fn (s1);
            return stage1_.downsample (s0, s1);
        }

        // 4x cascaded polyphase oversampling
        double u0 = 0.0, u1 = 0.0;
        stage1_.upsample (input, u0, u1);

        double q0 = 0.0, q1 = 0.0, q2 = 0.0, q3 = 0.0;
        stage2_.upsample (u0, q0, q1);
        stage2_.upsample (u1, q2, q3);

        q0 = fn (q0);
        q1 = fn (q1);
        q2 = fn (q2);
        q3 = fn (q3);

        const double d0 = stage2_.downsample (q0, q1);
        const double d1 = stage2_.downsample (q2, q3);
        return stage1_.downsample (d0, d1);
    }

private:
    static constexpr std::array<double, 8> kStage1Coeffs {
        0.038059051363, 0.141739219887, 0.285516809583, 0.441572773445,
        0.589937976805, 0.721787863804, 0.837829978576, 0.945517473251
    };

    static constexpr std::array<double, 4> kStage2Coeffs {
        0.043646929609, 0.174628080915, 0.399125646691, 0.749510679417
    };

    HalfBand2xStage<8> stage1_;
    HalfBand2xStage<4> stage2_;
};

} // namespace nv73::dsp
