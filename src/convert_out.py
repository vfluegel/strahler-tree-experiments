import sys
import re 

if len(sys.argv) < 2:
    print("Missing required argument filename")
    sys.exit(2)

tree_file = sys.argv[1]

with open(tree_file, "r") as f:
    tree_data = f.read()

pattern = r"Bits:\s*(.*?)\s*Blocks:\s*(.*?)\s*(Part.*)"

match = re.search(pattern, tree_data, re.S)

if match:
    bits = match.group(1)
    blocks = match.group(2)
    p_level_groups = match.group(3)

    p_level_counts = [int(line.split()[1]) for line in p_level_groups.splitlines()]
    i = 0
    p_level_successors = []
    for group in p_level_counts:
        i += group
        p_level_successors.extend([f"{i}"] * group)

    header_content = f"""
#include <vector>
#define HAS_P

int k = , t = , h = , p =;

std::vector<std::vector<bool>> tree_b =
{{
{bits.replace("}", "},")}
}};

std::vector<std::vector<int>> tree_d =
{{
{blocks.replace("}", "},")}
}};

std::vector<int> p_successors = {{ 
    {", ".join(p_level_successors)} 
}};
"""

    with open(tree_file, "w") as f:
        f.write(header_content)

    print(f"Written {tree_file}")