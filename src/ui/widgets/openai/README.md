# OpenAI UI Widgets

XER UI widgets for OpenAI integration, built on the XER UI framework with WinAPI and Direct2D.

## Overview

Provides modern, ShadcnUI-style widgets for interacting with OpenAI features:
- **ChatWidget**: Chat interface with streaming support
- **ImageWidget**: Image generation and gallery
- **AgentWidget**: Agent interaction and memory management
- **AudioTranscriptionWidget**: Audio transcription with waveform visualization
- **TextProcessingWidget**: Text processing and analysis
- **SkillsWidget**: Skill management and execution
- **OpenAIDashboardWidget**: Main dashboard with tabbed interface
- **UsageStatsWidget**: API usage statistics display
- **SystemStatusWidget**: System status indicators
- **SentimentAnalysisWidget**: Sentiment visualization
- **WaveformWidget**: Audio waveform visualization

## Architecture

```
src/ui/widgets/openai/
├── chat_widget.h/cc        # Chat interface
├── image_widget.h/cc       # Image generation
├── agent_widget.h/cc       # Agent interaction
├── audio_widget.h/cc       # Audio transcription
├── text_widget.h/cc        # Text processing
├── skills_widget.h/cc      # Skill management
├── dashboard_widget.h/cc   # Main dashboard
├── sample_app.cc           # Example applications
└── CMakeLists.txt          # Build configuration
```

## Integration with XER UI

All widgets inherit from `XER::UI::Widgets::Widget` and integrate with:
- **XER UI Core**: Application, Window management
- **XER Rendering**: Direct2D rendering abstraction
- **XER Layout**: Geometry primitives (Rect, Size, Point)
- **XER ColorSpace**: Full color space and HDR support
- **XER OpenAI**: Complete OpenAI subsystem integration
- **XER Audio**: Audio engine and DSP algorithms
- **XER Hardware**: GPU detection and acceleration

## Widget Features

### ChatWidget

- Message display with role-based styling
- Input field with multi-line support
- Streaming response support
- Token count display
- History navigation
- Model selection
- Temperature/max_tokens configuration

### ImageWidget

- Prompt input area
- Image size selector (512x512, 1024x1024, etc.)
- Style/quality options
- Generated image gallery
- Download/save functionality
- Image preview

### AgentWidget

- Agent selection dropdown
- Memory view with relevance scores
- Tool invocation display
- Agent state indicator (thinking, executing)
- Memory management (clear, export)
- Real-time status updates

### AudioTranscriptionWidget

- Audio file selection
- Language selection
- Real-time transcription display
- Timestamped segments
- Speaker diarization visualization
- Audio waveform visualization
- Export transcription (TXT, SRT, VTT)

### OpenAIDashboardWidget

- Tabbed interface (Chat, Image, Audio, Agent, Settings)
- Usage statistics display
- API key management
- System status indicators
- Quick actions
- Model selection

## Usage

### Basic Chat Window

```cpp
#include "ui/core/application.h"
#include "ui/core/window.h"
#include "ui/widgets/openai/chat_widget.h"
#include "content/openai/openai.h"

using namespace XER::UI::Core;
using namespace XER::UI::Widgets::OpenAI;
using namespace Content::OpenAI;

int main() {
    auto& app = Application::Instance();
    app.Initialize();

    auto openai_subsystem = OpenAIFactory::CreateFromEnv();
    openai_subsystem->Initialize();

    Window window;
    window.Create("XER Chat", 800, 600);
    
    auto chat_widget = std::make_shared<ChatWidget>();
    chat_widget->SetOpenAISubsystem(openai_subsystem);
    chat_widget->SetModel("gpt-4");
    chat_widget->EnableStreaming(true);
    
    window.SetContent(chat_widget);
    window.Show();
    
    return app.Run();
}
```

### Dashboard with All Features

```cpp
#include "ui/widgets/openai/dashboard_widget.h"

Window window;
window.Create("XER OpenAI Dashboard", 1200, 800);

auto dashboard = std::make_shared<OpenAIDashboardWidget>();
dashboard->SetOpenAISubsystem(openai_subsystem);
dashboard->SetActiveTab(DashboardTab::kChat);

window.SetContent(dashboard);
window.Show();
```

### Custom Styling

```cpp
auto chat_widget = std::make_shared<ChatWidget>();

// Set custom colors (hex format)
chat_widget->SetUserMessageColor("#E2E8F0");
chat_widget->SetAssistantMessageColor("#F1F5F9");
chat_widget->SetSystemMessageColor("#94A3B8");
```

### Callbacks

```cpp
// Chat message callback
chat_widget->OnMessageReceived([](const ChatMessageUI& msg) {
    std::cout << "[" << msg.timestamp << "] " << msg.content << std::endl;
});

// Image generation callback
image_widget->OnImageGenerated([](const std::string& url) {
    std::cout << "Image generated: " << url << std::endl;
});

// Agent response callback
agent_widget->OnAgentResponse([](const std::string& response) {
    std::cout << "Agent: " << response << std::endl;
});

// Transcription callback
audio_widget->OnTranscriptionSegment([](const TranscriptionSegmentUI& segment) {
    std::cout << "[" << segment.start_time << "s] " << segment.text << std::endl;
});
```

## Building

### Requirements

- Windows 10 or later
- Visual Studio 2019 or later
- C++23 support
- Windows SDK
- DirectX SDK (for Direct2D)
- XER UI framework
- XER OpenAI subsystem

### CMake Integration

The widgets are built as a separate library:

```cmake
add_subdirectory(src/ui/widgets/openai)

target_link_libraries(your_app
    PRIVATE
        XER_UI_OpenAI
        XER_OpenAI
        XER_UI_Core
        XER_UI_Widgets
)
```

## Styling

Widgets follow ShadcnUI design principles:
- Clean, minimal interface
- Subtle shadows and borders
- Consistent spacing
- Accessible color contrast
- Smooth animations

### Color Themes

Default color scheme uses Tailwind CSS-inspired colors:
- Background: #FFFFFF
- Surface: #F8FAFC
- Border: #E2E8F0
- Text: #0F172A
- Primary: #3B82F6
- Secondary: #64748B

### HDR Support

Widgets support HDR rendering through XER ColorSpace integration:
- Automatic HDR detection
- PQ/HLG transfer functions
- Wide gamut color support (Rec.2020, Display P3)

## Performance

- Efficient rendering using Direct2D
- Minimal redraws (dirty rect tracking)
- Async operations via IOThreadPool
- Streaming responses without blocking UI
- Image caching for gallery

## Future Enhancements

- More widget types (Skill selector, Sandbox viewer)
- Theme customization API
- Keyboard shortcuts
- Drag-and-drop for images/audio
- Export/import conversations
- Voice input for chat
- Real-time collaboration
- Plugin system for custom widgets

## Examples

See `sample_app.cc` for complete examples:
- Main dashboard
- Chat window
- Image generation
- Agent studio
- Audio transcription

## License

Part of XER project. See main project LICENSE.
