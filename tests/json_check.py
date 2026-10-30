"""Dependency-free checker for the three tree document schemas in developing.md."""


def natural(value):
    assert type(value) is int and value >= 0, value


def ids(value):
    assert isinstance(value, list)
    for vertex in value:
        natural(vertex)
    assert value == sorted(set(value)), value


def pair(value, check):
    assert isinstance(value, list) and len(value) == 2
    for item in value:
        check(item)


def decomposition(node):
    if node is None:
        return
    assert list(node) == ['h', 'H', 'T', 'S', 'children'], node
    natural(node['h'])
    for part in 'HTS':
        ids(node[part])
    assert isinstance(node['children'], list)
    for child in node['children']:
        assert child is not None
        decomposition(child)


def result(value):
    assert type(value['complete']) is bool
    pair(value['regions'], ids)
    ids(value['unresolved'])
    pair(value['trees'], decomposition)
    assert isinstance(value['shaves'], list)
    for shave in value['shaves']:
        assert list(shave) == ['certified', 'attracted']
        assert list(shave['certified']) == ['complete', 'regions', 'unresolved', 'trees', 'shaves']
        result(shave['certified'])
        pair(shave['attracted'], ids)


def prefix(node, measures=False):
    if node is None:
        return
    assert list(node) == (['priority', 'vertices', 'children'] if measures else ['children'])
    assert isinstance(node['children'], list)
    if measures:
        if node['children']:
            natural(node['priority'])
            assert node['vertices'] is None
        else:
            assert node['priority'] is None
            ids(node['vertices'])
    for branch in node['children']:
        assert list(branch) == ['label', 'node']
        assert isinstance(branch['label'], str) and set(branch['label']) <= set('01')
        assert branch['node'] is not None
        prefix(branch['node'], measures)


def metrics(value):
    assert list(value) == ['trees', 'shaves']
    def check(item):
        if item is None:
            return
        assert list(item) == ['nodes', 'leaves', 'internal_nodes', 'height', 'width',
                              'max_arity', 'mean_internal_arity', 'branching_nodes',
                              'min_leaf_depth', 'mean_leaf_depth', 'strahler']
        for name, number in item.items():
            if name.startswith('mean_'):
                assert type(number) in (int, float) and number >= 0
            else:
                natural(number)
    pair(value['trees'], check)
    for shave in value['shaves']:
        metrics(shave)


def document(value):
    assert isinstance(value, dict)
    assert list(value)[:4] == ['schema', 'version', 'algorithm', 'options']
    assert isinstance(value['version'], str) and value['version']
    assert isinstance(value['algorithm'], str) and value['algorithm']
    assert isinstance(value['options'], list) and all(isinstance(x, str) for x in value['options'])
    if 'game' in value:
        assert list(value['game']) == ['vertices', 'edges', 'priorities']
        natural(value['game']['vertices'])
        natural(value['game']['edges'])
        ids(value['game']['priorities'])
    if value['schema'] == 'strahler-decomp/1':
        assert list(value)[4:] == ['game', 'initial_won', 'complete', 'regions', 'unresolved', 'trees', 'shaves', 'metrics', 'parameters', 'budget_exhausted']
        pair(value['initial_won'], ids)
        result(value)
        metrics(value['metrics'])
        def parameters(item):
            assert isinstance(item, dict)
            assert set(item) <= {'H', 'K', 't', 'arity'}
            for number in item.values():
                natural(number)
        pair(value['parameters'], parameters)
        assert type(value['budget_exhausted']) is bool
    elif value['schema'] == 'strahler-tree/1':
        assert list(value)[4:] == ['parameters', 'tree']
        assert list(value['parameters']) == ['k', 'h', 't']
        for number in value['parameters'].values():
            natural(number)
        assert 1 <= value['parameters']['k'] <= value['parameters']['h']
        assert value['tree'] is not None
        prefix(value['tree'])
    else:
        assert value['schema'] == 'strahler-pm/1'
        if 'game' not in value:
            assert list(value)[4:] == ['tree'] and value['tree'] is not None
            prefix(value['tree'])
        else:
            assert list(value)[4:] == ['game', 'rounds']
            assert isinstance(value['rounds'], list) and value['rounds']
            previous = 0
            for round_ in value['rounds']:
                assert list(round_) == ['round', 'complete', 'unresolved', 'trees']
                natural(round_['round'])
                assert round_['round'] > previous
                previous = round_['round']
                assert type(round_['complete']) is bool
                ids(round_['unresolved'])
                assert round_['complete'] == (not round_['unresolved'])
                def player(tree):
                    if tree is None:
                        return
                    assert list(tree) == ['parameters', 'tree', 'shaved', 'attracted']
                    assert list(tree['parameters']) == ['k', 't', 'bits', 'depth']
                    for number in tree['parameters'].values():
                        natural(number)
                    prefix(tree['tree'], True)
                    ids(tree['shaved'])
                    ids(tree['attracted'])
                pair(round_['trees'], player)
