// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightProcessesExport.h"

#include <QObject>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <verdigris/wobjectdefs.h>

#include <optional>

class IntegrationDataModel;

class AFTERNIGHTPROCESSES_API PresetManager : public QObject
{
    W_OBJECT(PresetManager)

public:
    explicit PresetManager(const QString& storageDir, QObject* parent = nullptr);
    ~PresetManager() override;

    /// Writes the model's configuration as a format 3 preset: an envelope stating its format around a session
    /// document.
    bool savePreset(const QString& name, const IntegrationDataModel& model);
    bool loadPreset(const QString& name, IntegrationDataModel& model);

    /// The model's state with a preset's configuration applied. A preset is read strictly: one of another format, or
    /// one that states none, is refused naming the file, the format found and the one this version reads, and one
    /// holding a value no table knows is refused naming the value; a refused preset is left as it is.
    [[nodiscard]] std::optional<QJsonObject> buildAppliedState(const QString& name, const IntegrationDataModel& model);

    /// Why the last save or load failed; empty after one that did not.
    [[nodiscard]] const QString& lastError() const noexcept;
    QStringList listPresets() const;
    bool deletePreset(const QString& name);
    [[nodiscard]] bool presetExists(const QString& name) const;
    [[nodiscard]] static QString sanitizeName(const QString& name);

private:
    QString m_storageDir;
    QString m_lastError;
};
