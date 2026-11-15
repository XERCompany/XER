const xer = require('./index.js');
const UI = xer.UI || xer.XER;

const card = new UI.Card();
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
// sanity checks
const assert = require('assert');
assert(xml.includes('<DebugMapUI'), 'wrapper');
assert(xml.includes('type="'), 'type attr');
assert(xml.includes('&lt;World&gt;') || xml.includes('Hello'), 'label present');
assert(xml.includes('shown="false"') || xml.includes('hidden="true"'), 'hidden marker');
assert(xml.includes('tooltip="card &quot;tip&quot; &lt;b&gt;&amp;"'), 'escaped tooltip: ' + xml.match(/tooltip="[^"]*"/));
console.log('--- assertions passed ---');
