#include <ctype.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dot_utils.h"
#include "ordered_tree.h"
#include "utils.h"

static void set_error(OrderedTreeError *error, char const *input, size_t offset,
                      char const *message) {
  if (error == nullptr) {
    return;
  }

  error->line = 1;
  error->column = 1;
  for (size_t index = 0; index < offset && input[index] != '\0'; index++) {
    if (input[index] == '\n') {
      error->line++;
      error->column = 1;
    } else {
      error->column++;
    }
  }

  (void)snprintf(error->message, sizeof(error->message), "%s", message);
}

static OrderedTreeNode *new_node(void) {
  return calloc(1, sizeof(OrderedTreeNode));
}

void ordered_tree_destroy(OrderedTreeNode *root) {
  if (root == nullptr) {
    return;
  }
  for (size_t index = 0; index < root->child_count; index++) {
    free(root->children[index].label);
    ordered_tree_destroy(root->children[index].child);
  }
  free(root->children);
  free(root);
}

[[nodiscard]] static bool normalized_label(char const *input, size_t begin,
                                           size_t end, char **label,
                                           OrderedTreeError *error) {
  size_t length = 0;
  for (size_t index = begin; index < end; index++) {
    if (input[index] == '0' || input[index] == '1') {
      length++;
    } else if (input[index] != 'e') {
      set_error(error, input, index, "invalid component character");
      return false;
    }
  }

  char *result = malloc(length + 1);
  if (result == nullptr) {
    set_error(error, input, begin, "failed to allocate a component label");
    return false;
  }

  size_t write_index = 0;
  for (size_t index = begin; index < end; index++) {
    if (input[index] == '0' || input[index] == '1') {
      result[write_index++] = input[index];
    }
  }
  result[write_index] = '\0';
  *label = result;
  return true;
}

typedef struct {
  char **components;
  size_t component_count;
  size_t component_capacity;
  size_t input_offset;
} LeafPath;

typedef struct {
  LeafPath *paths;
  size_t path_count;
  size_t path_capacity;
} LeafPathList;

static void leaf_path_destroy(LeafPath *path) {
  for (size_t index = 0; index < path->component_count; index++) {
    free(path->components[index]);
  }
  free((void *)path->components);
  *path = (LeafPath){0};
}

static void leaf_path_list_destroy(LeafPathList *list) {
  for (size_t index = 0; index < list->path_count; index++) {
    leaf_path_destroy(&list->paths[index]);
  }
  free(list->paths);
  *list = (LeafPathList){0};
}

[[nodiscard]] static bool append_component(LeafPath *path, char *component,
                                           char const *input, size_t offset,
                                           OrderedTreeError *error) {
  if (path->component_count == path->component_capacity) {
    ArrayGrowth const growth =
        grow_array((void *)path->components, path->component_capacity,
                   sizeof(path->components[0]));
    if (!growth.succeeded) {
      set_error(error, input, offset, "failed to grow a leaf path");
      return false;
    }
    path->components = (char **)growth.data;
    path->component_capacity = growth.capacity;
  }
  path->components[path->component_count++] = component;
  return true;
}

[[nodiscard]] static bool append_path(LeafPathList *list, LeafPath *path,
                                      char const *input, size_t offset,
                                      OrderedTreeError *error) {
  if (list->path_count == list->path_capacity) {
    ArrayGrowth const growth =
        grow_array(list->paths, list->path_capacity, sizeof(list->paths[0]));
    if (!growth.succeeded) {
      set_error(error, input, offset, "failed to grow the leaf-path list");
      return false;
    }
    list->paths = growth.data;
    list->path_capacity = growth.capacity;
  }
  list->paths[list->path_count++] = *path;
  *path = (LeafPath){0};
  return true;
}

[[nodiscard]] static bool parse_paths(char const *input, LeafPathList *list,
                                      OrderedTreeError *error) {
  size_t offset = 0;
  while (input[offset] != '\0') {
    LeafPath path = {.input_offset = offset};
    if (input[offset] == '|') {
      offset++;
    } else {
      while (true) {
        size_t const component_begin = offset;
        while (input[offset] != '\0' && input[offset] != ',' &&
               input[offset] != '|') {
          offset++;
        }
        if (offset == component_begin) {
          set_error(error, input, offset,
                    "an empty component is ambiguous; use e");
          goto path_failure;
        }
        if (input[offset] == '\0') {
          set_error(error, input, offset, "missing final '|'");
          goto path_failure;
        }

        char *label = nullptr;
        if (!normalized_label(input, component_begin, offset, &label, error)) {
          goto path_failure;
        }
        if (!append_component(&path, label, input, component_begin, error)) {
          free(label);
          goto path_failure;
        }

        if (input[offset] == '|') {
          offset++;
          break;
        }
        offset++;
        if (input[offset] == '|') {
          // Generator streams historically end every path with a comma. It
          // terminates the final level and does not denote another edge.
          offset++;
          break;
        }
      }
    }

    if (!append_path(list, &path, input, path.input_offset, error)) {
      goto path_failure;
    }
    if (input[offset] == '\0') {
      break;
    }
    if (isspace((unsigned char)input[offset])) {
      while (isspace((unsigned char)input[offset])) {
        offset++;
      }
      if (input[offset] != '\0') {
        set_error(error, input, offset,
                  "unexpected text after trailing whitespace");
        return false;
      }
      break;
    }
    continue;

  path_failure:
    leaf_path_destroy(&path);
    return false;
  }
  return list->path_count != 0;
}

// This is the in-order traversal of the infinite binary tree from the paper:
// 0 beta < epsilon < 1 beta, recursively below equal leading bits.
[[nodiscard]] static int compare_bitstrings(char const *left,
                                            char const *right) {
  size_t index = 0;
  while (left[index] == right[index] && left[index] != '\0') {
    index++;
  }
  if (left[index] == right[index]) {
    return 0;
  }

  int const left_rank = left[index] == '0' ? 0 : left[index] == '\0' ? 1 : 2;
  int const right_rank = right[index] == '0' ? 0 : right[index] == '\0' ? 1 : 2;
  return left_rank < right_rank ? -1 : 1;
}

[[nodiscard]] static int compare_paths(LeafPath const *left,
                                       LeafPath const *right) {
  size_t const common_length = left->component_count < right->component_count
                                   ? left->component_count
                                   : right->component_count;
  for (size_t index = 0; index < common_length; index++) {
    int const comparison =
        compare_bitstrings(left->components[index], right->components[index]);
    if (comparison != 0) {
      return comparison;
    }
  }
  if (left->component_count == right->component_count) {
    return 0;
  }
  return left->component_count < right->component_count ? -1 : 1;
}

[[nodiscard]] static int compare_paths_for_qsort(void const *left,
                                                 void const *right) {
  return compare_paths(left, right);
}

[[nodiscard]] static OrderedTreeNode *
find_or_append_child(OrderedTreeNode *parent, char *label, char const *input,
                     size_t offset, OrderedTreeError *error) {
  for (size_t index = 0; index < parent->child_count; index++) {
    if (strcmp(parent->children[index].label, label) == 0) {
      free(label);
      return parent->children[index].child;
    }
  }

  OrderedTreeNode *child = new_node();
  if (child == nullptr) {
    free(label);
    set_error(error, input, offset, "failed to allocate a tree node");
    return nullptr;
  }

  if (parent->child_count == parent->child_capacity) {
    ArrayGrowth const growth = grow_array(
        parent->children, parent->child_capacity, sizeof(parent->children[0]));
    if (!growth.succeeded) {
      free(label);
      free(child);
      set_error(error, input, offset, "failed to grow the child list");
      return nullptr;
    }
    parent->children = growth.data;
    parent->child_capacity = growth.capacity;
  }

  parent->children[parent->child_count++] =
      (OrderedTreeChild){.label = label, .child = child};
  return child;
}

[[nodiscard]] static bool cache_leaf_counts(OrderedTreeNode *node) {
  if (node->is_leaf) {
    if (node->child_count != 0) {
      return false;
    }
    node->leaf_count = 1;
    return true;
  }
  if (node->child_count == 0) {
    return false;
  }

  size_t total = 0;
  for (size_t index = 0; index < node->child_count; index++) {
    if (!cache_leaf_counts(node->children[index].child) ||
        total > SIZE_MAX - node->children[index].child->leaf_count) {
      return false;
    }
    total += node->children[index].child->leaf_count;
  }
  node->leaf_count = total;
  return true;
}

bool ordered_tree_parse_leaf_stream_ordered(char const *input,
                                            OrderedTreeBranchOrder order,
                                            OrderedTreeNode **root,
                                            OrderedTreeError *error) {
  if (root == nullptr) {
    return false;
  }
  *root = nullptr;
  if (error != nullptr) {
    *error = (OrderedTreeError){.line = 1, .column = 1, .message = ""};
  }
  if (input == nullptr) {
    set_error(error, "", 0, "the leaf stream is null");
    return false;
  }
  if (input[0] == '\0') {
    set_error(error, input, 0, "the leaf stream is empty");
    return false;
  }
  if (order != ORDERED_TREE_PRESERVE_INPUT_ORDER &&
      order != ORDERED_TREE_CHECK_VECTOR_ORDER &&
      order != ORDERED_TREE_REORDER_VECTOR_ORDER) {
    set_error(error, input, 0, "invalid branch-order mode");
    return false;
  }

  LeafPathList paths = {0};
  if (!parse_paths(input, &paths, error)) {
    leaf_path_list_destroy(&paths);
    return false;
  }
  if (order == ORDERED_TREE_REORDER_VECTOR_ORDER) {
    qsort(paths.paths, paths.path_count, sizeof(paths.paths[0]),
          compare_paths_for_qsort);
  }

  OrderedTreeNode *result = new_node();
  if (result == nullptr) {
    set_error(error, input, 0, "failed to allocate the tree root");
    leaf_path_list_destroy(&paths);
    return false;
  }

  for (size_t path_index = 0; path_index < paths.path_count; path_index++) {
    LeafPath const *path = &paths.paths[path_index];
    OrderedTreeNode *node = result;
    for (size_t component_index = 0; component_index < path->component_count;
         component_index++) {
      if (node->is_leaf) {
        set_error(error, input, path->input_offset,
                  "an existing path is a prefix of this path");
        goto failure;
      }
      size_t const label_length = strlen(path->components[component_index]);
      char *label = malloc(label_length + 1);
      if (label == nullptr) {
        set_error(error, input, path->input_offset,
                  "failed to allocate a component label");
        goto failure;
      }
      memcpy(label, path->components[component_index], label_length + 1);
      node =
          find_or_append_child(node, label, input, path->input_offset, error);
      if (node == nullptr) {
        goto failure;
      }
    }

    if (node->is_leaf) {
      set_error(error, input, path->input_offset, "duplicate normalized path");
      goto failure;
    }
    if (node->child_count != 0) {
      set_error(error, input, path->input_offset,
                "this path is a prefix of an existing path");
      goto failure;
    }
    node->is_leaf = true;
  }

  if (!cache_leaf_counts(result)) {
    set_error(error, input, 0, "invalid or excessively large tree");
    goto failure;
  }
  if (order == ORDERED_TREE_CHECK_VECTOR_ORDER) {
    for (size_t index = 1; index < paths.path_count; index++) {
      if (compare_paths(&paths.paths[index - 1], &paths.paths[index]) >= 0) {
        set_error(error, input, paths.paths[index].input_offset,
                  "branches are not in bitstring-vector order");
        goto failure;
      }
    }
  }

  leaf_path_list_destroy(&paths);
  *root = result;
  return true;

failure:
  leaf_path_list_destroy(&paths);
  ordered_tree_destroy(result);
  return false;
}

bool ordered_tree_parse_leaf_stream(char const *input, OrderedTreeNode **root,
                                    OrderedTreeError *error) {
  return ordered_tree_parse_leaf_stream_ordered(
      input, ORDERED_TREE_PRESERVE_INPUT_ORDER, root, error);
}

size_t ordered_tree_height(OrderedTreeNode const *root) {
  if (root == nullptr || root->is_leaf) {
    return 0;
  }

  size_t height = 0;
  for (size_t index = 0; index < root->child_count; index++) {
    size_t const child_height =
        ordered_tree_height(root->children[index].child);
    if (child_height == SIZE_MAX) {
      return SIZE_MAX;
    }
    if (child_height + 1 > height) {
      height = child_height + 1;
    }
  }
  return height;
}

size_t ordered_tree_leaf_count(OrderedTreeNode const *root) {
  return root == nullptr ? 0 : root->leaf_count;
}

[[nodiscard]] static bool write_paths(FILE *out, OrderedTreeNode const *node,
                                      char const **path, size_t depth) {
  if (node->is_leaf) {
    if (node->child_count != 0) {
      return false;
    }
    for (size_t index = 0; index < depth; index++) {
      if (index != 0 && fputc(',', out) == EOF) {
        return false;
      }
      if (fputs(path[index][0] == '\0' ? "e" : path[index], out) < 0) {
        return false;
      }
    }
    return fputc('|', out) != EOF;
  }

  for (size_t index = 0; index < node->child_count; index++) {
    path[depth] = node->children[index].label;
    if (!write_paths(out, node->children[index].child, path, depth + 1)) {
      return false;
    }
  }
  return true;
}

bool ordered_tree_write_leaf_stream(FILE *out, OrderedTreeNode const *root) {
  if (out == nullptr || root == nullptr) {
    return false;
  }

  size_t const height = ordered_tree_height(root);
  if (height == SIZE_MAX || height > SIZE_MAX / sizeof(char const *)) {
    return false;
  }
  char const **path =
      height == 0 ? nullptr : (char const **)malloc(height * sizeof(*path));
  if (height != 0 && path == nullptr) {
    return false;
  }
  bool const succeeded = write_paths(out, root, path, 0);
  free((void *)path);
  return succeeded;
}

[[nodiscard]] static bool write_dot_children(FILE *out,
                                             OrderedTreeNode const *node,
                                             size_t node_id, size_t *next_id) {
  for (size_t index = 0; index < node->child_count; index++) {
    if (*next_id == SIZE_MAX) {
      return false;
    }
    size_t const child_id = ++*next_id;
    if (fprintf(out, "\t%zu -- %zu [label=", node_id, child_id) < 0 ||
        !dot_write_quoted(out, node->children[index].label) ||
        fputs("];\n", out) < 0 ||
        !write_dot_children(out, node->children[index].child, child_id,
                            next_id)) {
      return false;
    }
  }
  return true;
}

bool ordered_tree_write_dot(FILE *out, OrderedTreeNode const *root) {
  if (out == nullptr || root == nullptr || fputs("strict graph {\n", out) < 0) {
    return false;
  }
  size_t next_id = 1;
  if ((root->is_leaf && fputs("\t1;\n", out) < 0) ||
      !write_dot_children(out, root, 1, &next_id)) {
    return false;
  }
  return fputs("}\n", out) >= 0;
}

[[nodiscard]] static bool
write_partition_at_depth(FILE *out, OrderedTreeNode const *node, size_t depth,
                         size_t target_depth, bool *stopped) {
  if (depth == target_depth) {
    return fprintf(out, "Part %zu\n", node->leaf_count) >= 0;
  }
  if (node->is_leaf) {
    *stopped = true;
    return fprintf(stderr,
                   "Got to a leaf or size %zu, something is going wrong!\n",
                   node->leaf_count) >= 0;
  }
  for (size_t index = 0; index < node->child_count && !*stopped; index++) {
    if (!write_partition_at_depth(out, node->children[index].child, depth + 1,
                                  target_depth, stopped)) {
      return false;
    }
  }
  return true;
}

bool ordered_tree_write_p_partition(FILE *out, OrderedTreeNode const *root,
                                    unsigned const p) {
  if (out == nullptr || root == nullptr || p == UINT_MAX) {
    return false;
  }
  bool stopped = false;
  return write_partition_at_depth(out, root, 0, (size_t)p + 1, &stopped);
}
