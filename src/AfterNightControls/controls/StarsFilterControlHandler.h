// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include <QtWidgets/QMainWindow>
#include "controls/StarFilter.h"

#include "ImageViewControl.h"

class StarsFilterControlHandler;

class StarsFilterControlBase
{
public:
    StarsFilterControlBase(StarsFilterControlHandler* parent);

    virtual void updateFilter() = 0;

    void releaseControlHandler()
    {
        if (m_Process)
        {
            m_Process = nullptr;
        }
    }

    virtual bool isPreviewEnabled() const = 0;

    virtual void forceClose() = 0;

    virtual ~StarsFilterControlBase();

protected:
    StarsFilterControlHandler* m_Process;
};

// The process a star window works for: it holds the detected stars - one list of the engine's detected stars, whichever
// window shows them - and the view they were detected on. Inherited class MUST be a QDialog based object.
class StarsFilterControlHandler
{
public:
    StarsFilterControlHandler() {}

    // The stars the process holds now.
    virtual an::ui::StarListPtr getCurrentStarList()
    {
        return std::make_shared<const an::ui::StarList>();
    }

    // Replaces the stars the process holds; a full update also redraws what depends on them.
    virtual void setCurrentStarList(const an::ui::StarListPtr& starList, bool fullUpdate = false) {}

    virtual ImageViewControl* getViewControl() = 0;
    virtual bool isForProfiledStarmaskRecombination()
    {
        return false;
    }
    virtual bool isPreviewEnabled()
    {
        return false;
    }

    // Shows a selection of stars over the view without changing the stars the process holds.
    virtual void updateStarSelection(const an::ui::StarListPtr& starSelection) {}

    virtual double getCurrentScaleFactor() const
    {
        return 1.0f;
    }

    void attachCurrentFilter(StarsFilterControlBase* filter)
    {
        detachCurrentFilter();
        m_CurrentFilter = filter;
    }

    void detachCurrentFilter()
    {
        if (m_CurrentFilter)
        {
            m_CurrentFilter->releaseControlHandler();
            m_CurrentFilter = nullptr;
        }
    }

    void notifyFilterUpdated()
    {
        if (m_CurrentFilter)
        {
            m_CurrentFilter->updateFilter();
        }
    }

    virtual ~StarsFilterControlHandler()
    {
        detachCurrentFilter();
    }

protected:
    StarsFilterControlBase* m_CurrentFilter = nullptr;
};

inline StarsFilterControlBase::StarsFilterControlBase(StarsFilterControlHandler* parent) : m_Process(parent)
{
    if (m_Process)
    {
        m_Process->attachCurrentFilter(this);
    }
}

inline StarsFilterControlBase::~StarsFilterControlBase()
{
    if (m_Process)
    {
        m_Process->detachCurrentFilter();
    }
}
