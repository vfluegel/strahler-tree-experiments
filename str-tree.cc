#include <algorithm>
#include <vector>
#include <unordered_set>
#include <iostream>
#include <cassert>
#include <utility>

#include "examples/k4t3h5.hpp"

#ifndef HAS_P
#include <numeric>

int p = h - 2;
inline std::vector<int> make_indices(const std::vector<std::vector<bool>>& original) {
    std::vector<int> indices(original.size());
    std::iota(indices.begin(), indices.end(), 1);
    return indices;
}

std::vector<int> p_successors = make_indices(tree_b);
#endif

std::vector<bool> tmp_b;
std::vector<int> tmp_d;

int skipUntilNextLevel (std::vector<int>& curr_d, int i) 
{
    while ((i >= 0 && curr_d[i] == curr_d[i+1]) || std::cmp_equal(i + 1, curr_d.size())) 
    {
        tmp_b[i] = 0;
        i --;
    }
    return i;
}

void getLevelPSuccessor (int idx, int p, std::vector<std::vector<bool>>& use_b, std::vector<std::vector<int>>& use_d)
{
    tmp_b = use_b[idx];
    tmp_d = use_d[idx];

    bool skipLevel = false;
    int i = tmp_d.size() - 1;
    std::cout << "Start i in " << i << std::endl;
    std::cout << "Skipping bits below p\n";
    // skip bits "below p"
    while (i >= 0 && tmp_d[i] > p) 
    {
        tmp_b[i] = 0;
        i --;
    }
    std::cout << "After skipping i is " << i << std::endl;

    std::cout << "Calculating NES ";
    // Calculate number of Non-Empty Strings (NES): count unique values in tmp_d up until there
    size_t nes = std::unordered_set<int>( tmp_d.begin(), tmp_d.begin() + i + 1 ).size();
    std::cout << nes << std::endl;
    // Subtract the string that we currently look at (A, B only refers to strings "above")
    if (std::cmp_equal(nes, k)) {  // we actually want to check nes - 1 == k - 1, but we can
                     // +1 on both sides (saves us signedness craziness)
        // A: No next sibling on this layer
        std::cout << "Skipping a level\n";
        i = skipUntilNextLevel(tmp_d, i);
    }

    while (i >= 0) {
        // check if there was a level change
        if (std::cmp_equal(i + 1, tmp_d.size()) or tmp_d[i] != tmp_d[i+1]) 
        {
            std::cout << "Handling level change\n";
            // Calculate the Non-Leading Bits (NLB): take the complete length and subtract the number of NES (every NES has one leading bit)
            int nlb = (i + 1) - nes;
            nes --;
            std::cout << "NLB " << nlb << " i " << i << " NES " << nes << std::endl;
            if (nlb < t) 
            {
                std::cout <<  "Smaller than t\n";
                int new_index = std::cmp_equal(i + 1, tmp_d.size()) ? tmp_d[i] : tmp_d[i+1] - 1;
                i ++;

                if (i != 0 and tmp_d[i-1] == tmp_d[i]) nlb++;
                else nes++;

                if (std::cmp_greater(i + t - nlb + 1, tmp_b.size()))
                {
                    tmp_b.insert(tmp_b.end(), t - nlb, 0);
                    tmp_b[i] = 1;
                    tmp_d.insert(tmp_d.end(), t - nlb, new_index);
                    i += t - nlb;
                }
                else 
                {
                    tmp_b[i] = 1;
                    tmp_d[i] = new_index;
                    
                    int j = 1;
                    while (nlb + j <= t) 
                    {
                        tmp_b [i + j] = 0;
                        tmp_d [i + j] = new_index;
                        j ++;
                    }
                    // remember the last changed position
                    i += j;
                }
                break;
            }
            // Case B: Check if the current string is only leading bit (so all NLB are in strings 0 to r-1)
            else if (i == 0 or tmp_d[i-1] != tmp_d[i])
            {
                tmp_b[i] = 0;
                i --;
                continue;
            }
        }
        // For all following cases we know NLB == t        
        // Have to always check 0s - even when e.g. the first case applies
        if (tmp_b[i] == 0) 
        {
            if (i == 0 || tmp_d[i - 1] != tmp_d[i])
            {
                std::cout << "Found a 0 in the beginning\n";
                // The 0 is either the first bit in total, or it is the first bit of that level
                int strings_after_current = std::unordered_set<int> (tmp_d.begin() + i, tmp_d.end()).size();
                if (strings_after_current == h - tmp_d[i])
                {
                    // All bitstrings after the current level are non-empty, we simply move on
                    // C: No sibling on this layer
                    i --;
                    continue;
                }
                else
                {
                    // F: We have to remove one NES: The string is 01^j, so the new string would be empty
                    nes--;
                    skipLevel = true;
                }
                // We can use this level: Break out of the loop and start changing at the current i
                break;
            }
            else {
                assert (tmp_d[i - 1] == tmp_d[i]);
                std::cout << "A zero in the middle!\n";
                break;
            }
        }
        else 
        {
            std::cout << "start setting things to 0\n";
            // We can already start setting everything there to 0 and change where it belongs later
            tmp_b[i] = 0;
            i --;
        }
    }

    // A base case, we have reached the root: we are top
    // Special case of D
    if (i == -1) 
    {
        if (tmp_d[0] == 0)
        {
            std::cout << "We are at top\n";
            tmp_d[0] = -1;
            return;
        }
        else
        {
            // Special case: There are still empty strings "left" that can be filled instead
            i = 0;
            tmp_b[i] = 1;
            // TODO: maybe find more elegant way to avoid setting the index too high as opposed to starting out one lower...
            tmp_d[i] = tmp_d[i] - 2;
            skipLevel = true;
            nes--;
        }
    }

    // Change where the bits belong
    std::cout << "Adjusting bit level\n";
    std::cout << "starting at " << i << std::endl; 
    
    int no_of_needed_nes = (k-1) - (nes+1);
    if (no_of_needed_nes == 0) {
        // special case: Don't add anything, remove everything after the current position
        tmp_d.resize(i);
        tmp_b.resize(i);
    }
    else {
        int set_index = tmp_d[(skipLevel ? i : i-1)] + 1;
        // Fill up with just the next one as long as we still have bits
        std::cout << "Needed nes: " << no_of_needed_nes << std::endl;
        while (std::cmp_greater_equal(tmp_b.size(),i + no_of_needed_nes)) 
        {
            assert (i >= 0 and std::cmp_less(i, tmp_d.size()));
            tmp_d[i] = set_index;
            i ++;
        }
        std::cout << "Filling singles\n";
        // Now assign the rest of the bits one level a piece
        while (std::cmp_less(i, tmp_b.size()))
        {
            set_index ++;
            tmp_d[i] = set_index;
            i ++;
        }
    }

}

void prog_tmp(int pindex)
{
    int trace = 2;
    // Simple case 1: Top >_p Top
    if (tmp_d[0] == -1) return; // already Top

    bool skipLevel = false;
    int i = tmp_d.size() - 1;
#ifndef NDEBUG
    if (trace >= 2) 
    {
        std::cout << "Calculating successor of: ";
        for (auto &&bit : tmp_b)
        {
            std::cout << bit;
        }
        std::cout << " ";
        for (auto &&loc : tmp_d)
        {
            std::cout << loc;
        }
        std::cout << std::endl;
    std::cout << "Start i in " << i << std::endl;
    std::cout << "Skipping bits below p " << pindex << std::endl;
    }
#endif
    assert (*std::max_element(tmp_d.begin(), tmp_d.end()) < h-1);
    // skip bits "below p"
    while (i >= 0 && tmp_d[i] > pindex) 
    {
        tmp_b[i] = 0;
        i --;
    }
#ifndef NDEBUG
    if (trace >= 2) 
    {
    std::cout << "After skipping i is " << i << std::endl;

    std::cout << "Calculating NES ";
    }
#endif
    // Calculate number of Non-Empty Strings (NES): count unique values in tmp_d up until there
    int nes = std::unordered_set<int>( tmp_d.begin(), tmp_d.begin() + i + 1 ).size();
    assert (nes >= 0);
#ifndef NDEBUG
    if (trace >= 2) std::cout << nes << std::endl;
#endif
    // Subtract the string that we currently look at (A, B only refers to strings "above")
    if (nes - 1 == k - 1) {
        // A: No next sibling on this layer
#ifndef NDEBUG
        if (trace >= 2) std::cout << "Skipping a level\n";
#endif
        i = skipUntilNextLevel(tmp_d, i);
    }

    while (i >= 0) {
        // check if there was a level change
        if (std::cmp_equal(i+1, tmp_d.size()) or tmp_d[i] != tmp_d[i+1]) 
        {
#ifndef NDEBUG
            if (trace >= 2) std::cout << "Handling level change\n";
#endif
            // Calculate the Non-Leading Bits (NLB): take the complete length and subtract the number of NES (every NES has one leading bit)
            int nlb = (i + 1) - nes;
            nes --;
#ifndef NDEBUG
            if (trace >= 2) std::cout << "NLB " << nlb << " i " << i << " NES " << nes << std::endl;
#endif
            if (nlb < t) 
            {
#ifndef NDEBUG
                if (trace >= 2) std::cout <<  "Smaller than t\n";
#endif
                int new_index = std::min(std::cmp_equal(i+1, tmp_d.size()) ? tmp_d[i] : tmp_d[i+1] - 1, pindex);
                assert(new_index < h - 1);
                i ++;

                // Either we add the one at the end of the existing string, which means we add one NLB, or we add a NES!
                bool isNewString = false;
                if (i != 0 and (i == tmp_d.size() or tmp_d[i-1] == new_index)) nlb++;
                else {
                    nes++;
                    isNewString = true;
#ifndef NDEBUG
                    if (trace >= 2) std::cout <<  "Appending to an empty string...\n";
#endif
                }

                // Needed length: after current position, missing NLBs + NES so k-1 or height
                int total_nes = std::min((nes + h - 1 - new_index), k - 1);
                if (std::cmp_greater(total_nes + t, tmp_b.size()))
                {
                    size_t newBits = total_nes + t - tmp_d.size();
#ifndef NDEBUG
                    if (trace >= 2) std::cout <<  "Resizing to fit bits (" << newBits << " additional bits)\n";
#endif
                    tmp_b.insert(tmp_b.end(), newBits, 0);
                    tmp_b[i] = 1;
                    for (size_t j = i; j < tmp_d.size(); j++) tmp_d[j] = new_index;
                    tmp_d.insert(tmp_d.end(), newBits, new_index);
                    i += t - nlb + 1;
                    std::cout << "Resized tmp: now i = " << i << ", NES = " << nes << std::endl;
                }
                else 
                {
#ifndef NDEBUG
                  if (trace >= 2) std::cout <<  "Adding bits\n";
#endif
                    tmp_b[i] = 1;
                    tmp_d[i] = new_index;

                    int j = 1;
                    while (nlb + j <= t) 
                    {
                        tmp_b [i + j] = 0;
                        tmp_d [i + j] = new_index;
                        j ++;
                    }
                    // remember the last changed position
                    i += j;
                    std::cout << "Insterted into tmp: now " << i << ", " << std::endl;
                }
                break;
            }
            // Case B: Check if the current string is only leading bit (so all NLB are in strings 0 to r-1)
            else if (i == 0 or tmp_d[i-1] != tmp_d[i])
            {
                tmp_b[i] = 0;
                i --;
                continue;
            }
        }
        // For all following cases we know NLB == t        
        // Have to always check 0s - even when e.g. the first case applies
        if (tmp_b[i] == 0) 
        {
            if (i == 0 || tmp_d[i - 1] != tmp_d[i])
            {
#ifndef NDEBUG
                if (trace >= 2) std::cout << "Found a 0 in the beginning\n";
#endif
                // The 0 is either the first bit in total, or it is the first bit of that level
                int strings_after_current = std::unordered_set<int> (tmp_d.begin() + i, tmp_d.end()).size();
                std::cout << "Strings after current: " << strings_after_current << " of " << (h-1) << " - " << tmp_d[i] << std::endl;
                if (strings_after_current == (h-1) - tmp_d[i])
                {
                    // All bitstrings after the current level are non-empty, we simply move on
                    // C: No sibling on this layer
                    i --;
                    continue;
                }
                else
                {
                    // F: We have to remove one NES: The string is 01^j, so the new string would be empty
                    nes--;
                    skipLevel = true;
                }
                // We can use this level: Break out of the loop and start changing at the current i
                break;
            }
            else {
                assert (tmp_d[i - 1] == tmp_d[i]);
#ifndef NDEBUG
                if (trace >= 2) std::cout << "A zero in the middle!\n";
#endif
                break;
            }
        }
        else 
        {
#ifndef NDEBUG
            if (trace >= 2) std::cout << "start setting things to 0\n";
#endif
            // We can already start setting everything there to 0 and change where it belongs later
            tmp_b[i] = 0;
            i --;
        }
    }

    // A base case, we have reached the root: we are top
    // Special case of D
    if (i == -1) 
    {
        if (tmp_d[0] == 0)
        {
#ifndef NDEBUG
            if (trace >= 2) std::cout << "We are at top\n";
#endif
            tmp_d[0] = -1;
            return;
        }
        else
        {
            // Special case: There are still empty strings "left" that can be filled instead
            i = 0;
            tmp_b[i] = 1;
            // TODO: maybe find more elegant way to avoid setting the index too high as opposed to starting out one lower...
            tmp_d[i] = std::min(tmp_d[i], pindex+1) - 2;
            std::cout << "Need " << (t + k - 1) << " Bits\n";
            if (std::cmp_greater(t + k - 1, tmp_b.size()))
            {
                size_t newBits = t + k - 1 - tmp_d.size();
                std::cout << "Vector too small, appending " << newBits << "bits\n";
                tmp_b.insert(tmp_b.end(), newBits, 0);
                for (size_t j = i; j < tmp_d.size(); j++) tmp_d[j] = tmp_d[i];
                tmp_d.insert(tmp_d.end(), newBits, tmp_d[i]);
            }
            skipLevel = true;
            nes--;
        }
    }

    // Change where the bits belong
    #ifndef NDEBUG
    if (trace >= 2)
    {
    std::cout << "Adjusting bit level\n";
    std::cout << "starting at " << i << std::endl; 
    }
    #endif
    
    // We can only fill as many NES as we have height... Too many empty strings "in the middle" = less tham k-1 strings total
    int available_nes = h - 1 - (tmp_d[(skipLevel ? i : i-1)] + 1);
    int no_of_needed_nes = std::min((k-1) - (nes+1), available_nes);
    
    assert (no_of_needed_nes >= 0);
    if (no_of_needed_nes == 0) {
        // special case: Don't add anything, remove everything after the current position
        tmp_d.resize(i);
        tmp_b.resize(i);
    }
    else {
        int set_index = tmp_d[(skipLevel ? i : i-1)] + 1;
#ifndef NDEBUG
        if (trace >= 2) std::cout << "Needed nes: " << no_of_needed_nes << ", available levels: " << available_nes << std::endl;
#endif
        // Fill up with just the next one as long as we still have bits
        while (std::cmp_greater_equal(tmp_b.size(), i + no_of_needed_nes))
        {
            assert (i >= 0 and std::cmp_less(i, tmp_d.size()));
            assert(set_index < h-1);
#ifndef NDEBUG
            if (trace >= 2) std::cout << "Set bit " << i << " to " << set_index << std::endl;
#endif
            tmp_d[i] = set_index;
            i ++;
        }
#ifndef NDEBUG
        if (trace >= 2) std::cout << "Filling singles\n";
#endif
        // Now assign the rest of the bits one level a piece
        while (std::cmp_less(i, tmp_b.size()))
        {
            set_index ++;
#ifndef NDEBUG
            if (trace >= 2) std::cout << "Set bit " << i << " to " << set_index << std::endl;
#endif
            assert (set_index < h-1);
            if (set_index >= h-1) {
                std::cout << "Error when filling NES\n";
            }
            assert (set_index < h);
            tmp_d[i] = set_index;
            i ++;
        }
    }
     // Assert that the number of NLB is at most t
    assert ((tmp_d.size() - std::unordered_set<int>(tmp_d.begin(), tmp_d.end()).size()) <= t);
}


int compare(size_t idxA, size_t idxB, int pindex, std::vector<std::vector<bool>>& use_b, std::vector<std::vector<int>>& use_d)
{
    auto& bitsA = use_b[idxA];
    auto& indicesA = use_d[idxA];
    auto& bitsB = use_b[idxB];
    auto& indicesB = use_d[idxB];

    // cases involving Top
    if (indicesA[0] == -1 and indicesB[0] == -1) return 0;
    if (indicesA[0] == -1) return 1;
    if (indicesB[0] == -1) return -1;
    
    for (int i=0; std::cmp_less(i, std::max(bitsA.size(), bitsB.size())); i++) {
        if (std::cmp_greater_equal(i, bitsB.size()))
        {
            return bitsA[i] == 0 ? -1: 1;
        }
        else if (std::cmp_greater_equal(i, bitsA.size()))
        {
            return bitsB[i] == 0 ? 1 : -1;
        }
        else if (indicesA[i] > pindex and indicesB[i] > pindex) {
            // equal until pindex, return 0
            return 0;
        } else if (indicesA[i] < indicesB[i]) {
            // equal until best has [eps]
            return bitsA[i] == 0 ? -1: 1;
        } else if (indicesA[i] > indicesB[i]) {
            // equal until tmp has [eps]
            return bitsB[i] == 0 ? 1 : -1;
        } else if (bitsA[i] < bitsB[i]) {
            // equal until tmp<best
            return -1;
        } else if (bitsA[i] > bitsB[i]) {
            // equal until tmp>best
            return 1;
        }
    }
    return 0;
}

int main(int argc, char *argv[])
{
    // Switch out the tree to use here - don't forget to adjust the parameters k, t, h at the top as well
    auto& use_b = tree_b;
    auto& use_d = tree_d;

    // Don't pass parameter = Go through all
    if (argc == 1) 
    {
        for (size_t idx = 0; idx < use_b.size(); idx++)
        {
            tmp_b = use_b[idx];
            tmp_d = use_d[idx];
            prog_tmp(p); //use h-2 for bottom of tree
            // getLevelPSuccessor(idx, h-1, use_b, use_d);
            if (((idx == use_b.size() - 1 and tmp_d[0] == -1)) or
                (tmp_b == use_b[p_successors[idx]] and tmp_d == use_d[p_successors[idx]]))
            {
                std::cout << idx << ": \033[32mCorrect successor!\n\033[0m";
            }
            else 
            {
                std::cout << idx << ": \033[31mWrong successor!\n\033[0m";
                assert (false);
            }

            if (idx < use_b.size() - 1)
            {
                int fwd_res = compare (idx, idx+1, h-1, use_b, use_d);
                std::cout << idx << ": " << fwd_res << ( fwd_res == -1 ? " \033[32mCorrect forward comparison!\n\033[0m" : " \033[31mWrong forward comparison!\n\033[0m");
                
                int bwd_res = compare (idx+1, idx, h-1, use_b, use_d);
                std::cout << idx << ": " << bwd_res << (bwd_res == 1 ? " \033[32mCorrect backward comparison!\n\033[0m" : " \033[31mWrong backward comparison!\n\033[0m");
            }
        }
    }
    // Pass one parameter = only check that one and print result
    else if (argc == 2 or argc == 3) 
    {
        int idx = atoi(argv[1]);
        std::cout << "Searching successor for: ";
        for (auto &&i : use_b[idx])
        {
                std::cout << i;
        }
        std::cout << " ";
        for (auto &&i : use_d[idx])
        {
                std::cout << i;
        }
        std::cout << std::endl;

        tmp_b = use_b[idx];
        tmp_d = use_d[idx];

        int p = h - 2;
        if (argc == 3)
        {
            p = atoi(argv[2]);
        }

        prog_tmp(p);
        std::cout << "Result: ";
        for (auto &&i : tmp_b)
        {
                std::cout << i;
        }
        std::cout << " ";
        for (auto &&i : tmp_d)
        {
                std::cout << i;
        }
        std::cout << std::endl;

        std::cout << "Expected: ";
        for (auto &&i : use_b[p_successors[idx]])
        {
                std::cout << i;
        }
        std::cout << " ";
        for (auto &&i : use_d[p_successors[idx]])
        {
                std::cout << i;
        }
        std::cout << std::endl;
    }
    // Pass more params = invalid
    else 
    {
        std::cout << "Too many params" << std::endl;
        return 1;
    }
   
}
