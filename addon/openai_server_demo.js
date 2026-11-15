const { Content } = require('./index.js');

const server = new Content.OpenAI.Server();
const ai = new Content.AI();
const s3 = new Content.S3();

server.onHandlerKey((key, verify) => {
    console.log('API key:', key);
    verify(true);
}); 

server.onHandleCompletion((req, res) => {
    console.log('Completion request:', req);
    res.json({ message: 'Hello from OpenAI server demo!' });
});

server.onHandleAudio((req, res) => {
    console.log('Audio request:', req);
    res.json({ message: 'Hello from OpenAI server demo!' });
}); 

server.onHandleImage((req, res) => {
    console.log('Image request:', req);
    res.json({ message: 'Hello from OpenAI server demo!' });
});

server.start(3000);
