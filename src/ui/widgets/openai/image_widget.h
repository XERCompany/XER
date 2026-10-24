#ifndef XER_UI_WIDGETS_OPENAI_IMAGE_WIDGET_H
#define XER_UI_WIDGETS_OPENAI_IMAGE_WIDGET_H

#include "../../widgets/widget.h"
#include "../../../content/openai/openai.h"
#include <string>
#include <vector>
#include <functional>

namespace XER::UI::Widgets::OpenAI {

/**
 * ImageGenerationParamsUI - UI parameters for image generation
 */
struct ImageGenerationParamsUI {
    std::string prompt;
    int width = 1024;
    int height = 1024;
    int num_images = 1;
    std::string style = "vivid";
    std::string quality = "standard";
    std::string response_format = "url";
};

/**
 * ImageWidget - UI widget for OpenAI image generation
 *
 * Provides:
 * - Prompt input area
 * - Image size selector
 * - Style/quality options
 * - Generated image gallery
 * - Download/save functionality
 * - Image preview
 */
class ImageWidget : public Widget {
public:
    ImageWidget();
    ~ImageWidget() override = default;

    void Render(Rendering::Renderer& renderer) override;
    void Layout(const Layout::Rect& bounds) override;
    bool HandleEvent(const Event& event) override;

    Layout::Size GetMinSize() const override;
    Layout::Size GetPreferredSize() const override;
    Layout::Size GetMaxSize() const override;

    void SetOpenAISubsystem(std::shared_ptr<Content::OpenAI::OpenAISubsystem> subsystem);
    
    void GenerateImage(const ImageGenerationParamsUI& params);
    void ClearGallery();
    
    // Image operations
    void SaveImage(int index, const std::string& path);
    void SetImageAsBackground(int index);
    
    // Callbacks
    using ImageGeneratedCallback = std::function<void(const std::string& url)>;
    void OnImageGenerated(ImageGeneratedCallback callback);

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

/**
 * ImageGalleryWidget - Gallery display for generated images
 */
class ImageGalleryWidget : public Widget {
public:
    ImageGalleryWidget();
    ~ImageGalleryWidget() override = default;

    void Render(Rendering::Renderer& renderer) override;
    void Layout(const Layout::Rect& bounds) override;
    bool HandleEvent(const Event& event) override;

    Layout::Size GetMinSize() const override;
    Layout::Size GetPreferredSize() const override;
    Layout::Size GetMaxSize() const override;

    void AddImage(const std::string& url, const std::string& prompt);
    void ClearImages();
    int GetSelectedIndex() const;
    std::string GetSelectedImageUrl() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace XER::UI::Widgets::OpenAI

#endif // XER_UI_WIDGETS_OPENAI_IMAGE_WIDGET_H
