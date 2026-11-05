// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightControlsExport.h"

#include "SelectionControllerBase.h"

class ImageSampleSelection
{
public:
    bool operator==(const ImageSampleSelection& other) const
    {
        return pos == other.pos;
    }

    QPoint pos;
    size_t size;

    QRect scaledArea; // Computed from ImageViewWidget
};

typedef std::list<ImageSampleSelection> ImageSampleSelectionList;
typedef std::shared_ptr<ImageSampleSelectionList> ImageSampleSelectionListSPtr;

class ImageSampleSelectorControl
{
public:
    virtual size_t getSampleRadius() const = 0;
    virtual void addSampleSelection(const ImageSampleSelection& selection) = 0;
    virtual void removeSampleSelection(const ImageSampleSelection& selection) = 0;
};

class AFTERNIGHTCONTROLS_API ImageSampleSelectionController
    : public SelectionControllerBase<ImageSampleSelectionController>
{
public:
    ImageSampleSelectionController(ImageViewScrollArea* parent, ImageSampleSelectorControl* control);

    void enableSampleSelection(bool enable);

    bool isSampleSelectionEnabled() const;

    void updateSampleSelection(const ImageSampleSelectionListSPtr& sampleSelection);

    ImageSampleSelectionListSPtr getSampleSelection() const;

    void resetSampleSelection();

    bool onMousePressEvent(QMouseEvent* ev) override;
    void onSelectionEnabledChanged(bool enabled);

    virtual ~ImageSampleSelectionController() = default;

protected:
private:
    void onDraw(QPainter& painter) override;

    void drawSampleSelection(QPainter& painter);

    ImageSampleSelectorControl* m_Control;

    ImageSampleSelectionListSPtr m_ImageSampleSelectionList = std::make_shared<ImageSampleSelectionList>();
};
