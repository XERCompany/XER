#ifndef XER_UI_WIDGETS_ATTACHMENT_H
#define XER_UI_WIDGETS_ATTACHMENT_H

#include <functional>
#include <memory>
#include <string>

#include "widgets/widget.h"

namespace XER::UI::Widgets {

// Attachment — file chip: type icon tile, filename (+ optional size/meta),
// and a remove button. Fires on_remove when the × is clicked.
class Attachment final : public Widget {
public:
    Attachment(std::string filename = {}, std::string meta = {});

    void SetFilename(std::string filename);
    const std::string& GetFilename() const { return filename_; }
    void SetMeta(std::string meta);
    const std::string& GetMeta() const { return meta_; }
    void SetOnRemove(std::function<void()> callback);

    void Render(Rendering::Renderer& renderer, const Theme::Tokens& tokens) override;
    void Layout(const Layout::Rect& bounds) override;
    using Widget::Layout;
    bool HandleEvent(const Event& event) override;

    Layout::Size GetMinSize() const override;
    Layout::Size GetPreferredSize() const override;
    Layout::Size GetMaxSize() const override;

private:
    Layout::Rect RemoveRect(const Layout::Rect& bounds) const;

    std::string filename_;
    std::string meta_;
    Layout::Rect remove_rect_{};
    bool remove_hovered_ = false;
    std::function<void()> on_remove_;
};

} // namespace XER::UI::Widgets

#endif // XER_UI_WIDGETS_ATTACHMENT_H
