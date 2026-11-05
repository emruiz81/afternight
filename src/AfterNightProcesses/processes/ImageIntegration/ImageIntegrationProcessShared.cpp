// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/**
 * @file ImageIntegrationProcessShared.cpp
 * @brief Implementation of the shared ImageIntegrationProcess helpers.
 *
 * Moved out of ImageIntegrationProcess.cpp with the import and preset helpers. The two default
 * arguments - `detailedText` and `checkBoxText` - are declared in the header instead of here, which
 * is where they have to be once the declaration and the definition are in different translation
 * units.
 */
#include "stdafx.hpp"
#include "processes/ImageIntegration/ImageIntegrationProcessShared.h"

#include "controls/IAppContext.h"
#include "controls/settings/SettingsKeys.h"
#include "processes/ImageIntegration/ImageIntegrationConstants.h"
#include "processes/ImageIntegration/workflow/stages/RegistrationMetricsSchema.h"
#include "processes/ProcessCommonUtils.h"
#include "session/IEngineSessionProvider.h"

#include <AfterNightConsumerRuntime/EngineSession.h>
#include <AfterNightEngine/IO/CodecFormat.h>
#include <AfterNightEngine/IO/CodecRegistry.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightPlatform/Logging.h>

#include <QCheckBox>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QInputDialog>
#include <QLineEdit>
#include <QSettings>
#include <QWidget>

#include <optional>

namespace ImageIntegration::detail
{

QIcon integrationIcon(const QString& key)
{
    if (key.trimmed().isEmpty())
    {
        return QIcon();
    }

    if (const auto iconProvider = process_utils::currentIconProvider())
    {
        return iconProvider->getIcon(key);
    }

    return QIcon();
}

QIcon integrationIconOrFallback(const QWidget* widget, const QString& key, const QStyle::StandardPixmap fallback)
{
    if (const QIcon icon = integrationIcon(key); !icon.isNull())
    {
        return icon;
    }

    return widget && widget->style() ? widget->style()->standardIcon(fallback) : QIcon();
}

QString rememberedExistingPath(const QString& contextKey)
{
    QString path = process_utils::selectPath(contextKey);
    if (!path.isEmpty() && !QDir(path).exists())
    {
        path.clear();
    }
    return path;
}

void rememberDirectoryForPath(const QString& contextKey, const QString& path)
{
    if (!path.trimmed().isEmpty())
    {
        process_utils::rememberPath(contextKey, QFileInfo(path).absolutePath());
    }
}

void rememberDirectoryPath(const QString& contextKey, const QString& directory)
{
    if (!directory.trimmed().isEmpty())
    {
        process_utils::rememberPath(contextKey, directory);
    }
}

void openNonBlockingMessageBox(QWidget* parent, QMessageBox::Icon icon, const QString& title, const QString& text,
                               const QString& detailedText)
{
    auto* messageBox = new QMessageBox(icon, title, text, QMessageBox::Ok, parent);
    messageBox->setAttribute(Qt::WA_DeleteOnClose);
    if (!detailedText.isEmpty())
    {
        messageBox->setDetailedText(detailedText);
    }
    messageBox->open();
}

void openNonBlockingQuestionBox(QWidget* parent, const QString& title, const QString& text,
                                std::function<void(QMessageBox::StandardButton, bool)> finished,
                                const QString& checkBoxText)
{
    auto* messageBox = new QMessageBox(QMessageBox::Question, title, text, QMessageBox::Yes | QMessageBox::No, parent);
    messageBox->setAttribute(Qt::WA_DeleteOnClose);
    messageBox->setDefaultButton(QMessageBox::No);

    QCheckBox* checkBox = nullptr;
    if (!checkBoxText.isEmpty())
    {
        checkBox = new QCheckBox(checkBoxText, messageBox);
        messageBox->setCheckBox(checkBox);
    }

    QObject::connect(messageBox, &QDialog::finished, messageBox,
                     [messageBox, checkBox, finished = std::move(finished)](int result) mutable {
                         QMessageBox::StandardButton button = messageBox->standardButton(messageBox->clickedButton());
                         if (button == QMessageBox::NoButton)
                         {
                             button = static_cast<QMessageBox::StandardButton>(result);
                         }
                         finished(button, checkBox && checkBox->isChecked());
                     });
    messageBox->open();
}

void openNonBlockingTextInput(QWidget* parent, const QString& title, const QString& label,
                              std::function<void(const QString&)> accepted)
{
    auto* inputDialog = new QInputDialog(parent);
    inputDialog->setAttribute(Qt::WA_DeleteOnClose);
    inputDialog->setInputMode(QInputDialog::TextInput);
    inputDialog->setTextEchoMode(QLineEdit::Normal);
    inputDialog->setWindowTitle(title);
    inputDialog->setLabelText(label);
    inputDialog->setTextValue(QString());
    QObject::connect(inputDialog, &QInputDialog::textValueSelected, inputDialog,
                     [accepted = std::move(accepted)](const QString& text) {
                         if (accepted)
                         {
                             accepted(text);
                         }
                     });
    inputDialog->open();
}

QString outputDirectoryRefusal(const QString& outputDirectory)
{
    const QString directory = outputDirectory.trimmed();
    if (directory.isEmpty())
    {
        return {};
    }

    const QString metricsPath = QDir(directory).filePath(QStringLiteral("registration_metrics.db"));
    if (!QFileInfo::exists(metricsPath))
    {
        return {};
    }
    QString openError;
    const auto inspection = stage_utils::inspectRegistrationMetricsSchema(
        metricsPath, QStringLiteral("ImageIntegrationSchemaCheck"), &openError);
    if (!inspection.opened)
    {
        if (!openError.isEmpty())
        {
            LOG_WARNING("ImageIntegrationProcess: the registration records '{}' could not be inspected: {}",
                        metricsPath.toStdString(), openError.toStdString());
        }
        return {};
    }
    return stage_utils::registrationMetricsFormatRefusal(inspection, metricsPath);
}

bool checkOutputDirectoryBeforeRun(QWidget* parent, const QString& outputDirectory)
{
    const QString refusal = outputDirectoryRefusal(outputDirectory);
    if (refusal.isEmpty())
    {
        return true;
    }
    LOG_WARNING("ImageIntegrationProcess: {} The records were not changed, and the run was not started.",
                refusal.toStdString());
    openNonBlockingMessageBox(parent, QMessageBox::Warning,
                              QCoreApplication::translate("ImageIntegrationProcess", "Registration Records"),
                              QCoreApplication::translate("ImageIntegrationProcess",
                                                          "%1\n\nThey were not changed, and the run was not started.")
                                  .arg(refusal));
    return false;
}

void notifyWarnOrDialog(QWidget* parent, const QString& title, const QString& text)
{
    if (process_utils::currentDialogService())
    {
        process_utils::notifyWarnMessage(parent, title, text);
        return;
    }

    openNonBlockingMessageBox(parent, QMessageBox::Warning, title, text);
}

void notifyInfoOrDialog(QWidget* parent, const QString& title, const QString& text)
{
    if (process_utils::currentDialogService())
    {
        process_utils::notifyInfoMessage(parent, title, text);
        return;
    }

    openNonBlockingMessageBox(parent, QMessageBox::Information, title, text);
}

void notifyErrorOrDialog(QWidget* parent, const QString& title, const QString& text)
{
    if (process_utils::currentDialogService())
    {
        process_utils::notifyErrorMessage(parent, title, text);
        return;
    }

    openNonBlockingMessageBox(parent, QMessageBox::Critical, title, text);
}

QString frameTypeLabel(FrameType type)
{
    switch (type)
    {
    case FrameType::Bias:
        return QObject::tr("Bias");
    case FrameType::Dark:
        return QObject::tr("Darks");
    case FrameType::Flat:
        return QObject::tr("Flats");
    case FrameType::Light:
    default:
        return QObject::tr("Lights");
    }
}

QString importSummaryText(const std::map<FrameType, std::vector<FrameInfo>>& groupedInfos, int skipped)
{
    QStringList parts;
    int totalImported = 0;
    for (const auto& [type, infos] : groupedInfos)
    {
        const int count = static_cast<int>(infos.size());
        if (count <= 0)
        {
            continue;
        }
        totalImported += count;
        parts << QObject::tr("%1 %2").arg(QString::number(count), frameTypeLabel(type));
    }

    if (totalImported <= 0)
    {
        return QObject::tr("No files were imported.");
    }

    QString msg = QObject::tr("Imported: %1").arg(parts.join(QStringLiteral(", ")));
    if (skipped > 0)
    {
        msg += QObject::tr("\nSkipped: %1").arg(QString::number(skipped));
    }
    return msg;
}

const an::consumer::EngineSession* applicationEngineSession()
{
    an::ui::IAppContext* context = an::ui::appContext();
    an::ui::IEngineSessionProvider* provider = context != nullptr ? context->getEngineSessionProvider() : nullptr;
    return provider != nullptr ? &provider->engineSession() : nullptr;
}

bool isReadableImagePath(const QString& path)
{
    const an::CodecFormat format = an::codecFormatFromPath(path.toUtf8().toStdString());
    if (format == an::CodecFormat::Unknown)
    {
        return false;
    }
    const an::consumer::EngineSession* session = applicationEngineSession();
    if (session == nullptr)
    {
        return true;
    }
    // A hold reaches the session's engine context without asking to be admitted, so the GUI thread never waits.
    const an::Result<an::consumer::SessionHold> hold = session->hold();
    if (!hold.hasValue())
    {
        return false;
    }
    const an::CodecRegistry* registry = hold->engine().codecRegistry();
    return registry != nullptr && registry->capabilities(format).has_value();
}

} // namespace ImageIntegration::detail
