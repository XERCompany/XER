#pragma once

#include "../openai_types.h"
#include "../client/openai_client.h"
#include <string>
#include <vector>
#include <memory>
#include <functional>

namespace Content::OpenAI::Image {

// Image format
enum class ImageFormat {
    kPNG,
    kJPEG,
    kWebP,
    kGIF
};

// Image processing operation
enum class ImageOperation {
    kResize,
    kCrop,
    kRotate,
    kFlip,
    kGrayscale,
    kBlur,
    kSharpen,
    kAdjustBrightness,
    kAdjustContrast,
    kGenerate
};

// Image data structure
struct ImageData {
    std::vector<uint8_t> pixels;
    int width;
    int height;
    int channels;  // 3 for RGB, 4 for RGBA
    ImageFormat format;
    std::string mime_type;
};

// Image processing parameters
struct ImageProcessingParams {
    ImageOperation operation;
    int target_width = 0;
    int target_height = 0;
    int crop_x = 0;
    int crop_y = 0;
    int crop_width = 0;
    int crop_height = 0;
    float rotation_degrees = 0.0f;
    bool flip_horizontal = false;
    bool flip_vertical = false;
    float blur_radius = 1.0f;
    float sharpen_amount = 1.0f;
    float brightness_adjust = 0.0f;
    float contrast_adjust = 0.0f;
};

// Image analysis result
struct ImageAnalysisResult {
    std::string description;
    std::vector<std::string> objects_detected;
    std::vector<std::string> labels;
    float confidence_score;
    std::string dominant_color;
    std::map<std::string, float> attributes;
};

// Image processor interface
class IImageProcessor {
public:
    virtual ~IImageProcessor() = default;

    // Image generation
    virtual std::future<ImageResponse> GenerateImage(
        const ImageGenerationParams& params
    ) = 0;

    // Image analysis
    virtual std::future<ImageAnalysisResult> AnalyzeImage(
        const ImageData& image
    ) = 0;

    // Image processing
    virtual std::future<ImageData> ProcessImage(
        const ImageData& image,
        const ImageProcessingParams& params
    ) = 0;

    // Format conversion
    virtual std::future<ImageData> ConvertFormat(
        const ImageData& image,
        ImageFormat target_format
    ) = 0;

    // Utility functions
    virtual bool ValidateImageData(const ImageData& image) const = 0;
    virtual std::string GetImageInfo(const ImageData& image) const = 0;
};

// OpenAI image processor
class OpenAIImageProcessor : public IImageProcessor {
public:
    explicit OpenAIImageProcessor(
        std::shared_ptr<Client::OpenAIClient> client
    );
    ~OpenAIImageProcessor() override;

    std::future<ImageResponse> GenerateImage(
        const ImageGenerationParams& params
    ) override;

    std::future<ImageAnalysisResult> AnalyzeImage(
        const ImageData& image
    ) override;

    std::future<ImageData> ProcessImage(
        const ImageData& image,
        const ImageProcessingParams& params
    ) override;

    std::future<ImageData> ConvertFormat(
        const ImageData& image,
        ImageFormat target_format
    ) override;

    bool ValidateImageData(const ImageData& image) const override;
    std::string GetImageInfo(const ImageData& image) const override;

private:
    std::shared_ptr<Client::OpenAIClient> client_;
    std::string ImageToBase64(const ImageData& image) const;
    ImageData Base64ToImage(const std::string& base64, ImageFormat format) const;
};

// Local image processor (using existing audio/DSP infrastructure)
class LocalImageProcessor : public IImageProcessor {
public:
    LocalImageProcessor();
    ~LocalImageProcessor() override;

    std::future<ImageResponse> GenerateImage(
        const ImageGenerationParams& params
    ) override;

    std::future<ImageAnalysisResult> AnalyzeImage(
        const ImageData& image
    ) override;

    std::future<ImageData> ProcessImage(
        const ImageData& image,
        const ImageProcessingParams& params
    ) override;

    std::future<ImageData> ConvertFormat(
        const ImageData& image,
        ImageFormat target_format
    ) override;

    bool ValidateImageData(const ImageData& image) const override;
    std::string GetImageInfo(const ImageData& image) const override;

private:
    // Would integrate with existing image processing libraries
    // For now, placeholder
};

// Image processor factory
class ImageProcessorFactory {
public:
    static std::unique_ptr<IImageProcessor> CreateOpenAIProcessor(
        std::shared_ptr<Client::OpenAIClient> client
    );

    static std::unique_ptr<IImageProcessor> CreateLocalProcessor();

    static std::unique_ptr<IImageProcessor> CreateDefault();
};

// Image utilities
class ImageUtils {
public:
    static ImageData LoadFromFile(const std::string& file_path);
    static bool SaveToFile(const ImageData& image, const std::string& file_path);
    static std::string GetMimeType(ImageFormat format);
    static ImageFormat FormatFromMimeType(const std::string& mime_type);
    static ImageData CreateEmpty(int width, int height, int channels = 4);
    static ImageData ResizeSimple(const ImageData& image, int new_width, int new_height);
};

}  // namespace Content::OpenAI::Image
