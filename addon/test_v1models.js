const xer = require('./index.js');
const srv = new xer.OpenAI.Server({ port: 18924, host: '127.0.0.1' });
srv.start();
setTimeout(async () => {
  try {
    const r = await fetch('http://127.0.0.1:18924/v1/models');
    console.log('GET /v1/models ->', r.status, await r.text());
    const r2 = await fetch('http://127.0.0.1:18924/v1/chat/completions', {
      method: 'POST', headers: {'content-type':'application/json'},
      body: JSON.stringify({model:'x', messages:[{role:'user',content:'hi'}]})
    });
    console.log('POST /v1/chat/completions ->', r2.status, (await r2.text()).slice(0,200));
  } catch(e) { console.log('err', e.message); }
  srv.stop(); process.exit(0);
}, 400);
