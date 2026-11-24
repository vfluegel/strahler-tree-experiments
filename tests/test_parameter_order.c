#include <assert.h>
#include <stdio.h>

#include "parameter_queue.h"
#include "stree.h"

static void check(size_t const height, size_t const k_max, size_t const t_max,
                  uint64_t const *counts) {
  ParameterQueue order = {0}, ratio = {0};
  assert(parameter_table_init(&order, k_max, t_max, height));
  assert(parameter_queue_init(&ratio, k_max, t_max));
  bool seen[10][10] = {{false}};
  size_t const prefix_k = height <= 1000 ? (k_max < 6 ? k_max : 6) : 0;
  size_t const prefix_t = t_max < 6 ? t_max : 6;
  size_t const prefix = prefix_k * (prefix_t + 1);
  size_t visited = 0, zero_k = prefix_k + 1;
  uint64_t previous_size = 0;
  while (order.ordered_valid) {
    ParameterPair const pair = order.ordered;
    assert(pair.k >= 1 && pair.k <= k_max && pair.t <= t_max);
    assert(!seen[pair.k][pair.t]);
    seen[pair.k][pair.t] = true;
    if (visited < prefix) {
      assert(pair.k <= 6 && pair.t <= 6);
      uint64_t const size = counts[(height * 7 + pair.k) * 7 + pair.t];
      assert(size >= previous_size);
      previous_size = size;
    } else if (zero_k <= k_max) {
      assert(pair.k == zero_k && pair.t == 0);
      zero_k++;
    } else {
      while (ratio.count != 0 && height <= 1000 && ratio.heap[0].k <= 6 &&
             ratio.heap[0].t <= 6) {
        parameter_queue_advance(&ratio);
      }
      assert(ratio.count != 0);
      assert(pair.k == ratio.heap[0].k && pair.t == ratio.heap[0].t);
      parameter_queue_advance(&ratio);
    }
    visited++;
    parameter_table_advance(&order);
  }
  assert(visited == k_max * (t_max + 1));
  for (size_t k = 1; k <= k_max; k++) {
    for (size_t t = 0; t <= t_max; t++) {
      assert(seen[k][t]);
    }
  }
  parameter_queue_destroy(&ratio);
  parameter_queue_destroy(&order);
}

static void check_monotone(size_t const even_height, size_t const odd_height,
                           size_t const k_max, size_t const t_max) {
  size_t const height = even_height > odd_height ? even_height : odd_height;
  ParameterQueue queue = {0}, ratio = {0};
  assert(parameter_monotone_init(&queue, k_max, t_max, height));
  assert(parameter_queue_init(&ratio, k_max, t_max));
  bool visited[10][10] = {{false}};
  size_t previous[2] = {0};
  size_t count = 0, prefix_count = 0;
  bool fallback = false;
  while (queue.ordered_valid) {
    ParameterPair const pair = queue.ordered;
    assert(pair.k <= k_max && pair.t <= t_max);
    assert(!visited[pair.k][pair.t]);
    if (queue.ordered_prefix) {
      assert(!fallback);
      bool larger = false;
      for (size_t player = 0; player < 2; player++) {
        size_t const h = player == 0 ? even_height : odd_height;
        size_t const k = pair.k < h ? pair.k : h;
        unsigned const leaves = stree_count_leaves((int)k, (int)pair.t, (int)h);
        assert(leaves >= previous[player]);
        larger |= leaves > previous[player];
        previous[player] = leaves;
      }
      assert(larger && ++prefix_count <= 7);
    } else {
      fallback = true;
      while (ratio.count != 0 && visited[ratio.heap[0].k][ratio.heap[0].t]) {
        parameter_queue_advance(&ratio);
      }
      assert(ratio.count != 0);
      assert(pair.k == ratio.heap[0].k && pair.t == ratio.heap[0].t);
      parameter_queue_advance(&ratio);
    }
    visited[pair.k][pair.t] = true;
    count++;
    parameter_table_advance(&queue);
  }
  assert(count == 1 + k_max * t_max);
  assert(visited[1][0]);
  for (size_t k = 1; k <= k_max; k++) {
    for (size_t t = 1; t <= t_max; t++) {
      assert(visited[k][t]);
    }
  }
  parameter_queue_destroy(&ratio);
  parameter_queue_destroy(&queue);
}

int main(void) {
  for (size_t h = 1; h <= 7; h++) {
    for (size_t k = 1; k <= h; k++) {
      for (size_t t = 0; t <= 6; t++) {
        check_monotone(h, h == 1 ? 1 : h - 1, k, t);
      }
    }
  }
  check_monotone(1001, 1000, 3, 2);
  uint64_t *counts = nullptr;
  assert(stree_count_table(6, 6, 1000, &counts));
  size_t const heights[] = {1,   2,   3,   6,   7,    10,  100,
                            112, 113, 557, 558, 1000, 1001};
  for (size_t i = 0; i < sizeof(heights) / sizeof(heights[0]); i++) {
    size_t const k_max = heights[i] < 9 ? heights[i] : 9;
    check(heights[i], k_max, 0, counts);
    check(heights[i], k_max, 1, counts);
    check(heights[i], k_max, 8, counts);
  }
  /* A large final prefix host must not hide a small ratio-tail host. */
  assert(stree_count_leaves(6, 6, 7) == 988865);
  assert(stree_count_leaves(7, 1, 7) == 13);
  free(counts);
  puts("Table order: exact size prefixes, unique zero column and complete "
       "ratio tails");
}
