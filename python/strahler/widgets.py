"""Offline SVG notebook widgets. Install ``strahler[notebooks]`` to use them.

GameView(game, coordinates=None), TreeView(tree, game_view=None, mode=None),
and Stepper(trace, coordinates=None). No frontend dependencies or remote URLs.
"""

from html import escape
import json
from math import ceil, cos, hypot, isfinite, pi, sin, sqrt
import re
import warnings
from xml.etree import ElementTree as ET

try:
    import anywidget
    import traitlets as t
    from ipywidgets import VBox, HBox, Layout
except ImportError as error:
    raise ImportError('Notebook views need: pip install "strahler[notebooks]"') from error

from . import Node
from ._widget_state import TraceCursor

__all__ = ['GameView', 'TreeView', 'Stepper']

# A regular, point-up pentagon with the same 40-pixel height as the square.
_PENTAGON_RADIUS = 40 / (1 + cos(pi / 5))
_OWNER_STYLES = {
    '0': dict(player='Even', shape='rect', label='square',
              attrs=dict(x=-20, y=-20, width=40, height=40)),
    '1': dict(player='Odd', shape='polygon', label='pentagon', attrs=dict(points=' '.join(
        f'{_PENTAGON_RADIUS*sin(2*pi*i/5):.6f},{_PENTAGON_RADIUS*(1-cos(2*pi*i/5))-20:.6f}'
        for i in range(5)))),
}
_PART_COLOURS = dict(H='#dbeafe', T='#dcfce7', S='#ffedd5')
_EDGE_LIMIT = 200
_VIOLATION_COLOURS = ('#2563eb', '#b45309')


def _ascii(text):
    return text.encode('ascii', 'xmlcharrefreplace').decode('ascii')


def _violation_title(layer, violation):
    return f"{layer['name']}: {violation['condition']} at node {json.dumps(violation['path'], separators=(',', ':'))}"


def _violations(view, field, value):
    return [(layer, v) for layer in view.outlines for v in layer['violations'] if v[field] == value]


_CSS = """
.strahler { font: 14px system-ui, sans-serif; color: #182538; background: #fff;
  padding: 8px; border: 1px solid #b7c4d3; border-radius: 6px; overflow: auto; }
.strahler button, .strahler select, .strahler textarea { font: inherit; margin: 3px; }
.strahler button { cursor: pointer; } .strahler button:focus-visible,
.strahler [tabindex]:focus-visible { outline: 3px solid #2563eb; }
.strahler svg { min-width: 300px; max-width: 100%; }
.strahler text { fill: #182538; font: 12px system-ui, sans-serif; }
.strahler pre { white-space: pre-wrap; max-height: 240px; overflow: auto; }
.strahler .current { background: #fef08a; } .strahler .badge { padding: 6px; border-left: 5px solid #b45309; }
.strahler .ok { border-color: #15803d; } .strahler .timeline { width: 100%; height: 36px; }
"""

_ESM = r"""
const NS = 'http://www.w3.org/2000/svg';
function violations(model, field, value) {
  return model.get('outlines').flatMap(layer => layer.violations.filter(v => JSON.stringify(v[field]) === JSON.stringify(value)).map(v => [layer,v]));
}
function violationTitle(layer, v) { return `${layer.name}: ${v.condition} at node ${JSON.stringify(v.path)}`; }

function element(tag, text, parent) {
  const e = document.createElement(tag);
  if (text !== null) e.textContent = text;
  if (['button','input','select'].includes(tag)) e.setAttribute('data-control',text || tag);
  if (parent) parent.append(e);
  return e;
}
function svgElement(tag, attrs, parent) {
  const e = document.createElementNS(NS, tag);
  for (const [k, v] of Object.entries(attrs)) e.setAttribute(k, v);
  parent.append(e);
  return e;
}
function interactive(e, label, action) {
  e.setAttribute('tabindex', '0'); e.setAttribute('role', 'button');
  e.setAttribute('aria-label', label);
  e.onclick = event => { event.stopPropagation(); action(); };
  e.onkeydown = event => { if (event.key === 'Enter' || event.key === ' ') { event.preventDefault(); event.stopPropagation(); action(); } };
}
function vertexAttrs(model, id) {
  let fill = '#fff';
  for (const set of model.get('highlights')) if (set.vertices.includes(id)) fill = set.colour;
  const hot = model.get('hovered').includes(id), selected = model.get('kind') === 'game' && model.get('selected') === id;
  return {fill, stroke:hot ? '#c026d3' : selected ? '#dc2626' : '#182538', 'stroke-width':hot || selected ? 4 : 1.5};
}
function vertex(model, v, svg, painters) {
  const [x,y] = v.xy, group = svgElement('g', {'data-vertex':v.id}, svg);
  const style = model.get('owner_styles')[v.owner], name = v.name || String(v.id);
  if (style) {
    const shape = svgElement(style.shape, {...style.attrs, transform:`translate(${x} ${y})`}, group);
    painters.push(() => { for (const [k,value] of Object.entries(vertexAttrs(model,v.id))) shape.setAttribute(k,value); });
    const failed = violations(model,'vertex',v.id);
    failed.forEach(([layer,violation],i) => {
      const ring = svgElement(style.shape, {...style.attrs,transform:`translate(${x} ${y}) scale(${1.18+i*.15})`,fill:'none',stroke:layer.colour,'stroke-width':3},group);
      svgElement('title',{},ring).textContent = violationTitle(layer,violation);
    });
    svgElement('text', {x,y:y+4,'text-anchor':'middle'}, group).textContent = v.priority;
    svgElement('text', {x,y:y+31,'text-anchor':'middle'}, group).textContent = name;
  } else svgElement('text', {x,y:y+4,'text-anchor':'middle'}, group).textContent = name;
  const description = (style ? `${name}: priority ${v.priority}, ${style.player} (${style.label})` : `Vertex ${name}; game metadata unavailable`) + violations(model,'vertex',v.id).map(([l,v])=>' · '+violationTitle(l,v)).join('');
  svgElement('title', {}, group).textContent = description;
  if (model.get('kind') === 'game') interactive(group, `Select vertex ${description}`, () => { model.set('selected',v.id); model.save_changes(); });
  else group.setAttribute('pointer-events','none');
}
function gameEdges(svg, vertices, edges, markerID, colour, title=null, width=1) {
  if (!edges.length) return;
  const defs = svgElement('defs', {}, svg);
  const marker = svgElement('marker', {id:markerID,viewBox:'0 0 10 10',refX:9,refY:5,markerWidth:6,markerHeight:6,orient:'auto-start-reverse'}, defs);
  svgElement('path', {d:'M 0 0 L 10 5 L 0 10 z',fill:colour}, marker);
  const points = new Map(vertices.map(v => [v.id,v.xy]));
  for (const [a,b] of edges) {
    const [x,y] = points.get(a), [u,v] = points.get(b), dx = u-x, dy = v-y;
    const len = Math.hypot(dx,dy) || 1, r = 29;
    const path = a === b ? `M ${x-14} ${y-20} C ${x-48} ${y-46} ${x+48} ${y-46} ${x+14} ${y-20}` :
      `M ${x+dx*r/len} ${y+dy*r/len} Q ${(x+u)/2-dy*.12} ${(y+v)/2+dx*.12} ${u-dx*r/len} ${v-dy*r/len}`;
    const edge = svgElement('path', {d:path,fill:'none',stroke:colour,'stroke-width':width,'marker-end':`url(#${markerID})`}, svg);
    if(title)svgElement('title',{},edge).textContent=title;
    else edge.setAttribute('pointer-events','none');
  }
}
function game(model, el) {
  const d = model.get('data'), painters = [];
  const svg = svgElement('svg', {viewBox:'0 0 800 520',role:'group','aria-label':'Parity game. Squares Even, pentagons Odd.'}, el);
  gameEdges(svg,d.vertices,d.edges,'arrow-'+model.model_id,'#64748b');
  for (const [i,layer] of model.get('outlines').entries()) for (const v of layer.violations) if(v.edge) {
    gameEdges(svg,d.vertices,[v.edge],`violation-${model.model_id}-${i}`,layer.colour,violationTitle(layer,v),5-i*2);
  }
  for (const v of d.vertices) vertex(model,v,svg,painters);
  const legend = element('div','Square: Even · Pentagon: Odd · ',el);
  for (const set of model.get('highlights')) { const label = element('span',`${set.name}  `,legend); label.style.backgroundColor = set.colour; }
  for (const layer of model.get('outlines')) { const label=element('span',layer.name+'  ',legend); label.style.color=layer.colour; }
  return () => painters.forEach(paint => paint());
}
function tree(model, el) {
  const select = element('select', null, el), painters = [];
  select.setAttribute('aria-label', 'Tree layout');
  for (const value of ['squares', 'tree']) { const o = element('option', value === 'squares' ? 'Nested squares' : 'Node-link tree', select); o.value = value; }
  select.value = model.get('mode');
  select.onchange = () => { model.set('mode', select.value); model.save_changes(); };
  const root = model.get('data');
  if (!root) { element('p', '∅ — empty tree', el); return () => {}; }
  const hover = (e, n) => {
    e.setAttribute('data-path', JSON.stringify(n.path));
    const enter = () => { model.set('hovered_path', n.path); model.set('hovered', n.vertices); model.save_changes(); };
    const leave = () => { model.set('hovered_path', null); model.set('hovered', []); model.save_changes(); };
    e.onpointerover = event => { event.stopPropagation(); enter(); };
    e.onpointerout = event => { event.stopPropagation(); if (!e.contains(event.relatedTarget)) leave(); };
    e.onfocus = enter; e.onblur = leave;
    interactive(e, `Tree node ${n.label}`, () => {
      if (JSON.stringify(model.get('selected')) === JSON.stringify(n.path)) model.send({command:'select',path:n.path});
      else { model.set('selected', n.path); model.save_changes(); }
    });
  };
  const outline = (g, n, attrs) => {
    for (const [layer,v] of violations(model,'path',n.path)) {
      const ring=svgElement('rect',{...attrs,fill:'none',stroke:layer.colour,'stroke-width':7},g);
      svgElement('title',{},ring).textContent=violationTitle(layer,v);
    }
    const rect = svgElement('rect', {...attrs,'data-node-outline':JSON.stringify(n.path)},g);
    painters.push(() => {
      const hot = JSON.stringify(model.get('hovered_path')) === JSON.stringify(n.path);
      const selected = JSON.stringify(model.get('selected')) === JSON.stringify(n.path);
      rect.setAttribute('stroke',hot ? '#c026d3' : selected ? '#dc2626' : '#64748b');
      rect.setAttribute('stroke-width',hot || selected ? 4 : 1);
    });
  };
  const [width,height] = root.extent[select.value];
  const svg = svgElement('svg', {viewBox:`0 0 ${width} ${height}`,role:'group','aria-label':select.value === 'squares' ? 'Nested squares. Squares Even, pentagons Odd.' : 'Ordered tree'},el);
  if (select.value === 'squares') {
    const visit = (n, parent) => {
      const outer = svgElement('g', {}, parent); hover(outer,n);
      for (const part of ['H','T','S']) {
        const [x,y,w,h] = n.regions[part];
        svgElement('rect', {x,y,width:w,height:h,fill:root.colours[part],stroke:'#64748b',class:`part-${part}`},outer);
        const label = svgElement('text',{x:x+5,y:y+16,'pointer-events':'none'},outer);
        label.textContent = part;
        svgElement('tspan',{'baseline-shift':'super','font-size':9},label).textContent = n.name;
      }
      for (const child of n.children) visit(child,outer);
      const [x,y,w,h] = n.box;
      outline(outer,n,{x,y,width:w,height:h,fill:'none','pointer-events':'none'});
      svgElement('title',{},outer).textContent = n.label + violations(model,'path',n.path).map(([l,v])=>' · '+violationTitle(l,v)).join('');
    };
    visit(root,svg);
    if (model.get('edges') && root.game.edges.length <= root.edge_limit) gameEdges(svg,root.game.vertices,root.game.edges,'arrow-'+model.model_id,'#94a3b8');
    for (const v of root.game.vertices) vertex(model,v,svg,painters);
    svgElement('text',{x:5,y:height-8},svg).textContent = 'Square: Even · Pentagon: Odd';
  } else {
    const visit = n => {
      const [x,y] = n.xy;
      for (const child of n.children) {
        svgElement('line',{x1:x,y1:y,x2:child.xy[0],y2:child.xy[1],stroke:'#94a3b8'},svg);
        visit(child);
      }
      const g = svgElement('g',{},svg); hover(g,n);
      outline(g,n,{x:x-58,y:y-17,width:116,height:34,rx:5,fill:'#dbeafe'});
      svgElement('text',{x,y:y+4,'text-anchor':'middle','pointer-events':'none'},g).textContent = n.label;
      svgElement('title',{},g).textContent = `${n.label}: ${n.vertices.join(', ')}` + violations(model,'path',n.path).map(([l,v])=>' · '+violationTitle(l,v)).join('');
    };
    visit(root);
  }
  return () => painters.forEach(paint => paint());
}
function controls(model, el) {
  const d = model.get('data');
  const send = (command, extra={}) => model.send({command,...extra});
  for (const [label, command] of [['Back','back'],['Step','step'],['Run to end','end']]) {
    element('button',label,el).onclick = () => send(command);
  }
  const kinds = element('select',null,el); kinds.setAttribute('aria-label','Next event kind');
  for (const k of d.kinds) element('option',k,kinds).value=k;
  kinds.value = model.get('next_kind');
  kinds.onchange = () => { model.set('next_kind', kinds.value); model.save_changes(); };
  element('button','Next event of kind',el).onclick = () => send('next',{kind:kinds.value});
  for (const kind of ['call','return','check']) if(d.kinds.includes(kind)) {
    element('button','Next '+kind,el).onclick=()=>send('next',{kind});
  }
  const slider = element('input',null,el); slider.type='range'; slider.min=0; slider.max=d.length-1; slider.value=d.index;
  slider.style.width='100%'; slider.setAttribute('aria-label','Trace position');
  slider.onchange=()=>send('seek',{index:Number(slider.value)});
  element('div','Timeline (sampled): hover a tick for its event name; purple means check.',el);
  const timeline=svgElement('svg',{viewBox:'0 0 1000 32',class:'timeline',role:'img','aria-label':'Event timeline; purple ticks mark checks'},el);
  // A bounded number of ticks keeps 100k-event traces cheap to render.
  for (const tick of d.ticks) {
    const x=1000*tick.index/Math.max(1,d.length-1);
    const line=svgElement('line',{x1:x,x2:x,y1:0,y2:tick.kind==='check'?30:14,stroke:tick.kind==='check'?'#a21caf':'#94a3b8'},timeline);
    svgElement('title',{},line).textContent=`${tick.index}: ${tick.kind}`;
    line.onclick=()=>send('seek',{index:tick.index});
  }
  svgElement('line',{x1:1000*d.index/Math.max(1,d.length-1),x2:1000*d.index/Math.max(1,d.length-1),y1:0,y2:32,stroke:'#dc2626','stroke-width':3},timeline);
  element('strong',`${d.index} / ${d.length-1}: ${d.event.event}`,el);
  element('pre',JSON.stringify(d.event,null,2),el);
  element('strong','Call stack',el);
  element('pre',d.stack.join('\n') || '(outside recursive calls)',el);
  element('p',d.note,el);
  const code=element('div',null,el);
  d.pseudocode.forEach((line,i)=>{const row=element('pre',`${i+1}. ${line}`,code);if(i===d.line)row.className='current';});
  for (const badge of d.badges) {
    const box=element('div',null,el); box.className='badge'+(badge.ok?' ok':'');
    const claim=badge.claim;
    element('div',`${badge.player===0?'Even':'Odd'} — algorithm claim: `+(claim?`${claim.event.is_attractor_decomposition?'✔':'✘'} at step ${claim.index}, path ${JSON.stringify(claim.event.path)}, G={${claim.event.subgame.join(',')}}`:'no check event yet'),box);
    element('div',`Independent: ${badge.ok?'✔':'✘'} ${badge.status} on G={${badge.domain.join(',')}}; condition ${badge.condition}; path ${JSON.stringify(badge.path)}; vertex ${badge.vertex === null ? '—' : badge.vertex}. ${badge.message}`,box);
    if(badge.problems.length)element('pre','Materializer: '+JSON.stringify(badge.problems),box);
  }
}
export default { render({model,el}) {
  el.classList.add('strahler');
  let paint = () => {};
  const draw=()=>{
    const focused=el.contains(document.activeElement)?document.activeElement:null;
    const attribute=focused && focused.hasAttribute('data-vertex')?'data-vertex':'data-control';
    const active=focused?focused.getAttribute(attribute):null;
    el.replaceChildren();
    const kind=model.get('kind');
    if(kind==='game')paint=game(model,el);else if(kind==='tree')paint=tree(model,el);else { controls(model,el); paint=()=>{}; }
    paint();
    if(active!==null){const node=Array.from(el.querySelectorAll(`[${attribute}]`)).find(e=>e.getAttribute(attribute)===active);if(node)node.focus();}
  };
  const fields=model.get('kind')==='tree'?['data','mode','edges','outlines']:['data','outlines'];
  const paintFields=['hovered','hovered_path','selected'];
  // Game legends also change when highlight layers change.
  const highlights=()=>{if(model.get('kind')==='game')draw();else paint();};
  const repaint=()=>paint();
  for(const field of fields)model.on('change:'+field,draw);
  for(const field of paintFields)model.on('change:'+field,repaint);
  model.on('change:highlights',highlights);
  draw();
  return ()=>{
    for(const field of fields)model.off('change:'+field,draw);
    for(const field of paintFields)model.off('change:'+field,repaint);
    model.off('change:highlights',highlights);
  };
}};
"""


def _svg(parent, tag, attrs=None, text=None):
    element = ET.SubElement(parent, tag, {k: str(v) for k, v in (attrs or {}).items()})
    if text is not None:
        element.text = str(text)
    return element


def _svg_root(width, height, label):
    return ET.Element('svg', xmlns='http://www.w3.org/2000/svg',
                      viewBox=f'0 0 {width} {height}', width=str(width), height=str(height),
                      role='img', attrib={'aria-label': label, 'fill': '#182538',
                      'style': 'max-width:100%;height:auto;background:white;font:12px system-ui,sans-serif'})


def _svg_colour(colour):
    # SVG paint URLs could fetch external content; accept only plain colours.
    return colour if isinstance(colour, str) and re.fullmatch(
        r'#[\da-fA-F]{3,8}|[a-zA-Z]+|(?:rgb|hsl)a?\([\d.% ,+-]+\)', colour) else '#fff'


def _tree_layout(tree, game=None):
    """Shared geometry for the live JS renderer and the static SVG fallback."""
    root = _tree_data(tree)
    if root is None:
        return None
    leaf = depth = 0

    def region_size(n, part):
        count = len(n[part])
        columns = max(1, ceil(sqrt(count)))
        return max(48, len(n['name']) * 8 + 24, columns * 64 if count else 0), 24 + ceil(count / columns) * 64

    def measure(n, level):
        nonlocal leaf, depth
        sizes = [measure(c, level + 1) for c in n['children']]
        if sizes:
            x = sum(c['xy'][0] for c in n['children']) / len(sizes)
        else:
            leaf += 1
            x = leaf * 130
        n['xy'] = [x, 35 + level * 85]
        depth = max(depth, level)
        n['regions'] = {p: [0, 0, *region_size(n, p)] for p in 'HTS'}
        hw, hh = n['regions']['H'][2:]
        tw, th = n['regions']['T'][2:]
        sw, sh = n['regions']['S'][2:]
        width = max(hw, tw, sum(w for w, h in sizes)) + sw
        height = max(hh + th + max((h for w, h in sizes), default=0), sh)
        n['box'] = [0, 0, width, height]
        return width, height

    vertices = []

    def place(n, x, y, width, height):
        n['box'] = [x, y, width, height]
        sw = n['regions']['S'][2]
        left = width - sw
        hh, th = (n['regions'][p][3] for p in 'HT')
        if not n['children']:
            th = height - hh
        n['regions'] = dict(H=[x, y, left, hh], T=[x, y+hh, left, th],
                            S=[x+left, y, sw, height])
        for part in 'HTS':
            rx, ry, rw, rh = n['regions'][part]
            columns = max(1, ceil(sqrt(len(n[part]))))
            for i, v in enumerate(n[part]):
                vertices.append(dict(id=v, name=game.names.get(v) if game else None,
                    owner=game.owner.get(v) if game else None,
                    priority=game.priority.get(v) if game else None,
                    path=n['path'], part=part,
                    xy=[rx + (i % columns + .5) * rw / columns, ry + 50 + (i // columns) * 64]))
        child_x = x
        for i, child in enumerate(n['children']):
            cw = child['box'][2] if i < len(n['children']) - 1 else x + left - child_x
            place(child, child_x, y+hh+th, cw, height-hh-th)
            child_x += cw

    width, height = measure(root, 0)
    place(root, 5, 5, width, height)
    ids = {v['id'] for v in vertices}
    root['game'] = dict(vertices=vertices, edges=[list(edge) for edge in game.edges
                        if set(edge) <= ids] if game else [])
    root['colours'] = _PART_COLOURS
    root['edge_limit'] = _EDGE_LIMIT
    root['default_mode'] = ('squares' if root['decomposition'] and
                            len(root['vertices']) <= 40 and depth <= 4 else 'tree')
    root['extent'] = dict(squares=[width + 10, height + 34],
                          tree=[max(300, (leaf + 1)*130), 100 + depth*85])
    return root


def _vertex_attrs(view, vertex):
    fill = '#fff'
    for layer in view.highlights:
        if vertex in layer['vertices']:
            fill = layer['colour']
    hot = vertex in view.hovered
    selected = view.kind == 'game' and view.selected == vertex
    return dict(fill=_svg_colour(fill), stroke='#c026d3' if hot else '#dc2626' if selected else '#182538',
                **{'stroke-width': 4 if hot or selected else 1.5})


def _svg_vertex(svg, vertex, view):
    x, y = vertex['xy']
    group = _svg(svg, 'g', {'data-vertex': vertex['id']})
    style = _OWNER_STYLES.get(str(vertex['owner']))
    name = vertex['name'] or str(vertex['id'])
    if style:
        _svg(group, style['shape'], dict(style['attrs'], transform=f'translate({x} {y})',
                                        **_vertex_attrs(view, vertex['id'])))
        for i, (layer, violation) in enumerate(_violations(view, 'vertex', vertex['id'])):
            ring = _svg(group, style['shape'], dict(style['attrs'],
                        transform=f'translate({x} {y}) scale({1.18+i*.15})', fill='none',
                        stroke=layer['colour'], **{'stroke-width': 3}))
            _svg(ring, 'title', text=_violation_title(layer, violation))
        _svg(group, 'text', {'x': x, 'y': y+4, 'text-anchor': 'middle'}, vertex['priority'])
        _svg(group, 'text', {'x': x, 'y': y+31, 'text-anchor': 'middle'}, name)
        description = f"{name}: priority {vertex['priority']}, {style['player']} ({style['label']})"
    else:
        _svg(group, 'text', {'x': x, 'y': y+4, 'text-anchor': 'middle'}, name)
        description = f'Vertex {name}; game metadata unavailable'
    description += ''.join(' · ' + _violation_title(l, v) for l, v in _violations(view, 'vertex', vertex['id']))
    _svg(group, 'title', text=description)


def _svg_edges(svg, vertices, edges, marker_id, colour, title=None, width=1):
    if not edges:
        return
    marker = _svg(_svg(svg, 'defs'), 'marker', dict(id=marker_id, viewBox='0 0 10 10', refX=9, refY=5,
                  markerWidth=6, markerHeight=6, orient='auto-start-reverse'))
    _svg(marker, 'path', dict(d='M 0 0 L 10 5 L 0 10 z', fill=colour))
    points = {v['id']: v['xy'] for v in vertices}
    for a, b in edges:
        x, y = points[a]
        u, v = points[b]
        dx, dy = u-x, v-y
        length = hypot(dx, dy) or 1
        path = (f'M {x-14} {y-20} C {x-48} {y-46} {x+48} {y-46} {x+14} {y-20}'
                if a == b else f'M {x+dx*29/length} {y+dy*29/length} '
                f'Q {(x+u)/2-dy*.12} {(y+v)/2+dx*.12} {u-dx*29/length} {v-dy*29/length}')
        edge = _svg(svg, 'path', {'d': path, 'fill': 'none', 'stroke': colour,
                          'stroke-width': width, 'marker-end': f'url(#{marker_id})'})
        if title:
            _svg(edge, 'title', text=title)
        else:
            edge.set('pointer-events', 'none')


def _check_vertex_ids(vertices):
    # shortcut: JS numbers have 53-bit precision; use string IDs if larger games need them.
    if any(abs(v) > 2**53 - 1 for v in vertices):
        raise ValueError('SVG views require vertex IDs at most 2**53 - 1; renumber this game')

class _View(anywidget.AnyWidget):
    _esm = _ESM
    _css = _CSS
    kind = t.Unicode().tag(sync=True)
    data = t.Any(allow_none=True).tag(sync=True)
    hovered = t.List(t.Int()).tag(sync=True)
    highlights = t.List(t.Dict()).tag(sync=True)
    outlines = t.List(t.Dict()).tag(sync=True)
    selected = t.Any(allow_none=True).tag(sync=True)
    mode = t.Enum(['squares', 'tree', 'boxes'], default_value='squares').tag(sync=True)
    edges = t.Bool(True).tag(sync=True)
    hovered_path = t.Any(allow_none=True).tag(sync=True)
    owner_styles = t.Dict(_OWNER_STYLES).tag(sync=True)
    next_kind = t.Unicode('call').tag(sync=True)

    def show_check(self, verdict, player, path=()):
        """Replace one player's AD violation outlines; a successful check clears them."""
        if player not in (0, 1):
            raise ValueError('player must be 0 or 1')
        layer = dict(player=player, name=('Even', 'Odd')[player] + ' AD violation',
                     colour=_VIOLATION_COLOURS[player], violations=[
                         dict(condition=v.condition.name, path=list(path + v.path),
                              vertex=v.vertex, edge=None if v.edge is None else list(v.edge))
                         for v in verdict.violations])
        self.outlines = [l for l in self.outlines if l['player'] != player] + ([layer] if layer['violations'] else [])

    def _repr_mimebundle_(self, **kwargs):
        bundle = super()._repr_mimebundle_(**kwargs)
        if bundle is None:
            return None
        data = bundle[0] if isinstance(bundle, tuple) else bundle
        if self.kind in ('game', 'tree'):
            data.update({'image/svg+xml': self._static_svg(),
                         'text/plain': 'Parity game' if self.kind == 'game' else 'Ordered tree'})
        return bundle


class GameView(_View):
    """Parity game; coordinates map external IDs to (x, y), scaled to the view.

    The default deterministic layout groups vertices by input priority.
    ``highlight(vertices, colour, name)`` replaces a named highlight layer.
    ``selected`` is the clicked external vertex ID (or None).
    """

    def __init__(self, game, coordinates=None, **kwargs):
        self.game = game
        vertices = game.vertices
        _check_vertex_ids(vertices)
        if coordinates is None:
            levels = sorted(set(game.priority.values()), reverse=True)
            coordinates = {}
            for row, priority in enumerate(levels):
                group = [v for v in vertices if game.priority[v] == priority]
                for col, v in enumerate(group):
                    coordinates[v] = (col * 2 - len(group) + 1, row * 2)
        if set(coordinates) != set(vertices):
            raise ValueError('coordinates must contain every external vertex ID exactly once')
        if any(len(xy) != 2 or not all(isfinite(x) for x in xy) for xy in coordinates.values()):
            raise ValueError('coordinates must be finite (x, y) pairs')
        xs = [xy[0] for xy in coordinates.values()] or [0]
        ys = [xy[1] for xy in coordinates.values()] or [0]
        def scale(value, values, extent):
            return 60 + (value - min(values)) / (max(values) - min(values) or 1) * extent
        data = dict(vertices=[dict(id=v, name=game.names[v], priority=game.priority[v],
                                  owner=game.owner[v], xy=[scale(coordinates[v][0], xs, 680),
                                                         scale(coordinates[v][1], ys, 360)])
                              for v in vertices], edges=game.edges)
        super().__init__(kind='game', data=data, **kwargs)

    def highlight(self, vertices, colour='#bfdbfe', name='selection'):
        vertices = sorted(set(vertices))
        if not set(vertices) <= set(self.game.vertices):
            raise ValueError('highlight contains an unknown vertex')
        self.highlights = [s for s in self.highlights if s['name'] != name] + [
            dict(name=name, vertices=vertices, colour=colour)]

    def _static_svg(self):
        svg = _svg_root(800, 520, 'Parity game. Squares Even, pentagons Odd.')
        _svg_edges(svg, self.data['vertices'], self.data['edges'], 'strahler-arrow-game', '#64748b')
        for i, layer in enumerate(self.outlines):
            for v in layer['violations']:
                if v['edge'] is not None:
                    _svg_edges(svg, self.data['vertices'], [v['edge']], f'strahler-violation-{i}',
                               layer['colour'], _violation_title(layer, v), 5-i*2)
        for vertex in self.data['vertices']:
            _svg_vertex(svg, vertex, self)
        _svg(svg, 'text', dict(x=8, y=490), 'Square: Even · Pentagon: Odd')
        x = 8
        for layer in self.highlights:
            width = len(layer['name']) * 8 + 12
            _svg(svg, 'rect', dict(x=x, y=496, width=width, height=20, fill=_svg_colour(layer['colour'])))
            _svg(svg, 'text', dict(x=x+4, y=510), layer['name'])
            x += width + 4
        for layer in self.outlines:
            _svg(svg, 'text', dict(x=x, y=484, fill=layer['colour']), layer['name'])
            x += len(layer['name']) * 8 + 12
        return _ascii(ET.tostring(svg, encoding='unicode'))


def _tree_data(tree, path=(), label=None):
    if isinstance(tree, Node):
        tree = tree.to_dict()
    if isinstance(tree, str):
        tree = json.loads(tree)
    if tree is None:
        return None
    if 'schema' in tree or ('tree' in tree and 'children' not in tree):
        return _tree_data(tree['tree'], path, label)
    children = []
    for i, child in enumerate(tree.get('children', [])):
        child_label = None
        if child is not None and 'node' in child:
            child_label, child = child['label'], child['node']
        node = _tree_data(child, path + (i,), child_label)
        if node is not None:
            children.append(node)
    vertices = set(tree.get('vertices') or [])
    for part in 'HTS':
        vertices.update(tree.get(part, []))
    for child in children:
        vertices.update(child['vertices'])
    name = tree.get('label', label)
    if name is None:
        name = 'ε' + ''.join(str(i + 1) for i in path)
        if 'h' in tree:
            name += f" (h={tree['h']})"
    return dict(label=name or 'ε', name='ε' + ''.join(str(i + 1) for i in path),
                path=list(path), vertices=sorted(vertices), children=children,
                decomposition=tree.get('decomposition', any(p in tree for p in 'HTS')),
                **{part: sorted(tree.get(part, [])) for part in 'HTS'})


class TreeView(_View):
    """H/T/S Node/dict, genstree document, or progress-measure prefix tree.

    Decompositions with at most 40 vertices and depth 4 (root depth 0) default
    to ``squares``; other trees use ``tree``. Assign ``tree`` to refresh.
    Pass a GameView or ``game=`` for vertex shapes, priorities, and game edges.
    ``edges=False`` hides arrows; squares omit them above 200 visible edges.
    ``link(other)`` shares hover and selection for two views of the same tree.
    ``selected`` and ``hovered_path`` are zero-based child-index paths.
    """

    def __init__(self, tree=None, game_view=None, mode=None, *, game=None, edges=True, **kwargs):
        self.game_view = game_view
        self._game = game_view.game if game_view is not None else game
        if self._game is not None:
            _check_vertex_ids(self._game.vertices)
        self._auto_mode = mode is None
        self._choosing_mode = False
        self._links = []
        data = _tree_layout(tree, self._game)
        super().__init__(kind='tree', data=data, edges=edges,
                         mode=mode or (data['default_mode'] if data else 'squares'), **kwargs)
        self.observe(self._hover, names='hovered')
        self.observe(self._remember_mode, names='mode')
        if game_view is not None:
            self._links.append(t.dlink((game_view, 'highlights'), (self, 'highlights')))

    @t.validate('mode')
    def _validate_mode(self, proposal):
        if proposal['value'] == 'boxes':
            warnings.warn("mode='boxes' is deprecated; use mode='squares'", DeprecationWarning, stacklevel=3)
            return 'squares'
        return proposal['value']

    def _remember_mode(self, change):
        if not self._choosing_mode:
            self._auto_mode = False

    @property
    def tree(self):
        return self.data

    @tree.setter
    def tree(self, value):
        self.hovered_path = None
        self.hovered = []
        self.data = _tree_layout(value, self._game)
        if self._auto_mode:
            self._choosing_mode = True
            self.mode = self.data['default_mode'] if self.data else 'squares'
            self._choosing_mode = False

    def link(self, other):
        """Link hover and selected paths; both views must show the same tree."""
        self._links.extend(t.link((self, field), (other, field))
                           for field in ('hovered', 'hovered_path', 'selected'))
        return other

    def close(self):
        for link in getattr(self, '_links', []):
            link.unlink()
        self._links = []
        super().close()

    def _hover(self, change):
        if self.game_view is not None:
            self.game_view.hovered = change['new']

    def _static_svg(self):
        if self.data is None:
            svg = _svg_root(300, 40, 'Empty tree')
            _svg(svg, 'text', dict(x=8, y=24), '∅ — empty tree')
        else:
            svg = _svg_root(*self.data['extent'][self.mode],
                            'Nested squares. Squares Even, pentagons Odd.' if self.mode == 'squares' else 'Ordered tree')

            def outline(group, n, attrs):
                hot = self.hovered_path == n['path']
                selected = self.selected == n['path']
                for layer, violation in _violations(self, 'path', n['path']):
                    ring = _svg(group, 'rect', dict(attrs, fill='none', stroke=layer['colour'], **{'stroke-width': 7}))
                    _svg(ring, 'title', text=_violation_title(layer, violation))
                _svg(group, 'rect', dict(attrs, stroke='#c026d3' if hot else '#dc2626' if selected else '#64748b',
                     **{'data-node-outline': json.dumps(n['path'], separators=(',', ':')),
                        'stroke-width': 4 if hot or selected else 1}))

            def visit(n, parent):
                if self.mode == 'squares':
                    group = _svg(parent, 'g', {'data-path': json.dumps(n['path'], separators=(',', ':'))})
                    for part in 'HTS':
                        x, y, w, h = n['regions'][part]
                        _svg(group, 'rect', dict(x=x, y=y, width=w, height=h,
                            fill=_PART_COLOURS[part], stroke='#64748b', **{'class': f'part-{part}'}))
                        label = _svg(group, 'text', {'x': x+5, 'y': y+16, 'pointer-events': 'none'}, part)
                        _svg(label, 'tspan', {'baseline-shift': 'super', 'font-size': 9}, n['name'])
                    for child in n['children']:
                        visit(child, group)
                    x, y, w, h = n['box']
                    outline(group, n, dict(x=x, y=y, width=w, height=h, fill='none', **{'pointer-events': 'none'}))
                    _svg(group, 'title', text=n['label'] + ''.join(' · ' + _violation_title(l, v) for l, v in _violations(self, 'path', n['path'])))
                else:
                    x, y = n['xy']
                    for child in n['children']:
                        u, v = child['xy']
                        _svg(svg, 'line', dict(x1=x, y1=y, x2=u, y2=v, stroke='#94a3b8'))
                        visit(child, svg)
                    group = _svg(svg, 'g', {'data-path': json.dumps(n['path'], separators=(',', ':'))})
                    outline(group, n, dict(x=x-58, y=y-17, width=116, height=34, rx=5, fill='#dbeafe'))
                    _svg(group, 'text', {'x': x, 'y': y+4, 'text-anchor': 'middle', 'pointer-events': 'none'}, n['label'])
                    _svg(group, 'title', text=f"{n['label']}: {', '.join(map(str, n['vertices']))}" + ''.join(' · ' + _violation_title(l, v) for l, v in _violations(self, 'path', n['path'])))

            visit(self.data, svg)
            if self.mode == 'squares':
                graph = self.data['game']
                if self.edges and len(graph['edges']) <= _EDGE_LIMIT:
                    _svg_edges(svg, graph['vertices'], graph['edges'], 'strahler-arrow-squares', '#94a3b8')
                for vertex in graph['vertices']:
                    _svg_vertex(svg, vertex, self)
                _svg(svg, 'text', dict(x=5, y=self.data['extent']['squares'][1]-8), 'Square: Even · Pentagon: Odd')
        return _ascii(ET.tostring(svg, encoding='unicode'))

_PLAIN = [
    'McNZEven(G, h, ε, ω)  [McNZOdd exchanges players]',
    'If G is empty, return ∅ (paper base: h=0 before priority normalization)',
    'G₁ ← G; i ← 0',
    'Repeat:',
    '  i ← i+1; H_i ← priority-h vertices in Gᵢ',
    '  A_i ← Attr_Even^Gᵢ(H_i)',
    '  G′ᵢ ← Gᵢ \\ A_i',
    '  U′_i ← McNZOdd(G′ᵢ, h−1, ω·i, ε)',
    '  A′_i ← Attr_Odd^Gᵢ(U′_i)',
    '  S_Odd(ω·i) ← A′_i \\ U′_i; G_{i+1} ← Gᵢ \\ A′_i',
    'Until G_{i+1} = Gᵢ',
    'H_Even(ε) ← H_i; T_Even(ε) ← A_i \\ H_i',
    'Return Gᵢ',
]
_MEMORY = [
    'McNZFastEven(G, h, ε, ω)  [McNZFastOdd exchanges players]',
    'If the Even candidate at ε restricted to G is an AD:',
    '  Set S_Odd(ω) to G; return G',
    'If the Odd candidate at ω restricted to G is an AD:',
    '  Set S_Even(ε) to G; return ∅',
    'G₁ ← G; i ← 0',
    'Repeat with the next child ω·i:',
    '  H_i ← priority-h vertices; A_i ← Attr_Even^Gᵢ(H_i)',
    '  Set/Move top fibres using A_i and H_i',
    '  Remove the escaping attractor from the retained child (entry correction)',
    '  G′ᵢ ← the resulting child core in Gᵢ',
    '  U′_i ← McNZFastOdd(G′ᵢ, h−1, ω·i, ε)',
    '  S′_i ← Attr_Odd^Gᵢ(U′_i)',
    '  Set S_Odd(ω·i) to S′_i \\ U′_i',
    '  Move S′_i to at least S_Even(ε)',
    '  G_{i+1} ← Gᵢ \\ S′_i',
    'Until the Even candidate restricted to G_{i+1} is an AD',
    'Return G_{i+1}  [corrected subgame index]',
]
_LIFTING = [
    'Build the Strahler host for each player',
    'Initialize the least priority-compatible H/T/S labels',
    'Short-lift an unstable vertex to its next valid label',
    'End the round; repeat until labels stabilize',
    'Adaptive host: raise t and repeat if needed',
    'Iterative shaving: attract certified winners; restart on the remaining game (after preprocessing and earlier shaving rounds)',
    'Materialize and independently check the resulting certificates',
]


def _controls_html(data):
    def paragraph(text):
        return '<p>' + escape(text) + '</p>'
    html = paragraph(f"{data['index']} / {data['length']-1}: {data['event']['event']}")
    for badge in data['badges']:
        claim = badge['claim']
        player = 'Even' if badge['player'] == 0 else 'Odd'
        text = (f"{claim['event']['is_attractor_decomposition']} at step {claim['index']}, "
                f"path {claim['event']['path']}, G={claim['event']['subgame']}"
                if claim else 'no check event yet')
        html += paragraph(f'{player} — algorithm claim: {text}')
        html += paragraph(f"Independent: {'✔' if badge['ok'] else '✘'} {badge['status']} "
                          f"on G={badge['domain']}; condition {badge['condition']}; "
                          f"path {badge['path']}; vertex {badge['vertex']}. {badge['message']}")
        if badge['problems']:
            html += paragraph('Materializer: ' + json.dumps(badge['problems']))
    return _ascii(html)


class Stepper(VBox):
    """Recorded trace with step/back/next/end/seek and independent badges.

    ``state`` is the public package State. ``verify(player)`` checks the current
    candidate and restriction; ``badges`` keeps recorded claims separately.
    ``call_tree()`` returns a TreeView whose selected call jumps this Stepper.
    Keep trace.game open for as long as the widget is used.
    """

    def __init__(self, run, coordinates=None, checkpoint_interval=None, **kwargs):
        self.run = run
        self.cursor = TraceCursor(run, checkpoint_interval)
        self.game_view = GameView(run.game, coordinates)
        self.tree_views = tuple(TreeView(game_view=self.game_view) for _ in range(2))
        self.controls = _View(kind='controls')
        self.controls.next_kind = 'call' if 'call' in self.cursor.kinds else 'lift'
        self.controls.on_msg(self._message)
        super().__init__(children=[self.controls, self.game_view, HBox(self.tree_views,
                         layout=Layout(overflow='auto'))], **kwargs)
        self.seek(0)

    def _repr_mimebundle_(self, **kwargs):
        bundle = super()._repr_mimebundle_(**kwargs)
        bundle['text/plain'] = f"Step {self.index}: {self.state.event['event']}"
        bundle['text/html'] = (_controls_html(self.controls.data) + self.game_view._static_svg() +
                               ''.join(view._static_svg() for view in self.tree_views))
        return bundle

    @property
    def index(self):
        return self.cursor.index

    def seek(self, index):
        self.state = self.cursor.seek(index)
        self.badges = [self.cursor.badge(p) for p in range(2)]
        self.game_view.outlines = []
        backend = self.run.events[0]['algorithm']
        for player, (view, tree) in enumerate(zip(self.tree_views, self.state.trees)):
            if backend == 'lifting':
                tree = self.cursor.candidate(player)[0]
            elif backend.endswith('-sym') and tree is not None and self.state.event['event'] != 'result':
                # Symmetric checks locate nodes by host paths, including gaps.
                tree = self.state.candidate(player)
            view.tree = tree
            verdict = self.cursor.verify(player)
            self.game_view.show_check(verdict, player)
            view.show_check(verdict, player, self.cursor.candidate_path(player))
        self.game_view.highlights = []
        self.game_view.highlight(self.cursor.subgame, '#dbeafe', 'current subgame')
        colours = {'G_i': '#dbeafe', 'A_i': '#bbf7d0', 'H_i': '#fef08a', "U′_i": '#fed7aa', 'side attractor': '#e9d5ff'}
        if self.cursor.stack:
            sets = self.cursor.stack[-1]['sets']
            for name, colour in colours.items():
                if name in sets:
                    self.game_view.highlight(sets[name], colour, name)
        frames = []
        memory = backend.endswith('-sym')
        procedure = 'UnivFast' if backend == 'lifting-sym' else ('McNZFast' if memory else 'McNZ')
        for frame in self.cursor.stack:
            event = frame['event']
            player = 'Even' if event['player'] == 0 else 'Odd'
            frames.append(f"{procedure}{player}(G={event['subgame']}, h={event['h']}, ε={event['paths'][0]}, ω={event['paths'][1]})")
        event = self.state.event
        kind = event['event']
        code = _MEMORY if memory else _PLAIN
        if backend == 'lifting-sym':
            code = list(_MEMORY)
            code[0] = 'UnivFastEven(G, h, ε, ω)  [UnivFastOdd exchanges players]'
            code[16] = 'Continue through all host children; finish with short-lift certification'
        # Trace events group multiple C statements; highlight a semantic block.
        if memory:
            frame = self.cursor.stack[-1] if self.cursor.stack else {}
            previous = frame.get('last_attractor', {})
            lower = frame.get('event', {}).get('player')
            if kind == 'check':
                line = 16 if previous else (1 if event['player'] == lower else 3)
            elif kind == 'attractor':
                line = 7 if event['kind'] == 'top' else (9 if event['player'] == lower else 12)
            elif kind in ('set', 'move', 'labels'):
                line = 8 if previous.get('kind') == 'top' else (9 if previous.get('player') == lower else 14)
            else:
                line = 17 if kind == 'return' else 0
        else:
            line = {'call': 0, 'attractor': 5 if event.get('kind') == 'top' else 8,
                    'set': 11, 'move': 9, 'return': 12}.get(kind, 0)
        if backend == 'lifting':
            code = _LIFTING
            line = {'lift': 2, 'labels': 2, 'round_end': 3, 'result': 6}.get(kind, 0)
            if kind == 'phase':
                line = {'labels_reset': 1, 't_raised': 4, 'shave_applied': 5,
                        'dominion_certified': 6}.get(event['phase'], 0)
        note = 'Yellow marks the current semantic block, not a one-to-one C source line.'
        if memory:
            note += ' Entry correction: allow the higher player’s S fibre (root S = TOP), and remove the escaping attractor before recursion; see docs/symmetric.md.'
        self.controls.data = dict(index=self.index, length=len(self.run), event=event,
                                  stack=frames, badges=self.badges, kinds=list(self.cursor.kinds),
                                  pseudocode=code, line=line, ticks=self.cursor.ticks, note=note)
        return self.state

    def step(self):
        return self.seek(min(self.index + 1, len(self.run) - 1))

    def back(self):
        return self.seek(max(0, self.index - 1))

    def next(self, kind='call'):
        return self.seek(self.cursor.next_index(kind))

    def end(self):
        return self.seek(len(self.run) - 1)

    def verify(self, player):
        return self.cursor.verify(player)

    def _message(self, widget, content, buffers):
        command = content.get('command')
        if command == 'seek':
            self.seek(content['index'])
        elif command == 'next':
            self.next(content['kind'])
        elif command in ('step', 'back', 'end'):
            getattr(self, command)()

    def call_tree(self):
        nodes, roots = {}, []
        indices = {}
        for call in self.cursor.calls:
            e = call['event']
            node = dict(label=f"#{call['index']} {'E' if e['player'] == 0 else 'O'} h={e['h']}",
                        vertices=e['subgame'], children=[])
            if call['parent'] is None:
                path = (len(roots),)
                roots.append(node)
            else:
                parent, parent_path = nodes[call['parent']]
                path = parent_path + (len(parent['children']),)
                parent['children'].append(node)
            nodes[call['index']] = node, path
            indices[path] = call['index']
        view = TreeView(dict(label='calls', children=roots), game_view=self.game_view, mode='tree')
        def jump(change):
            if tuple(change['new'] or ()) in indices:
                self.seek(indices[tuple(change['new'])])
        view.observe(jump, names='selected')
        view.on_msg(lambda widget, content, buffers: jump(dict(new=content['path']))
                    if content.get('command') == 'select' else None)
        return view
