const { UI } = require('./index.js'); // loads xer.node and exposes the XER global

const app = new UI.Application();
const window = new UI.Window();

class ContentWidget extends UI.Widget {
    constructor() {
        super(); 

        this.label1 = new UI.Label("Hello World");  

        this.AddChild(this.label1); 
    }
}
  
 
window.Create("Example", 800, 600);

const content = new ContentWidget();
window.SetContent(content);

window.Show();

// const app = new Application();
app.Initialize();
app.Run();
