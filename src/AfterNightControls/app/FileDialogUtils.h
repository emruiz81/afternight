// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "controls/settings/SettingsDefaults.h"
#include "controls/settings/SettingsKeys.h"

#include <algorithm>
#include <array>
#include <memory>
#include <QAbstractItemView>
#include <QApplication>
#include <QComboBox>
#include <QEvent>
#include <QFileIconProvider>
#include <QFileDialog>
#include <QFileInfo>
#include <QGuiApplication>
#include <QLayout>
#include <QObject>
#include <QScreen>
#include <QSizePolicy>
#include <QStyle>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QWidget>

namespace an::ui::dialogs
{

namespace detail
{
inline constexpr std::array<const char*, 28> kSupportedImageSuffixes = {
    "fit", "fits", "fts", "xisf", "tif", "tiff", "png", "jpg", "jpeg", "jpe", "crw", "cr2", "cr3", "nef",
    "dng", "arw",  "sr2", "srf",  "dcr", "k25",  "kdc", "erf", "mrw",  "orf", "pef", "raf", "raw", "x3f"};
inline constexpr std::array<const char*, 9> kSupportedSequenceSuffixes = {"ser", "anseq", "mp4",  "avi", "mov",
                                                                          "mkv", "mpg",   "mpeg", "webm"};

[[nodiscard]] inline bool isSupportedImageSuffix(const QString& suffix)
{
    if (suffix.trimmed().isEmpty())
    {
        return false;
    }

    for (const char* ext : kSupportedImageSuffixes)
    {
        if (suffix.compare(QLatin1String(ext), Qt::CaseInsensitive) == 0)
        {
            return true;
        }
    }

    return false;
}

[[nodiscard]] inline bool isSupportedSequenceSuffix(const QString& suffix)
{
    if (suffix.trimmed().isEmpty())
    {
        return false;
    }

    for (const char* ext : kSupportedSequenceSuffixes)
    {
        if (suffix.compare(QLatin1String(ext), Qt::CaseInsensitive) == 0)
        {
            return true;
        }
    }

    return false;
}

class ThemedFileIconProvider final : public QFileIconProvider
{
public:
    [[nodiscard]] QIcon icon(IconType type) const override
    {
        const QStyle* style = QApplication::style();
        if (!style)
        {
            return QFileIconProvider::icon(type);
        }

        switch (type)
        {
        case QFileIconProvider::Computer:
        case QFileIconProvider::Drive:
            return style->standardIcon(QStyle::SP_ComputerIcon);
        case QFileIconProvider::Folder:
            return style->standardIcon(QStyle::SP_DirIcon);
        case QFileIconProvider::File:
            return style->standardIcon(QStyle::SP_FileIcon);
        default:
            break;
        }

        return QFileIconProvider::icon(type);
    }

    [[nodiscard]] QIcon icon(const QFileInfo& info) const override
    {
        if (info.isDir())
        {
            return icon(QFileIconProvider::Folder);
        }

        const QStyle* style = QApplication::style();
        if (style)
        {
            const QString suffix = info.suffix().trimmed();
            if (isSupportedImageSuffix(suffix))
            {
                return style->standardIcon(QStyle::SP_FileDialogStart);
            }
            if (isSupportedSequenceSuffix(suffix))
            {
                return style->standardIcon(QStyle::SP_FileDialogEnd);
            }
        }

        return icon(QFileIconProvider::File);
    }
};

// Holds a dialog's themed icon provider for as long as the dialog, because QFileDialog::setIconProvider takes no
// ownership. It is made after the dialog's file model, so the model - whose information thread reads the provider - is
// deleted before it.
class FileIconProviderOwner final : public QObject
{
public:
    explicit FileIconProviderOwner(QFileDialog& dialog)
        : QObject(&dialog), m_provider(std::make_unique<ThemedFileIconProvider>())
    {
        setObjectName(QStringLiteral("anFileIconProviderOwner"));
    }

    [[nodiscard]] ThemedFileIconProvider* provider() const
    {
        return m_provider.get();
    }

private:
    std::unique_ptr<ThemedFileIconProvider> m_provider;
};

[[nodiscard]] inline bool isLikelyNameFilterCombo(const QComboBox* combo)
{
    if (!combo || combo->count() <= 0)
    {
        return false;
    }

    int filterLikeEntries = 0;
    const int sampleCount = (std::min)(combo->count(), 5);
    for (int i = 0; i < sampleCount; ++i)
    {
        const QString text = combo->itemText(i);
        if (text.contains(QStringLiteral("(*")) || text.contains(QStringLiteral("*.")))
        {
            ++filterLikeEntries;
        }
    }

    if (filterLikeEntries > 0)
    {
        return true;
    }

    if (combo->isEditable())
    {
        return false;
    }

    const auto* dialog = qobject_cast<const QFileDialog*>(combo->window());
    if (!dialog)
    {
        return false;
    }

    const int filterCount = dialog->nameFilters().size();
    return filterCount > 1 && combo->count() == filterCount;
}

inline void compactNameFilterCombo(QFileDialog& dialog)
{
    const auto combos = dialog.findChildren<QComboBox*>();
    for (QComboBox* combo : combos)
    {
        if (!isLikelyNameFilterCombo(combo))
        {
            continue;
        }

        combo->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Fixed);
        combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        combo->setMinimumContentsLength(20);
        const int maxComboWidth = (std::max)(360, combo->fontMetrics().horizontalAdvance(QStringLiteral("M")) * 34);
        combo->setMaximumWidth(maxComboWidth);
        if (combo->view())
        {
            combo->view()->setTextElideMode(Qt::ElideRight);
        }
    }
}

inline void clampInitialDialogSize(QFileDialog& dialog)
{
    if (!dialog.testOption(QFileDialog::DontUseNativeDialog))
    {
        return;
    }

    QScreen* targetScreen = dialog.screen();
    if (!targetScreen)
    {
        targetScreen = QGuiApplication::primaryScreen();
    }
    if (!targetScreen)
    {
        return;
    }

    const QSize available = targetScreen->availableGeometry().size();
    // Keep default dialogs at a standard starting footprint and prevent
    // full-width expansion on wide displays. Users can still resize freely.
    const int maxWidth = std::clamp((available.width() * 40) / 100, 720, 960);
    const int maxHeight = std::clamp((available.height() * 72) / 100, 500, 760);

    // Lay out what is pending, without invalidating: an invalidated layout posts another layout request, which the
    // clamp's own filter answers by clamping again, and the dialog's event loop then never rests.
    if (QLayout* layout = dialog.layout())
    {
        layout->activate();
    }

    const QSize current = dialog.size();
    const QSize bounded((std::min)(current.width(), maxWidth), (std::min)(current.height(), maxHeight));

    if (bounded.width() < current.width() || bounded.height() < current.height())
    {
        dialog.resize(bounded);
        dialog.setProperty("anFileDialogInitialGeometryClamped", true);
    }
}

class InitialDialogGeometryClamp final : public QObject
{
public:
    explicit InitialDialogGeometryClamp(QFileDialog& dialog) : QObject(&dialog), m_dialog(dialog)
    {
        m_dialog.installEventFilter(this);
        QTimer::singleShot(0, this, [this]() { tryClamp(); });
        QTimer::singleShot(40, this, [this]() { tryClamp(); });
        QTimer::singleShot(160, this, [this]() { tryClamp(); });
    }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (watched != &m_dialog || !event || m_stopped)
        {
            return QObject::eventFilter(watched, event);
        }

        switch (event->type())
        {
        case QEvent::Show:
        case QEvent::LayoutRequest:
        case QEvent::Resize:
            tryClamp();
            break;

        case QEvent::MouseButtonPress:
        case QEvent::KeyPress:
            m_stopped = true;
            break;

        default:
            break;
        }

        return QObject::eventFilter(watched, event);
    }

private:
    void tryClamp()
    {
        if (m_stopped)
        {
            return;
        }

        clampInitialDialogSize(m_dialog);
    }

    QFileDialog& m_dialog;
    bool m_stopped = false;
};
inline void applyNonNativeDialogChrome(QFileDialog& dialog)
{
    if (!dialog.testOption(QFileDialog::DontUseNativeDialog))
    {
        return;
    }

    dialog.setOption(QFileDialog::DontUseCustomDirectoryIcons, true);
    dialog.setSizeGripEnabled(true);
    dialog.setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    if (QLayout* layout = dialog.layout())
    {
        layout->setSizeConstraint(QLayout::SetNoConstraint);
    }

    if (!dialog.property("anFileDialogChromeHooksInstalled").toBool())
    {
        dialog.setProperty("anFileDialogChromeHooksInstalled", true);
        dialog.setIconProvider((new FileIconProviderOwner(dialog))->provider());
        dialog.setProperty("anFileDialogInitialGeometryClamped", false);
        QObject::connect(&dialog, &QFileDialog::filterSelected, &dialog,
                         [&dialog](const QString&) { compactNameFilterCombo(dialog); });
    }

    if (!dialog.property("anFileDialogGeometryClampInstalled").toBool())
    {
        dialog.setProperty("anFileDialogGeometryClampInstalled", true);
        new InitialDialogGeometryClamp(dialog);
    }

    compactNameFilterCombo(dialog);
    QTimer::singleShot(0, &dialog, [&dialog]() {
        compactNameFilterCombo(dialog);
        clampInitialDialogSize(dialog);
    });
}

} // namespace detail

[[nodiscard]] inline bool parseUseSystemFileBrowserSetting(const QString& value)
{
    return value.trimmed() != QStringLiteral("0");
}

[[nodiscard]] inline QFileDialog::Options withDialogBackendPreference(QFileDialog::Options options,
                                                                      const bool useSystemFileBrowser)
{
    if (useSystemFileBrowser)
    {
        return options & ~QFileDialog::DontUseNativeDialog;
    }

    return options | QFileDialog::DontUseNativeDialog;
}

inline void applyCompactOpenFilterPresentation(QFileDialog& dialog)
{
    dialog.setOption(QFileDialog::HideNameFilterDetails, true);
}

template <typename SettingsT> [[nodiscard]] inline bool useSystemFileBrowser(SettingsT* settings)
{
    if (!settings)
    {
        return SettingsDefaults::INTERFACE_USE_SYSTEM_FILE_BROWSER;
    }

    const QString value = settings->select(SettingsKeys::INTERFACE_USE_SYSTEM_FILE_BROWSER,
                                           SettingsDefaults::INTERFACE_USE_SYSTEM_FILE_BROWSER ? QStringLiteral("1")
                                                                                               : QStringLiteral("0"));
    return parseUseSystemFileBrowserSetting(value);
}

template <typename SettingsT>
[[nodiscard]] inline QFileDialog::Options resolveDialogOptions(SettingsT* settings,
                                                               QFileDialog::Options options = QFileDialog::Options())
{
    return withDialogBackendPreference(options, useSystemFileBrowser(settings));
}

template <typename SettingsT> inline void applyDialogBackendPreference(QFileDialog& dialog, SettingsT* settings)
{
    dialog.setOptions(resolveDialogOptions(settings, dialog.options()));
    detail::applyNonNativeDialogChrome(dialog);
}

template <typename AppT> [[nodiscard]] inline auto* settingsFromApp(AppT* app)
{
    return app ? app->getGlobalSettingsPersistence() : nullptr;
}

template <typename AppT> inline void applyDialogBackendPreferenceForApp(QFileDialog& dialog, AppT* app)
{
    applyDialogBackendPreference(dialog, settingsFromApp(app));
}

[[nodiscard]] inline QStringList supportedImageOpenNameFilterPatterns()
{
    QStringList patterns;
    patterns.reserve(static_cast<int>(detail::kSupportedImageSuffixes.size()));
    for (const char* suffix : detail::kSupportedImageSuffixes)
    {
        patterns << QStringLiteral("*.%1").arg(QLatin1String(suffix));
    }
    return patterns;
}

[[nodiscard]] inline QStringList filterSupportedImageOpenPaths(const QStringList& paths,
                                                               const bool acceptSequences = false)
{
    QStringList supportedPaths;
    supportedPaths.reserve(paths.size());

    for (const QString& path : paths)
    {
        const QString suffix = QFileInfo(path).suffix();
        if (detail::isSupportedImageSuffix(suffix) || (acceptSequences && detail::isSupportedSequenceSuffix(suffix)))
        {
            supportedPaths.push_back(path);
        }
    }

    return supportedPaths;
}

[[nodiscard]] inline QString supportedFormatsNameFilter(const QStringList& patterns)
{
    return QObject::tr("All supported formats (%1)").arg(patterns.join(QLatin1Char(' ')));
}

[[nodiscard]] inline QStringList supportedSequenceOpenNameFilterPatterns()
{
    QStringList patterns;
    patterns.reserve(static_cast<int>(detail::kSupportedSequenceSuffixes.size()));
    for (const char* suffix : detail::kSupportedSequenceSuffixes)
    {
        patterns << QStringLiteral("*.%1").arg(QLatin1String(suffix));
    }
    return patterns;
}

[[nodiscard]] inline QStringList supportedImageOpenNameFilters()
{
    const QString allSupported = supportedImageOpenNameFilterPatterns().join(QLatin1Char(' '));
    return {QObject::tr("All Supported Images (%1)").arg(allSupported),
            QObject::tr("FITS/XISF Images (*.fit *.fits *.fts *.xisf)"),
            QObject::tr("TIFF Images (*.tif *.tiff)"),
            QObject::tr("PNG Images (*.png)"),
            QObject::tr("JPEG Images (*.jpg *.jpeg *.jpe)"),
            QObject::tr("Camera RAW Images (*.crw *.cr2 *.cr3 *.nef *.dng *.arw *.sr2 *.srf *.dcr *.k25 *.kdc "
                        "*.erf *.mrw *.orf *.pef *.raf *.raw *.x3f)")};
}

[[nodiscard]] inline QString supportedImageOpenNameFilter()
{
    return supportedImageOpenNameFilters().join(QStringLiteral(";;"));
}

inline void applySupportedImageOpenFilters(QFileDialog& dialog)
{
    const QStringList filters = supportedImageOpenNameFilters();
    applyCompactOpenFilterPresentation(dialog);
    dialog.setNameFilters(filters);
    if (!filters.isEmpty())
    {
        dialog.selectNameFilter(filters.constFirst());
    }
}

template <typename SettingsT>
[[nodiscard]] inline QString getOpenFileName(SettingsT* settings, QWidget* parent, const QString& caption = QString(),
                                             const QString& dir = QString(), const QString& filter = QString(),
                                             QString* selectedFilter = nullptr,
                                             QFileDialog::Options options = QFileDialog::Options())
{
    QFileDialog dialog(parent, caption, dir, filter);
    dialog.setAcceptMode(QFileDialog::AcceptOpen);
    dialog.setFileMode(QFileDialog::ExistingFile);
    applyCompactOpenFilterPresentation(dialog);
    dialog.setOptions(resolveDialogOptions(settings, options));
    detail::applyNonNativeDialogChrome(dialog);

    if (selectedFilter && !selectedFilter->trimmed().isEmpty())
    {
        dialog.selectNameFilter(*selectedFilter);
    }

    if (dialog.exec() != QDialog::Accepted)
    {
        return {};
    }

    if (selectedFilter)
    {
        *selectedFilter = dialog.selectedNameFilter();
    }

    const QStringList files = dialog.selectedFiles();
    return files.isEmpty() ? QString() : files.constFirst();
}

template <typename AppT>
[[nodiscard]] inline QString getOpenFileNameForApp(AppT* app, QWidget* parent, const QString& caption = QString(),
                                                   const QString& dir = QString(), const QString& filter = QString(),
                                                   QString* selectedFilter = nullptr,
                                                   QFileDialog::Options options = QFileDialog::Options())
{
    return getOpenFileName(settingsFromApp(app), parent, caption, dir, filter, selectedFilter, options);
}

template <typename SettingsT>
[[nodiscard]] inline QStringList getOpenFileNames(SettingsT* settings, QWidget* parent,
                                                  const QString& caption = QString(), const QString& dir = QString(),
                                                  const QString& filter = QString(), QString* selectedFilter = nullptr,
                                                  QFileDialog::Options options = QFileDialog::Options())
{
    QFileDialog dialog(parent, caption, dir, filter);
    dialog.setAcceptMode(QFileDialog::AcceptOpen);
    dialog.setFileMode(QFileDialog::ExistingFiles);
    applyCompactOpenFilterPresentation(dialog);
    dialog.setOptions(resolveDialogOptions(settings, options));
    detail::applyNonNativeDialogChrome(dialog);

    if (selectedFilter && !selectedFilter->trimmed().isEmpty())
    {
        dialog.selectNameFilter(*selectedFilter);
    }

    if (dialog.exec() != QDialog::Accepted)
    {
        return {};
    }

    if (selectedFilter)
    {
        *selectedFilter = dialog.selectedNameFilter();
    }

    return dialog.selectedFiles();
}

template <typename AppT>
[[nodiscard]] inline QStringList
getOpenFileNamesForApp(AppT* app, QWidget* parent, const QString& caption = QString(), const QString& dir = QString(),
                       const QString& filter = QString(), QString* selectedFilter = nullptr,
                       QFileDialog::Options options = QFileDialog::Options())
{
    return getOpenFileNames(settingsFromApp(app), parent, caption, dir, filter, selectedFilter, options);
}

template <typename SettingsT>
[[nodiscard]] inline QString getSaveFileName(SettingsT* settings, QWidget* parent, const QString& caption = QString(),
                                             const QString& dir = QString(), const QString& filter = QString(),
                                             QString* selectedFilter = nullptr,
                                             QFileDialog::Options options = QFileDialog::Options())
{
    QFileDialog dialog(parent, caption, dir, filter);
    dialog.setAcceptMode(QFileDialog::AcceptSave);
    dialog.setFileMode(QFileDialog::AnyFile);
    dialog.setOptions(resolveDialogOptions(settings, options));
    detail::applyNonNativeDialogChrome(dialog);

    if (selectedFilter && !selectedFilter->trimmed().isEmpty())
    {
        dialog.selectNameFilter(*selectedFilter);
    }

    if (dialog.exec() != QDialog::Accepted)
    {
        return {};
    }

    if (selectedFilter)
    {
        *selectedFilter = dialog.selectedNameFilter();
    }

    const QStringList files = dialog.selectedFiles();
    return files.isEmpty() ? QString() : files.constFirst();
}

template <typename AppT>
[[nodiscard]] inline QString getSaveFileNameForApp(AppT* app, QWidget* parent, const QString& caption = QString(),
                                                   const QString& dir = QString(), const QString& filter = QString(),
                                                   QString* selectedFilter = nullptr,
                                                   QFileDialog::Options options = QFileDialog::Options())
{
    return getSaveFileName(settingsFromApp(app), parent, caption, dir, filter, selectedFilter, options);
}

template <typename SettingsT>
[[nodiscard]] inline QString getExistingDirectory(SettingsT* settings, QWidget* parent = nullptr,
                                                  const QString& caption = QString(), const QString& dir = QString(),
                                                  QFileDialog::Options options = QFileDialog::ShowDirsOnly)
{
    QFileDialog dialog(parent, caption, dir);
    dialog.setAcceptMode(QFileDialog::AcceptOpen);
    dialog.setFileMode(QFileDialog::Directory);
    dialog.setOptions(resolveDialogOptions(settings, options));
    detail::applyNonNativeDialogChrome(dialog);

    if (dialog.exec() != QDialog::Accepted)
    {
        return {};
    }

    const QStringList files = dialog.selectedFiles();
    return files.isEmpty() ? QString() : files.constFirst();
}

template <typename AppT>
[[nodiscard]] inline QString
getExistingDirectoryForApp(AppT* app, QWidget* parent = nullptr, const QString& caption = QString(),
                           const QString& dir = QString(), QFileDialog::Options options = QFileDialog::ShowDirsOnly)
{
    return getExistingDirectory(settingsFromApp(app), parent, caption, dir, options);
}

} // namespace an::ui::dialogs
