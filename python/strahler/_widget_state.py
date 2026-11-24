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
        self.lifts = {}
        self.lift_rounds = {}
        round_numbers, first_lifts = [1, 1], [None, None]
        parents = []
        for i, event in enumerate(run.events):
            kind = event['event']
            if i % max(1, len(run) // 400) == 0 or kind == 'check':
                self.ticks.append(dict(index=i, kind=kind))
            self.kinds.setdefault(kind, []).append(i)
            player = event.get('player', 0)
            if kind == 'phase' and event['phase'] == 'labels_reset':
                round_numbers[player], first_lifts[player] = 1, None
            elif kind == 'lift':
                self.lifts.setdefault((player, event['vertex']), []).append(i)
                if first_lifts[player] is None:
                    # A labels write immediately precedes the first lift.
                    first_lifts[player] = i - 2
                self.lift_rounds[i] = first_lifts[player], round_numbers[player]
            elif kind == 'round_end':
                round_numbers[player] += 1
                first_lifts[player] = None
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

    def next_lift_index(self, vertex, player=None):
        indices = (self.lifts.get((player, vertex), []) if player is not None else
                   sorted(self.lifts.get((0, vertex), []) + self.lifts.get((1, vertex), [])))
        pos = bisect_right(indices, self.index)
        return indices[pos] if pos < len(indices) else self.index

    def explain_lift(self, index=None):
        """Recompute one failed constraint against the common pre-round labels.

        No explanation or per-vertex snapshot is computed during indexing.
        Symmetric writes deliberately have their own operation semantics.
        """
        index = self.index if index is None else index
        event = self.run.events[index]
        kind = event['event']
        if kind in ('move', 'set') or (kind == 'labels' and self.run.events[0]['algorithm'].endswith('-sym')):
            return dict(kind='symmetric', reason='Recursive cross-update in the current subgame; provisional separator labels. '
                        'This operation is not a next-compatible short lift. Final short lifts and verification certify bounded hosts.',
                        subgame=list(self.subgame), operation=event)
        if kind != 'lift':
            return None
        before, round_number = self.lift_rounds[index]
        state = self.run.state(before)
        player, vertex = event['player'], event['vertex']
        host = state.hosts[player]
        if host is None or not host.complete:
            return dict(kind='unavailable', reason='Complete native host metadata unavailable in this recording; exact lift explanation unavailable.')
        labels = {v: host.index(path, part) for v, (path, part, h) in state.raw_labels[player].items()}
        source, destination = event['from'], event['to']
        if labels.get(vertex) != source:
            raise ValueError('Lift source disagrees with pre-round labels')
        active = set(host.active)
        game = self.run.game
        position = host.positions[source]
        successors = set(game.successors[vertex]) & active
        result = dict(kind=position.part, player=player, vertex=vertex, owner=game.owner[vertex],
                      original_priority=game.priority[vertex], priority=host.priority_map[vertex],
                      epoch=host.epoch, round=round_number, before=before,
                      source=dict(index=source, path=list(position.path), part=position.part, name=position.name),
                      destination=dict(index=destination, path=event['path'], part=event['part'],
                                       name=host.positions[destination].name),
                      skipped=list(range(source + 1, min(destination, source + 65))),
                      skipped_count=destination - source - 1,
                      next=host.next_compatible_position(source, host.priority_map[vertex]),
                      applied=[self.run.events[i]['vertex'] for i in range(before + 1, index)
                               if self.run.events[i]['event'] == 'lift' and self.run.events[i]['player'] == player])
        pending = []
        for j in range(index + 1, len(self.run)):
            e = self.run.events[j]
            if e.get('player') != player:
                continue
            if e['event'] == 'round_end' or (e['event'] == 'phase' and e['phase'] == 'labels_reset'):
                break
            if e['event'] == 'lift':
                pending.append(e['vertex'])
        result['pending'] = pending
        if position.part == 'H':
            threshold = host.by_path[position.path].positions[2]
            allowed = {v for v in successors if labels[v] < threshold}
            failed = successors - allowed
            valid = bool(allowed) if game.owner[vertex] == player else not failed
            result.update(threshold=threshold, allowed=sorted(allowed),
                          edges=[[vertex, w] for w in sorted(failed)], valid=valid,
                          reason=(f'All active successors fail μ(w) < {threshold} (the after boundary).'
                                  if game.owner[vertex] == player else
                                  f'An opponent edge violates μ(w) < {threshold} (the after boundary).') +
                                 ' Earlier labels outside this core are allowed.')
        else:
            target = {v for v in active if labels[v] < source}
            safe = {v for v in active if labels[v] <= source}
            reached = set(target)
            while True:
                added = set()
                for v in safe - reached:
                    outgoing = set(game.successors[v]) & active
                    if (bool(outgoing & reached) if game.owner[v] == player else outgoing <= reached):
                        added.add(v)
                if not added:
                    break
                reached |= added
            outside = successors - reached
            escape = successors - safe
            result.update(target=sorted(target), safe=sorted(safe), reached=sorted(reached),
                          avoidance=sorted(safe - reached), valid=vertex in reached,
                          edges=[[vertex, w] for w in sorted(outside)], escape=sorted(escape),
                          reason=('Opponent can choose a successor outside R' +
                                  (' (including an exit from safe A).' if escape else ', preventing forced progress to L.')
                                  if game.owner[vertex] != player else
                                  'No active successor can force the earlier target L through safe A.') +
                                 ' R is the reach-through-safe attractor; opponent exits are retained.')
            # Compare the independently recomputed sets with the native pass.
            result['native_match'] = None
            for j in range(before, -1, -1):
                e = self.run.events[j]
                if e.get('player') != player:
                    continue
                if e['event'] == 'round_end' or (e['event'] == 'phase' and e['phase'] == 'labels_reset'):
                    break
                if (e['event'] == 'attractor' and set(e.get('safe', ())) == safe
                        and set(e['target']) == target):
                    result['native_match'] = set(e['vertices']) == reached
                    break
        result['destination_reason'] = (f"Position {destination} is the first strictly later position accepting normalized priority "
                                        f"{result['priority']}; {result['skipped_count']} incompatible position(s) skipped.")
        return result

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
