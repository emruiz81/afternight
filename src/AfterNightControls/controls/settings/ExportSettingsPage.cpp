// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "ExportSettingsPage.h"
#include "SettingsKeys.h"
#include "SettingsDefaults.h"
#include "../GlobalSettingsPersistence.h"
#include "app/FilePolicies.h"
#include "controls/IAppContext.h"

#include <verdigris/wobjectimpl.h>

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QComboBox>
#include <QSlider>
#include <QCheckBox>
#include <QRadioButton>
#include <QLabel>
#include <QButtonGroup>

W_OBJECT_IMPL(ExportSettingsPage)

namespace
{

QIcon exportSettingsPageIcon(const QString& iconName)
{
    if (auto* context = an::ui::appContext())
    {
        if (const auto iconProvider = context->getIconProvider())
        {
            return iconProvider->getIcon(iconName);
        }
    }

    return {};
}

} // anonymous namespace

ExportSettingsPage::ExportSettingsPage(GlobalSettingsPersistence* settingsPersistence, QWidget* parent)
    : SettingsPage(parent), m_Settings(settingsPersistence)
{
    setupUi();
    connectSignals();
}

QString ExportSettingsPage::pageTitle() const
{
    return tr("Export");
}

QIcon ExportSettingsPage::pageIcon() const
{
    return exportSettingsPageIcon(QStringLiteral("file_save"));
}

QStringList ExportSettingsPage::searchKeywords() const
{
    return QStringList{tr("Export"),   tr("Save"),     tr("TIFF"),        tr("XISF"),    tr("JPEG"),
                       tr("JPG"),      tr("PNG"),      tr("Compression"), tr("Quality"), tr("EXIF"),
                       tr("Metadata"), tr("Checksum"), tr("Zstandard"),   tr("LZ4")};
}

void ExportSettingsPage::setupUi()
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(16);
    mainLayout->setContentsMargins(0, 0, 0, 0);

    // === TIFF Settings Section ===
    auto* tiffGroup = new QGroupBox(tr("TIFF Settings"), this);
    auto* tiffLayout = new QVBoxLayout(tiffGroup);
    tiffLayout->setSpacing(12);

    auto* tiffFormLayout = new QFormLayout();
    m_TiffCompressionCombo = new QComboBox(tiffGroup);
    m_TiffCompressionCombo->addItem(tr("None (Fastest, Largest Files)"), "none");
    m_TiffCompressionCombo->addItem(tr("LZW (Lossless, Good Compression)"), "lzw");
    m_TiffCompressionCombo->addItem(tr("ZIP/Deflate (Lossless, Better Compression)"), "zip");
    m_TiffCompressionCombo->addItem(tr("JPEG (Lossy, Smallest Files)"), "jpeg");
    m_TiffCompressionCombo->setToolTip(tr("Default compression algorithm for TIFF file exports"));
    tiffFormLayout->addRow(tr("Default Compression:"), m_TiffCompressionCombo);
    tiffLayout->addLayout(tiffFormLayout);

    mainLayout->addWidget(tiffGroup);

    // === XISF Settings Section ===
    auto* xisfGroup = new QGroupBox(tr("XISF Settings"), this);
    auto* xisfLayout = new QVBoxLayout(xisfGroup);
    xisfLayout->setSpacing(12);

    auto* xisfFormLayout = new QFormLayout();
    m_XisfCompressionCombo = new QComboBox(xisfGroup);
    m_XisfCompressionCombo->setObjectName(QStringLiteral("xisfCompression"));
    m_XisfCompressionCombo->addItem(tr("None (Read by Every XISF Reader)"), "none");
    m_XisfCompressionCombo->addItem(tr("zlib (Lossless)"), "zlib");
    m_XisfCompressionCombo->addItem(tr("LZ4 (Lossless, Fastest)"), "lz4");
    m_XisfCompressionCombo->addItem(tr("LZ4-HC (Lossless, Smaller, Slower to Write)"), "lz4hc");
    m_XisfCompressionCombo->addItem(tr("Zstandard (Lossless, Smallest)"), "zstd");
    m_XisfCompressionCombo->setToolTip(tr("Default compression of the pixel data of XISF file exports"));
    xisfFormLayout->addRow(tr("Default Compression:"), m_XisfCompressionCombo);

    m_XisfByteShuffleCheck = new QCheckBox(tr("Shuffle the bytes of each sample before compressing"), xisfGroup);
    m_XisfByteShuffleCheck->setObjectName(QStringLiteral("xisfByteShuffle"));
    m_XisfByteShuffleCheck->setToolTip(
        tr("Store every sample's first byte, then every sample's second, which makes 16-bit and floating point "
           "images compress better"));
    xisfFormLayout->addRow(QString(), m_XisfByteShuffleCheck);

    m_XisfChecksumCombo = new QComboBox(xisfGroup);
    m_XisfChecksumCombo->setObjectName(QStringLiteral("xisfChecksum"));
    m_XisfChecksumCombo->addItem(tr("None"), "none");
    m_XisfChecksumCombo->addItem(tr("SHA-256"), "sha256");
    m_XisfChecksumCombo->setToolTip(tr("A checksum of the pixel data, which a reader verifies before it uses them"));
    xisfFormLayout->addRow(tr("Checksum:"), m_XisfChecksumCombo);
    xisfLayout->addLayout(xisfFormLayout);

    auto* xisfNote = createInfoLabel(
        tr("zlib and LZ4 are read by every XISF reader, Zstandard only by readers of the specification's "
           "Revision 1. Compressed files are written in pieces a reader can decode one at a time."));
    xisfLayout->addWidget(xisfNote);

    mainLayout->addWidget(xisfGroup);

    // === JPEG Settings Section ===
    auto* jpegGroup = new QGroupBox(tr("JPEG Settings"), this);
    auto* jpegLayout = new QVBoxLayout(jpegGroup);
    jpegLayout->setSpacing(12);

    auto* jpegSliderLayout = new QHBoxLayout();
    m_JpegLowLabel = new QLabel(tr("Low"), jpegGroup);
    applySecondaryTextStyle(m_JpegLowLabel);
    jpegSliderLayout->addWidget(m_JpegLowLabel);

    m_JpegQualitySlider = new QSlider(Qt::Horizontal, jpegGroup);
    m_JpegQualitySlider->setRange(1, 100);
    m_JpegQualitySlider->setTickPosition(QSlider::TicksBelow);
    m_JpegQualitySlider->setTickInterval(10);
    m_JpegQualitySlider->setToolTip(tr("JPEG compression quality (higher = better quality, larger files)"));
    jpegSliderLayout->addWidget(m_JpegQualitySlider, 1);

    m_JpegHighLabel = new QLabel(tr("High"), jpegGroup);
    applySecondaryTextStyle(m_JpegHighLabel);
    jpegSliderLayout->addWidget(m_JpegHighLabel);

    m_JpegQualityLabel = new QLabel(jpegGroup);
    m_JpegQualityLabel->setMinimumWidth(40);
    m_JpegQualityLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    jpegSliderLayout->addWidget(m_JpegQualityLabel);

    auto* jpegFormLayout = new QFormLayout();
    jpegFormLayout->addRow(tr("Quality:"), jpegSliderLayout);
    jpegLayout->addLayout(jpegFormLayout);

    auto* jpegNote = createInfoLabel(tr("Higher quality results in larger file sizes."));
    jpegLayout->addWidget(jpegNote);

    mainLayout->addWidget(jpegGroup);

    // === PNG Settings Section ===
    auto* pngGroup = new QGroupBox(tr("PNG Settings"), this);
    auto* pngLayout = new QVBoxLayout(pngGroup);
    pngLayout->setSpacing(12);

    auto* pngSliderLayout = new QHBoxLayout();
    m_PngFastLabel = new QLabel(tr("Fast"), pngGroup);
    applySecondaryTextStyle(m_PngFastLabel);
    pngSliderLayout->addWidget(m_PngFastLabel);

    m_PngCompressionSlider = new QSlider(Qt::Horizontal, pngGroup);
    m_PngCompressionSlider->setRange(0, 9);
    m_PngCompressionSlider->setTickPosition(QSlider::TicksBelow);
    m_PngCompressionSlider->setTickInterval(1);
    m_PngCompressionSlider->setToolTip(tr("PNG compression level (higher = smaller files, slower export)"));
    pngSliderLayout->addWidget(m_PngCompressionSlider, 1);

    m_PngSmallLabel = new QLabel(tr("Small"), pngGroup);
    applySecondaryTextStyle(m_PngSmallLabel);
    pngSliderLayout->addWidget(m_PngSmallLabel);

    m_PngCompressionLabel = new QLabel(pngGroup);
    m_PngCompressionLabel->setMinimumWidth(30);
    m_PngCompressionLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    pngSliderLayout->addWidget(m_PngCompressionLabel);

    auto* pngFormLayout = new QFormLayout();
    pngFormLayout->addRow(tr("Compression Level:"), pngSliderLayout);
    pngLayout->addLayout(pngFormLayout);

    auto* pngNote = createInfoLabel(tr("Higher compression results in smaller files but slower export."));
    pngLayout->addWidget(pngNote);

    mainLayout->addWidget(pngGroup);

    // === Metadata Section ===
    auto* metadataGroup = new QGroupBox(tr("Metadata"), this);
    auto* metadataLayout = new QVBoxLayout(metadataGroup);
    metadataLayout->setSpacing(8);

    m_EmbedExifCheck = new QCheckBox(tr("Embed EXIF metadata in exported files"), metadataGroup);
    m_EmbedExifCheck->setToolTip(tr("Copy camera and exposure metadata to exported images"));
    metadataLayout->addWidget(m_EmbedExifCheck);

    m_EmbedHistoryCheck = new QCheckBox(tr("Embed processing history in FITS header"), metadataGroup);
    m_EmbedHistoryCheck->setToolTip(tr("Record the list of processing steps applied to the image"));
    metadataLayout->addWidget(m_EmbedHistoryCheck);

    auto* metadataNote =
        createInfoLabel(tr("A FITS or XISF file always carries the image's header; Embed EXIF decides it "
                           "for TIFF, PNG and JPEG. No file is written with a processing history yet: "
                           "the setting is kept for when one is."));
    metadataLayout->addWidget(metadataNote);

    mainLayout->addWidget(metadataGroup);

    // Add stretch to push content to top
    mainLayout->addStretch(1);

    // Initialize labels
    updateJpegQualityLabel();
    updatePngCompressionLabel();
    updateXisfByteShuffleState();
}

void ExportSettingsPage::connectSignals()
{
    trackChanges(m_TiffCompressionCombo);
    trackChanges(m_XisfCompressionCombo);
    trackChanges(m_XisfByteShuffleCheck);
    trackChanges(m_XisfChecksumCombo);
    connect(m_XisfCompressionCombo, &QComboBox::currentIndexChanged, this, [this]() { updateXisfByteShuffleState(); });

    connect(m_JpegQualitySlider, &QSlider::valueChanged, this, [this]() {
        updateJpegQualityLabel();
        markAsModified();
    });

    connect(m_PngCompressionSlider, &QSlider::valueChanged, this, [this]() {
        updatePngCompressionLabel();
        markAsModified();
    });

    trackChanges(m_EmbedExifCheck);
    trackChanges(m_EmbedHistoryCheck);
}

void ExportSettingsPage::loadSettings()
{
    auto blocker = blockSignalsDuringLoad();

    if (!m_Settings)
    {
        resetToDefaults();
        return;
    }

    // Read through the table every save reads the settings through, so the page shows what a save writes with.
    const an::ui::ExportSettings settings = an::ui::readExportSettings(
        [this](const QString& key, const QString& fallback) { return m_Settings->select(key, fallback); });

    const int tiffIndex = m_TiffCompressionCombo->findData(an::ui::storedTiffCompression(settings.tiffCompression));
    m_TiffCompressionCombo->setCurrentIndex(tiffIndex >= 0 ? tiffIndex : 0);
    const int xisfIndex = m_XisfCompressionCombo->findData(an::ui::storedXisfCompression(settings.xisfCompression));
    m_XisfCompressionCombo->setCurrentIndex(xisfIndex >= 0 ? xisfIndex : 0);
    m_XisfByteShuffleCheck->setChecked(settings.xisfByteShuffle);
    const int checksumIndex = m_XisfChecksumCombo->findData(an::ui::storedXisfChecksum(settings.xisfChecksum));
    m_XisfChecksumCombo->setCurrentIndex(checksumIndex >= 0 ? checksumIndex : 0);
    m_JpegQualitySlider->setValue(static_cast<int>(settings.jpegQuality));
    m_PngCompressionSlider->setValue(static_cast<int>(settings.pngCompressionLevel));
    m_EmbedExifCheck->setChecked(settings.embedExif);
    m_EmbedHistoryCheck->setChecked(settings.embedHistory);

    updateJpegQualityLabel();
    updatePngCompressionLabel();
    updateXisfByteShuffleState();
    clearModified();
}

void ExportSettingsPage::saveSettings()
{
    if (!m_Settings)
    {
        return;
    }

    an::ui::ExportSettings settings;
    settings.tiffCompression = an::ui::tiffCompressionFromStored(m_TiffCompressionCombo->currentData().toString());
    settings.xisfCompression = an::ui::xisfCompressionFromStored(m_XisfCompressionCombo->currentData().toString());
    settings.xisfByteShuffle = m_XisfByteShuffleCheck->isChecked();
    settings.xisfChecksum = an::ui::xisfChecksumFromStored(m_XisfChecksumCombo->currentData().toString());
    settings.jpegQuality = static_cast<std::uint32_t>(m_JpegQualitySlider->value());
    settings.pngCompressionLevel = static_cast<std::uint32_t>(m_PngCompressionSlider->value());
    settings.embedExif = m_EmbedExifCheck->isChecked();
    settings.embedHistory = m_EmbedHistoryCheck->isChecked();
    an::ui::writeExportSettings(settings,
                                [this](const QString& key, const QString& value) { m_Settings->upsert(key, value); });

    clearModified();
}

void ExportSettingsPage::resetToDefaults()
{
    auto blocker = blockSignalsDuringLoad();

    // TIFF compression
    int tiffIndex = m_TiffCompressionCombo->findData(QString(SettingsDefaults::EXPORT_TIFF_COMPRESSION));
    m_TiffCompressionCombo->setCurrentIndex(tiffIndex >= 0 ? tiffIndex : 0);

    // XISF compression, byte shuffling and checksum
    const int xisfIndex = m_XisfCompressionCombo->findData(QString(SettingsDefaults::EXPORT_XISF_COMPRESSION));
    m_XisfCompressionCombo->setCurrentIndex(xisfIndex >= 0 ? xisfIndex : 0);
    m_XisfByteShuffleCheck->setChecked(SettingsDefaults::EXPORT_XISF_BYTE_SHUFFLE);
    const int checksumIndex = m_XisfChecksumCombo->findData(QString(SettingsDefaults::EXPORT_XISF_CHECKSUM));
    m_XisfChecksumCombo->setCurrentIndex(checksumIndex >= 0 ? checksumIndex : 0);

    // JPEG quality
    m_JpegQualitySlider->setValue(SettingsDefaults::EXPORT_JPEG_QUALITY);

    // PNG compression
    m_PngCompressionSlider->setValue(SettingsDefaults::EXPORT_PNG_COMPRESSION);

    // Metadata options
    m_EmbedExifCheck->setChecked(SettingsDefaults::EXPORT_EMBED_EXIF);
    m_EmbedHistoryCheck->setChecked(SettingsDefaults::EXPORT_EMBED_HISTORY);

    updateJpegQualityLabel();
    updatePngCompressionLabel();
    updateXisfByteShuffleState();
    markAsModified();
}

ValidationResult ExportSettingsPage::validateSettings() const
{
    return ValidationResult::valid();
}

void ExportSettingsPage::updateXisfByteShuffleState()
{
    m_XisfByteShuffleCheck->setEnabled(m_XisfCompressionCombo->currentData().toString() != QStringLiteral("none"));
}

void ExportSettingsPage::updateJpegQualityLabel()
{
    m_JpegQualityLabel->setText(QString("%1%").arg(m_JpegQualitySlider->value()));
}

void ExportSettingsPage::updatePngCompressionLabel()
{
    m_PngCompressionLabel->setText(QString::number(m_PngCompressionSlider->value()));
}

void ExportSettingsPage::refreshStyles()
{
    // Call base class to refresh theme-dependent palettes
    SettingsPage::refreshStyles();

    // Re-apply secondary text style (proxy re-polishes on theme change,
    // but explicit call ensures font is correct)
    applySecondaryTextStyle(m_JpegLowLabel);
    applySecondaryTextStyle(m_JpegHighLabel);
    applySecondaryTextStyle(m_PngFastLabel);
    applySecondaryTextStyle(m_PngSmallLabel);
}
