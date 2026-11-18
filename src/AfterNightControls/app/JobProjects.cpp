// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"

#include "app/JobProjects.h"

#include "controls/IAppContext.h"

namespace an::ui
{

IJobProjects* jobProjects()
{
    IAppContext* const context = appContext();
    return context != nullptr ? context->getJobProjects() : nullptr;
}

} // namespace an::ui
