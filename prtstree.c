#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "utrees.h"
#include "prtstree.h"

typedef struct LabdTree {
  size_t size;
  char const **labs;
  unsigned id;
  int height;
} LabdTree;

[[nodiscard]] static char const *after_next_comma(char const str[static 1])
    [[unsequenced]] {
  assert(str != nullptr);
  char const *cur = str;
  while (*cur != COMMA && *cur != EOS) {
    assert(*cur != '\0');
    cur++;
  }
  // We use EOS as the end of string marker, so keep it if we reached it
  // already
  if (*cur == COMMA)
    cur++;
  return cur;
}

[[nodiscard]] static bool same_before_comma(char const first[static 1],
                                            char const second[static 1])
    [[unsequenced]] {
  char const *cur1 = first;
  char const *cur2 = second;

  // We match every non-epsilon character from first to the same in second
  while (*cur1 != COMMA && *cur1 != EOS) {
    assert(*cur1 != '\0');
    if (*cur1 == ONE || *cur1 == ZERO) {
      while (true) {
        assert(*cur2 != '\0');
        if (*cur2 == COMMA || *cur2 == EOS) {
          return false;
        } else if (*cur2 == EPSILON) {
          cur2++; // ignore it
          continue;
        } else if (*cur2 != *cur1) {
          return false; // no match? then not equal
        } else {
          cur2++; // match!
          break;
        }
      }
    }
    cur1++;
  }

  // Now it remains to check that cur2 is also done, we can skip over epsilons
  while (*cur2 != COMMA && *cur2 != EOS) {
    assert(*cur2 != '\0');
    if (*cur2 == ONE || *cur2 == ZERO)
      return false;
    cur2++;
  }

  return true;
}

/**
 * FIXME: The DFS traversal in this function is used again, almost
 * identically, in a function later on (hence the repeated comments, etc.) If
 * another function comes along that requires the same, it would be best to
 * factor it out and take a callback function as argument to call on the
 * leaves.
 */
void print_tree_ppart(unsigned const nlabs, char const labels[nlabs],
                      int const p) [[unsequenced]] {
  assert(labels != nullptr);
  char const **lab_ptrs = malloc(sizeof(char *[nlabs]));

  // This will be a DFS-like procedure, we need a stack of arrays of pointers
  LabdTree *stack = nullptr;
  size_t maxs = 0;
  size_t lens = 0;

  // And we put in it the array of pointers to all labels to being with
  unsigned next_id = 1;
  lab_ptrs[0] = labels;
  unsigned cur_label_idx = 1;
  for (char const *cur = labels; *cur != '\0'; cur++) {
    assert(cur_label_idx <= nlabs);
    if (cur_label_idx < nlabs && *cur == EOS) {
      lab_ptrs[cur_label_idx] = cur + 1;
      cur_label_idx++;
    }
  }
  // Hacky: no edges indexed yet so we give the root a "height" of -1
  LabdTree ltree = {
      .size = nlabs, .labs = lab_ptrs, .id = next_id++, .height = -1};
  PUSH(stack, lens, maxs, ltree);

  // Recall we're going to try and handle this DFS-fashion. Each time we
  // "treat" a labelled tree we consider its leaf labels so that:
  // 1. We move all labels that coincide with the first part of the first
  // label towards the start of the array.
  // 2. We advance the pointer to those labels close to the start to the next
  // part of the label (so after the next COMMA)
  // 3. We push a labelled tree that focuses on those labels close to the
  // start (so we need to reduce the length) in the new labelled tree.
  //
  // Level p nodes are base cases print their size.
  while (lens > 0) {
    LabdTree const tree = stack[lens - 1];
    if (tree.height == p) { // base cases, print and pop
      lens--;
      printf("Part %ld\n", tree.size);
      continue;
    }
    if (tree.size == 0) { // we're done!
      lens--;
      assert(lens == 0);
      continue;
    }
    if (tree.size == 1 && tree.labs[0][0] == EOS) {
      lens--;
      fprintf(stderr, "Got to a leaf or size %ld, something is going wrong!\n",
              tree.size);
      break;
    }

    // 1. We have at least one leaf label, we can start moving labels around
    size_t bucket_size = 1;
    for (size_t idx = 1; idx < tree.size; idx++) {
      char const *cur = tree.labs[idx];
      if (same_before_comma(tree.labs[bucket_size - 1], cur)) {
        if (tree.labs[bucket_size] != tree.labs[idx]) { // only swap if
                                                        // needed
          char const *temp = tree.labs[bucket_size];
          tree.labs[bucket_size] = tree.labs[idx];
          tree.labs[idx] = temp;
        }
        bucket_size++;
      }
    }

    // 2. Now that we have a bucket of labels that have the same initial part,
    // we need to advance their pointers to the next part of the label.
    for (size_t idx = 0; idx < bucket_size; idx++)
      tree.labs[idx] = after_next_comma(tree.labs[idx]);

    // 3. Now we need to push a tree into the stack that points at the initial
    // segment of tree.labs. But first, we update the top of the stack to
    // ignore that initial part
    stack[lens - 1].size = tree.size - bucket_size;
    stack[lens - 1].labs = tree.labs + bucket_size;
    ltree.size = bucket_size;
    ltree.labs = tree.labs;
    ltree.height = tree.height + 1;
    ltree.id = next_id++;
    PUSH(stack, lens, maxs, ltree);
  }

  free(stack);
  free(lab_ptrs);
}

void print_tree_dot(unsigned const nlabs, char const labels[nlabs])
    [[unsequenced]] {
  assert(labels != nullptr);
  char const **lab_ptrs = malloc(sizeof(char *[nlabs]));

  // This will be a DFS-like procedure, we need a stack of arrays of pointers
  LabdTree *stack = nullptr;
  size_t maxs = 0;
  size_t lens = 0;

  // And we put in it the array of pointers to all labels to being with
  unsigned next_id = 1;
  lab_ptrs[0] = labels;
  unsigned cur_label_idx = 1;
  for (char const *cur = labels; *cur != '\0'; cur++) {
    assert(cur_label_idx <= nlabs);
    if (cur_label_idx < nlabs && *cur == EOS) {
      lab_ptrs[cur_label_idx] = cur + 1;
      cur_label_idx++;
    }
  }
  LabdTree ltree = {.size = nlabs, .labs = lab_ptrs, .id = next_id++};
  PUSH(stack, lens, maxs, ltree);

  // start the tree in the output
  puts("strict graph {");

  // Recall we're going to try and handle this DFS-fashion. Each time we
  // "treat" a labelled tree we consider its leaf labels so that:
  // 1. We move all labels that coincide with the first part of the first
  // label towards the start of the array.
  // 2. We advance the pointer to those labels close to the start to the next
  // part of the label (so after the next COMMA)
  // 3. We push a labelled tree that focuses on those labels close to the
  // start (so we need to reduce the length) in the new labelled tree.
  //
  // Parents are responsible for printing nodes of their newly created
  // children (when pushing a new labelled tree) and edges to them.
  while (lens > 0) {
    LabdTree const tree = stack[lens - 1];
    if (tree.size == 0 ||
        (tree.size == 1 && tree.labs[0][0] == EOS)) { // base cases, just pop
      lens--;
      continue;
    }

    // 1. We have at least one leaf label, we can start moving labels around
    size_t bucket_size = 1;
    for (size_t idx = 1; idx < tree.size; idx++) {
      char const *cur = tree.labs[idx];
      if (same_before_comma(tree.labs[bucket_size - 1], cur)) {
        if (tree.labs[bucket_size] != tree.labs[idx]) { // only swap if
                                                        // needed
          char const *temp = tree.labs[bucket_size];
          tree.labs[bucket_size] = tree.labs[idx];
          tree.labs[idx] = temp;
        }
        bucket_size++;
      }
    }

    // 2. Now that we have a bucket of labels that have the same initial part,
    // we need to advance their pointers to the next part of the label. Before
    // we do that, we print the new node id and we print the edge to it.
    printf("\t%d -- %d [label=\"", tree.id, next_id);
    for (char const *cur = tree.labs[0]; *cur != COMMA && *cur != EOS; cur++) {
      switch (*cur) {
      case ZERO:
        fputs("0", stdout);
        break;
      case ONE:
        fputs("1", stdout);
        break;
      case EPSILON:
        break;
      default:
        assert(false);
      }
    }
    puts("\"];");
    // here's the actual fast forward
    for (size_t idx = 0; idx < bucket_size; idx++)
      tree.labs[idx] = after_next_comma(tree.labs[idx]);

    // 3. Now we need to push a tree into the stack that points at the initial
    // segment of tree.labs. But first, we update the top of the stack to
    // ignore that initial part
    stack[lens - 1].size = tree.size - bucket_size;
    stack[lens - 1].labs = tree.labs + bucket_size;
    ltree.size = bucket_size;
    ltree.labs = tree.labs;
    ltree.id = next_id++;
    PUSH(stack, lens, maxs, ltree);
  }
  puts("}");

  free(stack);
  free(lab_ptrs);
}
