// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The Annotate Image window over a solved image's view: the choices of an annotated picture - how much of the
/// catalogues it names, its layers, its palette and sizes, its panel and its size - beside a preview of the picture
/// they make, and the picture exported to a file or opened as an image of its own.
///
/// **The preview is the picture, smaller.** It draws the scene and layout the export draws, over the image presented
/// as the picture presents it, so what the preview shows is what the file holds. Zoomed in, it asks for the image
/// presented at the size it now shows, and the annotations, drawn as outlines, stay sharp at every zoom.
///
/// **Nothing waits.** The catalogues' entries are read once for the solution, the scene and its layout are built
/// again for each change of the choices, the preview's image is presented for each stretch and size, and the picture
/// is drawn and written, each on a worker; the window shows the latest answer to land and drops one that a later
/// request, a change of the image or the window's closing made stale. The choices but the title are kept for the next
/// window.
#pragma once

#include "AfterNightControlsExport.h"
#include "app/BundledCatalogs.h"
#include "controls/ImageViewControlListener.h"
#include "controls/annotation/AnnotationSettings.h"

#include <QDialog>
#include <verdigris/wobjectdefs.h>

#include <cstddef>
#include <memory>

class ImageViewControl;
class QCloseEvent;

namespace an::ui
{

struct AnnotationLayout;
struct AnnotationScene;

class AFTERNIGHTCONTROLS_API AnnotateImageControl : public QDialog, public ImageViewControlListener
{
    W_OBJECT(AnnotateImageControl)

public:
    /// Opens the window over a view, with the choices kept from the last window.
    ///
    /// @param viewControl the view of the solved image.
    /// @param parent the window's parent; the view when null.
    explicit AnnotateImageControl(ImageViewControl* viewControl, QWidget* parent = nullptr);
    ~AnnotateImageControl() override;

    void onImageLoaded(ImageViewControl* ctrl) override;
    void onImageUpdated(ImageViewControl* ctrl) override;
    void onImageRenamed(ImageViewControl* ctrl) override;
    void onImageViewControlDestroyed(ImageViewControl* ctrl) override;

    /// The choices the window shows.
    [[nodiscard]] AnnotationSettings settings() const;

    /// Shows choices, as a person choosing them would, and lays the preview out again.
    void setSettings(const AnnotationSettings& settings);

    /// Reads the catalogues' text from another source than the product's resources, and asks for the field's
    /// entries again.
    void setCatalogSource(BundledCatalogSource source);

    /// Whether a request is on its way: the catalogues, the scene, the preview's image or a picture.
    [[nodiscard]] bool isWorking() const;

    /// The scene the preview draws now, or null before the first lands.
    [[nodiscard]] std::shared_ptr<const AnnotationScene> scene() const;

    /// The layout the preview draws now, or null before the first lands.
    [[nodiscard]] std::shared_ptr<const AnnotationLayout> layout() const;

    /// Draws the picture the choices make and writes it to a file, its container named by its extension, through the
    /// export settings; the window says when it is written or why not.
    ///
    /// @param path the file.
    void exportTo(const QString& path);

    /// Draws the picture the choices make and opens it in a view of its own.
    void openAsImage();

    /// What the window last said of an export or an opened picture: empty while it has said nothing.
    [[nodiscard]] QString lastOutcome() const;

    /// How many pictures were written or opened.
    [[nodiscard]] std::size_t picturesMade() const;

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace an::ui
