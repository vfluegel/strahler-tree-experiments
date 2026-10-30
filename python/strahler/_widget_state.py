"""Lazy, checkpointed notebook navigation; no optional widget imports."""

from bisect import bisect_right
from copy import deepcopy

from . import Node, check, materialize_partial


class TraceCursor:
    """Replay each prefix once, cache at most about 128 regularly spaced states.

    Public Trace.state handles tree checkpoints. Context checkpoints include
    the call stack, active game and recorded check claims.
    """

    def __init__(self, run, checkpoint_interval=None):
        self.run = run
        self.compress = '--compress' in run.result.data['options']
        self.interval = checkpoint_interval if checkpoint_interval is not None else max(256, (len(run) + 127) // 128)
        if not isinstance(self.interval, int) or self.interval < 1:
            raise ValueError('checkpoint_interval must be positive')
        self.index = -1
        self.stack = []
        self.active = list(run.game.vertices)
        self.claims = [None, None]
        self.checkpoints = {-1: self._snapshot()}
        self.kinds = {}
        self.calls = []
        self.ticks = []
        parents = []
        for i, event in enumerate(run.events):
            kind = event['event']
            if i % max(1, len(run) // 400) == 0 or kind == 'check':
                self.ticks.append(dict(index=i, kind=kind))
            self.kinds.setdefault(kind, []).append(i)
            if kind == 'call':
                self.calls.append(dict(index=i, parent=parents[-1] if parents else None,
                                       depth=len(parents), event=event))
                parents.append(i)
            elif kind == 'return' and parents:
                parents.pop()
        self.ticks = self.ticks[::max(1, (len(self.ticks) + 999) // 1000)]

    def _snapshot(self):
        return deepcopy((self.stack, self.active, self.claims))

    def seek(self, index):
        """Return State after index; a backwards seek starts at a checkpoint."""
        if not isinstance(index, int):
            raise TypeError('Trace index must be an integer')
        if index < 0:
            index += len(self.run)
        if not 0 <= index < len(self.run):
            raise IndexError('Trace index out of range')
        checkpoint = max(i for i in self.checkpoints if i <= index)
        if index < self.index or checkpoint > self.index:
            self.index = checkpoint
            self.stack, self.active, self.claims = deepcopy(self.checkpoints[self.index])
        while self.index < index:
            self.index += 1
            event = self.run.events[self.index]
            kind = event['event']
            if kind == 'call':
                self.stack.append(dict(index=self.index, event=event, sets={}))
            elif kind == 'return' and self.stack:
                self.stack.pop()
                if self.stack:
                    self.stack[-1]['sets']["U′_i"] = event['vertices']
            elif kind == 'phase' and event['phase'] in ('preprocessed', 'shave_applied'):
                self.active = event['vertices']
            elif kind == 'check':
                self.claims[event['player']] = dict(index=self.index, event=event)
            elif kind == 'attractor' and self.stack:
                self.stack[-1]['last_attractor'] = event
                sets = self.stack[-1]['sets']
                if event['kind'] == 'top':
                    sets.clear()
                    sets.update(G_i=event.get('subgame', self.active),
                                H_i=event['target'], A_i=event['vertices'])
                else:
                    sets['side attractor'] = event['vertices']
            if self.index % self.interval == 0:
                self.checkpoints[self.index] = self._snapshot()
        self.state = self.run.state(index)
        return self.state

    def next_index(self, kind):
        indices = self.kinds.get(kind, [])
        pos = bisect_right(indices, self.index)
        return indices[pos] if pos < len(indices) else self.index

    @property
    def subgame(self):
        event = self.state.event
        if 'subgame' in event:
            return event['subgame']
        if self.stack:
            return self.stack[-1]['sets'].get('G_i', self.stack[-1]['event']['subgame'])
        return self.active

    def candidate_path(self, player):
        event = self.state.event
        if event['event'] == 'result' or self.run.events[0]['algorithm'] == 'lifting':
            return ()
        if event['event'] == 'check' and event['player'] == player:
            return tuple(event['path'])
        return tuple(self.stack[-1]['event']['paths'][player]) if self.stack else ()

    def candidate(self, player):
        """Candidate, domain, and partial-materializer diagnostics at this step."""
        if player not in (0, 1):
            raise ValueError('player must be 0 or 1')
        event = self.state.event
        if event['event'] == 'result':
            return self.state.materialized(player), event['result']['regions'][player], []
        domain = self.subgame
        path = self.stack[-1]['event']['paths'][player] if self.stack else []
        if event['event'] == 'check' and event['player'] == player:
            path = event['path']
        if self.run.events[0]['algorithm'] == 'zielonka':
            return self.state.candidate(player, path, domain), domain, []
        root = self.state.trees[player]
        if root is None:
            return None, domain, []
        labels = {}

        def collect(node):
            if node is None:
                return
            for part in 'HTS':
                for vertex in getattr(node, part):
                    labels[vertex] = node.path, part
            for child in node.children:
                collect(child)

        collect(root)
        if not labels:
            return self.state.candidate(player, path, domain), domain, []
        # The public materializer needs just the occupied host prefixes. Empty
        # host branches contain no labels and cannot change this candidate.
        root_h = root.h
        paths = {p for p, part in labels.values()}
        leaves = []

        def host(prefix):
            if len(prefix) == root_h // 2:
                leaves.append(','.join(format(i, 'b') for i in prefix))
                return
            indices = [p[len(prefix)] for p in paths if len(p) > len(prefix) and p[:len(prefix)] == prefix]
            for i in range(max(indices, default=0) + 1):
                host(prefix + (i,))

        host(())
        stream = '|'.join(leaves) + '|'
        document = dict(player=player, h=root_h, tree=stream, labels=[
            dict(vertex=v, path=list(p), part=part) for v, (p, part) in labels.items()])
        partial = materialize_partial(self.run.game, document, compress=self.compress)
        if self.run.events[0]['algorithm'] == 'lifting':
            return Node.from_dict(partial['tree']), domain, partial['problems']
        # Keep original child indices, including gaps, for located checker paths.
        tree = self.state.candidate(player, path, domain)
        return tree, domain, partial['problems']

    def verify(self, player):
        tree, domain, problems = self.candidate(player)
        return check(self.run.game, tree, player, domain, compress=self.compress)

    def badge(self, player):
        tree, domain, problems = self.candidate(player)
        verdict = check(self.run.game, tree, player, domain, compress=self.compress)
        claim = self.claims[player]
        return dict(player=player, claim=claim, domain=list(domain), problems=problems,
                    status='attractor decomposition' if verdict.ok else 'decomposition candidate',
                    ok=verdict.ok, condition=verdict.condition.name, path=list(verdict.path),
                    vertex=verdict.vertex, message=verdict.message)
