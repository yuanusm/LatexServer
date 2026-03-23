#include "pdf_renderer.h"

#include <cstring>

PdfRenderer::PdfRenderer() {
    ctx_ = fz_new_context(nullptr, nullptr, FZ_STORE_UNLIMITED);
    if (ctx_) {
        fz_register_document_handlers(ctx_);
    }
}

PdfRenderer::~PdfRenderer() {
    clearTexture();
    if (ctx_) {
        fz_drop_context(ctx_);
        ctx_ = nullptr;
    }
}

bool PdfRenderer::reloadIfChanged(const std::filesystem::path& pdfPath) {
    if (!ctx_) {
        lastError_ = "MuPDF context initialization failed.";
        return false;
    }
    if (!std::filesystem::exists(pdfPath)) {
        return false;
    }

    const auto writeTime = std::filesystem::last_write_time(pdfPath);
    if (pdfPath == currentPdfPath_ && writeTime == lastPdfWrite_) {
        return true;
    }

    auto page = renderFirstPage(pdfPath);
    if (!page) {
        return false;
    }

    uploadTexture(*page);
    currentPdfPath_ = pdfPath;
    lastPdfWrite_ = writeTime;
    lastError_.clear();
    return true;
}

std::optional<RenderedPage> PdfRenderer::renderFirstPage(const std::filesystem::path& pdfPath) {
    RenderedPage result;
    fz_document* doc = nullptr;
    fz_page* page = nullptr;
    fz_device* dev = nullptr;
    fz_pixmap* pix = nullptr;

    fz_try(ctx_) {
        doc = fz_open_document(ctx_, pdfPath.string().c_str());
        page = fz_load_page(ctx_, doc, 0);
        const fz_rect bounds = fz_bound_page(ctx_, page);
        const fz_matrix matrix = fz_scale(1.5f, 1.5f);
        const fz_rect transformed = fz_transform_rect(bounds, matrix);
        const fz_irect bbox = fz_round_rect(transformed);

        pix = fz_new_pixmap_with_bbox(ctx_, fz_device_rgb(ctx_), bbox, nullptr, 1);
        fz_clear_pixmap_with_value(ctx_, pix, 0xFF);
        dev = fz_new_draw_device(ctx_, matrix, pix);
        fz_run_page(ctx_, page, dev, fz_identity, nullptr);
        fz_close_device(ctx_, dev);

        result.width = fz_pixmap_width(ctx_, pix);
        result.height = fz_pixmap_height(ctx_, pix);
        const int stride = fz_pixmap_stride(ctx_, pix);
        const unsigned char* samples = fz_pixmap_samples(ctx_, pix);
        result.pixels.resize(static_cast<std::size_t>(result.width * result.height * 4));

        for (int y = 0; y < result.height; ++y) {
            for (int x = 0; x < result.width; ++x) {
                const unsigned char* src = samples + y * stride + x * 4;
                unsigned char* dst = result.pixels.data() + (y * result.width + x) * 4;
                std::memcpy(dst, src, 4);
            }
        }
    }
    fz_catch(ctx_) {
        lastError_ = fz_caught_message(ctx_);
        if (dev) fz_drop_device(ctx_, dev);
        if (pix) fz_drop_pixmap(ctx_, pix);
        if (page) fz_drop_page(ctx_, page);
        if (doc) fz_drop_document(ctx_, doc);
        return std::nullopt;
    }

    if (dev) fz_drop_device(ctx_, dev);
    if (pix) fz_drop_pixmap(ctx_, pix);
    if (page) fz_drop_page(ctx_, page);
    if (doc) fz_drop_document(ctx_, doc);
    return result;
}

void PdfRenderer::uploadTexture(const RenderedPage& page) {
    if (!textureId_) {
        glGenTextures(1, &textureId_);
    }

    width_ = page.width;
    height_ = page.height;

    glBindTexture(GL_TEXTURE_2D, textureId_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width_, height_, 0, GL_RGBA, GL_UNSIGNED_BYTE, page.pixels.data());
    glBindTexture(GL_TEXTURE_2D, 0);
}

void PdfRenderer::clearTexture() {
    if (textureId_) {
        glDeleteTextures(1, &textureId_);
        textureId_ = 0;
    }
}
