#ifndef PRTSTREE_H
#define PRTSTREE_H 1

enum : char { ONE = '1', ZERO = '0', EPSILON = 'e', COMMA = ',', EOS = '|' };

void print_tree_dot(unsigned const nlabs, char const labels[nlabs]);

void print_tree_ppart(unsigned const nlabs, char const labels[nlabs],
                      int const p);

#endif
