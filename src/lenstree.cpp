#include <boost/functional/hash.hpp>
#include <cstring>
#include <iostream>
#include <stack>
#include <unordered_map>
#include <utility>
#include <vector>

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

unsigned tree_size(int k, int t, int h, bool isU) {
  std::cout << "Looking into k=" << k <<", t="<<t << ", h="<<h<<std::endl;
  // Check for cache hit
  auto& requested_tree = isU ? treeU : treeV;
  auto cached = requested_tree.find(std::make_tuple(k, t, h));
  if (cached != treeV.end()) {
    return cached->second;
  }
  
  std::stack<Node> stack;

  stack.push({k, t, h, isU});
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

  return isU ? treeU[std::make_tuple(k, t, h)] : treeV[std::make_tuple(k, t, h)];
}

std::pair<std::vector<bool>, std::vector<int>> label_lth_leaf(int k, int t, int h, int lth)
{
  std::pair<std::vector<bool>, std::vector<int>> leaf;
  Node node { k, t, h, true };
  int lth_tmp = lth;

  while (true)
  {
    if (node.isU and node.k == 1)
    {
      assert (lth_tmp == 1);
      // Just empty string... (We can combine case 1 and 2 from the C)
      break;
    }
    else if (node.h >= node.k and node.k >= 2 and node.t == 0)
    {
      if (node.isU)
      {
        leaf.first.push_back(0);
        leaf.second.push_back(h - node.h); // Double check!
      }
      node.isU = true;
      node.k--;
      node.h--;
    }
    else if (!node.isU and node.h >= node.k and node.k >= 2 and node.t >= 1)
    {
      unsigned size_child1 = tree_size(node.k, node.t - 1, node.h, false);
      unsigned size_child2 = tree_size(node.k - 1, node.t, node.h - 1, true);
      if (size_child1 >= lth_tmp)
      {
        leaf.first.push_back(0);
        leaf.second.push_back(h - node.h); // Double check!
        node.t--;
        node.isU = false;
      }
      else if (size_child1 + size_child2 >= lth_tmp)
      {
        lth_tmp -= size_child1;
        node.isU = true;
        node.k--;
        node.h--;
      }
      else
      {
        lth_tmp -= size_child1 + size_child2;
        leaf.first.push_back(1);
        leaf.second.push_back(h - node.h); // Double check!
        node.isU = false;
        node.t--;
      }
    }
    else if (node.isU and node.h == node.k and node.k >= 2)
    {
      leaf.first.push_back(0);
      leaf.second.push_back(h - node.h); // Double check!
      node.isU = false;
    }
    else if (node.isU and node.h > node.k and node.k >= 2)
    {
      unsigned size_child1 = tree_size(node.k, node.t, node.h, false);
      unsigned size_child2 = tree_size(node.k, node.t, node.h-1, true);
      if (size_child1 >= lth_tmp)
      {
        leaf.first.push_back(0);
        leaf.second.push_back(h - node.h); // Double check!
        node.isU = false;
      }
      else if (size_child1 + size_child2 >= lth_tmp)
      {
        lth_tmp -= size_child1;
        node.isU = true;
        node.h--;
      }
      else 
      {
        lth_tmp -= size_child1 + size_child2;
        leaf.first.push_back(1);
        leaf.second.push_back(h - node.h); // Double check!
        node.isU = false;
      }
    }
    else 
    {
      assert (false);
    }
  }

  return leaf;
}

void print_usage(char *argv[]) {
  char *progname = strrchr(argv[0], '/');
  progname = progname ? progname + 1 : argv[0];
  fprintf(stderr, "Usage: %s -k K -t T -h H\n", progname);
}

int main(int argc, char **argv) {
  opterr = 0;
  int opt;
  int k;
  int t;
  int h;
  int l;
  bool kset = false;
  bool tset = false;
  bool hset = false;
  bool lset= false;

  while ((opt = getopt(argc, argv, "k:t:h:l:")) != -1) {
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
    case 'l':
      l = atoi(optarg);
      if (l < 1) {
        fputs("L must be a positive integer\n", stderr);
        return EXIT_FAILURE;
      }
      lset = true;
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

  if (lset) 
  {
    auto leaf = label_lth_leaf(k, t, h, l);
    std::cout << "Leaf no " << l << " is ";
    std::cout << std::noboolalpha;
    for (bool bit: leaf.first)
    {
      std::cout << bit;
    }
    std::cout << std::boolalpha << " ";
    for (int lvl: leaf.second)
    {
      std::cout << lvl;
    }
    std::cout << std::endl;
  }
  else
  {
    unsigned len = tree_size(k, t, h, true);
    std::cout << "It has " << len << " leaves\n";
  }
  

  return EXIT_SUCCESS;
}
