#include "GSSamplingTest.h"

#include <fstream>
#include <random>

#include <Ray/internal/Core.h>
#include <Ray/internal/SmallVector.h>
#include <SW/SW.h>
#include <Sys/Json.h>

#include "../Viewer.h"
#include "../eng/GameStateManager.h"
#include "../eng/Halton.h"
#include "../eng/Random.h"
#include "../gui/FontStorage.h"
#include "../gui/Renderer.h"
#include "../ren/Context.h"

namespace GSSamplingTestInternal {
extern const int g_primes[] = {
    2,    3,    5,    7,    11,   13,   17,   19,   23,   29,   31,   37,   41,   43,   47,   53,   59,   61,   67,
    71,   73,   79,   83,   89,   97,   101,  103,  107,  109,  113,  127,  131,  137,  139,  149,  151,  157,  163,
    167,  173,  179,  181,  191,  193,  197,  199,  211,  223,  227,  229,  233,  239,  241,  251,  257,  263,  269,
    271,  277,  281,  283,  293,  307,  311,  313,  317,  331,  337,  347,  349,  353,  359,  367,  373,  379,  383,
    389,  397,  401,  409,  419,  421,  431,  433,  439,  443,  449,  457,  461,  463,  467,  479,  487,  491,  499,
    503,  509,  521,  523,  541,  547,  557,  563,  569,  571,  577,  587,  593,  599,  601,  607,  613,  617,  619,
    631,  641,  643,  647,  653,  659,  661,  673,  677,  683,  691,  701,  709,  719,  727,  733,  739,  743,  751,
    757,  761,  769,  773,  787,  797,  809,  811,  821,  823,  827,  829,  839,  853,  857,  859,  863,  877,  881,
    883,  887,  907,  911,  919,  929,  937,  941,  947,  953,  967,  971,  977,  983,  991,  997,  1009, 1013, 1019,
    1021, 1031, 1033, 1039, 1049, 1051, 1061, 1063, 1069, 1087, 1091, 1093, 1097, 1103, 1109, 1117, 1123, 1129, 1151,
    1153, 1163, 1171, 1181, 1187, 1193, 1201, 1213, 1217, 1223, 1229, 1231, 1237, 1249, 1259, 1277, 1279, 1283, 1289,
    1291, 1297, 1301, 1303, 1307, 1319, 1321, 1327, 1361, 1367, 1373, 1381, 1399, 1409, 1423, 1427, 1429, 1433, 1439,
    1447, 1451, 1453, 1459, 1471, 1481, 1483, 1487, 1489, 1493, 1499, 1511, 1523, 1531, 1543, 1549, 1553, 1559, 1567,
    1571, 1579, 1583, 1597, 1601, 1607, 1609, 1613, 1619, 1621, 1627, 1637, 1657, 1663, 1667, 1669, 1693, 1697, 1699,
    1709, 1721, 1723, 1733, 1741, 1747, 1753, 1759, 1777, 1783, 1787, 1789, 1801, 1811, 1823, 1831, 1847, 1861, 1867,
    1871, 1873, 1877, 1879, 1889, 1901, 1907, 1913, 1931, 1933, 1949, 1951, 1973, 1979, 1987, 1993, 1997, 1999, 2003,
    2011, 2017, 2027, 2029, 2039, 2053, 2063, 2069, 2081, 2083, 2087, 2089, 2099, 2111, 2113, 2129, 2131, 2137, 2141,
    2143, 2153, 2161, 2179, 2203, 2207, 2213, 2221, 2237, 2239, 2243, 2251, 2267, 2269, 2273, 2281, 2287, 2293, 2297,
    2309, 2311, 2333, 2339, 2341, 2347, 2351, 2357, 2371, 2377, 2381, 2383, 2389, 2393, 2399, 2411, 2417, 2423, 2437,
    2441, 2447, 2459, 2467, 2473, 2477, 2503, 2521, 2531, 2539, 2543, 2549, 2551, 2557, 2579, 2591, 2593, 2609, 2617,
    2621, 2633, 2647, 2657, 2659, 2663, 2671, 2677, 2683, 2687, 2689, 2693, 2699, 2707, 2711, 2713, 2719, 2729, 2731,
    2741, 2749, 2753, 2767, 2777, 2789, 2791, 2797, 2801, 2803, 2819, 2833, 2837, 2843, 2851, 2857, 2861, 2879, 2887,
    2897, 2903, 2909, 2917, 2927, 2939, 2953, 2957, 2963, 2969, 2971, 2999, 3001, 3011, 3019, 3023, 3037, 3041, 3049,
    3061, 3067, 3079, 3083, 3089, 3109, 3119, 3121, 3137, 3163, 3167, 3169, 3181, 3187, 3191, 3203, 3209, 3217, 3221,
    3229, 3251, 3253, 3257, 3259, 3271, 3299, 3301, 3307, 3313, 3319, 3323, 3329, 3331, 3343, 3347, 3359, 3361, 3371,
    3373, 3389, 3391, 3407, 3413, 3433, 3449, 3457, 3461, 3463, 3467, 3469, 3491, 3499, 3511, 3517, 3527, 3529, 3533,
    3539, 3541, 3547, 3557, 3559, 3571, 3581, 3583, 3593, 3607, 3613, 3617, 3623, 3631, 3637, 3643, 3659, 3671, 3673,
    3677, 3691, 3697, 3701, 3709, 3719, 3727, 3733, 3739, 3761, 3767, 3769, 3779, 3793, 3797, 3803, 3821, 3823, 3833,
    3847, 3851, 3853, 3863, 3877, 3881, 3889, 3907, 3911, 3917, 3919, 3923, 3929, 3931, 3943, 3947, 3967, 3989, 4001,
    4003, 4007, 4013, 4019, 4021, 4027, 4049, 4051, 4057, 4073, 4079, 4091, 4093, 4099, 4111, 4127, 4129, 4133, 4139,
    4153, 4157, 4159, 4177, 4201, 4211, 4217, 4219, 4229, 4231, 4241, 4243, 4253, 4259, 4261, 4271, 4273, 4283, 4289,
    4297, 4327, 4337, 4339, 4349, 4357, 4363, 4373, 4391, 4397, 4409, 4421, 4423, 4441, 4447, 4451, 4457, 4463, 4481,
    4483, 4493, 4507, 4513, 4517, 4519, 4523, 4547, 4549, 4561, 4567, 4583, 4591, 4597, 4603, 4621, 4637, 4639, 4643,
    4649, 4651, 4657, 4663, 4673, 4679, 4691, 4703, 4721, 4723, 4729, 4733, 4751, 4759, 4783, 4787, 4789, 4793, 4799,
    4801, 4813, 4817, 4831, 4861, 4871, 4877, 4889, 4903, 4909, 4919, 4931, 4933, 4937, 4943, 4951, 4957, 4967, 4969,
    4973, 4987, 4993, 4999, 5003, 5009, 5011, 5021, 5023, 5039, 5051, 5059, 5077, 5081, 5087, 5099, 5101, 5107, 5113,
    5119, 5147, 5153, 5167, 5171, 5179, 5189, 5197, 5209, 5227, 5231, 5233, 5237, 5261, 5273, 5279, 5281, 5297, 5303,
    5309, 5323, 5333, 5347, 5351, 5381, 5387, 5393, 5399, 5407, 5413, 5417, 5419, 5431, 5437, 5441, 5443, 5449, 5471,
    5477, 5479, 5483, 5501, 5503, 5507, 5519, 5521, 5527, 5531, 5557, 5563, 5569, 5573, 5581, 5591, 5623, 5639, 5641,
    5647, 5651, 5653, 5657, 5659, 5669, 5683, 5689, 5693, 5701, 5711, 5717, 5737, 5741, 5743, 5749, 5779, 5783, 5791,
    5801, 5807, 5813, 5821, 5827, 5839, 5843, 5849, 5851, 5857, 5861, 5867, 5869, 5879, 5881, 5897, 5903, 5923, 5927,
    5939, 5953, 5981, 5987, 6007, 6011, 6029, 6037, 6043, 6047, 6053, 6067, 6073, 6079, 6089, 6091, 6101, 6113, 6121,
    6131, 6133, 6143, 6151, 6163, 6173, 6197, 6199, 6203, 6211, 6217, 6221, 6229, 6247, 6257, 6263, 6269, 6271, 6277,
    6287, 6299, 6301, 6311, 6317, 6323, 6329, 6337, 6343, 6353, 6359, 6361, 6367, 6373, 6379, 6389, 6397, 6421, 6427,
    6449, 6451, 6469, 6473, 6481, 6491, 6521, 6529, 6547, 6551, 6553, 6563, 6569, 6571, 6577, 6581, 6599, 6607, 6619,
    6637, 6653, 6659, 6661, 6673, 6679, 6689, 6691, 6701, 6703, 6709, 6719, 6733, 6737, 6761, 6763, 6779, 6781, 6791,
    6793, 6803, 6823, 6827, 6829, 6833, 6841, 6857, 6863, 6869, 6871, 6883, 6899, 6907, 6911, 6917, 6947, 6949, 6959,
    6961, 6967, 6971, 6977, 6983, 6991, 6997, 7001, 7013, 7019, 7027, 7039, 7043, 7057, 7069, 7079, 7103, 7109, 7121,
    7127, 7129, 7151, 7159, 7177, 7187, 7193, 7207, 7211, 7213, 7219, 7229, 7237, 7243, 7247, 7253, 7283, 7297, 7307,
    7309, 7321, 7331, 7333, 7349, 7351, 7369, 7393, 7411, 7417, 7433, 7451, 7457, 7459, 7477, 7481, 7487, 7489, 7499,
    7507, 7517, 7523, 7529, 7537, 7541, 7547, 7549, 7559, 7561, 7573, 7577, 7583, 7589, 7591, 7603, 7607, 7621, 7639,
    7643, 7649, 7669, 7673, 7681, 7687, 7691, 7699, 7703, 7717, 7723, 7727, 7741, 7753, 7757, 7759, 7789, 7793, 7817,
    7823, 7829, 7841, 7853, 7867, 7873, 7877, 7879, 7883, 7901, 7907, 7919};
extern const int g_primes_count = sizeof(g_primes) / sizeof(float);

float EvalFunc(const float x, const float y, const float xmax, const float ymax) {
    const float Pi = 3.14159265358979323846f;
    return 0.5f + 0.5f * std::sin(2.0f * Pi * (x / xmax) * std::exp(8.0f * (x / xmax)));
}

uint32_t hash(uint32_t x) {
    x = ((x >> 16) ^ x) * 0x45d9f3b;
    x = ((x >> 16) ^ x) * 0x45d9f3b;
    x = (x >> 16) ^ x;
    return x;
}

float construct_float(uint32_t m) {
    const uint32_t ieeeMantissa = 0x007FFFFFu; // binary32 mantissa bitmask
    const uint32_t ieeeOne = 0x3F800000u;      // 1.0 in IEEE binary32

    m &= ieeeMantissa; // Keep only mantissa bits (fractional part)
    m |= ieeeOne;      // Add fractional part to 1.0

    float f = reinterpret_cast<float &>(m); // Range [1:2]
    return f - 1.0f;                        // Range [0:1]
}

std::vector<uint16_t> radical_inv_perms;

std::mt19937 gen;
std::uniform_real_distribution<float> n_float_distr(0, 1);

// clang-format off
unsigned permute(unsigned i, unsigned l, unsigned p) {
    unsigned w = l - 1;
    w |= w >> 1;
    w |= w >> 2;
    w |= w >> 4;
    w |= w >> 8;
    w |= w >> 16;
    do {
        i ^= p;             i *= 0xe170893d;
        i ^= p       >> 16;
        i ^= (i & w) >> 4;
        i ^= p       >> 8;  i *= 0x0929eb3f;
        i ^= p       >> 23;
        i ^= (i & w) >> 1;  i *= 1 | p >> 27;
                            i *= 0x6935fa69;
        i ^= (i & w) >> 11; i *= 0x74dcb303;
        i ^= (i & w) >> 2;  i *= 0x9e501cc3;
        i ^= (i & w) >> 2;  i *= 0xc860a3df;
        i &= w;
        i ^= i       >> 5;
    } while (i >= l);
    return (i + p) % l;
}

float randfloat(unsigned i, unsigned p) {
    i ^= p;
    i ^= i >> 17;
    i ^= i >> 10; i *= 0xb36534e5;
    i ^= i >> 12;
    i ^= i >> 21; i *= 0x93fc4795;
    i ^= 0xdf6e307f;
    i ^= i >> 17; i *= 1 | p >> 18;
    return i * (1.0f / 4294967808.0f);
}

std::pair<float, float> cmj_ordered(int s, int m, int n, int p) {
    int sx = permute(s % m, m, p * 0xa511e9b3);
    int sy = permute(s / m, n, p * 0x63d83595);
    float jx = randfloat(s, p * 0xa399d265);
    float jy = randfloat(s, p * 0x711ad6a5);
    std::pair<float, float> r = {(s % m + (sy + jx) / n) / m,
                                 (s / m + (sx + jy) / m) / n};
    return r;
}

std::pair<float, float> cmj(int s, int N, int p, float a = 1.0f) {
    int m = static_cast<int>(sqrtf(N * a));
    int n = (N + m - 1) / m;
    s = permute(s, N, p * 0x51633e2d);
    int sx = permute(s % m, m, p * 0x68bc21eb);
    int sy = permute(s / m, n, p * 0x02e5be93);
    float jx = randfloat(s, p * 0x967a889b);
    float jy = randfloat(s, p * 0x368cc8b7);
    std::pair<float, float> r = {(sx + (sy + jx) / n) / m,
                                 (s + jy) / N};
    return r;
}
// clang-format on

uint32_t hash_combine(uint32_t seed, uint32_t v) { return seed ^ (v + (seed << 6) + (seed >> 2)); }

inline uint32_t reverse_bits(uint32_t x) {
    x = (((x & 0xaaaaaaaa) >> 1) | ((x & 0x55555555) << 1));
    x = (((x & 0xcccccccc) >> 2) | ((x & 0x33333333) << 2));
    x = (((x & 0xf0f0f0f0) >> 4) | ((x & 0x0f0f0f0f) << 4));
    x = (((x & 0xff00ff00) >> 8) | ((x & 0x00ff00ff) << 8));
    return ((x >> 16) | (x << 16));
}

inline uint32_t laine_karras_permutation(uint32_t x, uint32_t seed) {
    x += seed;
    x ^= x * 0x6c50b47cu;
    x ^= x * 0xb82f1e52u;
    x ^= x * 0xc7afe638u;
    x ^= x * 0x8d22f6e6u;
    return x;
}

inline uint32_t nested_uniform_scramble_base2(uint32_t x, uint32_t seed) {
    x = reverse_bits(x);
    x = laine_karras_permutation(x, seed);
    x = reverse_bits(x);
    return x;
}
} // namespace GSSamplingTestInternal

GSSamplingTest::GSSamplingTest(Viewer *viewer) : viewer_(viewer) {
    state_manager_ = viewer->GetComponent<GameStateManager>(STATE_MANAGER_KEY);
    random_ = viewer->random.get();
}

void GSSamplingTest::Enter() {
    swEnable(SW_FAST_PERSPECTIVE_CORRECTION);
    swEnable(SW_DEPTH_TEST);

    using namespace GSSamplingTestInternal;

    radical_inv_perms = ComputeRadicalInversePermutations(g_primes, g_primes_count, ::rand);
}

void GSSamplingTest::Exit() {}

void GSSamplingTest::Draw(uint64_t dt_us) {
    using namespace Ren;
    using namespace GSSamplingTestInternal;

    const int width = viewer_->width, height = viewer_->height;

    int sample_limit = 1;
    if (++iteration_ > sample_limit) {
        // return;
    }

    pixels_.resize(width * height * 4);

    static int loop_index = 0;
    if (iteration_ == 1) {
        std::fill(pixels_.begin(), pixels_.end(), 0.0f);
        loop_index++;
    }

    const int nsamplesx = 8, nsamplesy = 8;

#if 1
    // grid
    for (int y = 0; y < height; ++y) {
        for (int i = 1; i < nsamplesx * nsamplesy; ++i) {
            const int x = int((float(i) / float(nsamplesx * nsamplesy)) * width);

            pixels_[4 * (y * width + x) + 0] =
                std::max(pixels_[4 * (y * width + x) + 0], (i % nsamplesx == 0) ? 1.0f : 0.25f);
            pixels_[4 * (y * width + x) + 1] = pixels_[4 * (y * width + x) + 2] = pixels_[4 * (y * width + x) + 0];
            pixels_[4 * (y * width + x) + 3] = 1.0f;
        }
    }
    for (int j = 1; j < nsamplesx * nsamplesy; ++j) {
        for (int x = 0; x < width; ++x) {
            const int y = int((float(j) / float(nsamplesx * nsamplesy)) * height);

            pixels_[4 * (y * width + x) + 0] =
                std::max(pixels_[4 * (y * width + x) + 0], (j % nsamplesy == 0) ? 1.0f : 0.25f);
            pixels_[4 * (y * width + x) + 1] = pixels_[4 * (y * width + x) + 2] = pixels_[4 * (y * width + x) + 0];
            pixels_[4 * (y * width + x) + 3] = 1.0f;
        }
    }

    // canonical arrangement
    /*Ray::SmallVector<std::pair<float, float>, 128> points;
    for (int j = 0; j < nsamplesy; ++j) {
        for (int i = 0; i < nsamplesx; ++i) {
            const float rx = (i + (j + n_float_distr(gen)) / nsamplesy) / nsamplesx;
            const float ry = (j + (i + n_float_distr(gen)) / nsamplesx) / nsamplesy;

            points.emplace_back(rx, ry);
        }
    }

    // shuffle
    for (int j = 0; j < nsamplesy; ++j) {
        const int k = int(j + n_float_distr(gen) * (nsamplesy - j));
        for (int i = 0; i < nsamplesx; ++i) {
            // const int k = int(j + n_float_distr(gen) * (nsamplesy - j));
            std::swap(points[j * nsamplesx + i].first, points[k * nsamplesx + i].first);
        }
    }
    for (int i = 0; i < nsamplesx; ++i) {
        const int k = int(i + n_float_distr(gen) * (nsamplesx - i));
        for (int j = 0; j < nsamplesy; ++j) {
            // const int k = int(i + n_float_distr(gen) * (nsamplesx - i));
            std::swap(points[j * nsamplesx + i].second, points[j * nsamplesx + k].second);
        }
    }

    for (const auto &p : points) {
        const int x = int(p.first * float(width));
        const int y = height - 1 - int(p.second * float(height));

        for (int offy = -1; offy <= 1; ++offy) {
            const int yy = std::min(std::max(y + offy, 0), height - 1);
            for (int offx = -1; offx <= 1; ++offx) {
                const int xx = std::min(std::max(x + offx, 0), width - 1);

                pixels_[4 * (yy * width + xx) + 0] = pixels_[4 * (yy * width + xx) + 1] =
                    pixels_[4 * (yy * width + xx) + 2] = 1.0f;
                pixels_[4 * (yy * width + xx) + 3] = 1.0f;
            }
        }
    }*/

    // for (int i = 0; i < nsamplesx * nsamplesy; ++i) {
    /*{
        int i = iteration_;

        float rx, ry;
        // std::tie(rx, ry) = cmj_ordered(i, nsamplesx, nsamplesy, 0);
        std::tie(rx, ry) = cmj(i, nsamplesx * nsamplesy, 0);

        const int x = int(rx * float(width));
        const int y = height - 1 - int(ry * float(height));

        for (int offy = -1; offy <= 1; ++offy) {
            const int yy = std::min(std::max(y + offy, 0), height - 1);
            for (int offx = -1; offx <= 1; ++offx) {
                const int xx = std::min(std::max(x + offx, 0), width - 1);

                pixels_[4 * (yy * width + xx) + 0] = pixels_[4 * (yy * width + xx) + 1] =
                    pixels_[4 * (yy * width + xx) + 2] = 1.0f;
                pixels_[4 * (yy * width + xx) + 3] = 1.0f;
            }
        }
    }*/

    {
        const int i = std::min(iteration_, Ray::__pmj02_sample_count - 1);

        uint32_t x_seed = hash_combine(0x98fc82u, loop_index);
        uint32_t y_seed = hash_combine(0xab773au, loop_index);
        uint32_t shuffle_seed = hash_combine(0xacc75au, loop_index);

        uint32_t shuffled_i = nested_uniform_scramble_base2(i, shuffle_seed) % Ray::__pmj02_sample_count;

        //float rx = Ray::__pmj02_samples[2 * shuffled_i + 0];
        //float ry = Ray::__pmj02_samples[2 * shuffled_i + 1];

        uint32_t ux = Ray::__pmj02_samples[2 * shuffled_i + 0];
        uint32_t uy = Ray::__pmj02_samples[2 * shuffled_i + 1];

        ux = nested_uniform_scramble_base2(ux, x_seed);
        uy = nested_uniform_scramble_base2(uy, y_seed);

        float rx = float(ux >> 8) / 16777216.0f;
        float ry = float(uy >> 8) / 16777216.0f;

        const int x = int(rx * float(width));
        const int y = height - 1 - int(ry * float(height));

        for (int offy = -1; offy <= 1; ++offy) {
            const int yy = std::min(std::max(y + offy, 0), height - 1);
            for (int offx = -1; offx <= 1; ++offx) {
                const int xx = std::min(std::max(x + offx, 0), width - 1);

                pixels_[4 * (yy * width + xx) + 0] = pixels_[4 * (yy * width + xx) + 1] =
                    pixels_[4 * (yy * width + xx) + 2] = 1.0f;
                pixels_[4 * (yy * width + xx) + 3] = 1.0f;
            }
        }
    }
#endif

#if 0
    for (uint32_t y = 0; y < height; ++y) {
        if (y < height / 4) {
            for (uint32_t x = 0; x < width; ++x) {
                float sum = 0;

                for (uint32_t ny = 0; ny < nsamplesy; ++ny) {
                    for (uint32_t nx = 0; nx < nsamplesx; ++nx) {
                        sum += EvalFunc(float(x) + (iteration_ - 1) / float(sample_limit) +
                                            (float(nx) + 0.5f) / float(nsamplesx * sample_limit),
                                        float(y) + (float(ny) + 0.5f) / float(nsamplesy), float(width), float(height));
                    }
                }

                float val = sum / (nsamplesx * nsamplesy);
                float prev = pixels_[4 * (y * width + x) + 0];
                pixels_[4 * (y * width + x) + 0] += (val - prev) / iteration_;
                pixels_[4 * (y * width + x) + 1] = pixels_[4 * (y * width + x) + 2] = pixels_[4 * (y * width + x) + 0];
                pixels_[4 * (y * width + x) + 3] = 1.0f;
            }
        } else if (y < 2 * (height / 4)) {
            for (uint32_t x = 0; x < width; ++x) {
                float sum = 0;

                for (uint32_t ny = 0; ny < nsamplesy; ++ny) {
                    for (uint32_t nx = 0; nx < nsamplesx; ++nx) {
                        sum += EvalFunc(x + random_->GetNormalizedFloat(), y + random_->GetNormalizedFloat(),
                                        float(width), float(height));
                    }
                }

                float val = sum / (nsamplesx * nsamplesy);
                float prev = pixels_[4 * (y * width + x) + 0];
                pixels_[4 * (y * width + x) + 0] += (val - prev) / iteration_;
                pixels_[4 * (y * width + x) + 1] = pixels_[4 * (y * width + x) + 2] = pixels_[4 * (y * width + x) + 0];
                pixels_[4 * (y * width + x) + 3] = 1.0f;
            }
        } else if (y < 3 * (height / 4)) {
            for (uint32_t x = 0; x < width; ++x) {
                float sum = 0;

                for (uint32_t ny = 0; ny < nsamplesy; ++ny) {
                    for (uint32_t nx = 0; nx < nsamplesx; ++nx) {
                        sum += EvalFunc((x + (nx + random_->GetNormalizedFloat()) / nsamplesx),
                                        (y + (ny + random_->GetNormalizedFloat()) / nsamplesy), float(width),
                                        float(height));
                    }
                }

                float val = sum / (nsamplesx * nsamplesy);
                float prev = pixels_[4 * (y * width + x) + 0];
                pixels_[4 * (y * width + x) + 0] += (val - prev) / iteration_;
                pixels_[4 * (y * width + x) + 1] = pixels_[4 * (y * width + x) + 2] = pixels_[4 * (y * width + x) + 0];
                pixels_[4 * (y * width + x) + 3] = 1.0f;
            }
        } else {
            uint32_t ndx = 0;
            for (uint32_t x = 0; x < width; ++x) {
                float sum = 0;

                int i = int(iteration_) - 0;

                for (uint32_t ny = 0; ny < nsamplesy; ++ny) {
                    for (uint32_t nx = 0; nx < nsamplesx; ++nx) {
                        // int last_ndx = ndx;
                        // ndx = ((y - 3 * (height / 4)) * width + x) * nsamplesx * sample_limit * 31 + i * nsamplesx +
                        // nx;
                        ndx = ((y - 3 * (height / 4)) * width + x) * 31 + i * nsamplesx + nx;
                        // ndx = (i * (width + height) + x) * nsamplesx + nx;

                        float ff = construct_float(hash(x));

                        // float rx = RadicalInverse<3>(ndx);
                        float _unused;
                        float rx =
                            std::modf(Ray::ScrambledRadicalInverse(29, &radical_inv_perms[100], ndx) + ff, &_unused);
                        float ry = 0; // RadicalInverse<2>(i * nsamplesx + nx);

                        // sum += EvalFunc(x + (nx + rx) / nsamplesx, y + (ny + ry) / nsamplesy, width, height);
                        sum += EvalFunc(x + rx, y + ry, float(width), float(height));
                    }
                }

                float val = sum / (nsamplesx * nsamplesy);
                float prev = pixels_[4 * (y * width + x) + 0];
                pixels_[4 * (y * width + x) + 0] += (val - prev) / iteration_;
                pixels_[4 * (y * width + x) + 1] = pixels_[4 * (y * width + x) + 2] = pixels_[4 * (y * width + x) + 0];
                pixels_[4 * (y * width + x) + 3] = 1.0f;
            }
        }
    }
#endif

#if 0
    int i = (int)iteration_ * nsamplesx;
    for (int j = 0; j < nsamplesx; j++) {
        const float rx = Ray::ScrambledRadicalInverse(3, &radical_inv_perms[2], (3000 + i + j) % 4096);
        const float ry = Ray::ScrambledRadicalInverse(5, &radical_inv_perms[5], (3000 + i + j) % 4096);

        const int x = int(rx * float(width - 0));
        const int y = height - 1 - int(ry * float(height - 0));

        pixels_[4 * (y * width + x) + 0] = 1.0f;
        pixels_[4 * (y * width + x) + 1] = pixels_[4 * (y * width + x) + 2] = pixels_[4 * (y * width + x) + 0];
        pixels_[4 * (y * width + x) + 3] = 1.0f;

        /*for (int y = 0; y < height; y++) {
        	pixels_[4 * (y * width + x) + 0] = 1.0f;
        	pixels_[4 * (y * width + x) + 1] = pixels_[4 * (y * width + x) + 2] = pixels_[4 * (y * width + x) + 0];
        	pixels_[4 * (y * width + x) + 3] = 1.0f;
        }*/
    }
#endif

#if 0
    int i = (int)iteration_ * nsamplesx;
    for (int j = 0; j < (int)nsamplesx; j++) {
        //float rx = RadicalInverse<3>(i + j);
        float rx = Ray::ScrambledRadicalInverse(3, &radical_inv_perms[2], (i + j));
        //float ry = RadicalInverse<5>(i + j);
        float ry = Ray::ScrambledRadicalInverse(5, &radical_inv_perms[5], (i + j));


        if (true) {
            if (rx < 0.5f) {
                rx = std::sqrt(2.0f * rx) - 1.0f;
            } else {
                rx = 1.0f - std::sqrt(2.0f - 2.0f * rx);
            }

            if (ry < 0.5f) {
                ry = std::sqrt(2.0f * ry) - 1.0f;
            } else {
                ry = 1.0f - std::sqrt(2.0f - 2.0f * ry);
            }

            rx = rx * 0.5f + 0.5f;
            ry = ry * 0.5f + 0.5f;
        }

        int x = (int)(rx * (width - 0));
        int y = (int)(height - 1 - ry * (height - 0));

        pixels_[4 * (y * width + x) + 0] = 1.0f;
        pixels_[4 * (y * width + x) + 1] = pixels_[4 * (y * width + x) + 2] = pixels_[4 * (y * width + x) + 0];
        pixels_[4 * (y * width + x) + 3] = 1.0f;
    }
#endif

    swBlitPixels(0, 0, 0, SW_FLOAT, SW_FRGBA, width, height, &pixels_[0], 1);
}

void GSSamplingTest::Update(uint64_t dt_us) {}

void GSSamplingTest::HandleInput(const InputManager::Event &evt) {
    switch (evt.type) {
    case InputManager::RAW_INPUT_P1_DOWN: {

    } break;
    case InputManager::RAW_INPUT_P1_UP: {

    } break;
    case InputManager::RAW_INPUT_P1_MOVE: {

    } break;
    case InputManager::RAW_INPUT_KEY_UP: {
        if (evt.key == InputManager::RAW_INPUT_BUTTON_SPACE) {
            iteration_ = 0;
        }
    } break;
    case InputManager::RAW_INPUT_RESIZE: {
        iteration_ = 0;
    } break;
    default:
        break;
    }
}
