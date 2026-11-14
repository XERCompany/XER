require('./index.js'); // loads xer.node and exposes the XER global

class ContentWidget extends XER.Widget {
    constructor() {
        super(); 

        this.label1 = new XER.Label("Hello World");

        this.label1.SetPosition(100, 100);
        this.label1.SetSize(200, 50);
 
        this.addChild(this.label1); 
    }
}

const Application = XER.Application;

const Window = XER.Window;

const window = new Window();
window.Create("Example", 800, 600);

const content = new ContentWidget();
window.SetContent(content);

const app = new Application();
app.Initialize();
app.Run();
