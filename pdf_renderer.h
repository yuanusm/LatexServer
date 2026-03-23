#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <GL/gl.h>
#include <mupdf/fitz.h>

struct RenderedPage {
    int width = 0;
    int height = 0;
    std::vector<unsigned char> pixels;
};

class PdfRenderer {
public:
    PdfRenderer();
    ~PdfRenderer();

    PdfRenderer(const PdfRenderer&) = delete;
    PdfRenderer& operator=(const PdfRenderer&) = delete;

    bool reloadIfChanged(const std::filesystem::path& pdfPath);
    GLuint texture() const { return textureId_; }
    int width() const { return width_; }
    int height() const { return height_; }
    const std::string& lastError() const { return lastError_; }
    bool hasTexture() const { return textureId_ != 0; }

private:
    std::optional<RenderedPage> renderFirstPage(const std::filesystem::path& pdfPath);
    void uploadTexture(const RenderedPage& page);
    void clearTexture();

    fz_context* ctx_ = nullptr;
    std::filesystem::file_time_type lastPdfWrite_{};
    std::filesystem::path currentPdfPath_;
    GLuint textureId_ = 0;
    int width_ = 0;
    int height_ = 0;
    std::string lastError_;
};
