#include <boost/functional/hash.hpp>
#include <iostream>
#include <stack>
#include <unordered_map>
#include <utility>

struct Node {
  int k;
  int t;
  int h;
  bool isU;
};

// Keep track of already computed sizes, this is cached beyond single calls of
// tree_size below
std::unordered_map<std::tuple<int, int, int>, unsigned,
                   boost::hash<std::tuple<int, int, int>>>
    treeU;
std::unordered_map<std::tuple<int, int, int>, unsigned,
                   boost::hash<std::tuple<int, int, int>>>
    treeV;

unsigned tree_size(int k, int t, int h) {
  std::cout << "Looking into k=" << k <<", t="<<t << ", h="<<h<<std::endl;
  std::stack<Node> stack;

  stack.push({k, t, h, true});
  while (!stack.empty()) {
    Node &tos = stack.top();
    if (tos.isU and tos.h == 1 and tos.k == 1) {
      treeU[std::make_tuple(tos.k, tos.t, tos.h)] = 1;
      stack.pop();
    } else if (tos.isU and tos.h > 1 and tos.k == 1) {
      auto son = treeU.find(std::make_tuple(tos.k, tos.t, tos.h - 1));
      if (son != treeU.end()) {
        treeU[std::make_tuple(tos.k, tos.t, tos.h)] = son->second;
        stack.pop();
      } else
        stack.push({tos.k, tos.t, tos.h - 1, true});
    } else if (tos.h >= tos.k and tos.k >= 2 and tos.t == 0) {
      auto son = treeU.find(std::make_tuple(tos.k - 1, tos.t, tos.h - 1));
      if (son != treeU.end()) {
        if (tos.isU)
          treeU[std::make_tuple(tos.k, tos.t, tos.h)] = son->second;
        else
          treeV[std::make_tuple(tos.k, tos.t, tos.h)] = son->second;
        stack.pop();
      } else
        stack.push({tos.k - 1, tos.t, tos.h - 1, true});
    } else if (!tos.isU and tos.h >= tos.k and tos.k >= 2 and tos.t >= 1) {
      auto son1 = treeV.find(std::make_tuple(tos.k, tos.t - 1, tos.h));
      auto son2 = treeU.find(std::make_tuple(tos.k - 1, tos.t, tos.h - 1));
      if (son1 != treeV.end() and son2 != treeU.end()) {
        treeV[std::make_tuple(tos.k, tos.t, tos.h)] =
            son1->second * 2 + son2->second;
        stack.pop();
      } else {
        stack.push({tos.k - 1, tos.t, tos.h - 1, true});
        stack.push({tos.k, tos.t - 1, tos.h, false});
      }
    } else if (tos.isU and tos.h == tos.k and tos.k >= 2) {
      auto son = treeV.find(std::make_tuple(tos.k, tos.t, tos.h));
      if (son != treeV.end()) {
        treeU[std::make_tuple(tos.k, tos.t, tos.h)] = son->second;
        stack.pop();
      } else
        stack.push({tos.k, tos.t, tos.h, false});
    } else if (tos.isU and tos.h > tos.k and tos.k >= 2) {
      auto son1 = treeV.find(std::make_tuple(tos.k, tos.t, tos.h));
      auto son2 = treeU.find(std::make_tuple(tos.k, tos.t, tos.h - 1));
      if (son1 != treeV.end() and son2 != treeU.end()) {
        treeU[std::make_tuple(tos.k, tos.t, tos.h)] =
            son1->second * 2 + son2->second;
        stack.pop();
      } else {
        stack.push({tos.k, tos.t, tos.h - 1, true});
        stack.push({tos.k, tos.t, tos.h, false});
      }
    } else
      assert(false); // We should never get here
  }

  return treeU[std::make_tuple(k, t, h)];
}

void print_usage(char *argv[]) {
  fprintf(stderr, "Usage: %s -k K -t T -h H\n", argv[0]);
}

int main(int argc, char **argv) {
  opterr = 0;
  int opt;
  int k;
  int t;
  int h;
  bool kset = false;
  bool tset = false;
  bool hset = false;

  while ((opt = getopt(argc, argv, "k:t:h:")) != -1) {
    switch (opt) {
    case 'k':
      k = atoi(optarg);
      if (k < 1) {
        fputs("K must be a positive integer\n", stderr);
        return EXIT_FAILURE;
      }
      kset = true;
      break;
    case 't':
      t = atoi(optarg);
      if (t < 0) {
        fputs("T must be a nonnegative integer\n", stderr);
        return EXIT_FAILURE;
      }
      tset = true;
      break;
    case 'h':
      h = atoi(optarg);
      if (h < 1) {
        fputs("H must be a positive integer\n", stderr);
        return EXIT_FAILURE;
      }
      hset = true;
      break;
    default: /* '?' */
      print_usage(argv);
      return EXIT_FAILURE;
    }
  }

  if (!(kset && tset && hset)) {
    fputs("Expected three arguments\n", stderr);
    print_usage(argv);
    return EXIT_FAILURE;
  }

  unsigned len = tree_size(k, t, h);
  std::cout << "It has " << len << " leaves\n";

  return EXIT_SUCCESS;
}
