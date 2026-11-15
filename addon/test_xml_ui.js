// XML UI loader smoke test — parseXml / parseXmlFile / xmlSupportedTags.
const assert = require('assert');
const fs = require('fs');
const path = require('path');
const xer = require('./index.js');
const UI = xer.UI || xer.XER;

const frames = parseInt(process.env.XER_DEMO_FRAMES || '30', 10);

// 1. Basic parse — nested tree of known tags.
const res = UI.parseXml(`
<Widget tooltip="root card">
  <Form columns="2" gap="8">
    <Label text="Name"/>
    <TextInput placeholder="Enter name"/>
    <Button text="Save" style="primary"/>
    <Checkbox text="Remember" checked="true"/>
  </Form>
</Widget>`);
assert.strictEqual(res.ok, true, 'parse failed: ' + res.error);
assert(res.widget, 'no widget returned');
assert.strictEqual(res.warnings.length, 0,
                   'unexpected warnings: ' + res.warnings.join('; '));
console.log('[xml] parsed ok, warnings =', res.warnings.length);

// 2. Unknown child -> warning, tree still builds.
const res2 = UI.parseXml(`<Widget><Label text="a"/><BogusTag foo="1"/><Label text="b"/></Widget>`);
assert.strictEqual(res2.ok, true);
assert(res2.warnings.some(w => w.includes('BogusTag')),
       'expected BogusTag warning, got: ' + JSON.stringify(res2.warnings));
console.log('[xml] unknown-tag warning ok:', res2.warnings[0]);

// 3. Unknown root -> fatal.
const res3 = UI.parseXml(`<BogusRoot><Label text="x"/></BogusRoot>`);
assert.strictEqual(res3.ok, false);
assert(res3.widget === undefined);
assert(res3.error.length > 0);
console.log('[xml] bad root rejected:', res3.error);

// 4. Malformed markup -> fatal with error text.
const res4 = UI.parseXml(`<Widget><Label text="unclosed"`);
assert.strictEqual(res4.ok, false);
console.log('[xml] malformed rejected:', res4.error.slice(0, 80));

// 5. File loading.
const tmpXml = path.join(__dirname, 'test_xml_ui_tmp.xml');
fs.writeFileSync(tmpXml,
  `<Widget><Label text="from file" dir="ltr"/></Widget>`, 'utf8');
const res5 = UI.parseXmlFile(tmpXml);
assert.strictEqual(res5.ok, true, 'file parse failed: ' + res5.error);
fs.unlinkSync(tmpXml);
console.log('[xml] file load ok');

// 6. SupportedTags surface.
const tags = UI.xmlSupportedTags();
assert(typeof tags === 'string' && tags.includes('Button') && tags.includes('Label'));
console.log('[xml] supported tags:', tags.split(',').length, 'tags');

// 7. Round-trip: parse -> window -> DumpXml contains the widgets.
const app = new UI.Application();
const win = new UI.Window();
win.Create('XML UI Test', 640, 480);
win.SetContent(res.widget);
win.Show();

let n = 0;
win.SetFrameCallback(() => {
  n++;
  if (n === frames) {
    const dump = win.GetContent().DumpXml();
    assert(dump.includes('type="'), 'dump missing type attrs');
    assert(dump.includes('DebugMapUI'), 'dump missing wrapper');
    console.log('[xml] render ok, frames =', n,
                'renderError =', JSON.stringify(win.GetLastRenderError()));
    const stats = win.GetRenderStats();
    console.log('[xml] stats:', JSON.stringify(stats));
    win.Close();
  }
});
app.Initialize();
app.Run();
console.log('[xml] PASS');
