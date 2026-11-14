// preview.exe — Image preview tool using XER image API
//
// Usage: preview.exe <image_file>
//
// Displays information about image files: format, dimensions, color space,
// pixel format, and metadata.

#include <iostream>
#include <string>
#include <memory>

#include <absl/strings/str_cat.h>

#include "image/io/image_loader.h"
#include "image/core/image_buffer.h"
#include "image/core/image_descriptor.h"
#include "flux/terminal/terminal_output_renderer.h"
#include "flux/terminal/terminal_colors.h"
#include "flux/terminal/terminal_styles.h"
#include "helper/string_utils.h"

namespace {
namespace Str = XER::Helper::String;

void PrintUsage() {
    std::cout << "Usage: preview.exe <image_file>\n";
    std::cout << "\nDisplays detailed information about image files.\n";
    std::cout << "Supported formats: JPEG, PNG, WEBP, TIFF, GIF, BMP, PSD, EXR, HEIF, SVG\n";
}

std::string PixelFormatToString(image::PixelFormat fmt) {
    switch (fmt) {
        case image::PixelFormat::RGBA8: return "RGBA8";
        case image::PixelFormat::BGRA8: return "BGRA8";
        case image::PixelFormat::RGB8: return "RGB8";
        case image::PixelFormat::BGR8: return "BGR8";
        case image::PixelFormat::Gray8: return "Gray8";
        case image::PixelFormat::Gray16: return "Gray16";
        case image::PixelFormat::RGBA16: return "RGBA16";
        case image::PixelFormat::RGBA32F: return "RGBA32F (HDR)";
        case image::PixelFormat::YUV420P: return "YUV420P";
        default: return "Unknown";
    }
}

std::string ColorSpaceToString(image::ColorSpace cs) {
    switch (cs) {
        case image::ColorSpace::sRGB: return "sRGB";
        case image::ColorSpace::LinearRGB: return "Linear RGB";
        case image::ColorSpace::AdobeRGB: return "Adobe RGB";
        case image::ColorSpace::ACES_CG: return "ACES CG";
        case image::ColorSpace::Rec709: return "Rec. 709";
        case image::ColorSpace::Rec2020: return "Rec. 2020";
        case image::ColorSpace::P3_D65: return "Display P3";
        case image::ColorSpace::Gray: return "Gray";
        default: return "Unknown";
    }
}

std::string FormatSize(int width, int height) {
    const double megapixels = (width * height) / 1000000.0;
    char buf[64];
    snprintf(buf, sizeof(buf), "%dx%d (%.2f MP)", width, height, megapixels);
    return std::string(buf);
}

std::string FormatBytes(size_t bytes) {
    const double mb = bytes / (1024.0 * 1024.0);
    char buf[64];
    snprintf(buf, sizeof(buf), "%.2f MB", mb);
    return std::string(buf);
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        PrintUsage();
        return 1;
    }

    std::string input_path = argv[1];

    XER::Flux::Terminal::WriteLine(
        XER::Flux::Terminal::OutputStream::kStdout,
        absl::StrCat(
            XER::Flux::Terminal::ForegroundSequence(XER::Flux::Terminal::TerminalColor::kCyan),
            XER::Flux::Terminal::BeginStyleSequence({.bold = true}),
            "Image Preview: ", input_path
        )
    );

    // Load image
    image::ImageLoader loader;
    image::ImageDescriptor desc;
    auto buffer = loader.Load(input_path, desc);

    if (!buffer || !buffer->IsValid()) {
        std::cerr << "Error: Failed to load image\n";
        return 1;
    }

    // Print format information
    XER::Flux::Terminal::WriteLine(
        XER::Flux::Terminal::OutputStream::kStdout,
        absl::StrCat(
            XER::Flux::Terminal::ForegroundSequence(XER::Flux::Terminal::TerminalColor::kYellow),
            XER::Flux::Terminal::BeginStyleSequence({.bold = true}),
            "\n=== Format Information ==="
        )
    );

    XER::Flux::Terminal::WriteLine(
        XER::Flux::Terminal::OutputStream::kStdout,
        absl::StrCat(
            "Format: ", desc.format_name, "\n",
            "Source: ", desc.source_path
        )
    );

    // Print dimensions
    XER::Flux::Terminal::WriteLine(
        XER::Flux::Terminal::OutputStream::kStdout,
        absl::StrCat(
            XER::Flux::Terminal::ForegroundSequence(XER::Flux::Terminal::TerminalColor::kYellow),
            XER::Flux::Terminal::BeginStyleSequence({.bold = true}),
            "\n=== Dimensions ==="
        )
    );

    XER::Flux::Terminal::WriteLine(
        XER::Flux::Terminal::OutputStream::kStdout,
        absl::StrCat(
            "Size: ", FormatSize(desc.width, desc.height), "\n",
            "Width: ", desc.width, " px\n",
            "Height: ", desc.height, " px\n",
            "DPI: ", desc.dpi_x, " x ", desc.dpi_y
        )
    );

    // Print color information
    XER::Flux::Terminal::WriteLine(
        XER::Flux::Terminal::OutputStream::kStdout,
        absl::StrCat(
            XER::Flux::Terminal::ForegroundSequence(XER::Flux::Terminal::TerminalColor::kYellow),
            XER::Flux::Terminal::BeginStyleSequence({.bold = true}),
            "\n=== Color Information ==="
        )
    );

    XER::Flux::Terminal::WriteLine(
        XER::Flux::Terminal::OutputStream::kStdout,
        absl::StrCat(
            "Pixel Format: ", PixelFormatToString(desc.format), "\n",
            "Color Space: ", ColorSpaceToString(desc.color_space), "\n",
            "Channels: ", desc.channels, "\n",
            "Bit Depth: ", desc.depth, " bits\n",
            "Has Alpha: ", desc.has_alpha ? "Yes" : "No", "\n",
            "HDR: ", desc.is_hdr ? "Yes" : "No"
        )
    );

    // Print buffer information
    XER::Flux::Terminal::WriteLine(
        XER::Flux::Terminal::OutputStream::kStdout,
        absl::StrCat(
            XER::Flux::Terminal::ForegroundSequence(XER::Flux::Terminal::TerminalColor::kYellow),
            XER::Flux::Terminal::BeginStyleSequence({.bold = true}),
            "\n=== Buffer Information ==="
        )
    );

    XER::Flux::Terminal::WriteLine(
        XER::Flux::Terminal::OutputStream::kStdout,
        absl::StrCat(
            "Buffer Size: ", FormatBytes(buffer->DataSize()), "\n",
            "Buffer Format: ", PixelFormatToString(buffer->Format()), "\n",
            "Buffer Color Space: ", ColorSpaceToString(buffer->GetColorSpace())
        )
    );

    // Print multi-page info
    if (desc.page_count > 1) {
        XER::Flux::Terminal::WriteLine(
            XER::Flux::Terminal::OutputStream::kStdout,
            absl::StrCat(
                XER::Flux::Terminal::ForegroundSequence(XER::Flux::Terminal::TerminalColor::kYellow),
                XER::Flux::Terminal::BeginStyleSequence({.bold = true}),
                "\n=== Multi-Page Information ==="
            )
        );

        XER::Flux::Terminal::WriteLine(
            XER::Flux::Terminal::OutputStream::kStdout,
            absl::StrCat("Pages: ", desc.page_count)
        );
    }

    // Print metadata if available
    if (!desc.metadata.empty()) {
        XER::Flux::Terminal::WriteLine(
            XER::Flux::Terminal::OutputStream::kStdout,
            absl::StrCat(
                XER::Flux::Terminal::ForegroundSequence(XER::Flux::Terminal::TerminalColor::kYellow),
                XER::Flux::Terminal::BeginStyleSequence({.bold = true}),
                "\n=== Metadata ==="
            )
        );

        for (const auto& [key, value] : desc.metadata) {
            XER::Flux::Terminal::WriteLine(
                XER::Flux::Terminal::OutputStream::kStdout,
                absl::StrCat(key, ": ", value)
            );
        }
    }

    return 0;
}
