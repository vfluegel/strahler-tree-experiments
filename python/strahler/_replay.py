"""Replay borrowed-set and label events without using result trees as state."""


def vertices(node, side=True):
    if node is None:
        return set()
    return (set(node['H']) | set(node['T']) |
            (set(node['S']) if side else set()) |
            set().union(*(vertices(c) for c in node['children'])))


class Replay:
    def __init__(self):
        self.nodes = [{}, {}]
        self.labels = [None, None]
        self.levels = [{}, {}]
        self.shaves = []
        self.step = 0

    def tree(self, player, root=(), domain=None, candidate=False):
        if self.labels[player] is None:
            def visit(path):
                node = self.nodes[player].get(path)
                if node is None:
                    return None
                return {**{k: sorted(node[k]) for k in 'HTS'}, 'h': node['h'],
                        'children': [visit(path + (i,))
                                     for i in range(node['children'])]}
            return visit(root)
        if domain is not None and not domain:
            return None
        parts = {}
        for vertex, (path, part, h) in self.labels[player].items():
            if domain is not None and vertex not in domain:
                continue
            if path[:len(root)] != root:
                continue
            for depth in range(len(root), len(path) + 1):
                prefix = path[:depth]
                parts.setdefault(prefix, {'h': h + 2 * (len(path) - depth),
                                           'H': [], 'T': [], 'S': []})
            parts[path][part].append(vertex)
        if candidate:
            parts.setdefault(root, {'h': self.levels[player][root],
                                    'H': [], 'T': [], 'S': []})

        def visit(path):
            if path not in parts:
                return (dict(h=parts[path[:-1]]['h'] - 2, H=[], T=[], S=[], children=[])
                        if candidate else None)
            node = parts[path]
            indices = [p[-1] for p in sorted(parts)
                       if len(p) == len(path) + 1 and p[:-1] == path]
            if candidate:
                # Checker paths must retain gaps in the occupied host children.
                indices = range(max(indices, default=-1) + 1)
            node = {**{k: sorted(node[k]) for k in 'HTS'}, 'h': node['h'],
                    'children': [visit(path + (i,)) for i in indices]}
            return node
        return visit(root)

    def materialized(self, player):
        """Root TOP lies outside the certified dominion, but stays in replay."""
        node = self.tree(player)
        if self.labels[player] is not None and node is not None:
            node['S'] = []
            if not vertices(node, False):
                return None
        return node

    def apply(self, event):
        kind = event['event']
        if kind == 'header':
            assert event['schema'] == 'strahler-trace/1'
            return
        assert event['step'] == self.step + 1, event
        self.step = event['step']
        player = event.get('player', 0)
        path = tuple(event.get('path', ()))
        if kind == 'phase':
            if event['phase'] == 'labels_reset':
                self.labels[player] = {}
                self.levels[player] = {}
            elif event['phase'] == 'discard':
                self.nodes[player] = {p: n for p, n in self.nodes[player].items()
                                      if p[:len(path)] != path}
            elif event['phase'] == 'shave_applied':
                actual = [self.materialized(p) for p in range(2)]
                assert actual == event['result']['trees'], (actual, event)
                self.shaves.append(actual)
                self.nodes = [{}, {}]
                self.labels = [None, None]
                self.levels = [{}, {}]
        elif kind == 'set':
            self.labels[player] = None
            node = self.nodes[player].setdefault(
                path, {'H': set(), 'T': set(), 'S': set()})
            node.update(h=event['h'], children=event['children'])
            node[event['part']] = set(event['vertices'])
            for child in list(self.nodes[player]):
                if (len(child) > len(path) and child[:len(path)] == path and
                        child[len(path)] >= event['children']):
                    del self.nodes[player][child]
        elif kind == 'move':
            moved = set(event['vertices'])
            self.nodes[player][path][event['part']] -= moved
            self.nodes[player][tuple(event['to_path'])][event['to_part']] |= moved
        elif kind == 'labels':
            assert self.labels[player] is not None, event
            self.labels[player][event['vertex']] = path, event['part'], event['h']
            for depth in range(len(path) + 1):
                self.levels[player][path[:depth]] = event['h'] + 2 * (len(path) - depth)
        elif kind == 'result':
            actual = [self.materialized(p) for p in range(2)]
            assert actual == event['result']['trees'], (actual, event['result']['trees'])
            assert self.shaves == [s['certified']['trees']
                                    for s in event['result']['shaves']]


def check_input(event, node):
    """The tiny C checker accepts a preorder tree, using external vertex IDs."""
    def ids(values):
        return f'{len(values)} ' + ' '.join(map(str, sorted(values))) + ' '

    def encode(current):
        if current is None:
            return '0 '
        output = f"1 {current['h']} " + ids(set(current['H']) | set(current['T']))
        output += f"{len(current['children'])} "
        for child in current['children']:
            output += ids(vertices(child, False)) + ids(vertices(child)) + encode(child)
        return output
    return f"{event['player']} " + ids(event['subgame']) + encode(node) + '\n'
