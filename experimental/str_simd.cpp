#include <iostream>
#include <experimental/simd>
#include <vector>

namespace stdx = std::experimental;

void print_simd_bits(auto const& val)
{
    for (std::size_t i{}; i != std::size(val); ++i)
        std::cout << std::bitset<8>(val[i]) << ' ';
    std::cout << '\n';
}

int main()
{
    int t = 4;
    int k = 5;

    // Set for initial bitstring: k-1 bitstrings with t NLB in the first one, all 0s
    stdx::fixed_size_simd<int8_t, 8> lengths ([k](int8_t i) {return static_cast<int8_t>(i < k-1); } );
    lengths[0] = (1 << (t+1)) - 1;
    print_simd_bits(lengths);
    stdx::fixed_size_simd<int8_t, 8> bits = 0;

    // Count total bits
    size_t cnt = 0;
    for (size_t n = 0; n < lengths.size(); n++)
    {
        cnt += std::popcount(static_cast<uint8_t>(lengths[n]));
    }
    std::cout << "Total bits: " << cnt << std::endl;
}