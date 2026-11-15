const xer = require('./index.js');
const srv = new xer.OpenAI.Server({ port: 18923, host: '127.0.0.1' });
srv.on('/hello', (req, res) => {
  res.json({ ok: true, path: req.path, method: req.method });
});
srv.on('POST', '/echo', (req, res) => {
  res.json({ body: req.body, bodyJson: req.bodyJson });
});
const started = srv.start();
console.log('start ->', started, 'running:', srv.isRunning());
setTimeout(() => {
  fetch('http://127.0.0.1:18923/hello?a=1').then(r => r.json()).then(j => {
    console.log('GET /hello ->', JSON.stringify(j));
    return fetch('http://127.0.0.1:18923/echo', { method: 'POST', body: JSON.stringify({x:42}), headers: {'content-type':'application/json'} });
  }).then(r => r.json()).then(j => {
    console.log('POST /echo ->', JSON.stringify(j));
    return fetch('http://127.0.0.1:18923/nope');
  }).then(r => r.text()).then(t => {
    console.log('GET /nope ->', t.slice(0, 80));
    srv.stop();
    console.log('stopped, running:', srv.isRunning());
    process.exit(0);
  }).catch(e => { console.log('fetch err', e.message); srv.stop(); process.exit(1); });
}, 300);
