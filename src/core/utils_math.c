/* Copyright (c) 2026 hors<horsicq@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/* utils_math.c - the math functions, without libm.
 *
 * floor, ceil, fabs, fmod and sqrt are exact. The transcendentals use
 * argument reduction plus a minimax polynomial and land within a few ULP,
 * which is far tighter than anything the signature database can observe:
 * x_log feeds the entropy calculation (compared against thresholds like
 * 7.0) and the rest are only reachable through the JavaScript Math object.
 */

#include "utils.h"

typedef union {
    double nDouble;
    unsigned long long nBits;
} XMathBits;

static unsigned long long math_bits(double nValue)
{
    XMathBits u;

    u.nDouble = nValue;

    return u.nBits;
}

static double math_from_bits(unsigned long long nBits)
{
    XMathBits u;

    u.nBits = nBits;

    return u.nDouble;
}

double x_nan(void)
{
    return math_from_bits(0x7FF8000000000000ull);
}

double x_inf(void)
{
    return math_from_bits(0x7FF0000000000000ull);
}

int x_isnan(double nValue)
{
    return (nValue != nValue) ? 1 : 0;
}

int x_isinf(double nValue)
{
    unsigned long long nBits = math_bits(nValue) & 0x7FFFFFFFFFFFFFFFull;

    return (nBits == 0x7FF0000000000000ull) ? 1 : 0;
}

int x_signbit(double nValue)
{
    /* The bit itself, so that -0.0 and a negative NaN answer 1 where a
     * comparison against zero would not.                                   */
    return (int)(math_bits(nValue) >> 63);
}

double x_fabs(double nValue)
{
    return math_from_bits(math_bits(nValue) & 0x7FFFFFFFFFFFFFFFull);
}

/* Truncates toward zero by masking off the fraction bits. */
static double math_trunc(double nValue)
{
    unsigned long long nBits = math_bits(nValue);
    int nExponent = (int)((nBits >> 52) & 0x7FF) - 1023;
    unsigned long long nMask = 0;

    if (nExponent < 0) {
        return (nBits >> 63) ? -0.0 : 0.0;
    }

    if (nExponent >= 52) {
        return nValue; /* already an integer, or NaN/infinity */
    }

    nMask = (1ull << (52 - nExponent)) - 1;

    if ((nBits & nMask) == 0) {
        return nValue;
    }

    return math_from_bits(nBits & ~nMask);
}

double x_floor(double nValue)
{
    double nTruncated = 0;

    if (x_isnan(nValue) || x_isinf(nValue)) {
        return nValue;
    }

    nTruncated = math_trunc(nValue);

    if ((nValue < 0) && (nTruncated != nValue)) {
        return nTruncated - 1.0;
    }

    return nTruncated;
}

double x_ceil(double nValue)
{
    double nTruncated = 0;

    if (x_isnan(nValue) || x_isinf(nValue)) {
        return nValue;
    }

    nTruncated = math_trunc(nValue);

    if ((nValue > 0) && (nTruncated != nValue)) {
        return nTruncated + 1.0;
    }

    return nTruncated;
}

#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_AMD64))
#include <emmintrin.h> /* compiler intrinsics, not a library header */
#endif

/* Square root comes from the hardware wherever the compiler exposes it
 * without a library call: an SSE2 intrinsic on 64-bit MSVC, a builtin on
 * GCC and Clang. Everything else falls back to Newton-Raphson below.      */
#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_AMD64))
#define CDIE_SQRT_INTRINSIC 1
#elif (defined(__GNUC__) || defined(__clang__)) && !defined(__TINYC__)
/* TinyCC sometimes defines __GNUC__ for header compatibility but has no
 * __builtin_sqrt, so it must fall through to the software Newton path. */
#define CDIE_SQRT_INTRINSIC 2
#endif

#if !defined(CDIE_SQRT_INTRINSIC)
/* One Newton step done in effectively double-double precision.
 *
 * Plain Newton in floating point reaches a fixed point that can sit 1 ULP
 * off, because every iteration rounds: without this, sqrt(2) comes out as
 * 1.414213562373095 rather than 1.4142135623730951. The fix is to compute the
 * residual v - g^2 exactly - g^2 needs 106 bits, so it is split with Dekker's
 * trick into an exact hi + lo pair - and take one more step from there.
 */
static double math_sqrt_refine(double nValue, double nGuess)
{
    const double nSplit = 134217729.0; /* 2^27 + 1 */
    double c = nSplit * nGuess;
    double nHi = c - (c - nGuess);
    double nLo = nGuess - nHi;
    double nSquareHi = nGuess * nGuess;
    double nSquareLo = ((nHi * nHi - nSquareHi) + 2.0 * nHi * nLo) + nLo * nLo;
    double nResidual = (nValue - nSquareHi) - nSquareLo;

    return nGuess + nResidual / (2.0 * nGuess);
}
#endif

double x_sqrt(double nValue)
{
    if (x_isnan(nValue) || (nValue < 0)) {
        return (nValue < 0) ? x_nan() : nValue;
    }

    if ((nValue == 0) || x_isinf(nValue)) {
        return nValue;
    }

#if CDIE_SQRT_INTRINSIC == 1
    /* SSE2 gives a correctly rounded result in one instruction. */
    return _mm_cvtsd_f64(_mm_sqrt_sd(_mm_setzero_pd(), _mm_set_sd(nValue)));
#elif CDIE_SQRT_INTRINSIC == 2
    /* A compiler builtin, not a library call: on any target with a square
     * root instruction this is that instruction inline.                    */
    return __builtin_sqrt(nValue);
#else
    {
        /* Seed from the exponent: halving it halves the magnitude. */
        unsigned long long nBits = math_bits(nValue);
        unsigned long long nSeed = (nBits >> 1) + (0x3FF0000000000000ull >> 1);
        double nGuess = math_from_bits(nSeed);
        int i = 0;

        /* Newton-Raphson converges quadratically; six rounds is ample from a
         * seed already accurate to a few bits.                             */
        for (i = 0; i < 6; i++) {
            nGuess = 0.5 * (nGuess + nValue / nGuess);
        }

        return math_sqrt_refine(nValue, nGuess);
    }
#endif
}

double x_fmod(double nValue, double nDivisor)
{
    double nAbsValue = 0;
    double nAbsDivisor = 0;
    double nResult = 0;

    if (x_isnan(nValue) || x_isnan(nDivisor) || x_isinf(nValue) || (nDivisor == 0)) {
        return x_nan();
    }

    if (x_isinf(nDivisor)) {
        return nValue;
    }

    nAbsValue = x_fabs(nValue);
    nAbsDivisor = x_fabs(nDivisor);

    if (nAbsValue < nAbsDivisor) {
        return nValue;
    }

    /* Repeated subtraction with exponent scaling: every step is exact, so
     * the remainder is exact too.                                          */
    nResult = nAbsValue;

    while (nResult >= nAbsDivisor) {
        double nScaled = nAbsDivisor;
        double nNext = nScaled * 2.0;

        while ((nNext <= nResult) && (!x_isinf(nNext))) {
            nScaled = nNext;
            nNext = nScaled * 2.0;
        }

        nResult -= nScaled;
    }

    return (nValue < 0) ? -nResult : nResult;
}

/* ln(2) split so that the reduction stays exact to more than double
 * precision.                                                               */
static const double MATH_LN2_HI = 6.93147180369123816490e-01;
static const double MATH_LN2_LO = 1.90821492927058770002e-10;
static const double MATH_INV_LN2 = 1.44269504088896338700e+00;
static const double MATH_INV_LN10 = 4.34294481903251827651e-01;
static const double MATH_PI_2 = 1.57079632679489661923e+00;
/* math_sin_core / math_cos_core are minimax polynomials fitted on
 * [-pi/4, pi/4]; outside it they are not sine and cosine at all, and past
 * about 0.9 they already return magnitudes above 1. The reduction therefore
 * keeps going until the remainder is back inside that interval - the limit
 * is the domain of the cores, not a convergence tolerance. */
static const double MATH_REDUCE_LIMIT = 7.85398163397448309616e-01; /* pi/4 */

double x_log(double nValue)
{
    int nExponent = 0;
    double nMantissa = 0;
    double f = 0;
    double s = 0;
    double z = 0;
    double w = 0;
    double nPoly = 0;

    if (x_isnan(nValue)) {
        return nValue;
    }

    if (nValue < 0) {
        return x_nan();
    }

    if (nValue == 0) {
        return -x_inf();
    }

    if (x_isinf(nValue)) {
        return nValue;
    }

    /* Split into mantissa in [1,2) and a binary exponent. */
    {
        unsigned long long nBits = math_bits(nValue);
        int nRaw = (int)((nBits >> 52) & 0x7FF);

        if (nRaw == 0) {
            /* Subnormal: scale up by 2^54 first. */
            nValue *= 18014398509481984.0;
            nBits = math_bits(nValue);
            nRaw = (int)((nBits >> 52) & 0x7FF);
            nExponent = nRaw - 1023 - 54;
        } else {
            nExponent = nRaw - 1023;
        }

        nMantissa = math_from_bits((nBits & 0x000FFFFFFFFFFFFFull) | 0x3FF0000000000000ull);
    }

    /* Centre the mantissa around 1 to keep the series short. */
    if (nMantissa > 1.4142135623730951) {
        nMantissa *= 0.5;
        nExponent++;
    }

    /* log(m) = 2*atanh(s) with s = f/(2+f); the polynomial is the classic
     * fdlibm minimax fit, good to well under one ULP.                      */
    f = nMantissa - 1.0;
    s = f / (2.0 + f);
    z = s * s;
    w = z * z;

    {
        double t1 = w * (3.999999999940941908e-01 + w * (2.222219843214978396e-01 + w * 1.531383769920937332e-01));
        double t2 = z * (6.666666666666735130e-01 + w * (2.857142874366239149e-01 + w * (1.818357216161805012e-01 + w * 1.479819860511658591e-01)));
        double nHalfSquare = 0.5 * f * f;

        nPoly = t1 + t2;

        return (double)nExponent * MATH_LN2_HI - ((nHalfSquare - (s * (nHalfSquare + nPoly) + (double)nExponent * MATH_LN2_LO)) - f);
    }
}

double x_log10(double nValue)
{
    return x_log(nValue) * MATH_INV_LN10;
}

double x_exp(double nValue)
{
    int nK = 0;
    double nHi = 0;
    double nLo = 0;
    double r = 0;
    double t = 0;
    double c = 0;
    double y = 0;

    if (x_isnan(nValue)) {
        return nValue;
    }

    if (nValue > 709.782712893384) {
        return x_inf();
    }

    if (nValue < -745.1332191019411) {
        return 0.0;
    }

    if (nValue == 0) {
        return 1.0;
    }

    /* x = k*ln2 + r with |r| <= ln2/2, ln2 split in two so the reduction
     * carries more than double precision.                                  */
    nK = (int)((nValue * MATH_INV_LN2) + ((nValue >= 0) ? 0.5 : -0.5));
    nHi = nValue - (double)nK * MATH_LN2_HI;
    nLo = (double)nK * MATH_LN2_LO;
    r = nHi - nLo;

    /* fdlibm rational form: more accurate than a bare Taylor series. */
    t = r * r;
    c = r - t * (1.66666666666666019037e-01 +
                 t * (-2.77777777770155933842e-03 + t * (6.61375632143793436117e-05 + t * (-1.65339022054652515390e-06 + t * 4.13813679705723846039e-08))));

    y = 1.0 - ((nLo - (r * c) / (2.0 - c)) - nHi);

    /* Scale by 2^k through the exponent field. */
    {
        int nRaw = 1023 + nK;

        if (nRaw <= 0) {
            /* Subnormal result: scale in two steps to keep the bits. */
            if (nRaw <= -52) {
                return 0.0;
            }

            y *= math_from_bits((unsigned long long)(nRaw + 54) << 52);

            return y * math_from_bits((unsigned long long)(1023 - 54) << 52);
        }

        if (nRaw >= 0x7FF) {
            return x_inf();
        }

        y *= math_from_bits((unsigned long long)nRaw << 52);
    }

    return y;
}

double x_pow(double nBase, double nExponent)
{
    int bNegativeResult = 0;

    if (nExponent == 0) {
        return 1.0;
    }

    if (x_isnan(nBase) || x_isnan(nExponent)) {
        return x_nan();
    }

    if (nBase == 0) {
        /* IEEE 754 / ECMAScript 21.3.2.26: a negative zero base keeps its
         * sign when the exponent is an odd integer. The == test above is
         * true for -0.0 as well, so the sign bit is what decides.          */
        int bOddExponent = ((nExponent == math_trunc(nExponent)) && (x_fmod(nExponent, 2.0) != 0)) ? 1 : 0;
        int bNegative = (((math_bits(nBase) >> 63) != 0) && bOddExponent) ? 1 : 0;

        if (nExponent < 0) {
            return bNegative ? -x_inf() : x_inf();
        }

        return bNegative ? -0.0 : 0.0;
    }

    /* Integer exponents are done by squaring so that the common cases stay
     * exact rather than going through exp(log()).                          */
    if ((nExponent == math_trunc(nExponent)) && (x_fabs(nExponent) < 1024.0)) {
        double nResult = 1.0;
        double nFactor = nBase;
        long long nCount = (long long)x_fabs(nExponent);

        while (nCount) {
            if (nCount & 1) {
                nResult *= nFactor;
            }

            nFactor *= nFactor;
            nCount >>= 1;
        }

        return (nExponent < 0) ? (1.0 / nResult) : nResult;
    }

    if (nBase < 0) {
        return x_nan(); /* non-integer power of a negative base */
    }

    (void)bNegativeResult;

    return x_exp(nExponent * x_log(nBase));
}

/* sin/cos on the reduced range [-pi/4, pi/4]. */
static double math_sin_core(double x)
{
    double z = x * x;

    /* fdlibm __kernel_sin, S1..S6. Dropping S6 costs about 180 ULP at
     * x = 0.5, so the tail matters even though it looks negligible.        */
    return x * (1.0 + z * (-1.66666666666666324348e-01 +
                           z * (8.33333333332248946124e-03 +
                                z * (-1.98412698298579493134e-04 +
                                     z * (2.75573137070700676789e-06 +
                                          z * (-2.50507602534068634195e-08 +
                                               z * 1.58969099521155010221e-10))))));
}

static double math_cos_core(double x)
{
    double z = x * x;

    /* fdlibm __kernel_cos, C1..C6 after the leading -z/2. */
    return 1.0 + z * (-5.00000000000000000000e-01 +
                      z * (4.16666666666666019037e-02 +
                           z * (-1.38888888888741095749e-03 +
                                z * (2.48015872894767294178e-05 +
                                     z * (-2.75573143513906633035e-07 +
                                          z * (2.08757232129817482790e-09 +
                                               z * (-1.13596475577881948265e-11)))))));
}

/* The quadrant contributed by one reduction pass: nRounded modulo 4, with a
 * negative nRounded counted the way two's complement would.
 *
 * nRounded can be far outside long long range for a large x, where the
 * conversion is undefined, so the cast is taken only below 2^62. At or above
 * that the exponent is at least 62 and the 52-bit fraction puts the unit in
 * the last place at 2^10 or more, so the double is an exact multiple of 4
 * and the remainder is zero without any conversion. The result is exact -
 * this is bit arithmetic, not an approximation.                            */
static int math_quadrant_step(double nRounded)
{
    double nAbs = x_fabs(nRounded);
    int nStep = 0;

    if (nAbs < 4611686018427387904.0) { /* 2^62 */
        nStep = (int)((long long)nAbs & 3);
    }

    if ((nRounded < 0) && (nStep != 0)) {
        nStep = 4 - nStep;
    }

    return nStep;
}

/* Reduces x modulo pi/2 and reports the quadrant.
 *
 * The two-part pi/2 below keeps the reduction accurate for moderate
 * arguments, but once |x| is large enough that nRounded * pi/2 rounds by
 * more than pi/4 the remainder is out of range and the core polynomials,
 * evaluated far outside [-pi/4, pi/4], return values a sine cannot take -
 * up to +/-inf. Applying the split again brings the remainder back into
 * range. Such arguments are still not reduced accurately (that needs a
 * multi-word pi/2), but the result stays a value in [-1, 1].              */
static int math_reduce(double x, double *pnReduced)
{
    double r = x;
    int nQuadrant = 0;
    int i = 0;

    /* Each pass drops the remainder by roughly the working precision, so a
     * DBL_MAX argument needs about twenty of them; the bound is only there
     * so the loop cannot spin. */
    for (i = 0; i < 64; i++) {
        double nQuotient = r / MATH_PI_2;
        double nRounded = math_trunc(nQuotient + ((nQuotient >= 0) ? 0.5 : -0.5));
        int nStep = 0;

        if (nRounded == 0) {
            break;
        }

        /* Only the low two bits of nRounded matter. x_fmod would give the
         * same answer but costs a repeated-subtraction loop per pass, which
         * for a large argument dominates the whole call.                   */
        nStep = math_quadrant_step(nRounded);

        nQuadrant = (nQuadrant + nStep) & 3;

        r = (r - nRounded * 1.57079632673412561417e+00) - nRounded * 6.07710050650619224932e-11;

        /* One pass is the whole reduction for every argument the two-part
         * pi/2 can handle: the remainder is then already inside [-pi/4,
         * pi/4] and the loop leaves it untouched. A remainder still outside
         * the cores' domain means the subtraction cancelled, and one more
         * pass brings it back - a value just past the boundary needs at most
         * one, since subtracting pi/2 from it lands just inside.           */
        if (x_fabs(r) <= MATH_REDUCE_LIMIT) {
            break;
        }
    }

    *pnReduced = r;

    return nQuadrant;
}

double x_sin(double nValue)
{
    double r = 0;
    int nQuadrant = 0;

    if (x_isnan(nValue) || x_isinf(nValue)) {
        return x_nan();
    }

    nQuadrant = math_reduce(nValue, &r);

    switch (nQuadrant) {
        case 0: return math_sin_core(r);
        case 1: return math_cos_core(r);
        case 2: return -math_sin_core(r);
        default: return -math_cos_core(r);
    }
}

double x_cos(double nValue)
{
    double r = 0;
    int nQuadrant = 0;

    if (x_isnan(nValue) || x_isinf(nValue)) {
        return x_nan();
    }

    nQuadrant = math_reduce(nValue, &r);

    switch (nQuadrant) {
        case 0: return math_cos_core(r);
        case 1: return -math_sin_core(r);
        case 2: return -math_cos_core(r);
        default: return math_sin_core(r);
    }
}

double x_tan(double nValue)
{
    double nSin = x_sin(nValue);
    double nCos = x_cos(nValue);

    if (nCos == 0) {
        return (nSin < 0) ? -x_inf() : x_inf();
    }

    return nSin / nCos;
}
