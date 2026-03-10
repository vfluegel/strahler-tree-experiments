#include <iostream>
#include <experimental/simd>
#include <vector>
#include <chrono>
#include <cassert>

#include "../examples/k3t2h3.hpp"

namespace stdx = std::experimental;
using simd_uint8 = stdx::fixed_size_simd<uint8_t, 8>;
using simd_uint8_mask = stdx::fixed_size_simd_mask<uint8_t, 8>;

#ifndef HAS_P
#include <numeric>

int p = h - 2;
inline std::vector<int>
make_indices(const std::vector<std::vector<bool>> &original) {
  std::vector<int> indices(original.size());
  std::iota(indices.begin(), indices.end(), 1);
  return indices;
}

std::vector<int> p_successors = make_indices(tree_b);
#endif

void print_simd_bits(auto const& val)
{
    for (std::size_t i{}; i != std::size(val); ++i)
        std::cout << std::bitset<8>(val[i]) << ' ';
    std::cout << '\n';
}

void print_vector(auto const& vec)
{
    for (int i : vec)
    {
        std::cout << i << " ";
    }
    std::cout << std::endl;   
}

void prog_p_successor(const int pindex, simd_uint8& lengths, simd_uint8& bits, std::vector<int>& levels)
{
    simd_uint8_mask has_bits = (lengths > 0);
    simd_uint8 nlb_counts { std::popcount(static_cast<uint8_t>(lengths[0])) - has_bits[0] };  /*([lengths](int8_t n) { 
        return static_cast<int8_t> (std::popcount(static_cast<uint8_t>(lengths[n])));
    });*/
    for (size_t n = 1; n < lengths.size(); n++)
    {
        // Add the NLB in this to the previous result, subtract the leading bit if there is one
        nlb_counts[n] = nlb_counts[n-1] + std::popcount(static_cast<uint8_t>(lengths[n])) - has_bits[n];
    }
    
    simd_uint8_mask smaller_than_p = simd_uint8 ([&levels](uint8_t i) { return i < levels.size() ? levels[i] : 0b11111111; }) <= simd_uint8{pindex};
    simd_uint8 clear_first_bit(~simd_uint8{0} & ~1);
    simd_uint8 pattern_zero_and_ones = lengths & clear_first_bit;
    simd_uint8 strings_after ([&levels](uint8_t i) {
        return static_cast<uint8_t>(levels.size() - i);
    });
    simd_uint8 needed_after ([&levels](uint8_t i) {
        return i < levels.size() ? (h - levels[i] - 1) : 0;
    });
    simd_uint8 nlb_before ([&nlb_counts](uint8_t i) { return i == 0 ? 0 : nlb_counts[i-1]; });
    simd_uint8_mask no_successor = has_bits and ((nlb_before == t) or ((nlb_counts == t) and (
             ((bits == pattern_zero_and_ones) and (strings_after == needed_after)) // Third case
          or (bits == lengths))) // Fourth case
    );
    
    simd_uint8_mask has_successor = smaller_than_p and has_bits and !no_successor;
    std::cout << "Matches pattern: ";
    print_simd_bits(has_successor);

    // Use the last match to determine the successor
    int match = stdx::find_last_set(has_successor);
    std::cout << "Last match is: " << match << std::endl;
    if (match == -1)
    {
        if (levels[0] == 0)
        {
            // We are at top
            levels.resize(1);
            levels[0] = -1;
            return;
        }
        else 
        {
            bits[0] = 1;
            match = 0;
            levels[0] = std::min(levels[0] - 1, p); 
        }
    }
    else if (nlb_counts[match] == t)
    {
        // Case A: No more open bits, we erase the tail 01^j
        int reset_bits = std::countl_one(static_cast<uint8_t>(bits[match] | ~lengths[match])) + 1;
        int current_bits = std::popcount(static_cast<uint8_t>(lengths[match]));
        // reset the 1 to a 0
        bits[match] &= (1u << (8-reset_bits)) - 1;
        lengths[match] &= (1u << (8-reset_bits)) - 1;
        // Determine new number of NLB
        // Depends on whether we reset the entire string!
        if (reset_bits < 8)
        {
            nlb_counts[match] -= current_bits - std::popcount(static_cast<uint8_t>(lengths[match]));
            match++;
        }
        else 
        {
            // The new string is empty...
            nlb_counts[match] -= (current_bits - 1);
            if ((std::cmp_equal(match + 1, levels.size()) and levels[match] < h-2) or 
                (std::cmp_less(match + 1, levels.size()) and levels[match] + 1 < levels[match + 1]) )
            { 
                // Empty level!
                levels[match] = std::cmp_equal(match + 1, levels.size()) ? levels[match] + 1 : levels[match + 1] - 1;
            }
            else levels[match] ++;
        }
    }
    else 
    {
        // Case B: There are still open bits! Append to 10^j
        if ((std::cmp_less(match + 1, levels.size()) and levels[match] + 1 < levels[match + 1] and levels[match] != p))
        {
            // There is an empty level we can use! 
            match ++; // We append to the next string
            bits[match] = 1; 
            levels[match] = std::min(p, levels[match] - 1); // Move the empty string up to the next higher level
        }
        else 
        {
            int first_new = std::popcount(static_cast<uint8_t>(lengths[match]));
            bits[match] |= (1u << first_new);
        }
    }
    if (match < k-1)
    {
        // Add enough bits to fill t NLB
        int bits_before = match > 0 ? nlb_counts[match - 1] : 0;
        lengths[match] |= (1u << (t - bits_before + 1)) - 1;

        // Now append the new strings
        levels.resize(match+1);
        int set_to_level = levels[match] + 1;
        while (std::cmp_less(levels.size(), k-1) and set_to_level <= h-2)
        {
            levels.push_back(set_to_level);
            set_to_level++;
        }
        simd_uint8 after_match ([match](uint8_t i) { 
            return i > match; 
        });
        simd_uint8 before_levels ([&levels](uint8_t i) { 
            return i < levels.size(); 
        });
        stdx::where((before_levels > 0 and after_match > 0), lengths) = 1;
        stdx::where((after_match > 0), bits) = 0;
    }
    
}

void parse_to_vec(size_t idx, std::vector<uint8_t>& bits, std::vector<uint8_t>& lengths, std::vector<int>& levels)
{
    bits.clear();
    lengths.clear();
    levels.clear();
    auto& entry_b = tree_b[idx];
    auto& entry_d = tree_d[idx];
    int under_construction = -1;
    size_t current_bit;
    for (size_t i = 0; i < entry_b.size(); i++)
    {
        if (under_construction == -1 or entry_d[i] != entry_d[i-1])
        {
            // New layer!
            under_construction++;
            bits.push_back(0);
            lengths.push_back(0);
            levels.push_back(entry_d[i]);
            current_bit = 0;
        }
        lengths[under_construction] |= (1 << current_bit);
        if (entry_b[i]) bits[under_construction] |= (1 << current_bit);
        current_bit++;
    }
    // Fill so it matches the width of SIMD
    bits.resize(8);
    lengths.resize(8);
}

int compare(const int pindex, size_t idxA, size_t idxB)
{
    // Read vectors from old format
    std::vector<uint8_t> bits_vec_A, lengths_vec_A;
    std::vector<int> levels_A;
    parse_to_vec(idxA, bits_vec_A, lengths_vec_A, levels_A);
    std::vector<uint8_t> bits_vec_B, lengths_vec_B;
    std::vector<int> levels_B;
    parse_to_vec(idxB, bits_vec_B, lengths_vec_B, levels_B);

    // Put vectors into SIMD
    simd_uint8 bits_A, bits_B, lengths_A, lengths_B;
    bits_A.copy_from(bits_vec_A.data(), std::experimental::element_aligned);
    bits_B.copy_from(bits_vec_B.data(), std::experimental::element_aligned);
    lengths_A.copy_from(lengths_vec_A.data(), std::experimental::element_aligned);
    lengths_B.copy_from(lengths_vec_B.data(), std::experimental::element_aligned);
    
    simd_uint8 shorter_string = lengths_A & lengths_B;
    simd_uint8 first_length_difference = (lengths_A ^ lengths_B) & ~((lengths_A ^ lengths_B) * 2);
    simd_uint8 bit_xor = bits_A ^ bits_B;
    simd_uint8_mask a_less = ((lengths_A < lengths_B) and (bit_xor & (shorter_string + first_length_difference)) == first_length_difference) or
                             ((lengths_A > lengths_B) and (bit_xor & (shorter_string + first_length_difference)) == 0);
    simd_uint8_mask b_less = ((lengths_B < lengths_A) and (bit_xor & (shorter_string + first_length_difference)) == first_length_difference) or
                             ((lengths_B > lengths_A) and (bit_xor & (shorter_string + first_length_difference)) == 0);


    simd_uint8 different_bits = (shorter_string & bit_xor);
    simd_uint8 first_bit_difference ([&different_bits](uint8_t i){ 
        return 1u << (std::countr_zero(static_cast<uint8_t>(different_bits[i])));
    });
    simd_uint8_mask a_greater = (different_bits > 0) and ((bits_A & first_bit_difference) > 0);
    simd_uint8_mask b_greater = (different_bits > 0) and ((bits_B & first_bit_difference) > 0);

    for (size_t i = 0; i < std::max(levels_A.size(), levels_B.size()); i++)
    {
        // One of the two has more nonempty strings but we were equal up until here
        if (i >= levels_B.size())
        {
            return (bits_vec_A[i] & 1) == 0 ? -1 : 1;
        }
        else if (i >= levels_A.size())
        {
            return (bits_vec_B[i] & 1) == 0 ? 1 : -1;
        }
        // One of the two has a bitstring "earlier"
        else if (levels_A[i] < levels_B[i])
        {
            return (bits_vec_A[i] & 1) == 0 ? -1 : 1;
        }
        else if (levels_A[i] > levels_B[i])
        {
            return (bits_vec_B[i] & 1) == 0 ? 1 : -1;
        }
        // The two levels are equal... We have to compare strings
        else if (a_greater[i] or (!a_less[i] and b_less[i]))
        {
            return 1;
        }
        else if (b_greater[i] or (a_less[i] and !b_less[i]))
        {
            return -1;
        }
    }
    
    // The birstrings are entirely equal
    return 0;
}

int main()
{
    // Set for initial bitstring: k-1 bitstrings with t NLB in the first one, all 0s
    simd_uint8 lengths ([](uint8_t i) {return static_cast<uint8_t>(i < k-1); } );
    lengths[0] = (1 << (t+1)) - 1;
    simd_uint8 bits = 0;

    std::cout << "Bits: ";
    print_simd_bits(bits);
    std::cout << "Lengths: ";
    print_simd_bits(lengths);

    // Test loop
    std::vector<uint8_t> bits_vec;
    std::vector<uint8_t> lengths_vec;
    std::vector<int> levels;
    levels.reserve(k-1);
    parse_to_vec(0, bits_vec, lengths_vec, levels);
    for (size_t i = 0; i < tree_b.size() - 1; i++)
    {
        // read into the SIMD arrays
        parse_to_vec(i, bits_vec, lengths_vec, levels);
        bits.copy_from(bits_vec.data(), std::experimental::element_aligned);
        lengths.copy_from(lengths_vec.data(), std::experimental::element_aligned);
        
        // call the successor function
        prog_p_successor(p, lengths, bits, levels);

        // parse successor and compare to (converted) result
        std::vector<uint8_t> succ_bits (8);
        bits.copy_to(succ_bits.data(), std::experimental::element_aligned);
        std::vector<uint8_t> succ_lengths (8);
        lengths.copy_to(succ_lengths.data(), std::experimental::element_aligned);
        std::vector<int> succ_levels { levels };

        parse_to_vec(p_successors[i], bits_vec, lengths_vec, levels);
        std::cout << "Computed bits: ";
        print_simd_bits(succ_bits);
        std::cout << "Expected bits: ";
        print_simd_bits(bits_vec);
        std::cout << "Computed lengths: ";
        print_simd_bits(succ_lengths);
        std::cout << "Expected lengths: ";
        print_simd_bits(lengths_vec);
        std::cout << "Computed levels: ";
        print_vector(succ_levels);
        std::cout << "Expected levels: ";
        print_vector(levels);
        
        if (levels == succ_levels and bits_vec == succ_bits and lengths_vec == succ_lengths)
        {
            std::cout << i << ": \033[32mCorrect successor!\n\033[0m";
        }
        else 
        {
            std::cout << i << ": \033[31mWrong successor!\n\033[0m";
            assert(false);
        }

        int fwd_res = compare(p, i, i + 1);
        std::cout << i << ": " << fwd_res;
        if (fwd_res == -1) std::cout << " \033[32mCorrect forward comparison!\n\033[0m";
        else 
        {
            std::cout << " \033[31mWrong forward comparison!\n\033[0m";
            assert (false);
        }

        int bwd_res = compare(p, i + 1, i);
        std::cout << i << ": " << bwd_res;
        if (bwd_res == 1)std::cout << " \033[32mCorrect backward comparison!\n\033[0m";
        else 
        {
            std::cout << " \033[31mWrong backward comparison!\n\033[0m";
            assert (false);
        }
    }

    std::cout << "Compared: " << compare(p, 3, 1) << std::endl;
}