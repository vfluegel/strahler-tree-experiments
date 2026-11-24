"""Offline SVG notebook widgets. Install ``strahler[notebooks]`` to use them.

GameView(game, coordinates=None), TreeView(tree, game_view=None, mode=None),
and Stepper(trace, coordinates=None). No frontend dependencies or remote URLs.
"""

from html import escape
import json
from math import ceil, cos, hypot, isfinite, pi, sin, sqrt
import re
from textwrap import wrap
import warnings
from xml.etree import ElementTree as ET

try:
    import anywidget
    import traitlets as t
    from ipywidgets import VBox, HBox, Layout, HTML as WidgetHTML
except ImportError as error:
    raise ImportError('Notebook views need: pip install "strahler[notebooks]"') from error

from . import Node, HostSnapshot
from ._host import path_name
from ._widget_state import TraceCursor

__all__ = ['GameView', 'TreeView', 'HostView', 'Stepper']

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
.strahler .host-scroll { overflow-x: auto; } .strahler .host-scroll svg { max-width: none; }
.strahler .host-note { color: #475569; } .strahler details { margin: 8px 0; }
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
  e.setAttribute('data-control',label);
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
function host(model, el) {
  const d=model.get('data'), painters=[];
  element('strong',d.title,el); element('p',d.summary,el); element('p',d.note,el).className='host-note';
  if(!d.nodes.length) return ()=>{};
  const send=(command,extra={})=>model.send({command,...extra});
  const bar=element('div',null,el), priority=element('input',null,bar);
  priority.type='number'; priority.min=0; priority.value=d.priority??0; priority.style.width='65px';
  priority.setAttribute('aria-label','Teaching priority'); priority.setAttribute('data-control','Teaching priority'); priority.onchange=()=>send('priority',{priority:Number(priority.value)});
  element('button','Next compatible position',bar).onclick=()=>send('compatible');
  element('button','Reset teaching cursor',bar).onclick=()=>send('reset_teaching');
  const search=element('input',null,bar);search.type='number';search.min=0;search.max=d.total-1;search.style.width='75px';
  search.setAttribute('aria-label','Find flat position');
  search.setAttribute('data-control','Find flat position');
  element('button','Find position',bar).onclick=()=>send('position',{index:Number(search.value)});
  if(d.focus.length)element('button','Show root',bar).onclick=()=>send('focus',{path:[]});
  element('p',d.cursor_note,el);
  const hover=(group,path,index,occupants)=>{
    const enter=()=>{model.set('hovered_path',path);model.set('hovered_position',index);model.set('hovered',occupants);model.save_changes();};
    const leave=()=>{model.set('hovered_path',null);model.set('hovered_position',null);model.set('hovered',[]);model.save_changes();};
    group.onpointerover=e=>{e.stopPropagation();enter();};
    group.onpointerout=e=>{e.stopPropagation();if(!group.contains(e.relatedTarget))leave();};
    group.onfocus=enter;group.onblur=leave;
  };
  const paint=(rect,path,index,current,next,occupants)=>{
    painters.push(()=>{
      const hp=model.get('hovered_path'), fibre=model.get('hovered_position');
      const hot=(model.get('hovered')||[]).some(v=>occupants.includes(v)) || (fibre!==null&&fibre!==undefined?index===fibre:
        hp!==null&&hp!==undefined&&(index===null?JSON.stringify(hp)===JSON.stringify(path):
          d.intervals.some(n=>JSON.stringify(n.path)===JSON.stringify(hp)&&index>=n.core[0]&&index<n.core[1])));
      rect.setAttribute('stroke',hot?'#c026d3':current?'#dc2626':next?'#2563eb':'#64748b');
      rect.setAttribute('stroke-width',hot||current||next?3:1);
    });
  };
  const scroll=element('div',null,el); scroll.className='host-scroll';
  const svg=svgElement('svg',{viewBox:`0 0 ${d.extent[0]} ${d.extent[1]}`,width:d.extent[0],height:d.extent[1],role:'group','aria-label':d.title},scroll);
  for(const n of d.nodes) if(n.parent_xy)svgElement('line',{x1:n.parent_xy[0],y1:n.parent_xy[1],x2:n.box[0]+126,y2:n.box[1],stroke:'#94a3b8'},svg);
  for(const n of d.nodes) {
    const [x,y,w,h]=n.box, g=svgElement('g',{'data-host-path':JSON.stringify(n.path)},svg);
    const border=svgElement('rect',{x,y,width:w,height:h,rx:6,fill:'#fff',stroke:'#64748b'},g);
    paint(border,n.path,null,false,false,n.vertices);
    svgElement('text',{x:x+8,y:y+18},g).textContent=n.label;
    svgElement('text',{x:x+8,y:y+34},g).textContent=n.interval;
    svgElement('title',{},g).textContent=n.description;
    hover(g,n.path,null,n.vertices);
    interactive(g,'Focus host node '+n.label,()=>send('focus',{path:n.path}));
    for(const p of n.parts) {
      const part=svgElement('g',{'data-host-position':p.index},g),[px,py,pw,ph]=p.box;
      const rect=svgElement('rect',{x:px,y:py,width:pw,height:ph,fill:p.fill,stroke:'#64748b',opacity:p.compatible?1:.32,'stroke-dasharray':p.part==='S'?'4 3':'none',class:'host-part-'+p.part},part);
      svgElement('text',{x:px+5,y:py+16},part).textContent=p.label;
      svgElement('text',{x:px+5,y:py+31},part).textContent=p.caption;
      svgElement('title',{},part).textContent=p.description;
      hover(part,n.path,p.index,p.occupants);
      interactive(part,'Inspect '+p.label,()=>send('part',{index:p.index}));
      paint(rect,n.path,p.index,p.current,p.next,p.occupants);
    }
    if(n.collapsed)svgElement('text',{x:x+8,y:y+98},g).textContent=n.collapsed;
  }
  element('strong','Flattened host (lifting order)',el);
  const flatScroll=element('div',null,el);flatScroll.className='host-scroll';
  const flat=svgElement('svg',{viewBox:`0 0 ${d.flat_extent[0]} ${d.flat_extent[1]}`,width:d.flat_extent[0],height:d.flat_extent[1],role:'group','aria-label':'Flattened host (lifting order)'},flatScroll);
  for(const p of d.strip) {
    const g=svgElement('g',{'data-flat-position':p.index},flat),[x,y,w,h]=p.box;
    const r=svgElement('rect',{x,y,width:w,height:h,fill:p.fill,opacity:p.compatible?1:.32,stroke:'#64748b','stroke-dasharray':p.part==='S'?'4 3':'none'},g);
    svgElement('text',{x:x+5,y:y+17},g).textContent=p.index+' '+p.name;
    svgElement('text',{x:x+5,y:y+34},g).textContent=p.status;
    svgElement('title',{},g).textContent=p.description;
    hover(g,p.path,p.index,p.occupants); interactive(g,'Inspect position '+p.index+' '+p.name,()=>send('part',{index:p.index}));
    paint(r,p.path,p.index,p.current,p.next,p.occupants);
  }
  for(const interval of d.brackets) {
    svgElement('path',{d:interval.d,fill:'none',stroke:interval.side?'#c2410c':'#475569','stroke-dasharray':interval.side?'4 3':'none'},flat);
    svgElement('text',{x:interval.x,y:interval.y},flat).textContent=interval.label;
  }
  element('p',d.window_note,el);
  if(d.part) {
    element('p',d.part.description,el);
    for(const id of d.part.occupants)element('button','Select vertex '+id,el).onclick=()=>send('vertex',{vertex:id});
  }
  const focal=d.nodes.find(n=>n.parts.some(p=>p.current)) || d.nodes[0];
  scroll.scrollLeft=Math.max(0,focal.box[0]+126-(scroll.clientWidth||600)/2);
  const slot=d.strip.findIndex(p=>p.index===d.focus_index);
  flatScroll.scrollLeft=Math.max(0,8+Math.max(0,slot)*106-(flatScroll.clientWidth||600)/2+53);
  return ()=>painters.forEach(p=>p());
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
  for (const kind of ['call','return','check','lift','round_end']) if(d.kinds.includes(kind)) {
    element('button',kind==='round_end'?'Next round':'Next '+kind,el).onclick=()=>send('next',{kind});
  }
  if(d.kinds.includes('lift')) element('button','Next lift for selected vertex',el).onclick=()=>send('next_selected');
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
  if(d.inspector) {
    const i=d.inspector, box=element('div',null,el);
    element('strong','Lift inspector — pre-round state',box);
    if(i.vertex!==undefined) element('p',`${i.player===0?'Even':'Odd'} · vertex ${i.vertex} · owner ${i.owner===0?'Even':'Odd'} · priority ${i.original_priority} (normalized ${i.priority}) · host epoch ${i.epoch} · round ${i.round}`,box);
    if(i.source) element('p',`${i.source.index} ${i.source.name} → ${i.destination.index} ${i.destination.name}`,box);
    element('p','Why move? '+i.reason,box);
    if(i.edges?.length) element('p','Blocking edges: '+i.edges.map(([a,b])=>`${a} → ${b}`).join(', '),box);
    if(i.destination_reason) element('p','Why there? '+i.destination_reason,box);
    if(i.target) element('p',`L={${i.target}}; A={${i.safe}}; R={${i.reached}}. Lifted vertex is outside R.`,box);
    if(i.applied) element('p',`Already moved: {${i.applied}}. Showing: ${i.vertex}. Pending invalid vertices: {${i.pending}}. All checks use pre-round step ${i.before}.`,box);
    if(i.avoidance) {const detail=element('details',null,box);element('summary','Avoidance obstruction',detail);element('p',`A \\ R={${i.avoidance}}. Opponent can retain a successor outside R; a player-owned vertex has no successor in R. Exits from A also prevent safe progress.`,detail);}
  }
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
    if(kind==='game')paint=game(model,el);else if(kind==='tree')paint=tree(model,el);else if(kind==='host')paint=host(model,el);else { controls(model,el); paint=()=>{}; }
    paint();
    if(active!==null){const node=Array.from(el.querySelectorAll(`[${attribute}]`)).find(e=>e.getAttribute(attribute)===active);if(node)node.focus();}
  };
  const fields=model.get('kind')==='tree'?['data','mode','edges','outlines']:['data','outlines'];
  const paintFields=['hovered','hovered_path','hovered_position','selected'];
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
    hovered_position = t.Any(allow_none=True).tag(sync=True)
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
        if self.kind in ('game', 'tree', 'host'):
            data.update({'image/svg+xml': self._static_svg(),
                         'text/plain': {'game': 'Parity game', 'tree': 'Ordered tree', 'host': 'AD search host'}[self.kind]})
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

class HostView(_View):
    """Underlying host groups and a linked, windowed native position order.

    Pass State.hosts[player], or a native genstree document with root_bound.
    ``labels`` maps external vertex IDs to (path, part, bound). ``selected``
    is a vertex ID, shared with GameView. Teaching traversal never edits labels.
    Collapsed subtrees retain exact metrics and intervals; click to focus.
    """

    def __init__(self, host=None, game_view=None, *, labels=None, player=0,
                 root_bound=None, max_nodes=24, strip_window=24, **kwargs):
        if max_nodes < 2 or strip_window < 3:
            raise ValueError('Host windows must contain at least 2 nodes and 3 positions')
        self.game_view, self._links = game_view, []
        self.player = player
        self.max_nodes, self.strip_window = max_nodes, strip_window
        self.host = (host if host is None or isinstance(host, HostSnapshot) else
                     HostSnapshot.from_tree(host, player=player, root_bound=root_bound))
        self.labels = dict(labels or {})
        _check_vertex_ids(self.labels)
        self.occupancy_known = labels is not None
        self.focus = ()
        self.part_index = None
        self.teaching_priority = None
        self.teaching_position = None
        super().__init__(kind='host', **kwargs)
        self.on_msg(self._message)
        self.observe(self._refresh, names='selected')
        if game_view is not None:
            self._links.append(t.link((game_view, 'selected'), (self, 'selected')))
            self._links.append(t.link((game_view, 'hovered'), (self, 'hovered')))
        self._refresh()

    def update(self, host, labels):
        if self.host is None or host is None or (self.host.player, self.host.epoch) != (host.player, host.epoch):
            self.focus, self.part_index = (), None
            self.teaching_position = None
        self.host, self.labels = host, dict(labels)
        _check_vertex_ids(self.labels)
        self.occupancy_known = True
        self._refresh()

    def _refresh(self, change=None):
        host = self.host
        title = ('Even', 'Odd')[host.player if host is not None else self.player] + ' — Underlying host + associated positions (available search space)'
        if host is None:
            self.data = dict(title=title,
                             summary='Complete host unavailable', nodes=[],
                             note='This recording has no complete-host metadata. The current AD candidate shows occupied labels only; unused branches and native indices are unavailable.')
            return
        label = self.labels.get(self.selected)
        actual = host.index(*label[:2]) if label is not None and tuple(label[0]) in host.by_path else None
        priority = self.teaching_priority
        if priority is None:
            priority = host.priority_map.get(self.selected)
        cursor = self.teaching_position if self.teaching_position is not None else actual
        next_position = host.next_compatible_position(cursor, priority) if cursor is not None and priority is not None else None
        occupants = {}
        for v, (path, part, h) in self.labels.items():
            if tuple(path) in host.by_path:
                occupants.setdefault(host.index(path, part), []).append(v)
        children = host.children
        focus = self.focus if self.focus in host.by_path else ()
        shown, collapsed = [], set()

        def choose(path, allowance):
            shown.append(path)
            descendants = children[path]
            if len(descendants) >= allowance:
                if descendants:
                    collapsed.add(path)
                return
            remaining = allowance - 1
            for i, child in enumerate(descendants):
                budget = max(1, remaining // (len(descendants) - i))
                choose(child, budget)
                remaining -= budget

        choose(focus, self.max_nodes)
        # Geometry counts only visible nodes; collapsed nodes summarize their
        # entire subtree, retaining exact native intervals and Strahler values.
        shown_set, coordinates = set(shown), {}
        leaf = 0

        def place(path, depth):
            nonlocal leaf
            descendants = [p for p in children[path] if p in shown_set]
            if descendants:
                x = sum(place(p, depth + 1) for p in descendants) / len(descendants)
            else:
                x = 10 + leaf * 270
                leaf += 1
            coordinates[path] = x, 8 + depth * 140
            return x

        place(focus, 0)

        def part_data(position):
            p = position
            ids = sorted(occupants.get(p.index, ()))
            disabled = p.part == 'T' and p.h == 0
            compatible = not disabled and (priority is None or p.accepts(priority))
            status = ('disabled' if disabled else 'current' if p.index == actual else
                      'teaching cursor' if p.index == self.teaching_position else 'next' if p.index == next_position else f'{len(ids)} vertices')
            accepts = ('all priorities' if p.name == 'TOP' else f'p = {p.h}' if p.part == 'H' else
                       'no nonnegative priority' if disabled else f'p ≤ {p.h + (1 if p.part == "S" else -1)}')
            description = f'{p.index} {p.name}: {accepts}; ' + (f'occupants {ids}.' if self.occupancy_known else 'occupancy unknown.')
            if p.part == 'S':
                description += ' Outside its own core.'
            if p.name == 'TOP':
                description += ' Unresolved for this player; excluded from the certified AD. TOP is always valid, not a winning certificate.'
            return dict(index=p.index, path=list(p.path), part=p.part, name=p.name,
                        label=p.name, caption='disabled' if disabled else f'{len(ids)} ' + ('vertex' if len(ids) == 1 else 'vertices') if self.occupancy_known else 'unknown',
                        occupants=ids, compatible=compatible, current=p.index == actual,
                        next=p.index == next_position, fill='#e2e8f0' if disabled else _PART_COLOURS[p.part],
                        status=status if self.occupancy_known or disabled else 'unknown', description=description)

        nodes = []
        for path in shown:
            n = host.by_path[path]
            x, y = coordinates[path]
            parts = []
            for i, position in enumerate(n.positions):
                part = part_data(host.positions[position])
                part['box'] = [x + 5 + i * 81, y + 43, 81, 40]
                parts.append(part)
            parent_xy = None
            if path != focus:
                px, py = coordinates[path[:-1]]
                parent_xy = [px + 126, py + 108]
            a, b = n.core
            collapsed_label = (f'Expand: {n.nodes} nodes, {n.leaves} leaves; [{a},{b + 1})' if path in collapsed else '')
            vertices = sorted(v for index, ids in occupants.items() if a <= index < b for v in ids)
            nodes.append(dict(path=list(path), box=[x, y, 252, 108], parent_xy=parent_xy, parts=parts,
                              core=list(n.core), vertices=vertices, collapsed=collapsed_label,
                              label=f'{path_name(path)} · bound {n.h} · Str={n.strahler}',
                              interval=f'core [{a},{b}) · core + S [{a},{b + 1})',
                              description=f'One underlying node; H/T/S are associated positions. S lies outside its core. Subtree: {n.nodes} nodes, {n.leaves} leaves, Strahler {n.strahler}.'))
        center = self.part_index if self.part_index is not None else cursor if cursor is not None else 0
        start = max(0, min(center - self.strip_window // 2, len(host.positions) - self.strip_window))
        end = min(len(host.positions), start + self.strip_window)
        strip = []
        for i, p in enumerate(host.positions[start:end]):
            part = part_data(p)
            part['box'] = [8 + i * 106, 8, 102, 48]
            strip.append(part)
        # Brackets are clipped to the window, their labels retain exact bounds.
        bracket_nodes = [host.nodes[0]]
        if len(host.nodes) <= 6:
            bracket_nodes = list(host.nodes)
        elif center < len(host.positions):
            path = host.positions[center].path
            if path:
                bracket_nodes.append(host.by_path[path])
        brackets = []
        for row, n in enumerate(bracket_nodes):
            for side in (False, True):
                a, b = n.core
                b += side
                if a >= end or b <= start:
                    continue
                x, right = 8 + max(0, a-start)*106, 8 + min(end-start, b-start)*106 - 4
                y = 68 + row*42 + int(side)*18
                brackets.append(dict(d=f'M {x} {y-4} V {y} H {right} V {y-4}', x=x, y=y+13,
                                     side=side, label=f'{path_name(n.path)} core' + (' + S' if side else '') + f' [{a},{b})'))
        m = host.metrics
        prefix = '' if host.complete else 'Allocated prefix only: '
        summary = prefix + f"{m['nodes']} underlying nodes; {m['leaves']} leaves; height = {m['height']}; Strahler = {m['strahler']}; {m['positions']} allocated label positions."
        if host.parameters:
            summary += ' Requested parameters: ' + ', '.join(f'{k}={v}' for k, v in host.parameters) + '.'
        note = ('Groups count underlying nodes, not core boundaries. H is regular; T precedes children; dashed S is outside its core. Root S = TOP. '
                'Red: actual label. Blue: next compatible position. Grey: incompatible or disabled. Core intervals describe raw labels, not automatically traps or dominions.')
        if not host.complete:
            note += ' Implicit remainder unallocated: displayed indices/metrics refer only to this known prefix. Global next positions are unavailable.'
        if not self.occupancy_known:
            note += ' Occupancy was not recorded in this host-only view; unknown counts do not mean empty fibres.'
        cursor_note = f'Host epoch {host.epoch}. '
        cursor_note += (f'Selected vertex {self.selected}: actual position {actual}; normalized priority {host.priority_map.get(self.selected)}. ' if actual is not None else 'Select a game vertex to follow its actual label. ')
        cursor_note += f'Teaching cursor: {cursor}; priority {priority}; next {next_position}. Teaching controls do not change replay state.'
        self.data = dict(title=title,
                         summary=summary, note=note, cursor_note=cursor_note, nodes=nodes, strip=strip,
                         extent=[max(280, leaf*270), max(n['box'][1] for n in nodes)+118],
                         flat_extent=[max(300, len(strip)*106+16), 80 + 42*len(bracket_nodes)], brackets=brackets,
                         intervals=[dict(path=list(path), core=list(host.by_path[path].core))
                                    for path in sorted(shown_set | {host.positions[center].path})],
                         total=len(host.positions), priority=priority, focus=list(focus), focus_index=center,
                         window_note=f'Positions [{start},{end}) of {len(host.positions)}. Use Find position to inspect a different window.',
                         part=part_data(host.positions[self.part_index]) if self.part_index is not None else None)

    def _message(self, widget, content, buffers):
        if self.host is None:
            return
        command = content.get('command')
        if command == 'priority':
            priority = content['priority']
            if not isinstance(priority, int) or priority < 0:
                return
            self.teaching_priority, self.teaching_position = priority, None
        elif command == 'compatible':
            priority = self.data['priority'] if self.data['priority'] is not None else 0
            label = self.labels.get(self.selected)
            actual = self.host.index(*label[:2]) if label else -1
            cursor = self.teaching_position if self.teaching_position is not None else actual
            destination = self.host.next_compatible_position(cursor, priority)
            if destination is not None:
                self.teaching_position = destination
        elif command == 'reset_teaching':
            self.teaching_position, self.teaching_priority = None, None
        elif command in ('part', 'position'):
            i = content['index']
            if not isinstance(i, int) or not 0 <= i < len(self.host.positions):
                return
            self.part_index = i
            if command == 'position':
                self.focus = self.host.positions[i].path
        elif command == 'focus':
            path = tuple(content['path'])
            if path in self.host.by_path:
                self.focus = path
        elif command == 'vertex' and content['vertex'] in self.labels:
            self.selected = content['vertex']
        self._refresh()

    def close(self):
        for link in getattr(self, '_links', []):
            link.unlink()
        self._links = []
        super().close()

    def _static_svg(self):
        d = self.data
        if not d['nodes']:
            svg = _svg_root(720, 100, d['title'])
            _svg(svg, 'text', dict(x=8, y=24), d['summary'])
            _svg(svg, 'text', dict(x=8, y=48), 'Occupied candidate only; unused host branches unavailable.')
            return _ascii(ET.tostring(svg, encoding='unicode'))
        # Same Python geometry and text as live JS; SVG has no external assets.
        width = max(800, d['extent'][0], d['flat_extent'][0])
        lines = [line for text in (d['title'], d['summary'], d['note'], d['cursor_note'])
                 for line in wrap(text, max(30, int(width / 7)))]
        header = 20 + 17 * len(lines)
        height = header + d['extent'][1] + d['flat_extent'][1] + 85
        svg = _svg_root(width, height, d['title'])
        for i, text in enumerate(lines):
            _svg(svg, 'text', dict(x=8, y=20 + i*17), text)
        grouped = _svg(svg, 'g', dict(transform=f'translate(0 {header})'))
        for n in d['nodes']:
            if n['parent_xy']:
                x, y = n['parent_xy']
                _svg(grouped, 'line', dict(x1=x, y1=y, x2=n['box'][0]+126, y2=n['box'][1], stroke='#94a3b8'))
        for n in d['nodes']:
            x, y, w, h = n['box']
            group = _svg(grouped, 'g', {'data-host-path': json.dumps(n['path'], separators=(',', ':'))})
            hot = _host_hovered(self, n['path'], None, n['vertices'])
            _svg(group, 'rect', dict(x=x, y=y, width=w, height=h, rx=6, fill='#fff',
                                    stroke='#c026d3' if hot else '#64748b', **{'stroke-width': 3 if hot else 1}))
            _svg(group, 'text', dict(x=x+8, y=y+18), n['label'])
            _svg(group, 'text', dict(x=x+8, y=y+34), n['interval'])
            _svg(group, 'title', text=n['description'])
            for p in n['parts']:
                _host_svg_part(group, p, 'data-host-position', self)
            if n['collapsed']:
                _svg(group, 'text', dict(x=x+8, y=y+98), n['collapsed'])
        offset = header + d['extent'][1] + 24
        _svg(svg, 'text', dict(x=8, y=offset), 'Flattened host (lifting order)')
        flat = _svg(svg, 'g', dict(transform=f'translate(0 {offset+8})'))
        for p in d['strip']:
            _host_svg_part(flat, p, 'data-flat-position', self)
        for b in d['brackets']:
            _svg(flat, 'path', dict(d=b['d'], fill='none', stroke='#c2410c' if b['side'] else '#475569',
                                  **{'stroke-dasharray': '4 3' if b['side'] else 'none'}))
            _svg(flat, 'text', dict(x=b['x'], y=b['y']), b['label'])
        _svg(svg, 'text', dict(x=8, y=height-24), 'Dashed S: outside own core. TOP: unresolved. Strahler counts underlying branching only.')
        _svg(svg, 'text', dict(x=8, y=height-8), d['window_note'])
        return _ascii(ET.tostring(svg, encoding='unicode'))


def _host_hovered(view, path, index, vertices):
    if set(view.hovered) & set(vertices):
        return True
    fibre = getattr(view, 'hovered_position', None)
    if fibre is not None:
        return fibre == index
    if view.hovered_path is None:
        return False
    if index is None:
        return view.hovered_path == path
    return any(n['path'] == view.hovered_path and n['core'][0] <= index < n['core'][1]
               for n in view.data['intervals'])


def _host_svg_part(parent, p, attribute, view):
    group = _svg(parent, 'g', {attribute: p['index']})
    x, y, w, h = p['box']
    hot = _host_hovered(view, p['path'], p['index'], p['occupants'])
    _svg(group, 'rect', dict(x=x, y=y, width=w, height=h, fill=p['fill'],
                           opacity=1 if p['compatible'] else .32,
                           stroke='#c026d3' if hot else '#dc2626' if p['current'] else '#2563eb' if p['next'] else '#64748b',
                           **{'stroke-width': 3 if hot or p['current'] or p['next'] else 1,
                              'stroke-dasharray': '4 3' if p['part'] == 'S' else 'none'}))
    strip = attribute == 'data-flat-position'
    _svg(group, 'text', dict(x=x+5, y=y+(17 if strip else 16)),
         f"{p['index']} {p['name']}" if strip else p['label'])
    _svg(group, 'text', dict(x=x+5, y=y+(34 if strip else 31)), p['status'] if strip else p['caption'])
    _svg(group, 'title', text=p['description'])


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
    'Check one common round-start labelling; move invalid vertices to the next compatible position',
    'End the round; repeat until labels stabilize',
    'Adaptive host: raise t and repeat if needed',
    'Iterative shaving: attract certified winners; restart on the remaining game (after preprocessing and earlier shaving rounds)',
    'Materialize and independently check the resulting certificates',
]


def _controls_html(data):
    def paragraph(text):
        return '<p>' + escape(text) + '</p>'
    html = paragraph(f"{data['index']} / {data['length']-1}: {data['event']['event']}")
    inspector = data.get('inspector')
    if inspector:
        html += paragraph('Lift inspector — pre-round state')
        if 'vertex' in inspector:
            i = inspector
            html += paragraph(f"{'Even' if i['player'] == 0 else 'Odd'} · vertex {i['vertex']} · owner {i['owner']} · "
                              f"priority {i['original_priority']} (normalized {i['priority']}) · host epoch {i['epoch']} · round {i['round']}")
            html += paragraph(f"{i['source']['index']} {i['source']['name']} → {i['destination']['index']} {i['destination']['name']}")
        html += paragraph('Why move? ' + inspector['reason'])
        if inspector.get('edges'):
            html += paragraph('Blocking edges: ' + ', '.join(f'{a} → {b}' for a,b in inspector['edges']))
        if 'destination_reason' in inspector:
            html += paragraph('Why there? ' + inspector['destination_reason'])
        if 'target' in inspector:
            html += paragraph(f"L={inspector['target']}; A={inspector['safe']}; R={inspector['reached']}; avoidance A \\ R={inspector['avoidance']}.")
        if 'applied' in inspector:
            html += paragraph(f"Already moved: {inspector['applied']}. Showing: {inspector['vertex']}. Pending invalid vertices: {inspector['pending']}. All checks use pre-round step {inspector['before']}.")
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
        self.host_views = (tuple(HostView(game_view=self.game_view, player=p) for p in range(2))
                           if run.events[0]['algorithm'] != 'zielonka' else ())
        self.controls = _View(kind='controls')
        self.controls.next_kind = 'call' if 'call' in self.cursor.kinds else 'lift'
        self.controls.on_msg(self._message)
        self.game_view.observe(self._select_vertex, names='selected')
        candidates = [VBox([WidgetHTML(value=f'<strong>{name}: Current AD candidate / verified AD certificate</strong>'), view])
                      for name, view in zip(('Even', 'Odd'), self.tree_views)]
        panels = [self.controls, self.game_view, *self.host_views,
                  WidgetHTML(value='<p>Candidate paths may be pruned and renumbered. Links use vertex occupants; host and candidate paths are not equated. Core/trap conditions are relative to the current arena.</p>'),
                  HBox(candidates, layout=Layout(overflow='auto'))]
        super().__init__(children=panels, **kwargs)
        self.seek(0)

    def _select_vertex(self, change):
        """Map by occupants, since materialization may renumber child paths."""
        for view in self.tree_views:
            def locate(node):
                if node is None:
                    return None
                if any(change['new'] in node[p] for p in 'HTS'):
                    return node['path']
                return next((path for c in node['children'] if (path := locate(c)) is not None), None)
            view.selected = locate(view.data)

    def _repr_mimebundle_(self, **kwargs):
        bundle = super()._repr_mimebundle_(**kwargs)
        bundle['text/plain'] = f"Step {self.index}: {self.state.event['event']}"
        bundle['text/html'] = (_controls_html(self.controls.data) + self.game_view._static_svg() +
                               ''.join(view._static_svg() for view in self.host_views) +
                               ''.join(view._static_svg() for view in self.tree_views))
        return bundle

    @property
    def index(self):
        return self.cursor.index

    def seek(self, index):
        self.state = self.cursor.seek(index)
        for player, host_view in enumerate(self.host_views):
            host_view.update(self.state.hosts[player], self.state.raw_labels[player])
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
        self._select_vertex(dict(new=self.game_view.selected))
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
        inspector = self.cursor.explain_lift()
        if inspector and 'source' in inspector:
            if 'target' in inspector:
                for name, field, colour in [('safe A', 'safe', '#dcfce7'), ('earlier target L', 'target', '#fef08a'),
                                             ('reached R', 'reached', '#bfdbfe'), ('avoidance A minus R', 'avoidance', '#ffedd5')]:
                    self.game_view.highlight(inspector[field], colour, name)
            self.game_view.outlines += [dict(player=inspector['player'], name='Failed lift constraint', colour='#dc2626',
                violations=[dict(path=[], vertex=inspector['vertex'], edge=edge, condition='LIFT') for edge in inspector['edges']])]
        else:
            player = event.get('player', 0)
            if self.state.hosts[player] is not None:
                for part in 'HTS':
                    self.game_view.highlight([v for v, label in self.state.raw_labels[player].items() if label[1] == part],
                                             _PART_COLOURS[part], f'{("Even", "Odd")[player]} {part} positions')
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
                                  pseudocode=code, line=line, ticks=self.cursor.ticks, note=note, inspector=inspector)
        return self.state

    def step(self):
        return self.seek(min(self.index + 1, len(self.run) - 1))

    def back(self):
        return self.seek(max(0, self.index - 1))

    def next(self, kind='call'):
        return self.seek(self.cursor.next_index(kind))

    def end(self):
        return self.seek(len(self.run) - 1)

    def next_lift(self, vertex=None, player=None):
        """Jump using the pre-indexed lifts for an external vertex ID/player."""
        if vertex is None:
            return self.next('lift')
        return self.seek(self.cursor.next_lift_index(vertex, player))

    def next_round(self):
        return self.next('round_end')

    def verify(self, player):
        return self.cursor.verify(player)

    def _message(self, widget, content, buffers):
        command = content.get('command')
        if command == 'seek':
            self.seek(content['index'])
        elif command == 'next':
            self.next(content['kind'])
        elif command == 'next_selected' and self.game_view.selected is not None:
            self.next_lift(self.game_view.selected)
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
