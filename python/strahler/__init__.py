"""Parity games and tree-relative attractor decompositions via libstrahler.

Vertex sets and mappings use the input's external IDs. Result/trace bounds use
solver priorities, including compact priorities when ``compress=True``.
Keyword options map underscores to hyphens; True emits a flag, False/None
omit it. ``args`` appends exact CLI option strings. Formats are implicit.
"""

from copy import deepcopy
from dataclasses import dataclass
from enum import IntEnum
import json
from pathlib import Path
import weakref

from . import _native
from ._native import InputError, StrahlerError
from ._replay import Replay

__all__ = ['Game', 'Node', 'Result', 'Trace', 'State', 'CheckResult', 'Violation', 'Condition',
           'StrahlerError', 'InputError', 'decompose', 'trace', 'check',
           'progress_measures', 'genstree', 'pms_tree', 'materialize_partial',
           'version']


def version():
    """Return the Meson project version of the loaded library."""
    return _native.library().strahler_version().decode('ascii')


class Game:
    """An immutable native game; mappings and sets use external vertex IDs.

    ``vertices`` is a tuple of IDs, ``edges`` a tuple of (source, target) pairs.
    ``priority``, ``owner`` (0=Even, 1=Odd), ``successors`` and ``names`` are
    dictionaries keyed by ID. Missing names are None. Use ``close()`` or a
    context manager to release early; otherwise GC releases the native game.
    """

    @classmethod
    def from_text(cls, text):
        """Parse PGSolver text; raise InputError with the input location."""
        if not isinstance(text, str):
            raise TypeError('Game text must be a string')
        encoded = text.encode('utf-8')
        handle = _native.call('game_parse', encoded, len(encoded), handle=True)
        game = cls.__new__(cls)
        game._handle = handle
        game._finalizer = weakref.finalize(game, _native.library().strahler_game_free, handle)
        data = json.loads(_native.call('game_summary', handle))
        rows = data['vertices']
        game.vertices = tuple(row['id'] for row in rows)
        for name, key in [('priority', 'priority'), ('owner', 'owner'),
                          ('names', 'name')]:
            setattr(game, name, {row['id']: row[key] for row in rows})
        game.successors = {row['id']: tuple(row['successors']) for row in rows}
        game.edges = tuple((v, w) for v in game.vertices for w in game.successors[v])
        game.start = data['start']
        return game

    @classmethod
    def from_file(cls, path):
        """Read a UTF-8 PGSolver file and parse it."""
        return cls.from_text(Path(path).read_text(encoding='utf-8'))

    def _pointer(self):
        if not self._finalizer.alive:
            raise ValueError('Game is closed')
        return self._handle

    def close(self):
        """Release this game's native memory; subsequent calls reject it."""
        self._finalizer()

    def __enter__(self):
        self._pointer()
        return self

    def __exit__(self, *exc):
        self.close()


@dataclass(frozen=True)
class Node:
    """A tree-relative node. H/T/S are frozensets; path is child indices.

    H contains highest-priority vertices, T the remaining top attractor,
    S the side attractor. Children may be None in an unfinished trace.
    """
    h: int
    H: frozenset
    T: frozenset
    S: frozenset
    children: tuple = ()
    path: tuple = ()

    @classmethod
    def from_dict(cls, data, path=()):
        """Build a node (or None) from the documented tree JSON object."""
        if data is None:
            return None
        return cls(data['h'], *(frozenset(data[key]) for key in 'HTS'),
                   tuple(cls.from_dict(child, path + (i,))
                         for i, child in enumerate(data['children'])), path)

    def to_dict(self):
        """Return the {h,H,T,S,children} representation without path metadata."""
        return dict(h=self.h, **{k: sorted(getattr(self, k)) for k in 'HTS'},
                    children=[child.to_dict() if child else None for child in self.children])


class Result:
    """A decomposition document. ``to_json()`` retains the exact C output.

    ``trees`` is a pair of Node/None, ``regions`` a pair of dominion frozensets
    covered by those attractor decompositions, excluding earlier shaved wins;
    ``complete``, ``metrics`` and ``unresolved`` preserve the solver's claims.
    ``data`` includes initial_won, iterative shaving certificates, solver-reported
    host ``parameters`` (Even/Odd) and the adaptive leaf-limit ``budget_exhausted``.
    """

    def __init__(self, text):
        self._text = text
        self.data = json.loads(text)
        self.trees = tuple(Node.from_dict(tree) for tree in self.data['trees'])
        self.regions = tuple(map(frozenset, self.data['regions']))
        self.unresolved = frozenset(self.data['unresolved'])
        self.complete = self.data['complete']
        self.metrics = self.data.get('metrics', {})

    def to_json(self):
        """Return the exact strahler-decomp/1 JSON, including its newline."""
        return self._text


def decompose(game, algorithm=None, *, args=None, **options):
    """Run pg2adot (default: lifting), returning certified complete/partial trees.

    Algorithms: zielonka, zielonka-sym, lifting, lifting-sym. Host options and
    preprocessing use the CLI's spelling, e.g. adaptive_t='incremental',
    tree_k=2, kary=2, tree_file=path, initial_shave='all', iter_shave=True.
    """
    flags = _native.options(args, options, algorithm)
    return Result(_native.call('decompose', game._pointer(), flags, len(flags),
                               _native.Callback(), None))


class Condition(IntEnum):
    """Stable C ADVerifyCondition codes; .name is readable, int() is numeric."""

    OK = 0
    INPUT = 1
    MEMORY = 2
    NODE = 3
    PRIORITY = 4
    TOP = 5
    CHILD_BOUND = 6
    TRAP = 7
    CHILD = 8
    ATTRACTOR = 9
    PARTITION = 10
    CORE = 11
    SHAVE = 12
    TOTALITY = 13
    REGION = 14
    INCOMPLETE = 15
    INACTIVE = 16
    POSITION = 17
    COMPATIBILITY = 18
    DOMINION = 19
    REGULAR = 20
    LAZY = 21
    EMPTY_CORE = 22


@dataclass(frozen=True)
class Violation:
    """A located condition, with an optional directed edge in external IDs."""
    condition: Condition
    path: tuple
    vertex: int | None
    edge: tuple | None


@dataclass(frozen=True)
class CheckResult:
    """Definition 1 check: named condition, child-index path, external vertex.

    condition is a Condition IntEnum (0 means OK), vertex is None when
    unlocated. Failure messages explain the violated condition. ``violations``
    collects independent conditions at every reachable node. Set comparisons,
    priority bounds, totality and traps retain one vertex witness per condition
    and node; an edge is included when a direct move witnesses the failure. Invalid node
    metadata stops that branch; dependent attractor checks require total
    domains and contained targets. H/T/S inconsistencies locate every bad entry.
    Paths refer to the supplied tree, before restricting or pruning children.
    """
    ok: bool
    condition: Condition
    path: tuple
    vertex: int | None
    message: str
    violations: tuple = ()


def _json(value):
    if isinstance(value, str):
        if '\0' in value:
            raise ValueError('JSON must not contain NUL bytes')
        return value.encode('utf-8')
    if isinstance(value, Node):
        value = value.to_dict()
    return json.dumps(value, separators=(',', ':')).encode('utf-8')


def check(game, tree, player, subgame=None, *, compress=False):
    """Check a Node, dict, JSON string or None on an induced total subgame.

    subgame is an iterable of external IDs (default: all vertices). Stored
    sets are restricted to it; paths refer to the original tree. A failed
    decomposition returns ok=False; malformed inputs raise InputError.
    Bounds use solver normalization; compress must match the producing run.
    """
    if player not in (0, 1):
        raise ValueError('player must be 0 or 1')
    data = json.loads(_native.call('check', game._pointer(), _json(tree), player,
                                  None if subgame is None else _json(sorted(subgame)), int(compress)))
    data['path'] = tuple(data['path'])
    data['condition'] = Condition(data['condition'])
    data['violations'] = tuple(Violation(Condition(v['condition']), tuple(v['path']),
                                         v['vertex'], None if v['edge'] is None else tuple(v['edge']))
                               for v in data.get('violations', ()))
    return CheckResult(**data)


class State:
    """Both players' replayed trees immediately after an event.

    Trees preserve unfinished nodes and root TOP labels. ``candidate`` creates
    a path/subgame restriction matching an algorithm's check event. Shaved
    certificates are available as ``shaves``. No state certifies validity by
    itself; use check().
    """

    def __init__(self, replay, event, index):
        self._replay = deepcopy(replay)
        self.event, self.index = event, index
        self.trees = tuple(Node.from_dict(replay.materialized(p) if event['event'] == 'result'
                                         else replay.tree(p)) for p in range(2))
        self.shaves = deepcopy(replay.shaves)

    def candidate(self, player, path=(), subgame=None):
        """Materialize a check candidate, preserving empty label-based roots."""
        domain = None if subgame is None else set(subgame)
        return Node.from_dict(self._replay.tree(player, tuple(path), domain, candidate=True),
                              tuple(path))

    def materialized(self, player):
        """Return the final certificate shape (root TOP is outside its region)."""
        return Node.from_dict(self._replay.materialized(player))


class Trace:
    """Collected strahler-trace/1 events and their decomposition result.

    Indices are zero-based, including the header at 0; -1 is the final event.
    Moving backwards replays stored events without rerunning the algorithm.
    trace_limit raises StrahlerError on truncation; its events attribute keeps
    the observed prefix. Collection is synchronous and uses memory per event.
    """

    def __init__(self, game, events, result):
        self.game, self.events, self.result = game, tuple(events), result
        self._compress = '--compress' in result.data['options']
        self._cursor, self._replay = -1, Replay()
        self._checkpoint_interval = max(256, (len(self) + 127) // 128)
        self._checkpoints = {-1: deepcopy(self._replay)}

    def __len__(self):
        return len(self.events)

    def state(self, index):
        """Return both trees after event index, supporting reverse access."""
        if not isinstance(index, int):
            raise TypeError('Trace index must be an integer')
        if index < 0:
            index += len(self.events)
        if not 0 <= index < len(self.events):
            raise IndexError('Trace index out of range')
        checkpoint = max(i for i in self._checkpoints if i <= index)
        if index < self._cursor or checkpoint > self._cursor:
            self._cursor = checkpoint
            self._replay = deepcopy(self._checkpoints[checkpoint])
        while self._cursor < index:
            self._cursor += 1
            self._replay.apply(self.events[self._cursor])
            if self._cursor % self._checkpoint_interval == 0:
                self._checkpoints[self._cursor] = deepcopy(self._replay)
        return State(self._replay, self.events[index], index)

    def steps(self, kind=None):
        """Iterate states, optionally filtering by event kind (e.g. 'check')."""
        for i, event in enumerate(self.events):
            if kind is None or event['event'] == kind:
                yield self.state(i)

    def check_claims(self):
        """Yield (event, CheckResult) for independently rechecked trace claims."""
        for state in self.steps('check'):
            event = state.event
            candidate = state.candidate(event['player'], event['path'], event['subgame'])
            yield event, check(self.game, candidate, event['player'], event['subgame'], compress=self._compress)


def trace(game, algorithm=None, *, args=None, **options):
    """Collect all events; player must be 'both' so final trees match result."""
    flags = _native.options(args, options, algorithm)
    if any(flag in (b'--player=even', b'--player=odd') for flag in flags):
        raise ValueError("trace requires player='both'")
    events, errors = [], []

    @_native.Callback
    def collect(context, line, length):
        try:
            events.append(json.loads(_native.C.string_at(line, length)))
            return 1
        except BaseException as error:
            errors.append(error)
            return 0

    try:
        text = _native.call('decompose', game._pointer(), flags, len(flags), collect, None)
    except StrahlerError as error:
        if errors:
            raise errors[0] from error
        error.events = events
        raise
    return Trace(game, events, Result(text))


def progress_measures(game, *, args=None, **options):
    """Return the strahler-pm/1 dict of pg2pmdot rounds and prefix trees.

    algorithm='strahler' (default) or 'succinct'; k/t, bits, iter_shave and
    preprocessing options follow pg2pmdot. Each round keeps complete status.
    """
    flags = _native.options(args, options)
    return json.loads(_native.call('progress_measures', game._pointer(), flags, len(flags)))


def genstree(k, h, t):
    """Return genstree's strahler-tree/1 document for U^k_(t,h)."""
    if any(not isinstance(v, int) or not -(2**31) <= v < 2**31 for v in (k, h, t)):
        raise ValueError('k, h and t must be C integers')
    return json.loads(_native.call('genstree', k, h, t))


def pms_tree(text, *, args=None, **options):
    """Parse a leaf stream (at most 1025 levels); check_order/reorder use vector order."""
    encoded = text.encode('utf-8')
    flags = _native.options(args, options)
    return json.loads(_native.call('pms_tree', encoded, len(encoded), flags, len(flags)))


def materialize_partial(game, labels, *, compress=False):
    """Return {tree,problems} from compatible partial labels (see C API docs)."""
    return json.loads(_native.call('materialize_partial', game._pointer(), _json(labels), int(compress)))
