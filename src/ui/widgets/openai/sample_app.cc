/**
 * Sample OpenAI UI Application
 * 
 * Demonstrates integration of OpenAI widgets with XER UI framework
 */

#include "../../core/application.h"
#include "../../core/window.h"
#include "chat_widget.h"
#include "image_widget.h"
#include "agent_widget.h"
#include "audio_widget.h"
#include "dashboard_widget.h"
#include "../../../../content/openai/openai.h"
#include <memory>

using namespace XER::UI::Core;
using namespace XER::UI::Widgets::OpenAI;
using namespace Content::OpenAI;

int main() {
    // Initialize XER UI application
    auto& app = Application::Instance();
    if (!app.Initialize()) {
        return -1;
    }

    // Initialize OpenAI subsystem
    auto openai_subsystem = OpenAIFactory::CreateFromEnv();
    if (!openai_subsystem->Initialize()) {
        // Use default config if env vars not set
        OpenAISubsystem::Config config;
        config.api_key = "your-api-key-here";
        openai_subsystem = std::make_unique<OpenAISubsystem>(config);
        openai_subsystem->Initialize();
    }

    // Create main window
    Window window;
    window.Create("XER OpenAI Dashboard", 1200, 800);
    
    // Create dashboard widget
    auto dashboard = std::make_shared<OpenAIDashboardWidget>();
    dashboard->SetOpenAISubsystem(openai_subsystem);
    
    // Set window content
    window.SetContent(dashboard);
    
    // Show window
    window.Show();

    // Run event loop
    return app.Run();
}

/**
 * Chat Window Example
 */
int ChatWindowExample() {
    auto& app = Application::Instance();
    app.Initialize();

    auto openai_subsystem = OpenAIFactory::CreateFromEnv();
    openai_subsystem->Initialize();

    Window window;
    window.Create("XER Chat", 800, 600);
    
    auto chat_widget = std::make_shared<ChatWidget>();
    chat_widget->SetOpenAISubsystem(openai_subsystem);
    
    // Set styling
    chat_widget->SetUserMessageColor("#E2E8F0");
    chat_widget->SetAssistantMessageColor("#F1F5F9");
    chat_widget->SetSystemMessageColor("#94A3B8");
    
    // Configure
    chat_widget->SetModel("gpt-4");
    chat_widget->SetTemperature(0.7f);
    chat_widget->SetMaxTokens(2048);
    chat_widget->EnableStreaming(true);
    
    // Set callback for messages
    chat_widget->OnMessageReceived([](const ChatMessageUI& msg) {
        std::cout << "[" << msg.timestamp << "] ";
        switch (msg.role) {
            case MessageRole::kUser: std::cout << "User"; break;
            case MessageRole::kAssistant: std::cout << "Assistant"; break;
            case MessageRole::kSystem: std::cout << "System"; break;
            case MessageRole::kTool: std::cout << "Tool"; break;
        }
        std::cout << ": " << msg.content << std::endl;
    });
    
    window.SetContent(chat_widget);
    window.Show();
    
    return app.Run();
}

/**
 * Image Generation Window Example
 */
int ImageWindowExample() {
    auto& app = Application::Instance();
    app.Initialize();

    auto openai_subsystem = OpenAIFactory::CreateFromEnv();
    openai_subsystem->Initialize();

    Window window;
    window.Create("XER Image Generator", 1000, 700);
    
    auto image_widget = std::make_shared<ImageWidget>();
    image_widget->SetOpenAISubsystem(openai_subsystem);
    
    // Set callback for generated images
    image_widget->OnImageGenerated([](const std::string& url) {
        std::cout << "Image generated: " << url << std::endl;
    });
    
    window.SetContent(image_widget);
    window.Show();
    
    return app.Run();
}

/**
 * Agent Window Example
 */
int AgentWindowExample() {
    auto& app = Application::Instance();
    app.Initialize();

    auto openai_subsystem = OpenAIFactory::CreateFromEnv();
    openai_subsystem->Initialize();

    // Create specialized agents
    auto coding_agent = openai_subsystem->CreateCodingAgent();
    auto research_agent = openai_subsystem->CreateResearchAgent();
    
    Window window;
    window.Create("XER Agent Studio", 900, 650);
    
    auto agent_widget = std::make_shared<AgentWidget>();
    agent_widget->SetOpenAISubsystem(openai_subsystem);
    
    // Register agents
    agent_widget->RegisterAgent("coding", std::shared_ptr<Agent::IAgent>(coding_agent.get()));
    agent_widget->RegisterAgent("research", std::shared_ptr<Agent::IAgent>(research_agent.get()));
    
    // Select default agent
    agent_widget->SelectAgent("coding");
    
    // Set callbacks
    agent_widget->OnAgentResponse([](const std::string& response) {
        std::cout << "Agent response: " << response << std::endl;
    });
    
    agent_widget->OnToolInvoke([](const std::string& tool_name, const std::string& args) {
        std::cout << "Tool invoked: " << tool_name << " with args: " << args << std::endl;
    });
    
    window.SetContent(agent_widget);
    window.Show();
    
    return app.Run();
}

/**
 * Audio Transcription Window Example
 */
int AudioTranscriptionWindowExample() {
    auto& app = Application::Instance();
    app.Initialize();

    auto openai_subsystem = OpenAIFactory::CreateFromEnv();
    openai_subsystem->Initialize();

    Window window;
    window.Create("XER Audio Transcription", 900, 600);
    
    auto audio_widget = std::make_shared<AudioTranscriptionWidget>();
    audio_widget->SetOpenAISubsystem(openai_subsystem);
    
    // Enable real-time transcription
    audio_widget->EnableRealTimeTranscription(true);
    
    // Set callback for transcription segments
    audio_widget->OnTranscriptionSegment([](const TranscriptionSegmentUI& segment) {
        std::cout << "[" << segment.start_time << "s - " << segment.end_time << "s] "
                  << segment.text << " (confidence: " << segment.confidence << ")" << std::endl;
    });
    
    audio_widget->OnTranscriptionComplete([](const std::string& full_text) {
        std::cout << "Full transcription: " << full_text << std::endl;
    });
    
    window.SetContent(audio_widget);
    window.Show();
    
    return app.Run();
}
