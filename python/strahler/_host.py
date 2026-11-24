"""Immutable native host descriptions, independent of materialized AD trees."""

from dataclasses import dataclass
from functools import cached_property
from types import MappingProxyType


def path_name(path):
    # Dots disambiguate child 11 from child 1's child 1.
    return 'ε' + '.'.join(str(i + 1) for i in path)


@dataclass(frozen=True)
class HostNode:
    path: tuple
    h: int
    positions: tuple
    strahler: int
    leaves: int
    height: int
    nodes: int

    @property
    def core(self):
        return self.positions[0], self.positions[2]


@dataclass(frozen=True)
class HostPosition:
    index: int
    path: tuple
    part: str
    h: int

    @property
    def name(self):
        return 'TOP' if not self.path and self.part == 'S' else self.part + path_name(self.path)

    def accepts(self, priority):
        if not self.path and self.part == 'S':
            return True
        return priority == self.h if self.part == 'H' else priority <= self.h + (1 if self.part == 'S' else -1)


@dataclass(frozen=True)
class HostSnapshot:
    """One player's host epoch. Indices of implicit prefixes are provisional.

    ``height`` counts levels including the root. Node ``h`` is a priority
    bound. Actual Strahler/leaf/node metrics describe only underlying nodes.
    """
    player: int
    epoch: int
    nodes: tuple
    positions: tuple
    active: frozenset = frozenset()
    priorities: tuple = ()
    parameters: tuple = ()
    complete: bool = True

    def __deepcopy__(self, memo):
        # Checkpoints share the immutable host instead of repeatedly copying it.
        return self

    @cached_property
    def by_path(self):
        return MappingProxyType({n.path: n for n in self.nodes})

    @cached_property
    def priority_map(self):
        return MappingProxyType(dict(self.priorities))

    @cached_property
    def children(self):
        children = {n.path: [] for n in self.nodes}
        for n in self.nodes[1:]:
            children[n.path[:-1]].append(n.path)
        return MappingProxyType({p: tuple(paths) for p, paths in children.items()})

    @property
    def metrics(self):
        root = self.nodes[0]
        return dict(nodes=root.nodes, leaves=root.leaves, height=root.height,
                    strahler=root.strahler, positions=len(self.positions))

    def index(self, path, part):
        return self.by_path[tuple(path)].positions['HTS'.index(part)]

    def next_compatible_position(self, position, priority):
        if not self.complete:
            return None  # Unknown siblings may lie before this prefix's next node.
        index = position + 1
        while index < len(self.positions):
            p = self.positions[index]
            if p.accepts(priority):
                return index
            if p.part == 'H' and p.path and priority > p.h + 1:
                index = self.by_path[p.path].positions[2] + 1
            else:
                index += 1
        return None

    @classmethod
    def from_metadata(cls, data, player=0, epoch=0):
        if data.get('schema') != 'strahler-host/1':
            raise ValueError('Unsupported host schema')
        raw = {tuple(n['path']): n for n in data['nodes']}
        if () not in raw:
            raise ValueError('Host must contain its root')
        if player not in (0, 1) or any(n['h'] < 0 or n['h'] % 2 != player for n in raw.values()):
            raise ValueError('Host bounds must be nonnegative and have the player parity')
        children = {p: [] for p in raw}
        for p in sorted(raw):
            if p:
                if p[:-1] not in raw or p[-1] != len(children[p[:-1]]):
                    raise ValueError('Host child paths must be ordered and contiguous')
                if raw[p]['h'] != raw[p[:-1]]['h'] - 2:
                    raise ValueError('Host bounds must decrease by two')
                children[p[:-1]].append(p)
        positions, nodes = [], {}

        def position(path, part):
            i = len(positions)
            positions.append(HostPosition(i, path, part, raw[path]['h']))
            return i

        def visit(path):
            h, t = position(path, 'H'), position(path, 'T')
            descendants = [visit(p) for p in children[path]]
            s = position(path, 'S')
            order = (h, t, s)
            if data.get('complete', True) and 'positions' in raw[path] and tuple(raw[path]['positions']) != order:
                raise ValueError('Host indices disagree with native flattened order')
            values = [n.strahler for n in descendants]
            largest = max(values, default=1)
            node = HostNode(path, raw[path]['h'], order,
                            largest + (values.count(largest) >= 2),
                            sum(n.leaves for n in descendants) or 1,
                            1 + max((n.height for n in descendants), default=0),
                            1 + sum(n.nodes for n in descendants))
            nodes[path] = node
            return node

        visit(())
        return cls(player, epoch, tuple(nodes[p] for p in sorted(nodes)), tuple(positions),
                   frozenset(data.get('active', ())),
                   tuple(tuple(pair) for pair in data.get('priorities', ())),
                   tuple(sorted(data.get('parameters', {}).items())), data.get('complete', True))

    @classmethod
    def from_tree(cls, tree, *, player=0, root_bound=None, parameters=None):
        """Adapt a native genstree document (no universal-tree generator here)."""
        parameters = parameters or tree.get('parameters', {})
        root = tree.get('tree', tree)
        nodes = []

        def visit(node, path):
            nodes.append(dict(path=list(path), h=root_bound - 2 * len(path)))
            for i, child in enumerate(node.get('children', ())):
                visit(child.get('node', child), path + (i,))

        if root_bound is None:
            def height(n):
                return 1 + max((height(c.get('node', c)) for c in n.get('children', ())), default=0)
            root_bound = 2 * (height(root) - 1) + player
        visit(root, ())
        return cls.from_metadata(dict(schema='strahler-host/1', nodes=nodes,
                                      parameters=parameters), player)

    def extend(self, path, h):
        data = dict(schema='strahler-host/1', complete=False,
                    nodes=[dict(path=n.path, h=n.h) for n in self.nodes] + [dict(path=path, h=h)],
                    active=self.active, priorities=self.priorities, parameters=dict(self.parameters))
        return self.from_metadata(data, self.player, self.epoch)
