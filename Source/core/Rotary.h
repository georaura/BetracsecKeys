// BeTracks Keys — altavoz rotatorio (Leslie) propio. OpenNord no trae uno.
//
// Diseño clásico, de conocimiento público: cruce a ~800 Hz; los agudos van a una bocina y los graves
// a un tambor, cada uno girando a su velocidad. El giro se oye como Doppler (un retardo que crece y
// mengua con la posición del rotor) y como variación de volumen (el rotor apunta o no al micrófono).
// Dos micrófonos separados dan el estéreo. Lento ≈ 0,8 Hz, rápido ≈ 6–7 Hz; la bocina acelera en menos
// de un segundo y el tambor, más pesado, tarda varios.
#pragma once

#include <array>
#include <cmath>

namespace bk::organ
{
class Rotary
{
public:
    enum class Speed { Slow, Fast, Stop };

    void prepare (double sampleRate)
    {
        sr_ = sampleRate > 0 ? sampleRate : 48000;
        horn_.buf.fill (0); drum_.buf.fill (0);
        horn_.w = drum_.w = 0;
        lp1_ = lp2_ = 0;
        // Filtro de cruce: paso bajo de 2 polos a 800 Hz; los agudos son lo que queda (suma exacta).
        g_ = float (1.0 - std::exp (-2.0 * 3.14159265358979 * 800.0 / sr_));
        horn_.rate = targetRate (horn_, true);
        drum_.rate = targetRate (drum_, false);
    }

    void setSpeed (Speed s) { speed_ = s; }
    Speed speed() const { return speed_; }
    /** 0 = limpio, 1 = saturado. */
    void setDrive (float d) { drive_ = std::fmax (0.0f, std::fmin (1.0f, d)); }
    void setEnabled (bool on) { enabled_ = on; }
    bool enabled() const { return enabled_; }

    /** Mono → estéreo (sobrescribe L y R). */
    void process (const float* in, float* left, float* right, int n)
    {
        if (! enabled_)
        {
            for (int i = 0; i < n; ++i) left[i] = right[i] = in[i];
            return;
        }
        const double hornTarget = targetRate (horn_, true), drumTarget = targetRate (drum_, false);
        // Inercia: la bocina llega en ~0,5 s (frena en ~0,7), el tambor en ~3 s (frena en ~4).
        const double hornTau = hornTarget > horn_.rate ? 0.45 : 0.7;
        const double drumTau = drumTarget > drum_.rate ? 3.0 : 4.0;
        const double hk = 1.0 - std::exp (-double (n) / (hornTau * sr_));
        const double dk = 1.0 - std::exp (-double (n) / (drumTau * sr_));
        const double h0 = horn_.rate, d0 = drum_.rate;
        horn_.rate += (hornTarget - horn_.rate) * hk;
        drum_.rate += (drumTarget - drum_.rate) * dk;
        const float k = 1.0f + 5.0f * drive_, norm = 1.0f / std::tanh (k);

        for (int i = 0; i < n; ++i)
        {
            float x = in[i];
            if (drive_ > 0.001f) x = std::tanh (x * k) * norm * 0.9f;
            // Cruce.
            lp1_ += g_ * (x - lp1_);
            lp2_ += g_ * (lp1_ - lp2_);
            const float low = lp2_, high = x - low;
            const double t = double (i) / n;
            float hl, hr, dl, dr;
            spin (horn_, high, h0 + (horn_.rate - h0) * t, 0.30, 0.40f, hl, hr);
            spin (drum_, low, d0 + (drum_.rate - d0) * t, 0.12, 0.25f, dl, dr);
            left[i] = 0.85f * hl + dl;
            right[i] = 0.85f * hr + dr;
        }
    }

private:
    struct Rotor
    {
        std::array<float, 2048> buf {};
        int w = 0;
        double phase = 0, rate = 0;
    };

    double targetRate (const Rotor& r, bool horn) const
    {
        (void) r;
        switch (speed_)
        {
            case Speed::Stop: return 0.0;
            case Speed::Slow: return horn ? 0.83 : 0.67;
            case Speed::Fast: return horn ? 6.75 : 5.9;
        }
        return 0.0;
    }

    /** Un rotor visto por dos micrófonos a ±70°: Doppler (retardo) y directividad (volumen). */
    void spin (Rotor& r, float x, double rateHz, double dopplerMs, float amDepth, float& outL, float& outR)
    {
        r.buf[size_t (r.w)] = x;
        constexpr double twoPi = 6.283185307179586;
        constexpr double micAngle = 1.2217; // 70°
        const double base = (dopplerMs + 0.2) * sr_ / 1000.0, depth = dopplerMs * sr_ / 1000.0;
        auto read = [&] (double angle) -> float
        {
            const double d = base + depth * std::sin (angle);
            const double pos = r.w - d;
            const int i = int (std::floor (pos));
            const float f = float (pos - i);
            const float a = r.buf[size_t (i & 2047)], b = r.buf[size_t ((i + 1) & 2047)];
            const float am = 1.0f - amDepth * 0.5f * float (1.0 - std::cos (angle));
            return (a + (b - a) * f) * am;
        };
        const double th = twoPi * r.phase;
        outL = read (th - micAngle);
        outR = read (th + micAngle);
        r.phase += rateHz / sr_;
        if (r.phase >= 1) r.phase -= 1;
        r.w = (r.w + 1) & 2047;
    }

    double sr_ = 48000;
    Speed speed_ = Speed::Slow;
    float drive_ = 0;
    bool enabled_ = true;
    float g_ = 0.1f, lp1_ = 0, lp2_ = 0;
    Rotor horn_, drum_;
};
} // namespace bk::organ
