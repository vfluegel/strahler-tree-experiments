# Strahler Tree Experiments
![Tests Passing](https://github.com/gaperez64/strahler-tree-experiments/actions/workflows/regtest.yml/badge.svg)
![Coverage](https://img.shields.io/badge/coverage-0%25-red)

Repository to hold some algorithms and tools for Strahler trees. Based on the theory described in [this paper](https://arxiv.org/pdf/2003.08627).

## Tools
Run `make` to compile some tools.
* `genstree` can be used to count leaves of a tree (for given values of `k`, `t`
  and `h`; it can also print to `stdout` the labels of the leaves of the tree;
  and it can even print the tree in dot format so you can run, e.g. `./genstree
  -k 4 -t 2 -h 4 -d | dot -Tpng > tree.png`
* If you want `genstree` to also print the partitioning of leaf labels into
  p-level groups, you can use the `-p` option; then the sizes of groups of
  leaves whose p-level successor are the same are printed (their sum
  adds up to the total number of leaves)
* `pms2dot` can be used to read from `stdin` and print a dot format tree
  constructed from a progress-measure string, for instance `echo
  "00,0,1,e,1|00,1,1,0,0|" | ./pms2dot | dot -Tpng > tree.png` prints a tree
  with two branches.


## Computing P-Level Successors
The file `str-tree.cc` contains the code to compute a p-level successor of a node in the Strahler tree. 
The compiled binary contains a main function that can be used without or with one paramter:
* No parameter: Check every leaf in the tree against its successor
* One (integer) parameter: Check that specific leaf and output the result and expected result.  

The tree that is used is defined in the file before compiling. The expected format is two vectors of vectors: One storing the bits and a second storing the level that the corresponding bit in the first vector of vectors belongs to. Multiple examples are currently included and can be switched out by changing `use_b` (bits) and `use_d` (levels) in `main`. When the tree is switched out, `k`, `t`, and `h` need to be set accordingly!


## Explanations for Comments in Code
Some comments in `str-tree.cc` start with capital letters. These labels refer to conditions in page 19 of the theory paper:  
* _Cases where the siblings does not exist_
    * **A**: the number of non-empty strings among bitstrings h-1 and r+1 is k-1
    * **B**: the number of non-leadings bits in bitstrings h-1 to r+1 is t
    * **C**: bitstring r is 01^j for some j>=0, the number of non-leading bits used in bitstrings h-1 to r is t and all bitstrings r to 1 are non-empty
    * **D**: bitstring r is 1^j for some j>=1, and the number of non-leading bits used in bitstrings h-1 to r is t

* _Finding the sibling_
    * **E**: Less than t non-leading bits are used in bitstrings h-1 to r -> append 10^j with j>=0 to original string r, so that exactly t non-leading bits are used
    * **F**: Exactly t non-leading bits are used in bitstrings h-1 to r and r is in format b01^j for some j>=0 -> use b

* _Setting the remaining bits_
    * **G**: Set 00^j for some i>=0 so the total number of bits used is (k-1)+t
    * **H**: Add strings 0 so the number of non-empty bitstrings is k-1
