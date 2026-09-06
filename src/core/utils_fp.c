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

/* utils_fp.c - conversion between doubles and decimal strings, with no help
 * from the C runtime.
 *
 * Both directions are exact: the value is carried in a fixed-width big
 * integer and every decision (which digit, where to round) is made on exact
 * integers rather than on floating point. That is what lets the port keep
 * producing byte-identical output while dropping the CRT.
 *
 *   x_strtod          decimal -> double, correctly rounded (ties to even)
 *   x_dtoa_shortest   double  -> the shortest decimal that reads back equal
 *                               (ECMAScript Number::toString)
 *   x_dtoa_fixed      double  -> fixed number of fraction digits (toFixed)
 *   x_dtoa_precision  double  -> fixed number of significant digits
 *   x_dtoa_cformat    double  -> printf's %e / %f / %g, C99 rules
 *
 * The shortest-digit generator is the classic Steele & White / Dragon4
 * free-format algorithm.
 */

#include "utils.h"

/* ------------------------------------------------------------------------ */
/*  Fixed width big integer                                                  */
/* ------------------------------------------------------------------------ */

/* A double spans at most ~1080 bits (10^-323 needs 1073, plus the shift used
 * by the 54-bit division), so 48 limbs of 32 bits leaves comfortable
 * headroom. Values outside the double range are clamped long before here.
 *
 * The size matters: several routines hold five of these at once, and the
 * CRT-free build has no stack probes, so every frame must stay under 4 KB.  */
#define BIG_LIMBS 48

typedef struct {
    unsigned int pLimb[BIG_LIMBS]; /* little endian, 32 bits used per limb */
    int nCount;
} XBig;

static void big_zero(XBig *pBig)
{
    pBig->nCount = 0;
}

static int big_is_zero(const XBig *pBig)
{
    return (pBig->nCount == 0) ? 1 : 0;
}

static void big_trim(XBig *pBig)
{
    while ((pBig->nCount > 0) && (pBig->pLimb[pBig->nCount - 1] == 0)) {
        pBig->nCount--;
    }
}

static void big_from_u64(XBig *pBig, unsigned long long nValue)
{
    big_zero(pBig);

    while (nValue) {
        if (pBig->nCount >= BIG_LIMBS) {
            break;
        }

        pBig->pLimb[pBig->nCount++] = (unsigned int)(nValue & 0xFFFFFFFFu);
        nValue >>= 32;
    }
}

static void big_copy(XBig *pDestination, const XBig *pSource)
{
    int i = 0;

    pDestination->nCount = pSource->nCount;

    for (i = 0; i < pSource->nCount; i++) {
        pDestination->pLimb[i] = pSource->pLimb[i];
    }
}

/* pBig = pBig * nMultiplier + nAddend */
static void big_mul_add(XBig *pBig, unsigned int nMultiplier, unsigned int nAddend)
{
    unsigned long long nCarry = nAddend;
    int i = 0;

    for (i = 0; i < pBig->nCount; i++) {
        unsigned long long nProduct = (unsigned long long)pBig->pLimb[i] * nMultiplier + nCarry;

        pBig->pLimb[i] = (unsigned int)(nProduct & 0xFFFFFFFFu);
        nCarry = nProduct >> 32;
    }

    while (nCarry && (pBig->nCount < BIG_LIMBS)) {
        pBig->pLimb[pBig->nCount++] = (unsigned int)(nCarry & 0xFFFFFFFFu);
        nCarry >>= 32;
    }
}

static void big_shl(XBig *pBig, int nBits)
{
    int nLimbs = nBits / 32;
    int nRest = nBits % 32;
    int i = 0;

    if (big_is_zero(pBig) || (nBits <= 0)) {
        return;
    }

    if (nRest) {
        unsigned int nCarry = 0;

        for (i = 0; i < pBig->nCount; i++) {
            unsigned int nValue = pBig->pLimb[i];

            pBig->pLimb[i] = (nValue << nRest) | nCarry;
            nCarry = nValue >> (32 - nRest);
        }

        if (nCarry && (pBig->nCount < BIG_LIMBS)) {
            pBig->pLimb[pBig->nCount++] = nCarry;
        }
    }

    if (nLimbs) {
        if (pBig->nCount + nLimbs > BIG_LIMBS) {
            pBig->nCount = BIG_LIMBS - nLimbs;
        }

        for (i = pBig->nCount - 1; i >= 0; i--) {
            pBig->pLimb[i + nLimbs] = pBig->pLimb[i];
        }

        for (i = 0; i < nLimbs; i++) {
            pBig->pLimb[i] = 0;
        }

        pBig->nCount += nLimbs;
    }
}

static int big_cmp(const XBig *pLeft, const XBig *pRight)
{
    int i = 0;

    if (pLeft->nCount != pRight->nCount) {
        return (pLeft->nCount < pRight->nCount) ? -1 : 1;
    }

    for (i = pLeft->nCount - 1; i >= 0; i--) {
        if (pLeft->pLimb[i] != pRight->pLimb[i]) {
            return (pLeft->pLimb[i] < pRight->pLimb[i]) ? -1 : 1;
        }
    }

    return 0;
}

/* pLeft -= pRight, assuming pLeft >= pRight */
static void big_sub(XBig *pLeft, const XBig *pRight)
{
    unsigned long long nBorrow = 0;
    int i = 0;

    for (i = 0; i < pLeft->nCount; i++) {
        unsigned long long nRight = (i < pRight->nCount) ? pRight->pLimb[i] : 0;
        unsigned long long nValue = (unsigned long long)pLeft->pLimb[i] - nRight - nBorrow;

        pLeft->pLimb[i] = (unsigned int)(nValue & 0xFFFFFFFFu);
        nBorrow = (nValue >> 63) & 1;
    }

    big_trim(pLeft);
}

static void big_add(XBig *pLeft, const XBig *pRight)
{
    unsigned long long nCarry = 0;
    int nCount = (pLeft->nCount > pRight->nCount) ? pLeft->nCount : pRight->nCount;
    int i = 0;

    for (i = 0; i < nCount; i++) {
        unsigned long long nA = (i < pLeft->nCount) ? pLeft->pLimb[i] : 0;
        unsigned long long nB = (i < pRight->nCount) ? pRight->pLimb[i] : 0;
        unsigned long long nValue = nA + nB + nCarry;

        if (i < BIG_LIMBS) {
            pLeft->pLimb[i] = (unsigned int)(nValue & 0xFFFFFFFFu);
        }

        nCarry = nValue >> 32;
    }

    pLeft->nCount = nCount;

    if (nCarry && (pLeft->nCount < BIG_LIMBS)) {
        pLeft->pLimb[pLeft->nCount++] = (unsigned int)nCarry;
    }

    big_trim(pLeft);
}

static int big_bitlen(const XBig *pBig)
{
    unsigned int nTop = 0;
    int nBits = 0;

    if (big_is_zero(pBig)) {
        return 0;
    }

    nTop = pBig->pLimb[pBig->nCount - 1];
    nBits = (pBig->nCount - 1) * 32;

    while (nTop) {
        nBits++;
        nTop >>= 1;
    }

    return nBits;
}

/* pBig *= 10^nPower */
static void big_mul_pow10(XBig *pBig, int nPower)
{
    static const unsigned int pPow10[10] = {1u, 10u, 100u, 1000u, 10000u, 100000u, 1000000u, 10000000u, 100000000u, 1000000000u};

    while (nPower >= 9) {
        big_mul_add(pBig, pPow10[9], 0);
        nPower -= 9;
    }

    if (nPower > 0) {
        big_mul_add(pBig, pPow10[nPower], 0);
    }
}

/* ------------------------------------------------------------------------ */
/*  Double bit layout                                                        */
/* ------------------------------------------------------------------------ */

typedef union {
    double nDouble;
    unsigned long long nBits;
} XDoubleBits;

static unsigned long long double_bits(double nValue)
{
    XDoubleBits u;

    u.nDouble = nValue;

    return u.nBits;
}

static double bits_double(unsigned long long nBits)
{
    XDoubleBits u;

    u.nBits = nBits;

    return u.nDouble;
}

/* Splits a finite non-zero double into mantissa * 2^exponent. */
static void double_decompose(double nValue, unsigned long long *pnMantissa, int *pnExponent, int *pbIsSubnormal)
{
    unsigned long long nBits = double_bits(nValue);
    int nRawExponent = (int)((nBits >> 52) & 0x7FF);
    unsigned long long nRawMantissa = nBits & 0xFFFFFFFFFFFFFull;

    if (nRawExponent == 0) {
        *pnMantissa = nRawMantissa;
        *pnExponent = -1074;
        *pbIsSubnormal = 1;
    } else {
        *pnMantissa = nRawMantissa | (1ull << 52);
        *pnExponent = nRawExponent - 1075;
        *pbIsSubnormal = 0;
    }
}

/* ------------------------------------------------------------------------ */
/*  Decimal -> double                                                        */
/* ------------------------------------------------------------------------ */

static int is_digit(char nChar)
{
    return ((nChar >= '0') && (nChar <= '9')) ? 1 : 0;
}

static int is_space(char nChar)
{
    return ((nChar == ' ') || (nChar == '\t') || (nChar == '\n') || (nChar == '\r') || (nChar == '\f') || (nChar == '\v')) ? 1 : 0;
}

/* Builds the correctly rounded double closest to pDigits * 10^nExponent. */
static double decimal_to_double(const char *pDigits, int nDigitCount, int nExponent, int bNegative)
{
    XBig num;
    XBig den;
    unsigned long long nMantissa = 0;
    int nBinaryExponent = 0;
    int i = 0;
    int nShift = 0;
    int nRound = 0;
    int bSticky = 0;

    if (nDigitCount == 0) {
        return bNegative ? -0.0 : 0.0;
    }

    /* Cheap range rejection before touching the big integers. */
    if (nExponent + nDigitCount > 320) {
        return bNegative ? -x_inf() : x_inf();
    }

    if (nExponent + nDigitCount < -340) {
        return bNegative ? -0.0 : 0.0;
    }

    big_zero(&num);

    for (i = 0; i < nDigitCount; i++) {
        big_mul_add(&num, 10, (unsigned int)(pDigits[i] - '0'));
    }

    big_from_u64(&den, 1);

    if (nExponent > 0) {
        big_mul_pow10(&num, nExponent);
    } else if (nExponent < 0) {
        big_mul_pow10(&den, -nExponent);
    }

    if (big_is_zero(&num)) {
        return bNegative ? -0.0 : 0.0;
    }

    /* Bit-by-bit long division needs den <= num < 2*den, so that each round
     * yields exactly one quotient bit. Line the two up and remember the
     * binary exponent that the alignment implies.                          */
    nShift = big_bitlen(&num) - big_bitlen(&den);

    if (nShift > 0) {
        big_shl(&den, nShift);
    } else if (nShift < 0) {
        big_shl(&num, -nShift);
    }

    if (big_cmp(&num, &den) < 0) {
        big_shl(&num, 1);
        nShift--;
    }

    /* num/den now lies in [1, 2) and value = (num/den) * 2^nShift. */
    for (i = 0; i < 54; i++) {
        nMantissa <<= 1;

        if (big_cmp(&num, &den) >= 0) {
            big_sub(&num, &den);
            nMantissa |= 1;
        }

        big_shl(&num, 1);
    }

    bSticky = big_is_zero(&num) ? 0 : 1;

    /* The 54 bit integer represents value * 2^-(nShift-53). */
    nBinaryExponent = nShift - 53;

    /* Below the smallest normal the mantissa loses bits instead of the
     * exponent going lower; the discarded bits feed the sticky flag.       */
    {
        int nExtra = (-1074) - (nBinaryExponent + 1);

        if (nExtra > 0) {
            if (nExtra >= 64) {
                return bNegative ? -0.0 : 0.0;
            }

            while (nExtra-- > 0) {
                if (nMantissa & 1) {
                    bSticky = 1;
                }

                nMantissa >>= 1;
                nBinaryExponent++;
            }
        }
    }

    /* Round to 53 bits, ties to even. */
    nRound = (int)(nMantissa & 1);
    nMantissa >>= 1;
    nBinaryExponent++;

    if (nRound && (bSticky || (nMantissa & 1))) {
        nMantissa++;

        if (nMantissa >= (1ull << 53)) {
            nMantissa >>= 1;
            nBinaryExponent++;
        }
    }

    if (nMantissa == 0) {
        return bNegative ? -0.0 : 0.0;
    }

    /* Assemble.
     *
     * Which encoding applies is decided by the mantissa, not by the exponent:
     * the loop above has already shifted a subnormal result down to the fixed
     * exponent 2^-1074, so what is left to check is whether bit 52 survived.
     * If it did the value is normal - a subnormal that rounded up to the
     * smallest normal lands here too, which is what should happen. If it did
     * not, the exponent field is zero and the mantissa is the whole value.  */
    {
        unsigned long long nSign = bNegative ? (1ull << 63) : 0ull;
        int nRawExponent = 0;

        if (nMantissa < (1ull << 52)) {
            return bits_double(nSign | nMantissa);
        }

        nRawExponent = nBinaryExponent + 1075;

        if (nRawExponent >= 0x7FF) {
            return bNegative ? -x_inf() : x_inf();
        }

        return bits_double(nSign | ((unsigned long long)nRawExponent << 52) | (nMantissa & 0xFFFFFFFFFFFFFull));
    }
}

double x_strtod(const char *pString, const char **ppEnd)
{
    const char *p = pString;
    const char *pStart = pString;
    int bNegative = 0;
    /* 17 digits decide a double; the rest only affect the tie, which the
     * sticky digit below records.                                          */
    char sDigits[48];
    int nDigitCount = 0;
    int bSticky = 0;
    int nExponent = 0;
    int bSeenDigit = 0;
    int bSeenDot = 0;
    int nDroppedBeforeDot = 0;

    while (is_space(*p)) {
        p++;
    }

    if ((*p == '+') || (*p == '-')) {
        bNegative = (*p == '-') ? 1 : 0;
        p++;
    }

    /* Infinity / NaN, matching what the JavaScript layer expects. */
    if ((p[0] == 'I') && (x_strncmp(p, "Infinity", 8) == 0)) {
        if (ppEnd) {
            *ppEnd = p + 8;
        }

        return bNegative ? -x_inf() : x_inf();
    }

    if ((p[0] == 'N') && (x_strncmp(p, "NaN", 3) == 0)) {
        if (ppEnd) {
            *ppEnd = p + 3;
        }

        return x_nan();
    }

    for (; *p; p++) {
        if (is_digit(*p)) {
            bSeenDigit = 1;

            if (nDigitCount < (int)sizeof(sDigits) - 1) {
                /* Skip leading zeros so the digit budget is spent on
                 * significant digits.                                       */
                if ((nDigitCount > 0) || (*p != '0')) {
                    sDigits[nDigitCount++] = *p;

                    if (bSeenDot) {
                        nExponent--;
                    }
                } else if (bSeenDot) {
                    nExponent--;
                }
            } else {
                /* Past the window: a non-zero digit still breaks a tie, so
                 * remember that something was dropped.                      */
                if (*p != '0') {
                    bSticky = 1;
                }

                if (!bSeenDot) {
                    nExponent++;
                }
            }
        } else if ((*p == '.') && (!bSeenDot)) {
            bSeenDot = 1;
        } else {
            break;
        }
    }

    (void)nDroppedBeforeDot;

    if (!bSeenDigit) {
        if (ppEnd) {
            *ppEnd = pStart;
        }

        return 0.0;
    }

    if ((*p == 'e') || (*p == 'E')) {
        const char *pExp = p + 1;
        int bExpNegative = 0;
        int nValue = 0;
        int bAny = 0;

        if ((*pExp == '+') || (*pExp == '-')) {
            bExpNegative = (*pExp == '-') ? 1 : 0;
            pExp++;
        }

        while (is_digit(*pExp)) {
            if (nValue < 100000) {
                nValue = nValue * 10 + (*pExp - '0');
            }

            bAny = 1;
            pExp++;
        }

        if (bAny) {
            nExponent += bExpNegative ? -nValue : nValue;
            p = pExp;
        }
    }

    if (ppEnd) {
        *ppEnd = p;
    }

    /* A dropped non-zero digit is folded back in as a trailing 1 so that an
     * otherwise exact halfway case rounds up instead of to even.           */
    if (bSticky && (nDigitCount < (int)sizeof(sDigits))) {
        sDigits[nDigitCount++] = '1';
        nExponent--;
    }

    return decimal_to_double(sDigits, nDigitCount, nExponent, bNegative);
}

/* ------------------------------------------------------------------------ */
/*  Double -> decimal digits                                                 */
/* ------------------------------------------------------------------------ */

/* Generates the shortest digit string that reads back as nValue.
 * pDigits receives the significant digits, *pnDecimalPoint the position of
 * the decimal point relative to the first digit.                            */
static int dtoa_digits_shortest(double nValue, char *pDigits, int *pnDecimalPoint)
{
    XBig r;
    XBig s;
    XBig mPlus;
    XBig mMinus;
    XBig tmp;
    unsigned long long nMantissa = 0;
    int nExponent = 0;
    int bIsSubnormal = 0;
    int nCount = 0;
    int nK = 0;
    int bLowOk = 0;

    double_decompose(nValue, &nMantissa, &nExponent, &bIsSubnormal);

    /* R / S = value, M+ and M- are half the gap to the neighbours. */
    big_from_u64(&r, nMantissa);
    big_from_u64(&s, 1);
    big_from_u64(&mPlus, 1);
    big_from_u64(&mMinus, 1);

    if (nExponent >= 0) {
        big_shl(&r, nExponent + 1);
        big_shl(&s, 1);
        big_shl(&mPlus, nExponent);
        big_shl(&mMinus, nExponent);
    } else {
        big_shl(&r, 1);
        big_shl(&s, -nExponent + 1);
    }

    /* A mantissa sitting exactly on a power of two has a closer lower
     * neighbour, so the lower gap is half the upper one.                    */
    if ((nMantissa == (1ull << 52)) && (!bIsSubnormal)) {
        big_shl(&r, 1);
        big_shl(&s, 1);
        big_shl(&mPlus, 1);
    }

    /* Scale so that the first generated digit is the leading one. */
    nK = 0;

    for (;;) {
        big_copy(&tmp, &r);
        big_add(&tmp, &mPlus);

        if (big_cmp(&tmp, &s) >= 0) {
            big_mul_add(&s, 10, 0);
            nK++;
        } else {
            break;
        }
    }

    for (;;) {
        big_copy(&tmp, &r);
        big_add(&tmp, &mPlus);
        big_mul_add(&tmp, 10, 0);

        if (big_cmp(&tmp, &s) < 0) {
            big_mul_add(&r, 10, 0);
            big_mul_add(&mPlus, 10, 0);
            big_mul_add(&mMinus, 10, 0);
            nK--;
        } else {
            break;
        }
    }

    *pnDecimalPoint = nK;

    for (;;) {
        int nDigit = 0;
        int bHighOk = 0;

        big_mul_add(&r, 10, 0);
        big_mul_add(&mPlus, 10, 0);
        big_mul_add(&mMinus, 10, 0);

        nDigit = 0;

        while (big_cmp(&r, &s) >= 0) {
            big_sub(&r, &s);
            nDigit++;

            if (nDigit > 9) {
                break;
            }
        }

        bLowOk = (big_cmp(&r, &mMinus) < 0) ? 1 : 0;

        big_copy(&tmp, &r);
        big_add(&tmp, &mPlus);
        bHighOk = (big_cmp(&tmp, &s) > 0) ? 1 : 0;

        if (bLowOk || bHighOk || (nCount >= 17)) {
            /* Choose the digit that stays closest to the true value. */
            if (bLowOk && (!bHighOk)) {
                /* keep */
            } else if (bHighOk && (!bLowOk)) {
                nDigit++;
            } else {
                big_copy(&tmp, &r);
                big_shl(&tmp, 1);

                if (big_cmp(&tmp, &s) > 0) {
                    nDigit++;
                }
            }

            pDigits[nCount++] = (char)('0' + nDigit);
            break;
        }

        pDigits[nCount++] = (char)('0' + nDigit);
    }

    pDigits[nCount] = 0;

    return nCount;
}

/* Generates exactly nWanted digits starting at the leading digit, rounding
 * the tail. Used by toFixed and toPrecision.                                */
static int dtoa_digits_counted(double nValue, int nWanted, char *pDigits, int *pnDecimalPoint, int bFixedPoint)
{
    XBig num;
    XBig den;
    XBig tmp;
    unsigned long long nMantissa = 0;
    int nExponent = 0;
    int bIsSubnormal = 0;
    int nK = 0;
    int nCount = 0;
    int i = 0;
    int nGenerate = 0;

    double_decompose(nValue, &nMantissa, &nExponent, &bIsSubnormal);

    big_from_u64(&num, nMantissa);
    big_from_u64(&den, 1);

    if (nExponent >= 0) {
        big_shl(&num, nExponent);
    } else {
        big_shl(&den, -nExponent);
    }

    /* Find the decimal exponent: the smallest nK with value < 10^nK. */
    nK = 0;

    {
        /* Compare num/den against 10^nK by scaling whichever side is needed. */
        XBig lhs;
        XBig rhs;

        nK = 0;

        for (;;) {
            big_copy(&lhs, &num);
            big_copy(&rhs, &den);

            if (nK >= 0) {
                big_mul_pow10(&rhs, nK);
            } else {
                big_mul_pow10(&lhs, -nK);
            }

            if (big_cmp(&lhs, &rhs) >= 0) {
                nK++;
            } else {
                break;
            }

            if (nK > 320) {
                break;
            }
        }

        for (;;) {
            big_copy(&lhs, &num);
            big_copy(&rhs, &den);

            if ((nK - 1) >= 0) {
                big_mul_pow10(&rhs, nK - 1);
            } else {
                big_mul_pow10(&lhs, -(nK - 1));
            }

            if (big_cmp(&lhs, &rhs) < 0) {
                nK--;
            } else {
                break;
            }

            /* The smallest subnormal needs nK = -323, so the guard sits
             * below the subnormal floor; -340 matches the range check in
             * decimal_to_double. */
            if (nK < -340) {
                break;
            }
        }
    }

    *pnDecimalPoint = nK;

    /* Fixed point asks for nWanted digits after the point, so the digit
     * count depends on the magnitude.                                      */
    nGenerate = bFixedPoint ? (nK + nWanted) : nWanted;

    if (nGenerate <= 0) {
        /* The value sits below the last requested place. It still rounds up
         * to one unit there when it reaches half of it, which is what makes
         * (0.5).toFixed(0) come out as "1".                                 */
        XBig lhs;
        XBig rhs;

        big_copy(&lhs, &num);
        big_copy(&rhs, &den);
        big_shl(&lhs, 1);
        big_mul_pow10(&lhs, nWanted);

        pDigits[0] = 0;

        if (big_cmp(&lhs, &rhs) >= 0) {
            pDigits[0] = '1';
            pDigits[1] = 0;
            *pnDecimalPoint = 1 - nWanted;

            return 1;
        }

        return 0;
    }

    if (nGenerate > 440) {
        nGenerate = 440;
    }

    /* Scale so that num/den lands in [1, 10): the first division then yields
     * the leading digit and each following round shifts one place.         */
    if (nK - 1 >= 0) {
        big_mul_pow10(&den, nK - 1);
    } else {
        big_mul_pow10(&num, 1 - nK);
    }

    for (i = 0; i < nGenerate; i++) {
        int nDigit = 0;

        while (big_cmp(&num, &den) >= 0) {
            big_sub(&num, &den);
            nDigit++;

            if (nDigit > 9) {
                break;
            }
        }

        pDigits[nCount++] = (char)('0' + nDigit);
        big_mul_add(&num, 10, 0);
    }

    /* Round the tail. ECMAScript picks the larger candidate on a tie. */
    {
        big_copy(&tmp, &den);
        big_mul_add(&tmp, 10, 0);
        big_shl(&num, 1);

        if (big_cmp(&num, &tmp) >= 0) {
            int j = nCount - 1;

            for (;;) {
                if (j < 0) {
                    /* Carried past the first digit: 999 -> 1000 */
                    for (j = nCount; j > 0; j--) {
                        pDigits[j] = pDigits[j - 1];
                    }

                    pDigits[0] = '1';
                    nCount++;
                    (*pnDecimalPoint)++;
                    break;
                }

                if (pDigits[j] == '9') {
                    pDigits[j] = '0';
                    j--;
                } else {
                    pDigits[j]++;
                    break;
                }
            }
        }
    }

    pDigits[nCount] = 0;

    return nCount;
}

/* ------------------------------------------------------------------------ */
/*  Formatting                                                               */
/* ------------------------------------------------------------------------ */

typedef struct {
    char *pBuffer;
    size_t nCapacity;
    size_t nWritten;
} XOut;

static void out_char(XOut *pOut, char nChar)
{
    if (pOut->nWritten + 1 < pOut->nCapacity) {
        pOut->pBuffer[pOut->nWritten] = nChar;
    }

    pOut->nWritten++;
}

static void out_text(XOut *pOut, const char *pText)
{
    while (*pText) {
        out_char(pOut, *pText++);
    }
}

static void out_int(XOut *pOut, int nValue)
{
    char sTemp[16];
    int nCount = 0;

    if (nValue < 0) {
        out_char(pOut, '-');
        nValue = -nValue;
    }

    if (nValue == 0) {
        sTemp[nCount++] = '0';
    }

    while (nValue) {
        sTemp[nCount++] = (char)('0' + (nValue % 10));
        nValue /= 10;
    }

    while (nCount--) {
        out_char(pOut, sTemp[nCount]);
    }
}

static int out_finish(XOut *pOut)
{
    if (pOut->nCapacity) {
        size_t nAt = (pOut->nWritten < pOut->nCapacity) ? pOut->nWritten : (pOut->nCapacity - 1);

        pOut->pBuffer[nAt] = 0;
    }

    return (int)pOut->nWritten;
}

/* Handles NaN, the infinities and zero for every entry point. */
static int dtoa_special(double nValue, char *pBuffer, size_t nBufferSize, int *pbHandled)
{
    XOut out;

    out.pBuffer = pBuffer;
    out.nCapacity = nBufferSize;
    out.nWritten = 0;
    *pbHandled = 1;

    if (x_isnan(nValue)) {
        out_text(&out, "NaN");

        return out_finish(&out);
    }

    if (x_isinf(nValue)) {
        out_text(&out, (nValue < 0) ? "-Infinity" : "Infinity");

        return out_finish(&out);
    }

    /* Zero is deliberately not handled here. It prints as "0" only for the
     * shortest form; the counted forms have to pad it, so (0).toFixed(2) is
     * "0.00" and (0).toPrecision(3) is likewise "0.00". Each caller does its
     * own zero case.                                                       */

    *pbHandled = 0;

    return 0;
}

int x_dtoa_shortest(double nValue, char *pBuffer, size_t nBufferSize)
{
    char sDigits[32];
    int nCount = 0;
    int nPoint = 0;
    int bHandled = 0;
    int nResult = dtoa_special(nValue, pBuffer, nBufferSize, &bHandled);
    XOut out;
    int i = 0;

    if (bHandled) {
        return nResult;
    }

    out.pBuffer = pBuffer;
    out.nCapacity = nBufferSize;
    out.nWritten = 0;

    /* String(-0) is "0", so the sign is tested before the zero case. */
    if (nValue < 0) {
        out_char(&out, '-');
        nValue = -nValue;
    }

    if (nValue == 0) {
        out_char(&out, '0');

        return out_finish(&out);
    }

    nCount = dtoa_digits_shortest(nValue, sDigits, &nPoint);

    /* ECMAScript Number::toString step 5 onwards. */
    if ((nPoint >= -5) && (nPoint <= 21)) {
        if (nPoint <= 0) {
            out_char(&out, '0');
            out_char(&out, '.');

            for (i = 0; i < -nPoint; i++) {
                out_char(&out, '0');
            }

            for (i = 0; i < nCount; i++) {
                out_char(&out, sDigits[i]);
            }
        } else if (nPoint >= nCount) {
            for (i = 0; i < nCount; i++) {
                out_char(&out, sDigits[i]);
            }

            for (i = nCount; i < nPoint; i++) {
                out_char(&out, '0');
            }
        } else {
            for (i = 0; i < nPoint; i++) {
                out_char(&out, sDigits[i]);
            }

            out_char(&out, '.');

            for (i = nPoint; i < nCount; i++) {
                out_char(&out, sDigits[i]);
            }
        }
    } else {
        out_char(&out, sDigits[0]);

        if (nCount > 1) {
            out_char(&out, '.');

            for (i = 1; i < nCount; i++) {
                out_char(&out, sDigits[i]);
            }
        }

        out_char(&out, 'e');
        out_char(&out, (nPoint - 1 >= 0) ? '+' : '-');
        out_int(&out, (nPoint - 1 >= 0) ? (nPoint - 1) : -(nPoint - 1));
    }

    return out_finish(&out);
}

int x_dtoa_fixed(double nValue, int nDigits, char *pBuffer, size_t nBufferSize)
{
    char sDigits[460];
    int nCount = 0;
    int nPoint = 0;
    XOut out;
    int i = 0;
    int bNegative = 0;
    int bHandled = 0;

    if (x_isnan(nValue) || x_isinf(nValue)) {
        int nSpecial = dtoa_special(nValue, pBuffer, nBufferSize, &bHandled);

        if (bHandled) {
            return nSpecial;
        }
    }

    if (nDigits < 0) {
        nDigits = 0;
    }

    if (nDigits > 100) {
        nDigits = 100;
    }

    out.pBuffer = pBuffer;
    out.nCapacity = nBufferSize;
    out.nWritten = 0;

    /* The spec's test is "x < 0", which is false for -0, so (-0).toFixed(2)
     * is "0.00" and not "-0.00".                                           */
    if (nValue < 0) {
        bNegative = 1;
        nValue = -nValue;
    }

    if (nValue == 0) {
        nCount = 0;
        nPoint = 0;
    } else {
        nCount = dtoa_digits_counted(nValue, nDigits, sDigits, &nPoint, 1);
    }

    if (bNegative && ((nCount > 0) || (nDigits > 0))) {
        /* -0.00 keeps its sign only when the value really was negative. */
        int bAllZero = 1;

        for (i = 0; i < nCount; i++) {
            if (sDigits[i] != '0') {
                bAllZero = 0;
                break;
            }
        }

        if (!bAllZero) {
            out_char(&out, '-');
        }
    }

    /* Integer part. */
    if (nPoint <= 0) {
        out_char(&out, '0');
    } else {
        for (i = 0; i < nPoint; i++) {
            out_char(&out, (i < nCount) ? sDigits[i] : '0');
        }
    }

    if (nDigits > 0) {
        out_char(&out, '.');

        for (i = 0; i < nDigits; i++) {
            int nIndex = nPoint + i;

            if ((nIndex < 0) || (nIndex >= nCount)) {
                out_char(&out, '0');
            } else {
                out_char(&out, sDigits[nIndex]);
            }
        }
    }

    return out_finish(&out);
}

int x_dtoa_precision(double nValue, int nDigits, char *pBuffer, size_t nBufferSize)
{
    char sDigits[460];
    int nCount = 0;
    int nPoint = 0;
    XOut out;
    int i = 0;
    int bHandled = 0;
    int nResult = dtoa_special(nValue, pBuffer, nBufferSize, &bHandled);

    if (bHandled) {
        return nResult;
    }

    if (nDigits < 1) {
        nDigits = 1;
    }

    if (nDigits > 100) {
        nDigits = 100;
    }

    out.pBuffer = pBuffer;
    out.nCapacity = nBufferSize;
    out.nWritten = 0;

    if (nValue < 0) {
        out_char(&out, '-');
        nValue = -nValue;
    }

    /* Zero is its own case in the spec: "0", then a point and nDigits-1
     * zeros. Going through the digit generator would lose the trailing
     * zeros.                                                               */
    if (nValue == 0) {
        out_char(&out, '0');

        if (nDigits > 1) {
            out_char(&out, '.');

            for (i = 1; i < nDigits; i++) {
                out_char(&out, '0');
            }
        }

        return out_finish(&out);
    }

    nCount = dtoa_digits_counted(nValue, nDigits, sDigits, &nPoint, 0);

    /* dtoa_digits_counted may return fewer characters than asked for; the
     * spec works with exactly nDigits, so the rest are zeros.              */
    while (nCount < nDigits) {
        sDigits[nCount++] = '0';
    }

    /* ES5 15.7.4.7. Two differences from %g, and both are visible:
     *   - the exponential threshold is e < -6 or e >= p, not e < -4;
     *   - trailing zeros are kept, so (1e-7).toPrecision(2) is "1.0e-7"
     *     rather than "1e-7".
     * nPoint is the position of the decimal point, so the spec's e is
     * nPoint - 1.                                                          */
    {
        int nExponent = nPoint - 1;

        if ((nExponent < -6) || (nExponent >= nDigits)) {
            out_char(&out, sDigits[0]);

            if (nDigits > 1) {
                out_char(&out, '.');

                for (i = 1; i < nDigits; i++) {
                    out_char(&out, sDigits[i]);
                }
            }

            out_char(&out, 'e');
            out_char(&out, (nExponent >= 0) ? '+' : '-');

            /* No zero padding: ECMAScript writes 1.2e+3 where %g would
             * produce 1.2e+03. x_dtoa_shortest spells it the same way.     */
            out_int(&out, (nExponent >= 0) ? nExponent : -nExponent);
        } else if (nExponent == nDigits - 1) {
            /* The point falls just past the last digit: no fraction. */
            for (i = 0; i < nDigits; i++) {
                out_char(&out, sDigits[i]);
            }
        } else if (nExponent >= 0) {
            for (i = 0; i < nExponent + 1; i++) {
                out_char(&out, sDigits[i]);
            }

            out_char(&out, '.');

            for (i = nExponent + 1; i < nDigits; i++) {
                out_char(&out, sDigits[i]);
            }
        } else {
            out_char(&out, '0');
            out_char(&out, '.');

            for (i = 0; i < -(nExponent + 1); i++) {
                out_char(&out, '0');
            }

            for (i = 0; i < nDigits; i++) {
                out_char(&out, sDigits[i]);
            }
        }
    }

    return out_finish(&out);
}

/* ------------------------------------------------------------------------ */
/*  printf conversions                                                       */
/* ------------------------------------------------------------------------ */

/* The three entry points above answer ECMAScript questions; %e, %f and %g
 * are C questions and differ in the details (the exponent is at least two
 * digits, %g switches to the exponential form at e < -4 rather than e < -6
 * and drops the trailing zeros the ECMAScript forms keep). They share the
 * digit generator, so the difference is entirely in the layout below.
 *
 * Two deliberate departures from a hosted printf, both a consequence of
 * reusing the generator:
 *   - a tie rounds away from zero, where a hosted printf rounds to even, so
 *     x_dtoa_cformat(0.5, 'f', 0, ...) is "1" and glibc's "%.0f" is "0";
 *   - dtoa_digits_counted stops after 440 digits, so a precision that would
 *     reach past that on a very large value is padded with zeros instead of
 *     continuing the exact expansion.
 * Neither is reachable from anything the project prints; both are cheaper to
 * write down than to remove.                                                */

#define CFMT_MAX_PRECISION 400

/* One fraction digit of a fixed-point layout: pDigits holds nCount digits
 * whose decimal point sits after nPoint of them, and everything outside that
 * window is a zero.                                                         */
static char cfmt_fraction_digit(const char *pDigits, int nCount, int nPoint, int nIndex)
{
    int nAt = nPoint + nIndex;

    if ((nAt < 0) || (nAt >= nCount)) {
        return '0';
    }

    return pDigits[nAt];
}

/* %f of a non-negative, finite nValue. bStrip drops the trailing zeros of
 * the fraction, which is what %g does without the # flag.                   */
static void cfmt_fixed(double nValue, int nPrecision, int bAlt, int bStrip, XOut *pOut)
{
    char sDigits[460];
    int nCount = 0;
    int nPoint = 0;
    int nFraction = 0;
    int i = 0;

    if (nValue != 0) {
        nCount = dtoa_digits_counted(nValue, nPrecision, sDigits, &nPoint, 1);
    }

    if (nPoint <= 0) {
        out_char(pOut, '0');
    } else {
        for (i = 0; i < nPoint; i++) {
            out_char(pOut, (i < nCount) ? sDigits[i] : '0');
        }
    }

    nFraction = nPrecision;

    if (bStrip) {
        while ((nFraction > 0) && (cfmt_fraction_digit(sDigits, nCount, nPoint, nFraction - 1) == '0')) {
            nFraction--;
        }
    }

    if ((nFraction > 0) || bAlt) {
        out_char(pOut, '.');
    }

    for (i = 0; i < nFraction; i++) {
        out_char(pOut, cfmt_fraction_digit(sDigits, nCount, nPoint, i));
    }
}

/* %e of a non-negative, finite nValue. The exponent carries a sign and at
 * least two digits, which is the one place C and ECMAScript disagree on the
 * spelling of the same number.                                              */
static void cfmt_exponential(double nValue, int nPrecision, int bAlt, int bStrip, int bUpper, XOut *pOut)
{
    char sDigits[460];
    int nCount = 0;
    int nPoint = 1;
    int nExponent = 0;
    int nFraction = 0;
    int i = 0;

    if (nValue != 0) {
        nCount = dtoa_digits_counted(nValue, nPrecision + 1, sDigits, &nPoint, 0);
    }

    while (nCount < (nPrecision + 1)) {
        sDigits[nCount++] = '0';
    }

    nExponent = (nValue != 0) ? (nPoint - 1) : 0;
    nFraction = nPrecision;

    if (bStrip) {
        while ((nFraction > 0) && (sDigits[nFraction] == '0')) {
            nFraction--;
        }
    }

    out_char(pOut, sDigits[0]);

    if ((nFraction > 0) || bAlt) {
        out_char(pOut, '.');
    }

    for (i = 1; i <= nFraction; i++) {
        out_char(pOut, sDigits[i]);
    }

    out_char(pOut, bUpper ? 'E' : 'e');
    out_char(pOut, (nExponent >= 0) ? '+' : '-');

    if (nExponent < 0) {
        nExponent = -nExponent;
    }

    if (nExponent < 10) {
        out_char(pOut, '0');
    }

    out_int(pOut, nExponent);
}

int x_dtoa_cformat(double nValue, char nConversion, int nPrecision, int bAlt, char *pBuffer, size_t nBufferSize)
{
    XOut out;
    int bUpper = ((nConversion == 'E') || (nConversion == 'F') || (nConversion == 'G')) ? 1 : 0;

    out.pBuffer = pBuffer;
    out.nCapacity = nBufferSize;
    out.nWritten = 0;

    /* C99 7.19.6.1: an infinity prints as "inf" and a NaN as "nan", in the
     * case of the conversion specifier. The sign belongs to the caller, so
     * neither carries one here.                                            */
    if (x_isnan(nValue)) {
        out_text(&out, bUpper ? "NAN" : "nan");

        return out_finish(&out);
    }

    if (x_isinf(nValue)) {
        out_text(&out, bUpper ? "INF" : "inf");

        return out_finish(&out);
    }

    nValue = x_fabs(nValue);

    if (nPrecision < 0) {
        nPrecision = 6;
    }

    if (nPrecision > CFMT_MAX_PRECISION) {
        nPrecision = CFMT_MAX_PRECISION;
    }

    if ((nConversion == 'f') || (nConversion == 'F')) {
        cfmt_fixed(nValue, nPrecision, bAlt, 0, &out);
    } else if ((nConversion == 'e') || (nConversion == 'E')) {
        cfmt_exponential(nValue, nPrecision, bAlt, 0, bUpper, &out);
    } else {
        /* %g. A precision of zero means one significant digit, and the style
         * is chosen from the exponent the value has once it is rounded to
         * that many digits - which is what the generator hands back.       */
        char sDigits[460];
        int nPoint = 1;
        int nExponent = 0;

        if (nPrecision == 0) {
            nPrecision = 1;
        }

        if (nValue != 0) {
            dtoa_digits_counted(nValue, nPrecision, sDigits, &nPoint, 0);
            nExponent = nPoint - 1;
        }

        if ((nExponent < -4) || (nExponent >= nPrecision)) {
            cfmt_exponential(nValue, nPrecision - 1, bAlt, !bAlt, bUpper, &out);
        } else {
            cfmt_fixed(nValue, nPrecision - 1 - nExponent, bAlt, !bAlt, &out);
        }
    }

    return out_finish(&out);
}
