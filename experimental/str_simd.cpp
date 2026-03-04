#include <iostream>
#include <experimental/simd>
#include <vector>
#include <chrono>

namespace stdx = std::experimental;
using simd_uint8 = stdx::fixed_size_simd<uint8_t, 8>;
using simd_uint8_mask = stdx::fixed_size_simd_mask<uint8_t, 8>;

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
    int h = 5;

    // Set for initial bitstring: k-1 bitstrings with t NLB in the first one, all 0s
    simd_uint8 lengths ([k](uint8_t i) {return static_cast<uint8_t>(i < k-1); } );
    lengths[0] = (1 << (t+1)) - 1;
    std::cout << "Lengths: ";
    print_simd_bits(lengths);
    simd_uint8 bits = 0;
    std::vector<int> levels { 0, 1, 2, 3 };

    // Bad: Runs sequentially
    simd_uint8_mask multiple_ones ([](uint8_t i) { return std::popcount(i) > 1; });

    // Count total bits
    auto start = std::chrono::high_resolution_clock::now();
    size_t cnt = 0;
    for (size_t n = 0; n < lengths.size(); n++)
    {
        cnt += std::popcount(static_cast<uint8_t>(lengths[n]));
    }
    auto stop = std::chrono::high_resolution_clock::now();
    std::cout << "Total bits: " << cnt << ", time: " << (stop - start) << std::endl;

    // Alternative
    start = std::chrono::high_resolution_clock::now();
    simd_uint8 nlb_counts { std::popcount(static_cast<uint8_t>(lengths[0])) - 1 };  /*([lengths](int8_t n) { 
        return static_cast<int8_t> (std::popcount(static_cast<uint8_t>(lengths[n])));
    });*/
    for (size_t n = 1; n < lengths.size(); n++)
    {
        nlb_counts[n] = nlb_counts[n-1] + std::popcount(static_cast<uint8_t>(lengths[n])) - (lengths[n] > 0);
    }
    //size_t total = stdx::reduce(bit_counts);
    stop = std::chrono::high_resolution_clock::now();
    std::cout << "Total (SIMD): " << static_cast<int>(nlb_counts[7]) << ", time: " << (stop - start) << std::endl;
    for (std::size_t i{}; i != nlb_counts.size(); ++i)
        std::cout << static_cast<int>(nlb_counts[i]) << ' ';
    std::cout << '\n';

    simd_uint8 clear_first_bit(~simd_uint8{0} & ~1);
    simd_uint8 pattern_zero_and_ones = lengths & clear_first_bit;
    simd_uint8 strings_after ([levels](uint8_t i) {
        return i < levels.size() ? static_cast<uint8_t>(levels.size() - i) : 0;
    });
    simd_uint8 needed_after ([h, levels](uint8_t i) {
        return i < levels.size() ? (h - levels[i] - 1) : 1;
    });
    simd_uint8_mask no_successor = (lengths > 0) and (nlb_counts == t) and (
             ((bits == pattern_zero_and_ones) and (strings_after == needed_after)) // Third case
          or (bits == lengths) // Fourth case
    );
    
    simd_uint8_mask has_successor = (lengths > 0) and !no_successor;
    std::cout << "Matches pattern: ";
    print_simd_bits(has_successor);

    int match = stdx::find_last_set(has_successor);
    
    // TODO: Doesn't take empty strings into account...
    if (nlb_counts[match] == t)
    {
        // Case A: No more open bits, we erase everything starting from the 1
        // Find the index of the last bit that was set to 1 (i.e. first 1 from the left)
        int last_one = std::countl_zero(static_cast<uint8_t>(bits[match]));
        if (last_one >= 0) 
        {
            // reset the 1 to a 0
            bits[match] &= ~(1u << (7 - last_one));
            // reset all the indexing bits starting from the same bit
            levels[match] &= (1u << (last_one - 1)) - 1;
        }
    }
    else 
    {
        // Case B: There are still open bits! Append to 10^j
        if ((match == levels.size() - 1 and levels[match] < h-2) or (match < levels.size() - 1 and levels[match] + 1 < levels[match] + 1) )
        {
            // There is an empty level we can use! 
            match ++; // We append to the next string
            bits[match] = 1; 
            levels[match] --; // Move the empty string up to the next higher level
        }
        else 
        {
            int first_new = std::popcount(static_cast<uint8_t>(levels[match]));
            bits[match] |= (1u << first_new);
        }
        // Add enough bits to fill t NLB
        int bits_before = match > 0 ? nlb_counts[match - 1] : 0;
        levels[match] |= (1u << (t - bits_before + 1));
    }
}