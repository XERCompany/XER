// Introspects the built xer.node addon and writes xer_api_dump.json,
// the input format consumed by gen_types.js.
// Run: node gen_dump.js && node gen_types.js
'use strict';
const fs = require('fs');
const path = require('path');

const xer = require('./index.js');

const SKIP_CTOR_KEYS = new Set(['length', 'name', 'prototype', 'arguments', 'caller']);

function protoMethods(fn) {
  const out = [];
  const proto = fn.prototype;
  if (!proto) return out;
  for (const n of Object.getOwnPropertyNames(proto)) {
    if (n === 'constructor') continue;
    const d = Object.getOwnPropertyDescriptor(proto, n);
    if (d && typeof d.value === 'function') out.push(n);
  }
  return out.sort();
}

function statics(fn) {
  const out = [];
  for (const n of Object.getOwnPropertyNames(fn)) {
    if (SKIP_CTOR_KEYS.has(n)) continue;
    if (typeof fn[n] === 'function') out.push(n);
  }
  return out.sort();
}

const classes = [];
const funcs = [];
const ns = [];

function walk(obj, prefix) {
  for (const key of Object.keys(obj)) {
    if (key === 'XER') continue; // alias of UI — avoid duplicate paths
    const v = obj[key];
    const p = prefix ? prefix + '.' + key : key;
    if (typeof v === 'function') {
      const methods = protoMethods(v);
      if (methods.length > 0) {
        classes.push({ path: p, statics: statics(v), methods });
      } else {
        funcs.push({ path: p, arity: v.length });
      }
    } else if (v && typeof v === 'object') {
      ns.push(p);
      walk(v, p);
    }
  }
}

walk(xer, '');

const out = { classes, funcs, ns };
const dest = process.env.XER_DUMP_PATH ||
  path.join(process.env.TEMP || '/tmp', 'xer_api_dump.json');
fs.writeFileSync(dest, JSON.stringify(out, null, 1));
console.log(`dump: ${classes.length} classes, ${funcs.length} funcs, ` +
            `${ns.length} namespaces -> ${dest}`);
