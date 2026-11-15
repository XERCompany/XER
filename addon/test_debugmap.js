const xer = require('./index.js');
const UI = xer.UI || xer.XER;

const card = new UI.Widget();   // Widget = Card surface in addon
card.SetTooltip('card "tip" <b>&');
const btn = new UI.Button('Click & "Go"');
btn.SetPosition(10, 20);
btn.SetSize(120, 32);
const lbl = new UI.Label('Hello <World>');
lbl.SetVisible(false);
card.AddChild(btn);
card.AddChild(lbl);
const root = new UI.Widget();
root.AddChild(card);

const xml = root.DumpXml();
console.log(xml);
const assert = require('assert');
assert(xml.includes('<DebugMapUI'), 'wrapper');
assert(xml.includes('type="'), 'type attr');
assert(xml.includes('Hello'), 'label present');
assert(xml.includes('shown="false"') || xml.includes('hidden="true"'), 'hidden marker');
const m = xml.match(/tooltip="[^"]*"/);
console.log('tooltip attr:', m && m[0]);
assert(m && m[0].includes('&quot;'), 'escaped tooltip');
console.log('--- assertions passed ---');
