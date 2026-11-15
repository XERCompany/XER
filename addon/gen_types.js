// Generates index.d.ts from the runtime API dump (xer_api_dump.json).
// Run: node gen_types.js  (after producing the dump via introspection)
'use strict';
const fs = require('fs');
const path = require('path');

const dump = JSON.parse(
  fs.readFileSync('C:/Users/LXXV/AppData/Local/Temp/xer_api_dump.json', 'utf8'));

// Verified return-type overrides: "ClassPath.Method" -> "signature".
const OVERRIDES = {
  'UI.Window.GetRenderStats': '(): RenderStats',
  'UI.Window.GetLastRenderError': '(): string',
  'UI.Window.GetHWND': '(): number',
  'UI.Window.GetTitle': '(): string',
  'UI.Window.GetTheme': '(): string',
  'UI.Window.IsClosed': '(): boolean',
  'UI.Window.IsVisible': '(): boolean',
  'UI.Window.IsWindowMaximized': '(): boolean',
  'UI.Window.HasCustomTitleBar': '(): boolean',
  'UI.Window.OpenDevTools': '(): boolean',
  'UI.Widget.DumpXml': '(): string',
  'UI.Label.DumpXml': '(): string',
  'UI.parseXml': '(markup: string): XmlLoadResult',
  'UI.parseXmlFile': '(path: string): XmlLoadResult',
  'UI.xmlSupportedTags': '(): string',
  'UI.Widget.IsVisible': '(): boolean',
  'UI.Widget.IsEnabled': '(): boolean',
  'UI.Widget.IsFocusable': '(): boolean',
  'UI.Widget.IsFocused': '(): boolean',
  'UI.Widget.IsHovered': '(): boolean',
  'UI.Widget.IsEffectivelyVisible': '(): boolean',
  'UI.Widget.ClipsChildren': '(): boolean',
  'UI.Widget.GetContentHeight': '(): number',
  'UI.Widget.GetTooltip': '(): string',
  'info': '(): XERInfo',
};

const BOOL_RE = /^(Is|Has|Can)[A-Z]/;
const BOOL_NAMES = new Set([
  'ClipsChildren', 'IsEmpty', 'IsFull', 'IsValid', 'IsRunning',
  'IsLoaded', 'IsPlaying', 'IsPaused', 'IsStopped', 'IsInitialized',
  'IsAllocated', 'IsSupported', 'IsFloatingPoint', 'IsPitched',
  'IsValidSysex', 'Contains', 'HasFocus', 'IsOpen', 'IsClosed',
  'IsVisible', 'IsEnabled', 'IsChecked', 'IsSelected', 'IsExpanded',
  'IsCollapsed', 'IsActive', 'IsMuted', 'IsSolo', 'IsMonitoring',
  'IsSubscribed', 'IsConnected', 'IsReady', 'IsBusy', 'IsIdle',
]);

function retType(classPath, method) {
  const key = classPath ? classPath + '.' + method : method;
  if (OVERRIDES[key]) {
    const sig = OVERRIDES[key];
    return sig.startsWith('(') ? sig : '(...args: any[]): ' + sig;
  }
  if (BOOL_RE.test(method) || BOOL_NAMES.has(method)) return '(): boolean';
  return '(...args: any[]): any';
}

function emitClass(c, indent) {
  const pad = '  '.repeat(indent);
  const lines = [`${pad}class ${c.name} {`];
  lines.push(`${pad}  constructor(...args: any[]);`);
  for (const s of c.statics) {
    lines.push(`${pad}  static ${s}(...args: any[]): any;`);
  }
  for (const m of c.methods) {
    lines.push(`${pad}  ${m}${retType(c.fullPath, m)};`);
  }
  lines.push(`${pad}}`);
  return lines.join('\n');
}

// Rebuild hierarchy: paths like audio.dsp.Biquad -> nested namespaces.
// classes have {path, statics, methods}; funcs {path, arity}; ns {paths}.
const root = { namespaces: new Map(), classes: [], funcs: [] };

function ensureNs(parts) {
  let node = root;
  for (const p of parts) {
    if (!node.namespaces.has(p)) {
      node.namespaces.set(p, { namespaces: new Map(), classes: [], funcs: [] });
    }
    node = node.namespaces.get(p);
  }
  return node;
}

for (const c of dump.classes) {
  const parts = c.path.split('.');
  const name = parts.pop();
  const ns = ensureNs(parts);
  ns.classes.push({
    name,
    fullPath: parts.length ? parts.join('.') + '.' + name : name,
    statics: c.statics,
    methods: c.methods,
  });
}
for (const f of dump.funcs) {
  const parts = f.path.split('.');
  const name = parts.pop();
  const ns = ensureNs(parts);
  ns.funcs.push({ name, path: f.path });
}
// image/audio/notify/UI namespaces may have no classes — still need them.
for (const n of dump.ns) ensureNs(n.split('.'));

function emitNode(node, indent) {
  const pad = '  '.repeat(indent);
  const out = [];
  // namespaces first (sorted), then classes, then funcs — deterministic
  for (const [name, child] of [...node.namespaces.entries()].sort()) {
    out.push(`${pad}namespace ${name} {`);
    out.push(emitNode(child, indent + 1));
    out.push(`${pad}}`);
  }
  for (const c of node.classes.sort((a, b) => a.name.localeCompare(b.name))) {
    out.push(emitClass(c, indent));
  }
  for (const f of node.funcs.sort((a, b) => a.name.localeCompare(b.name))) {
    const key = f.path;
    const sig = OVERRIDES[key] || '(...args: any[]): any';
    const params = sig.startsWith('(') ? sig.slice(1, sig.indexOf(')')) : '';
    const ret = sig.includes('):') ? sig.slice(sig.indexOf('):') + 2).trim() : 'any';
    out.push(`${pad}function ${f.name}(${params}): ${ret};`);
  }
  return out.filter(Boolean).join('\n');
}

const body = emitNode(root, 1);

const dts = `// xer3 — TypeScript definitions for the native addon (xer.node).
//
// Generated from runtime introspection of the built module: every class,
// namespace and method name below exists in xer.node. Parameter and most
// return types are intentionally \`any\` — refine them as the API stabilizes.
// Boolean-returning predicates (Is*/Has*/Can*) are typed.

declare namespace xer3 {
${body}

  interface XERInfo {
    node: string;
    platform: string;
    arch: string;
    xer: string;
    [key: string]: any;
  }

  namespace UI {
    interface RenderStats {
      frames: number;
      lastFrameMs: number;
      avgFrameMs: number;
      lastDamagePct: number;
      lastFrameFull: boolean;
    }
    interface XmlLoadResult {
      ok: boolean;
      widget?: Widget;
      warnings: string[];
      error: string;
    }
  }

  // XER is the same object as UI at runtime (alias registered in addon.cc).
  const XER: typeof UI;
}

export = xer3;
`;

fs.writeFileSync(path.join(__dirname, 'index.d.ts'), dts);
console.log('index.d.ts written:', dts.length, 'bytes');
