// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "controls/ExtensionNativeProcessWindow.h"

#include <QAbstractButton>
#include <QAbstractSpinBox>
#include <QAction>
#include <QButtonGroup>
#include <QCborArray>
#include <QCborMap>
#include <QCheckBox>
#include <QCloseEvent>
#include <QColor>
#include <QComboBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QDesktopServices>
#include <QDoubleSpinBox>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QLabel>
#include <QLayoutItem>
#include <QLineEdit>
#include <QMouseEvent>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QProcessEnvironment>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QSlider>
#include <QStandardItemModel>
#include <QSpinBox>
#include <QStyle>
#include <QTabWidget>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <array>
#include <climits>
#include <cmath>
#include <vector>

#include <AfterNightConsumerRuntime/EngineSession.h>
#include <AfterNightConsumerRuntime/ImageAsset.h>
#include <AfterNightConsumerRuntime/OperationScope.h>
#include <AfterNightEngine/Algorithms/Pointwise/ToneCurve.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Image/ImageDescriptor.h>
#include <AfterNightPlatform/Logging.h>

#include <verdigris/wobjectimpl.h>

#include "components/AnCardWidget.h"
#include "components/AnCollapsibleSection.h"
#include "components/AnInfoBanner.h"
#include "app/FileDialogUtils.h"
#include "controls/drawing/DrawingCanvasController.h"
#include "controls/drawing/DrawingDocument.h"
#include "controls/drawing/DrawingEditQueue.h"
#include "controls/drawing/DrawingTypes.h"
#include "controls/IAppContext.h"
#include "controls/ImageViewControl.h"
#include "controls/ProcessExecution.h"
#include "session/ExecutionScope.h"
#include "session/IEngineSessionProvider.h"
#include "session/ImageDocument.h"
#include "session/PreviewProxies.h"
#include "controls/QSliderSpinBox.h"
#include "controls/RTPreviewUiCommon.h"
#include "theme/AnDesignTokens.h"
#include "widgets/HistogramCompanions.h"
#include "widgets/HistogramDisplayData.h"
#include "widgets/ImageViewWidget.h"
#include "widgets/ToneCurveModel.h"
#include "widgets/VectorSlopeWidget.h"

namespace an::ui::extensions
{

W_OBJECT_IMPL(ExtensionNativeProcessWindow)

namespace
{

QString selectorTooltipFromView(const std::map<std::string, QVariant>& view)
{
    const auto tooltip_it = view.find("selector_tooltip");
    if (tooltip_it != view.end())
    {
        const QString selector_tooltip = tooltip_it->second.toString();
        if (!selector_tooltip.isEmpty())
        {
            return selector_tooltip;
        }
    }

    const auto file_it = view.find("file_name");
    return file_it != view.end() ? file_it->second.toString() : QString();
}

QString viewNameFromDescriptor(const std::map<std::string, QVariant>& view)
{
    const auto name_it = view.find("name");
    return name_it != view.end() ? name_it->second.toString().trimmed() : QString();
}

bool viewHasImage(const std::map<std::string, QVariant>& view)
{
    const auto has_image_it = view.find("has_image");
    return has_image_it == view.end() || has_image_it->second.toBool();
}

int viewIntField(const std::map<std::string, QVariant>& view, const char* key)
{
    const auto it = view.find(key);
    return it != view.end() ? it->second.toInt() : 0;
}

constexpr int kDefaultPreviewSubAreaEdge = 600;

QSize sanitizeSubAreaSize(const QSize& size)
{
    if (size.width() > 0 && size.height() > 0)
    {
        return size;
    }

    return QSize(kDefaultPreviewSubAreaEdge, kDefaultPreviewSubAreaEdge);
}

QRect imageBounds(const std::shared_ptr<const an::consumer::ImageAsset>& image)
{
    const std::optional<an::ImageDescriptor> described = image ? image->descriptor() : std::nullopt;
    if (!described.has_value())
    {
        return {};
    }

    return QRect(0, 0, static_cast<int>(described->extent().width()), static_cast<int>(described->extent().height()));
}

void selectRtPreviewAutostretchMode(ImageViewControl* view)
{
    if (!view)
    {
        return;
    }

    const auto combos = view->findChildren<QComboBox*>();
    for (QComboBox* combo : combos)
    {
        if (combo && combo->accessibleName() == QStringLiteral("Display Mode"))
        {
            combo->setCurrentText(QStringLiteral("Autostretch"));
            return;
        }
    }
}

QString tooltipText(const QJsonObject& obj)
{
    return obj[QStringLiteral("tooltip")].toString().trimmed();
}

void applyTooltip(QWidget* widget, const QString& tooltip)
{
    if (widget && !tooltip.isEmpty())
    {
        widget->setToolTip(tooltip);
    }
}

QSize jsonSizePair(const QJsonValue& value)
{
    if (value.isArray())
    {
        const auto array = value.toArray();
        if (array.size() < 2)
        {
            return {};
        }
        const int width = array.at(0).toInt();
        const int height = array.at(1).toInt();
        return width > 0 && height > 0 ? QSize(width, height) : QSize();
    }

    if (value.isObject())
    {
        const auto object = value.toObject();
        const int width = object.value(QStringLiteral("width")).toInt();
        const int height = object.value(QStringLiteral("height")).toInt();
        return width > 0 && height > 0 ? QSize(width, height) : QSize();
    }

    return {};
}

constexpr QSize kDefaultNativeProcessWindowSize(1200, 760);
constexpr QSize kDefaultExternalToolManagementDialogSize(760, 510);

QString defaultNativeProcessDescription()
{
    return QStringLiteral("Extension process (native window mode).");
}

bool isAttributionParam(const QJsonObject& obj)
{
    const QString id = obj.value(QStringLiteral("id")).toString().trimmed().toLower();
    const QString role = obj.value(QStringLiteral("role")).toString().trimmed().toLower();
    const QString purpose = obj.value(QStringLiteral("purpose")).toString().trimmed().toLower();

    return id == QLatin1String("attribution") || id == QLatin1String("credits") || id == QLatin1String("credit") ||
           role == QLatin1String("attribution") || role == QLatin1String("credits") ||
           purpose == QLatin1String("attribution") || purpose == QLatin1String("credits");
}

QString headerDescriptionFromMeta(const QJsonObject& obj)
{
    const std::array keys{QStringLiteral("header_description"),
                          QStringLiteral("process_description"),
                          QStringLiteral("header_attribution"),
                          QStringLiteral("attribution"),
                          QStringLiteral("credits"),
                          QStringLiteral("credit")};
    for (const auto& key : keys)
    {
        const QString value = obj.value(key).toString().trimmed();
        if (!value.isEmpty())
        {
            return value;
        }
    }

    return {};
}

QString currentPlatformKey()
{
#if defined(Q_OS_WIN)
    return QStringLiteral("windows");
#elif defined(Q_OS_LINUX)
    return QStringLiteral("linux");
#elif defined(Q_OS_MACOS)
    return QStringLiteral("macos");
#else
    return {};
#endif
}

QString currentPlatformDisplayName()
{
#if defined(Q_OS_WIN)
    return QStringLiteral("Windows");
#elif defined(Q_OS_LINUX)
    return QStringLiteral("Linux");
#elif defined(Q_OS_MACOS)
    return QStringLiteral("macOS");
#else
    return QStringLiteral("this platform");
#endif
}

QString platformStringValue(const QJsonObject& config, const QString& key)
{
    const QJsonValue value = config.value(key);
    if (value.isString())
    {
        return value.toString().trimmed();
    }

    if (value.isObject())
    {
        const QJsonObject values = value.toObject();
        QString platform_value = values.value(currentPlatformKey()).toString().trimmed();
        if (platform_value.isEmpty())
        {
            platform_value = values.value(QStringLiteral("default")).toString().trimmed();
        }
        return platform_value;
    }

    return {};
}

QStringList platformStringListValue(const QJsonObject& config, const QString& key)
{
    const QJsonValue value = config.value(key);
    const auto append_array = [](const QJsonArray& array, QStringList* out_values) {
        for (const QJsonValue& item : array)
        {
            const QString text = item.toString().trimmed();
            if (!text.isEmpty() && !out_values->contains(text))
            {
                out_values->push_back(text);
            }
        }
    };

    QStringList values;
    if (value.isArray())
    {
        append_array(value.toArray(), &values);
        return values;
    }

    if (value.isObject())
    {
        const QJsonObject object = value.toObject();
        if (object.value(currentPlatformKey()).isArray())
        {
            append_array(object.value(currentPlatformKey()).toArray(), &values);
        }
        if (values.isEmpty() && object.value(QStringLiteral("default")).isArray())
        {
            append_array(object.value(QStringLiteral("default")).toArray(), &values);
        }
    }
    return values;
}

QString firstLineOrTrimmed(QString text, const int limit)
{
    text = text.trimmed();
    const int newline_index = text.indexOf(QLatin1Char('\n'));
    if (newline_index >= 0)
    {
        text = text.left(newline_index).trimmed();
    }
    return text.left(limit);
}

QString versionFromOutput(QString text, const QString& pattern, const int limit)
{
    text = text.trimmed();
    const QString trimmed_pattern = pattern.trimmed();
    if (!trimmed_pattern.isEmpty())
    {
        const QRegularExpression expression(trimmed_pattern);
        if (expression.isValid())
        {
            const QRegularExpressionMatch match = expression.match(text);
            if (match.hasMatch())
            {
                QString captured = match.lastCapturedIndex() >= 1 ? match.captured(1) : match.captured(0);
                captured = captured.trimmed();
                if (!captured.isEmpty())
                {
                    return captured.left(limit);
                }
            }
        }
    }

    return firstLineOrTrimmed(text, limit);
}

bool isExecutionLockCandidate(QWidget* widget)
{
    return qobject_cast<QAbstractButton*>(widget) != nullptr || qobject_cast<QAbstractSpinBox*>(widget) != nullptr ||
           qobject_cast<QComboBox*>(widget) != nullptr || qobject_cast<QLineEdit*>(widget) != nullptr ||
           qobject_cast<QSlider*>(widget) != nullptr;
}

struct ExternalToolReleaseAsset
{
    QString name;
    QString download_url;
    qint64 size = 0;
    QDateTime updated_at;
};

struct ExternalToolManagementOptions
{
    QString label;
    QString dialog_title;
    QSize dialog_size;
    QString current_directory;
    QString download_page_url;
    QString download_button_label;
    QString update_page_url;
    QString update_api_url;
    QString install_instructions;
    QString download_note;
    QStringList required_files;
    std::function<bool(const QString&, QString*)> validate_directory;
    std::function<bool(const QString&, std::string*)> save_directory;
    std::function<void()> configuration_changed;
};

class ExternalToolManagementDialog final : public QDialog
{
public:
    explicit ExternalToolManagementDialog(ExternalToolManagementOptions options, QWidget* parent = nullptr)
        : QDialog(parent), options_(std::move(options))
    {
        setObjectName(QStringLiteral("anExternalToolConfigurationDialog"));
        setWindowTitle(options_.dialog_title.isEmpty() ? QStringLiteral("Configure %1").arg(options_.label)
                                                       : options_.dialog_title);
        setAttribute(Qt::WA_DeleteOnClose, true);

        auto* root_layout = new QVBoxLayout(this);
        const auto& spacing = an::ui::tokens().spacing;
        root_layout->setSpacing(spacing.md);
        root_layout->setContentsMargins(spacing.lg, spacing.lg, spacing.lg, spacing.lg);

        auto* title_label = new QLabel(QStringLiteral("Configure %1").arg(options_.label), this);
        title_label->setObjectName(QStringLiteral("anExternalToolConfigTitle"));
        title_label->setFont(an::ui::tokens().typography.heading3);
        title_label->setWordWrap(true);
        root_layout->addWidget(title_label);

        status_banner_ = new an::ui::AnInfoBanner(QString(), an::ui::AnInfoBanner::Tone::Warning, this);
        status_banner_->setObjectName(QStringLiteral("anExternalToolConfigDialogStatusBanner"));
        root_layout->addWidget(status_banner_);

        auto* folder_layout = new QFormLayout();
        folder_layout->setSpacing(spacing.sm);
        folder_layout->setContentsMargins(0, 0, 0, 0);
        auto* folder_row = new QWidget(this);
        auto* folder_row_layout = new QHBoxLayout(folder_row);
        folder_row_layout->setSpacing(spacing.sm);
        folder_row_layout->setContentsMargins(0, 0, 0, 0);

        path_edit_ = new QLineEdit(folder_row);
        path_edit_->setObjectName(QStringLiteral("anExternalToolConfigPathEdit"));
        path_edit_->setText(options_.current_directory);
        path_edit_->setPlaceholderText(QStringLiteral("Select the %1 installation folder").arg(options_.label));
        folder_row_layout->addWidget(path_edit_, 1);

        browse_button_ = new QPushButton(QStringLiteral("Browse..."), folder_row);
        browse_button_->setObjectName(QStringLiteral("anExternalToolConfigBrowseButton"));
        folder_row_layout->addWidget(browse_button_, 0);
        folder_layout->addRow(QStringLiteral("Folder"), folder_row);
        root_layout->addLayout(folder_layout);

        if (!options_.install_instructions.isEmpty())
        {
            auto* instructions = new QLabel(options_.install_instructions, this);
            instructions->setObjectName(QStringLiteral("anExternalToolConfigInstallInstructions"));
            instructions->setWordWrap(true);
            root_layout->addWidget(instructions);
        }

        update_banner_ = new an::ui::AnInfoBanner(updateIntroText(), an::ui::AnInfoBanner::Tone::Info, this);
        update_banner_->setObjectName(QStringLiteral("anExternalToolConfigUpdateBanner"));
        root_layout->addWidget(update_banner_);

        auto* update_layout = new QVBoxLayout();
        update_layout->setSpacing(spacing.sm);
        update_layout->setContentsMargins(0, 0, 0, 0);
        version_label_ = new QLabel(QStringLiteral("Installed version: not checked"), this);
        version_label_->setObjectName(QStringLiteral("anExternalToolConfigVersionLabel"));
        version_label_->setWordWrap(true);
        update_layout->addWidget(version_label_);

        if (!options_.download_note.isEmpty())
        {
            auto* note = new QLabel(options_.download_note, this);
            note->setObjectName(QStringLiteral("anExternalToolConfigDownloadNote"));
            note->setWordWrap(true);
            update_layout->addWidget(note);
        }

        auto* update_buttons_layout = new QHBoxLayout();
        update_buttons_layout->setSpacing(spacing.sm);
        update_buttons_layout->setContentsMargins(0, 0, 0, 0);

        download_button_ =
            new QPushButton(options_.download_button_label.isEmpty()
                                ? QStringLiteral("Download %1 Suite...").arg(currentPlatformDisplayName())
                                : options_.download_button_label,
                            this);
        download_button_->setObjectName(QStringLiteral("anExternalToolConfigDownloadButton"));
        download_button_->setEnabled(!options_.download_page_url.isEmpty());
        update_buttons_layout->addWidget(download_button_, 0);

        if (hasUpdateSource())
        {
            check_updates_button_ = new QPushButton(QStringLiteral("Check for Updates"), this);
            check_updates_button_->setObjectName(QStringLiteral("anExternalToolConfigCheckUpdatesButton"));
            check_updates_button_->setEnabled(true);
            update_buttons_layout->addWidget(check_updates_button_, 0);
        }

        if (!options_.update_api_url.isEmpty())
        {
            update_button_ = new QPushButton(QStringLiteral("Update Installed Files"), this);
            update_button_->setObjectName(QStringLiteral("anExternalToolConfigUpdateButton"));
            update_button_->setEnabled(false);
            update_buttons_layout->addWidget(update_button_, 0);
        }
        update_buttons_layout->addStretch(1);
        update_layout->addLayout(update_buttons_layout);

        progress_bar_ = new QProgressBar(this);
        progress_bar_->setObjectName(QStringLiteral("anExternalToolConfigDownloadProgress"));
        progress_bar_->hide();
        update_layout->addWidget(progress_bar_);
        root_layout->addLayout(update_layout);
        root_layout->addStretch(1);

        button_box_ = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Close, Qt::Horizontal, this);
        save_button_ = button_box_->button(QDialogButtonBox::Save);
        if (save_button_)
        {
            save_button_->setObjectName(QStringLiteral("anExternalToolConfigSaveButton"));
        }
        root_layout->addWidget(button_box_);

        QObject::connect(path_edit_, &QLineEdit::textChanged, this, [this]() {
            refreshLocalStatus();
            clearUpdateState();
        });
        QObject::connect(browse_button_, &QPushButton::clicked, this, [this]() { openFolderDialog(); });
        QObject::connect(download_button_, &QPushButton::clicked, this,
                         [this]() { openUrl(options_.download_page_url); });
        if (check_updates_button_)
        {
            QObject::connect(check_updates_button_, &QPushButton::clicked, this, [this]() { checkForUpdates(); });
        }
        if (update_button_)
        {
            QObject::connect(update_button_, &QPushButton::clicked, this, [this]() { startUpdateDownload(); });
        }
        QObject::connect(button_box_, &QDialogButtonBox::accepted, this, [this]() { saveAndAccept(); });
        QObject::connect(button_box_, &QDialogButtonBox::rejected, this, [this]() { reject(); });

        refreshLocalStatus();
        setFixedSize(options_.dialog_size.isValid() ? options_.dialog_size : kDefaultExternalToolManagementDialogSize);
        setSizeGripEnabled(false);
    }

    ~ExternalToolManagementDialog() override
    {
        if (active_reply_)
        {
            active_reply_->abort();
        }
        if (active_file_)
        {
            active_file_->close();
            QFile::remove(active_temp_path_);
        }
    }

private:
    QString currentDirectory() const
    {
        return path_edit_ ? path_edit_->text().trimmed() : QString();
    }

    bool hasUpdateSource() const
    {
        return !options_.update_api_url.isEmpty() || !options_.update_page_url.isEmpty();
    }

    QString updateIntroText() const
    {
        if (options_.update_api_url.isEmpty())
        {
            return options_.update_page_url.isEmpty()
                       ? QStringLiteral("Automatic update checks are not available for %1.").arg(options_.label)
                       : QStringLiteral(
                             "Automatic update checks are not available. Use the download page to check updates.");
        }

        return QStringLiteral("Check the latest %1 update files for %2.")
            .arg(options_.label, currentPlatformDisplayName());
    }

    void refreshLocalStatus()
    {
        QString message;
        const bool valid = options_.validate_directory ? options_.validate_directory(currentDirectory(), &message)
                                                       : !currentDirectory().isEmpty();

        if (status_banner_)
        {
            status_banner_->setMessage(message);
            status_banner_->setTone(valid ? an::ui::AnInfoBanner::Tone::Success : an::ui::AnInfoBanner::Tone::Warning);
        }
        if (save_button_)
        {
            save_button_->setEnabled(valid);
        }
        updateLocalVersionLabel(valid);
        refreshUpdateButton();
    }

    void updateLocalVersionLabel(const bool valid)
    {
        if (!version_label_)
        {
            return;
        }

        if (!valid)
        {
            version_label_->setText(QStringLiteral("Installed version: unavailable until a valid folder is selected"));
            return;
        }

        if (latest_assets_.empty())
        {
            version_label_->setText(
                hasUpdateSource()
                    ? QStringLiteral("Installed version: unknown. Check for updates to compare installed files.")
                    : QStringLiteral("Installed version: unknown."));
            return;
        }

        const std::size_t asset_count = latest_assets_.size();
        const int total_count =
            asset_count > static_cast<std::size_t>(INT_MAX) ? INT_MAX : static_cast<int>(asset_count);
        const int present_count = (std::max)(0, total_count - missingAssetCount(currentDirectory()));
        version_label_->setText(QStringLiteral("Installed files: %1/%2 latest %3 update files present")
                                    .arg(present_count)
                                    .arg(total_count)
                                    .arg(currentPlatformDisplayName()));
    }

    int missingAssetCount(const QString& directory) const
    {
        const QDir dir(directory);
        int missing = 0;
        for (const ExternalToolReleaseAsset& asset : latest_assets_)
        {
            if (!QFileInfo::exists(dir.filePath(asset.name)))
            {
                ++missing;
            }
        }
        return missing;
    }

    void clearUpdateState()
    {
        latest_assets_.clear();
        update_candidates_.clear();
        if (update_banner_)
        {
            update_banner_->setMessage(updateIntroText());
            update_banner_->setTone(an::ui::AnInfoBanner::Tone::Info);
        }
        if (progress_bar_)
        {
            progress_bar_->hide();
        }
        refreshUpdateButton();
    }

    void refreshUpdateButton()
    {
        QString validation_message;
        const bool valid = options_.validate_directory
                               ? options_.validate_directory(currentDirectory(), &validation_message)
                               : !currentDirectory().isEmpty();
        const bool can_update = valid && !update_candidates_.empty() && !download_in_progress_;
        if (update_button_)
        {
            update_button_->setEnabled(can_update);
        }
    }

    void openUrl(const QString& raw_url)
    {
        const QUrl url = QUrl::fromUserInput(raw_url);
        if (!url.isValid())
        {
            showUpdateMessage(QStringLiteral("Download URL is invalid: %1").arg(raw_url),
                              an::ui::AnInfoBanner::Tone::Error);
            return;
        }
        QDesktopServices::openUrl(url);
    }

    void openFolderDialog()
    {
        if (browse_dialog_)
        {
            browse_dialog_->raise();
            browse_dialog_->activateWindow();
            return;
        }

        QString start_dir = currentDirectory();
        if (start_dir.isEmpty() || !QFileInfo(start_dir).isDir())
        {
            start_dir = QDir::homePath();
        }

        auto* dialog = new QFileDialog(this, QStringLiteral("Select %1 Folder").arg(options_.label));
        browse_dialog_ = dialog;
        dialog->setObjectName(QStringLiteral("anExternalToolConfigFolderDialog"));
        dialog->setAttribute(Qt::WA_DeleteOnClose, true);
        dialog->setDirectoryUrl(QUrl::fromLocalFile(start_dir));
        dialog->setFileMode(QFileDialog::Directory);
        dialog->setOption(QFileDialog::ShowDirsOnly, true);
        auto* context = an::ui::appContext();
        an::ui::dialogs::applyDialogBackendPreference(*dialog, context ? context->getGlobalSettings() : nullptr);

        QObject::connect(dialog, &QFileDialog::finished, this, [this, dialog](const int result) {
            if (browse_dialog_ == dialog)
            {
                browse_dialog_ = nullptr;
            }
            if (result != QDialog::Accepted)
            {
                return;
            }

            const QStringList selected_files = dialog->selectedFiles();
            if (!selected_files.isEmpty())
            {
                path_edit_->setText(selected_files.first().trimmed());
            }
        });
        QObject::connect(dialog, &QObject::destroyed, this, [this, dialog]() {
            if (browse_dialog_ == dialog)
            {
                browse_dialog_ = nullptr;
            }
        });

        dialog->open();
    }

    void saveAndAccept()
    {
        const QString directory = currentDirectory();
        QString validation_message;
        if (options_.validate_directory && !options_.validate_directory(directory, &validation_message))
        {
            showUpdateMessage(validation_message, an::ui::AnInfoBanner::Tone::Warning);
            return;
        }

        std::string error;
        if (options_.save_directory && !options_.save_directory(directory, &error))
        {
            showUpdateMessage(
                QString::fromStdString(error.empty() ? std::string("Failed to persist extension setting.") : error),
                an::ui::AnInfoBanner::Tone::Error);
            return;
        }

        if (options_.configuration_changed)
        {
            options_.configuration_changed();
        }
        accept();
    }

    void checkForUpdates()
    {
        if (options_.update_api_url.isEmpty())
        {
            if (!options_.update_page_url.isEmpty())
            {
                openUrl(options_.update_page_url);
            }
            return;
        }

        QString validation_message;
        if (options_.validate_directory && !options_.validate_directory(currentDirectory(), &validation_message))
        {
            showUpdateMessage(validation_message, an::ui::AnInfoBanner::Tone::Warning);
            return;
        }

        setNetworkBusy(true);
        showUpdateMessage(QStringLiteral("Checking latest %1 update files...").arg(currentPlatformDisplayName()),
                          an::ui::AnInfoBanner::Tone::Info);

        QNetworkRequest request(QUrl(options_.update_api_url));
        request.setRawHeader("User-Agent", "AfterNightExtensionHost");
        request.setRawHeader("Accept", "application/vnd.github+json");
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);

        QNetworkReply* reply = network_.get(request);
        QObject::connect(reply, &QNetworkReply::finished, this, [this, reply]() {
            reply->deleteLater();
            setNetworkBusy(false);

            if (reply->error() != QNetworkReply::NoError)
            {
                showUpdateMessage(QStringLiteral("Update check failed: %1").arg(reply->errorString()),
                                  an::ui::AnInfoBanner::Tone::Error);
                return;
            }

            QJsonParseError parse_error;
            const QJsonDocument document = QJsonDocument::fromJson(reply->readAll(), &parse_error);
            if (parse_error.error != QJsonParseError::NoError || !document.isObject())
            {
                showUpdateMessage(QStringLiteral("Update check returned invalid release metadata."),
                                  an::ui::AnInfoBanner::Tone::Error);
                return;
            }

            handleReleaseMetadata(document.object());
        });
    }

    void setNetworkBusy(const bool busy)
    {
        if (check_updates_button_)
        {
            check_updates_button_->setEnabled(!busy && hasUpdateSource());
        }
        if (download_button_)
        {
            download_button_->setEnabled(!busy && !options_.download_page_url.isEmpty());
        }
        download_in_progress_ = busy;
        refreshUpdateButton();
    }

    void handleReleaseMetadata(const QJsonObject& release)
    {
        latest_release_name_ = release.value(QStringLiteral("name")).toString().trimmed();
        const QString tag_name = release.value(QStringLiteral("tag_name")).toString().trimmed();
        if (latest_release_name_.isEmpty())
        {
            latest_release_name_ = tag_name;
        }

        latest_assets_.clear();
        const QJsonArray assets = release.value(QStringLiteral("assets")).toArray();
        latest_assets_.reserve(assets.size());
        for (const QJsonValue& value : assets)
        {
            const QJsonObject asset_object = value.toObject();
            ExternalToolReleaseAsset asset;
            asset.name = asset_object.value(QStringLiteral("name")).toString().trimmed();
            asset.download_url = asset_object.value(QStringLiteral("browser_download_url")).toString().trimmed();
            asset.size = asset_object.value(QStringLiteral("size")).toInteger();
            const QString updated_text = asset_object.value(QStringLiteral("updated_at")).toString().trimmed();
            asset.updated_at = QDateTime::fromString(updated_text, Qt::ISODate);
            if (asset.updated_at.isValid())
            {
                asset.updated_at = asset.updated_at.toUTC();
            }

            if (!asset.name.isEmpty() && !asset.download_url.isEmpty())
            {
                latest_assets_.push_back(asset);
            }
        }

        update_candidates_ = assetsNeedingUpdate();
        updateLocalVersionLabel(true);

        if (latest_assets_.empty())
        {
            showUpdateMessage(
                QStringLiteral("No downloadable update files were found for %1.").arg(currentPlatformDisplayName()),
                an::ui::AnInfoBanner::Tone::Warning);
        }
        else if (update_candidates_.empty())
        {
            showUpdateMessage(QStringLiteral("%1 appears up to date with %2.")
                                  .arg(options_.label, latest_release_name_.isEmpty()
                                                           ? QStringLiteral("the latest release")
                                                           : latest_release_name_),
                              an::ui::AnInfoBanner::Tone::Success);
        }
        else
        {
            showUpdateMessage(QStringLiteral("%1 update files are available for %2.")
                                  .arg(update_candidates_.size())
                                  .arg(options_.label),
                              an::ui::AnInfoBanner::Tone::Warning);
        }

        refreshUpdateButton();
    }

    std::vector<ExternalToolReleaseAsset> assetsNeedingUpdate() const
    {
        std::vector<ExternalToolReleaseAsset> result;
        const QDir directory(currentDirectory());
        for (const ExternalToolReleaseAsset& asset : latest_assets_)
        {
            const QFileInfo local_file(directory.filePath(asset.name));
            if (!local_file.exists() || !local_file.isFile())
            {
                result.push_back(asset);
                continue;
            }

            if (asset.updated_at.isValid() && local_file.lastModified().isValid() &&
                local_file.lastModified().toUTC().addSecs(60) < asset.updated_at)
            {
                result.push_back(asset);
            }
        }
        return result;
    }

    void startUpdateDownload()
    {
        if (update_candidates_.empty())
        {
            return;
        }

        QString validation_message;
        if (options_.validate_directory && !options_.validate_directory(currentDirectory(), &validation_message))
        {
            showUpdateMessage(validation_message, an::ui::AnInfoBanner::Tone::Warning);
            return;
        }

        download_queue_ = update_candidates_;
        download_index_ = 0;
        setNetworkBusy(true);
        if (progress_bar_)
        {
            progress_bar_->setRange(0, 100);
            progress_bar_->setValue(0);
            progress_bar_->show();
        }
        downloadNextAsset();
    }

    void downloadNextAsset()
    {
        if (download_index_ >= download_queue_.size())
        {
            update_candidates_.clear();
            if (progress_bar_)
            {
                progress_bar_->setRange(0, 100);
                progress_bar_->setValue(100);
            }
            showUpdateMessage(QStringLiteral("%1 update completed.").arg(options_.label),
                              an::ui::AnInfoBanner::Tone::Success);
            setNetworkBusy(false);
            refreshLocalStatus();
            if (options_.configuration_changed)
            {
                options_.configuration_changed();
            }
            return;
        }

        const ExternalToolReleaseAsset asset = download_queue_.at(download_index_);
        showUpdateMessage(QStringLiteral("Downloading %1 (%2/%3)...")
                              .arg(asset.name)
                              .arg(download_index_ + 1)
                              .arg(download_queue_.size()),
                          an::ui::AnInfoBanner::Tone::Info);

        const QDir directory(currentDirectory());
        const QString target_path = directory.filePath(asset.name);
        active_temp_path_ = target_path + QStringLiteral(".afternight-download");
        QFile::remove(active_temp_path_);
        active_file_ = std::make_unique<QFile>(active_temp_path_);
        if (!active_file_->open(QIODevice::WriteOnly))
        {
            failDownload(QStringLiteral("Could not write %1").arg(active_temp_path_));
            return;
        }

        QNetworkRequest request(QUrl(asset.download_url));
        request.setRawHeader("User-Agent", "AfterNightExtensionHost");
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
        active_reply_ = network_.get(request);

        QObject::connect(active_reply_, &QNetworkReply::readyRead, this, [this]() {
            if (active_reply_ && active_file_)
            {
                active_file_->write(active_reply_->readAll());
            }
        });
        QObject::connect(active_reply_, &QNetworkReply::downloadProgress, this,
                         [this](const qint64 received, const qint64 total) {
                             if (!progress_bar_)
                             {
                                 return;
                             }
                             if (total > 0)
                             {
                                 progress_bar_->setRange(0, 100);
                                 progress_bar_->setValue(static_cast<int>((received * 100) / total));
                             }
                             else
                             {
                                 progress_bar_->setRange(0, 0);
                             }
                         });
        QObject::connect(active_reply_, &QNetworkReply::finished, this,
                         [this, target_path]() { finishActiveDownload(target_path); });
    }

    void finishActiveDownload(const QString& target_path)
    {
        QNetworkReply* reply = active_reply_;
        active_reply_ = nullptr;
        if (!reply)
        {
            failDownload(QStringLiteral("Download failed before a response was received."));
            return;
        }

        reply->deleteLater();
        if (active_file_)
        {
            active_file_->write(reply->readAll());
            active_file_->close();
        }

        if (reply->error() != QNetworkReply::NoError)
        {
            failDownload(QStringLiteral("Download failed: %1").arg(reply->errorString()));
            return;
        }

        QFile::remove(target_path);
        if (!QFile::rename(active_temp_path_, target_path))
        {
            failDownload(QStringLiteral("Could not install downloaded file: %1").arg(target_path));
            return;
        }
        active_file_.reset();
        markExecutableIfRequired(target_path);

        ++download_index_;
        if (progress_bar_)
        {
            progress_bar_->setRange(0, 100);
            progress_bar_->setValue(0);
        }
        downloadNextAsset();
    }

    void failDownload(const QString& message)
    {
        if (active_reply_)
        {
            active_reply_->deleteLater();
            active_reply_ = nullptr;
        }
        if (active_file_)
        {
            active_file_->close();
            active_file_.reset();
        }
        QFile::remove(active_temp_path_);
        showUpdateMessage(message, an::ui::AnInfoBanner::Tone::Error);
        setNetworkBusy(false);
    }

    void markExecutableIfRequired(const QString& path) const
    {
        const QString file_name = QFileInfo(path).fileName();
        bool is_required_executable = false;
        for (const QString& required_file : options_.required_files)
        {
            if (QFileInfo(required_file).fileName().compare(file_name, Qt::CaseInsensitive) == 0)
            {
                is_required_executable = true;
                break;
            }
        }

        if (!is_required_executable)
        {
            return;
        }

        QFile file(path);
        file.setPermissions(file.permissions() | QFileDevice::ExeOwner | QFileDevice::ExeUser | QFileDevice::ExeGroup |
                            QFileDevice::ExeOther);
    }

    void showUpdateMessage(const QString& message, const an::ui::AnInfoBanner::Tone tone)
    {
        if (!update_banner_)
        {
            return;
        }
        update_banner_->setMessage(message);
        update_banner_->setTone(tone);
    }

    ExternalToolManagementOptions options_;
    QNetworkAccessManager network_;
    QLineEdit* path_edit_ = nullptr;
    QPushButton* browse_button_ = nullptr;
    QPushButton* download_button_ = nullptr;
    QPushButton* check_updates_button_ = nullptr;
    QPushButton* update_button_ = nullptr;
    QPushButton* save_button_ = nullptr;
    QDialogButtonBox* button_box_ = nullptr;
    QLabel* version_label_ = nullptr;
    QProgressBar* progress_bar_ = nullptr;
    an::ui::AnInfoBanner* status_banner_ = nullptr;
    an::ui::AnInfoBanner* update_banner_ = nullptr;
    QPointer<QFileDialog> browse_dialog_;
    QPointer<QNetworkReply> active_reply_;
    std::unique_ptr<QFile> active_file_;
    QString active_temp_path_;
    QString latest_release_name_;
    std::vector<ExternalToolReleaseAsset> latest_assets_;
    std::vector<ExternalToolReleaseAsset> update_candidates_;
    std::vector<ExternalToolReleaseAsset> download_queue_;
    std::size_t download_index_ = 0;
    bool download_in_progress_ = false;
};

} // namespace

ExtensionNativeProcessWindow::ExtensionNativeProcessWindow(const std::string& display_name,
                                                           const std::string& session_id, ImageViewControl* source_view)
    : ImageRTPreviewControl(source_view, display_name), session_id_(session_id), source_view_(source_view)
{
    build(display_name, std::nullopt);
}

ExtensionNativeProcessWindow::ExtensionNativeProcessWindow(const std::string& display_name,
                                                           const std::string& session_id, ImageViewControl* source_view,
                                                           const PageShape shape)
    : ImageRTPreviewControl(source_view, display_name), session_id_(session_id), source_view_(source_view),
      page_shape_(true)
{
    build(display_name, shape);
}

void ExtensionNativeProcessWindow::build(const std::string& display_name, const std::optional<PageShape> shape)
{
    // The host owns its window through a std::unique_ptr, so it must not self-delete on close or the unique_ptr will
    // later delete dangling memory. A window of a page's shape is the application's, and closes as a process does.
    setAttribute(Qt::WA_DeleteOnClose, false);
    const bool previews = !shape.has_value() || shape->previews;

    // The extension's preview is its body's alone: the mirror's mask is the application's to apply to what the window
    // commits, never blended into a frame, and the waiting spinner is the framework's, shown only for a slow frame.
    m_SupportMasking = false;
    m_ShowWaitingSpinner = true;

    root_layout_ = new QHBoxLayout(this);
    root_layout_->setContentsMargins(0, 0, 0, 0);
    root_layout_->setSpacing(0);

    QLayout* preview_layout = nullptr;
    if (previews)
    {
        const auto split = an::ui::processes::rt_preview_ui::buildMainProcessSplitter(root_layout_, this);
        main_splitter_ = split.mainSplitter;
        controls_panel_ = split.controlsPanel;
        preview_panel_ = split.previewPanel;
        controls_layout_ = split.controlsLayout;
        preview_layout = split.previewLayout;
    }
    else
    {
        // A page with no preview holds its controls alone: a shell shows the target beside them.
        controls_panel_ = new QWidget(this);
        controls_panel_->setObjectName(QStringLiteral("controlsPanel"));
        controls_layout_ = new QVBoxLayout(controls_panel_);
        root_layout_->addWidget(controls_panel_, 1);
    }

    if (main_splitter_)
    {
        last_splitter_sizes_ = main_splitter_->sizes();
    }

    view_area_widget_ = preview_panel_ ? preview_panel_->findChild<QWidget*>(QStringLiteral("previewHost")) : nullptr;
    if (!view_area_widget_)
    {
        view_area_widget_ = new QWidget(preview_panel_ ? preview_panel_ : this);
        if (preview_layout)
        {
            preview_layout->addWidget(view_area_widget_);
        }
        else
        {
            // A page with no preview has no view area to show.
            view_area_widget_->hide();
        }
    }
    view_area_layout_ = new QVBoxLayout(view_area_widget_);
    view_area_layout_->setContentsMargins(0, 0, 0, 0);
    view_area_layout_->setSpacing(0);

    placeholder_label_ = new QLabel(view_area_widget_);
    placeholder_label_->setWordWrap(true);
    placeholder_label_->setAlignment(Qt::AlignCenter);
    placeholder_label_->setText(QStringLiteral("Waiting for source image from host."));
    view_area_layout_->addWidget(placeholder_label_, 1);

    // The framework's preview view lands in the view area; the window keeps its own size whether it previews or not.
    m_ImagePreviewLayout = previews ? view_area_layout_ : nullptr;

    if (!shape.has_value())
    {
        param_scroll_area_ = new QScrollArea(controls_panel_ ? controls_panel_ : this);
        param_scroll_area_->setObjectName(QStringLiteral("anNativeProcessParamScrollArea"));
        param_scroll_area_->setWidgetResizable(true);
        param_scroll_area_->setFrameShape(QFrame::NoFrame);
        param_scroll_area_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        param_scroll_area_->hide();
    }

    // The extension hears each change as it is made, on this thread, as its window has always told it. A page's body
    // scrolls the panel itself.
    param_panel_ = new an::ui::extensions::ExtensionParamPanel(
        param_scroll_area_ ? static_cast<QWidget*>(param_scroll_area_) : controls_panel_);
    param_panel_->setEventDelivery(an::ui::extensions::ExtensionParamPanel::EventDelivery::EachChange);
    an::ui::extensions::ExtensionPanelHost panel_host;
    panel_host.interactionWatcher = this;
    panel_host.makeFrameHistogram = [this](QScrollArea* scroll_area) -> HistogramWidget* {
        m_HistogramPreview = an::ui::createPreviewHistogram(scroll_area, an::consumer::OperationPurpose::Preview);
        auto* histogram = dynamic_cast<HistogramWidget*>(m_HistogramPreview->widget());
        m_UseHistogramPreviewWidget = histogram != nullptr;
        return histogram;
    };
    panel_host.frameHistogramResolutionChosen = [this](const QString& resolution) {
        if (!m_HistogramPreview)
        {
            return;
        }
        m_HistogramPreview->setResolution(an::ui::extensions::previewHistogramResolutionNamed(resolution));
        if (updating_param_defs_)
        {
            return;
        }
        // The preview's histogram is made by the next frame at the new resolution, or counted again from what is
        // shown while the window does not preview.
        if (previewView())
        {
            requestFrame();
        }
        else
        {
            m_HistogramPreview->setImage(sourceImage());
        }
    };
    panel_host.histogramResolutionChosen = [this](HistogramWidget* histogram) {
        if (an::ui::HistogramCountScheduler* counts = histogramCountsFor(histogram))
        {
            counts->recount();
        }
    };
    panel_host.curveHistogramShown = [this](CurvesWidget*) { syncCurveEditorHistograms(); };
    panel_host.buildHosted = [this](const an::ui::extensions::PanelNode& node,
                                    const an::ui::extensions::ExtensionPanelHost::Placement& place) {
        return buildHostedControl(node, place);
    };
    param_panel_->setHost(std::move(panel_host));
    QObject::connect(param_panel_, &an::ui::extensions::ExtensionParamPanel::controlEvent, this,
                     [this](const QString& id, const QString& name, const QCborValue& value, bool) {
                         onPanelEvent(id, name, value);
                     });
    if (shape.has_value())
    {
        buildPageButtons();
    }
    else
    {
        buildWindowButtons();
    }

    syncEmbeddedViewLayout();

    setWindowTitle(QString::fromStdString(display_name));
    setProcessDescription(defaultNativeProcessDescription());
    if (shape.has_value())
    {
        // The framework presses the box's buttons, and a shell lays the page out from these parts.
        initializeStandardProcess(
            root_layout_, previews ? view_area_layout_ : nullptr, nullptr, button_box_, nullptr, nullptr,
            ProcessInitOptions{
                .supportPerChannel = false, .showWaitingSpinner = true, .previewAreaSelectionEnabled = true});
    }
    else
    {
        setProperty("anForceBottomActionBar", true);
        resize(kDefaultNativeProcessWindowSize);
    }
    initializeTopLevelProcess();
    refreshTheme();
}

void ExtensionNativeProcessWindow::buildPageButtons()
{
    // A page's buttons are a process's: Reset, Close and Apply in the box the framework presses and a shell shows in
    // its footer. The tool's Configure, for an extension that declares one, heads the controls.
    external_tool_config_button_ = new QPushButton(controls_panel_);
    external_tool_config_button_->setObjectName(QStringLiteral("anNativeProcessToolConfigButton"));
    external_tool_config_button_->setText(QStringLiteral("Configure"));
    external_tool_config_button_->setProperty("anProcessButtonRole", QStringLiteral("secondary"));
    external_tool_config_button_->hide();
    controls_layout_->addWidget(external_tool_config_button_, 0, Qt::AlignLeft);
    controls_layout_->addWidget(param_panel_, 0);
    controls_layout_->addStretch(1);

    button_box_ = new QDialogButtonBox(QDialogButtonBox::Apply | QDialogButtonBox::Close | QDialogButtonBox::Reset,
                                       Qt::Horizontal, controls_panel_);
    reset_button_ = button_box_->button(QDialogButtonBox::Reset);
    reset_button_->setObjectName(QStringLiteral("anNativeProcessResetButton"));
    execute_button_ = button_box_->button(QDialogButtonBox::Apply);
    execute_button_->setObjectName(QStringLiteral("anNativeProcessExecuteButton"));
    close_button_ = button_box_->button(QDialogButtonBox::Close);
    close_button_->setObjectName(QStringLiteral("anNativeProcessCloseButton"));
    close_button_default_text_ = close_button_->text();
    controls_layout_->addWidget(button_box_, 0);
    execute_button_->setEnabled(false);

    QObject::connect(external_tool_config_button_, &QPushButton::clicked, this,
                     [this]() { onExternalToolConfigClicked(); });
}

void ExtensionNativeProcessWindow::buildWindowButtons()
{
    param_scroll_area_->setWidget(param_panel_);

    action_bar_ = new QWidget(controls_panel_ ? controls_panel_ : this);
    action_bar_->setObjectName(QStringLiteral("anNativeProcessActionBar"));
    action_bar_->setProperty("anProcessActionBar", true);
    action_bar_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    auto* action_bar_layout = new QHBoxLayout(action_bar_);
    action_bar_layout->setContentsMargins(0, 0, 0, 0);
    action_bar_layout->setSpacing(an::ui::tokens().spacing.sm);

    external_tool_config_button_ = new QPushButton(action_bar_);
    external_tool_config_button_->setObjectName(QStringLiteral("anNativeProcessToolConfigButton"));
    external_tool_config_button_->setText(QStringLiteral("Configure"));
    external_tool_config_button_->setProperty("anProcessButtonRole", QStringLiteral("secondary"));
    external_tool_config_button_->hide();

    button_box_ = new QDialogButtonBox(QDialogButtonBox::Apply | QDialogButtonBox::Reset, Qt::Horizontal, action_bar_);
    if (auto* reset_button = button_box_->button(QDialogButtonBox::Reset))
    {
        button_box_->removeButton(reset_button);
        reset_button_ = reset_button;
        reset_button_->setParent(action_bar_);
        reset_button_->setObjectName(QStringLiteral("anNativeProcessResetButton"));
        reset_button_->setProperty("anProcessButtonRole", QStringLiteral("secondary"));
        reset_button_->hide();
    }
    execute_button_ = button_box_->button(QDialogButtonBox::Apply);
    execute_button_->setObjectName(QStringLiteral("anNativeProcessExecuteButton"));
    close_button_ = new QPushButton(tr("Close"), button_box_);
    button_box_->addButton(close_button_, QDialogButtonBox::ActionRole);
    close_button_->setObjectName(QStringLiteral("anNativeProcessCloseButton"));
    close_button_->setProperty("anProcessButtonRole", QStringLiteral("secondary"));
    close_button_default_text_ = close_button_->text();
    if (reset_button_)
    {
        action_bar_layout->addWidget(reset_button_, 0, Qt::AlignLeft);
    }
    action_bar_layout->addWidget(external_tool_config_button_, 0, Qt::AlignLeft);
    action_bar_layout->addStretch(1);
    action_bar_layout->addWidget(button_box_, 0, Qt::AlignRight);
    if (controls_layout_)
    {
        controls_layout_->addWidget(param_scroll_area_, 1);
        controls_layout_->addWidget(action_bar_, 0);
    }
    else
    {
        root_layout_->addWidget(param_scroll_area_);
        root_layout_->addWidget(action_bar_);
    }

    execute_button_->setEnabled(false);

    QObject::connect(button_box_, &QDialogButtonBox::clicked, this, [this](QAbstractButton* button) {
        if (button == execute_button_)
        {
            // In keep-open mode, Apply resets params and stays open.
            // In default mode, Apply commits and closes.
            close_after_execute_ = !keep_open_mode_;
            onExecuteClicked();
        }
        else if (button == close_button_)
        {
            if (execution_in_progress_)
            {
                onCancelClicked();
            }
            else if (close_callback_)
            {
                hide();
                close_callback_();
            }
            else
            {
                close();
            }
        }
        else if (button_box_->buttonRole(button) == QDialogButtonBox::ResetRole)
        {
            resetParamsToDefaults();
        }
    });
    if (reset_button_)
    {
        QObject::connect(reset_button_, &QPushButton::clicked, this, [this]() { resetParamsToDefaults(); });
    }
    QObject::connect(external_tool_config_button_, &QPushButton::clicked, this,
                     [this]() { onExternalToolConfigClicked(); });
}

ExtensionNativeProcessWindow::~ExtensionNativeProcessWindow()
{
    stopPreviews();
    destroyManualMaskCanvas();
    // The source view is the host's, lent while it is embedded: it is never deleted with the window.
    if (source_view_ && view_area_layout_ && source_view_->parent() == view_area_layout_->parentWidget())
    {
        view_area_layout_->removeWidget(source_view_);
        source_view_->setParent(nullptr);
    }
}

void ExtensionNativeProcessWindow::showEvent(QShowEvent* event)
{
    ImageRTPreviewControl::showEvent(event);
    if (closing_)
    {
        // Shown again after a close its session did not end on: it previews again.
        closing_ = false;
        syncPreviewControl();
        requestFrame();
    }
    applyNativeWindowSizeConstraints();

    const auto apply_deferred_constraints = [this]() { applyNativeWindowSizeConstraints(); };
    QTimer::singleShot(0, this, apply_deferred_constraints);
    QTimer::singleShot(30, this, apply_deferred_constraints);
}

void ExtensionNativeProcessWindow::refreshTheme()
{
    ImageRTPreviewControl::refreshTheme();

    if (root_layout_)
    {
        const auto& spacing = an::ui::tokens().spacing;
        root_layout_->setSpacing(0);
        root_layout_->setContentsMargins(0, 0, 0, 0);
    }

    if (controls_layout_)
    {
        const auto& t = an::ui::tokens();
        controls_layout_->setSpacing(t.spacing.md);
        if (page_shape_)
        {
            // A page's controls are its body, whose padding their side margins are.
            controls_layout_->setContentsMargins(t.metrics.pad, t.metrics.gap, t.metrics.pad, t.metrics.gap);
        }
        else
        {
            controls_layout_->setContentsMargins(0, 0, 0, 0);
        }
    }

    if (action_bar_)
    {
        if (auto* layout = qobject_cast<QHBoxLayout*>(action_bar_->layout()))
        {
            layout->setSpacing(an::ui::tokens().spacing.sm);
            layout->setContentsMargins(0, 0, 0, 0);
        }
    }

    if (target_selector_section_)
    {
        if (auto* layout = qobject_cast<QFormLayout*>(target_selector_section_->layout()))
        {
            const auto& spacing = an::ui::tokens().spacing;
            layout->setVerticalSpacing(spacing.xs);
            layout->setHorizontalSpacing(spacing.md);
            layout->setContentsMargins(0, 0, 0, 0);
        }
    }

    if (placeholder_label_)
    {
        placeholder_label_->setFont(an::ui::tokens().typography.body);
        QPalette palette = placeholder_label_->palette();
        palette.setColor(QPalette::WindowText, an::ui::tokens().colors.textSecondary);
        placeholder_label_->setPalette(palette);
    }

    if (external_tool_config_button_)
    {
        external_tool_config_button_->setProperty("anProcessButtonRole", QStringLiteral("secondary"));
        QIcon settings_icon;
        if (auto* context = an::ui::appContext())
        {
            if (const auto iconProvider = context->getIconProvider())
            {
                settings_icon = iconProvider->getIcon("settings");
            }
        }
        if (settings_icon.isNull() && style())
        {
            settings_icon = style()->standardIcon(QStyle::SP_FileDialogDetailedView);
        }
        external_tool_config_button_->setIcon(settings_icon);
        if (auto* style = external_tool_config_button_->style())
        {
            style->unpolish(external_tool_config_button_);
            style->polish(external_tool_config_button_);
        }
        external_tool_config_button_->update();
    }

    if (param_panel_)
    {
        param_panel_->refreshTheme();
    }
    panel_rows::applyRowLabelTheme(target_selector_label_);
    for (auto* widget : findChildren<QWidget*>())
    {
        if (auto* card = dynamic_cast<an::ui::AnCardWidget*>(widget))
        {
            card->refreshTheme();
        }
        else if (auto* banner = dynamic_cast<an::ui::AnInfoBanner*>(widget))
        {
            banner->refreshTheme();
        }
        else if (auto* section = dynamic_cast<an::ui::AnCollapsibleSection*>(widget))
        {
            section->refreshTheme();
        }
    }
}

void ExtensionNativeProcessWindow::showProcessMessage(const QString& title, const QString& message,
                                                      const ProcessMessageTone tone,
                                                      const std::optional<double> progressPercent)
{
    if (!header_progress_visible_ && tone == ProcessMessageTone::Info &&
        title.trimmed().compare(QStringLiteral("Working"), Qt::CaseInsensitive) == 0)
    {
        clearProcessMessage();
        return;
    }

    TopLevelProcessBaseControl::showProcessMessage(title, message, tone, progressPercent);
    const bool busy_message = title.trimmed().compare(QStringLiteral("Working"), Qt::CaseInsensitive) == 0 ||
                              title.trimmed().compare(QStringLiteral("Cancelling"), Qt::CaseInsensitive) == 0;
    if (header_progress_visible_ && tone == ProcessMessageTone::Info && busy_message && !progressPercent.has_value())
    {
        if (auto* progress = findChild<QProgressBar*>(QStringLiteral("anProcessShellBannerProgress")))
        {
            progress->setRange(0, 0);
            progress->setVisible(true);
        }
    }
    if (!header_progress_visible_)
    {
        if (auto* progress = findChild<QProgressBar*>(QStringLiteral("anProcessShellBannerProgress")))
        {
            progress->setVisible(false);
            progress->reset();
        }
    }
}

void ExtensionNativeProcessWindow::setHeaderProgressVisible(const bool visible)
{
    header_progress_visible_ = visible;
    if (!header_progress_visible_)
    {
        if (auto* progress = findChild<QProgressBar*>(QStringLiteral("anProcessShellBannerProgress")))
        {
            progress->setVisible(false);
            progress->reset();
        }
    }
}

void ExtensionNativeProcessWindow::setExecuteCallback(ExecuteCallback callback)
{
    execute_callback_ = std::move(callback);
    updateExecuteButtonEnabled();
}

void ExtensionNativeProcessWindow::requestClose()
{
    if (close_callback_)
    {
        closing_ = true;
        stopPreviews();
        hide();
        close_callback_();
        return;
    }
    close();
}

void ExtensionNativeProcessWindow::setCloseCallback(CloseCallback callback)
{
    close_callback_ = std::move(callback);
}

void ExtensionNativeProcessWindow::setCancelCallback(CancelCallback callback)
{
    cancel_callback_ = std::move(callback);
}

void ExtensionNativeProcessWindow::setKeepOpenMode(bool keep_open)
{
    keep_open_mode_ = keep_open;

    // In keep-open mode: Apply (reset+stay), Reset, Close.
    // In default mode:   Apply (commit+close), Close.
    if (reset_button_)
    {
        reset_button_->setVisible(keep_open);
    }

    // When not in keep-open mode, Apply behaves as apply-and-close,
    // so set the flag accordingly as the default.
    if (!keep_open)
    {
        close_after_execute_ = true;
    }
}

void ExtensionNativeProcessWindow::setExecutionInProgress(const bool in_progress)
{
    if (execution_in_progress_ == in_progress)
    {
        return;
    }

    execution_in_progress_ = in_progress;
    m_ExecutionInProgress = in_progress;
    if (in_progress)
    {
        execution_cancel_requested_ = false;
        close_after_execution_cancel_ = false;

        // The run reads the source view's own image, never a preview's: the frame on its way is refused and the
        // extension's run over it stopped, and the requests the run's end makes - its result shown, the parameters
        // reset - collapse into one frame of the parameters it leaves.
        cancelPreviews();
        holdPreviewRequests();

        execution_locked_widgets_.clear();
        const auto widgets = findChildren<QWidget*>();
        std::vector<QWidget*> candidates;
        candidates.reserve(static_cast<std::size_t>(widgets.size()));
        for (QWidget* widget : widgets)
        {
            if (!widget || widget == close_button_ || widget == button_box_ || widget->window() != this ||
                !widget->isEnabled() || !isExecutionLockCandidate(widget) ||
                std::find(candidates.begin(), candidates.end(), widget) != candidates.end())
            {
                continue;
            }
            candidates.push_back(widget);
        }

        execution_locked_widgets_.reserve(candidates.size());
        for (QWidget* widget : candidates)
        {
            execution_locked_widgets_.push_back({QPointer<QWidget>(widget), widget->isEnabled()});
            widget->setEnabled(false);
        }

        if (close_button_)
        {
            close_button_->setText(tr("Cancel"));
            close_button_->setEnabled(true);
        }
        return;
    }

    // What the run's end does next - show the result, reset the parameters, close - comes first, in the call that ended
    // it: the frame it is owed is asked for once the event loop turns.
    QMetaObject::invokeMethod(this, [this]() { releasePreviewRequests(); }, Qt::QueuedConnection);

    for (const auto& [widget, was_enabled] : execution_locked_widgets_)
    {
        if (widget)
        {
            widget->setEnabled(was_enabled);
        }
    }
    execution_locked_widgets_.clear();

    if (close_button_)
    {
        close_button_->setText(close_button_default_text_.isEmpty() ? tr("Close") : close_button_default_text_);
        close_button_->setEnabled(true);
    }
    updateExecuteButtonEnabled();
}

void ExtensionNativeProcessWindow::markExecutionCancelRequested() noexcept
{
    execution_cancel_requested_ = true;
    close_after_execute_ = false;
}

void ExtensionNativeProcessWindow::clearExecutionCancelRequested() noexcept
{
    execution_cancel_requested_ = false;
    close_after_execution_cancel_ = false;
}

void ExtensionNativeProcessWindow::requestCloseAfterExecutionCancel() noexcept
{
    execution_cancel_requested_ = true;
    close_after_execute_ = false;
    close_after_execution_cancel_ = true;
}

void ExtensionNativeProcessWindow::markManualPreviewRefreshCompleted(const bool success)
{
    if (!manual_preview_enabled_ || !manual_preview_refresh_in_flight_)
    {
        return;
    }

    manual_preview_refresh_in_flight_ = false;
    if (success && manual_preview_refresh_generation_ == manual_preview_dirty_generation_)
    {
        manual_preview_dirty_ = false;
    }
    updateManualPreviewRefreshUi();
}

void ExtensionNativeProcessWindow::clearCallbacks()
{
    execute_callback_ = {};
    close_callback_ = {};
    cancel_callback_ = {};
    param_action_callback_ = {};
    param_changed_callback_ = {};
    preview_body_.reset();
    stopPreviews();
    syncPreviewControl();
    if (execute_button_)
    {
        updateExecuteButtonEnabled();
    }
}

void ExtensionNativeProcessWindow::setPreviewBody(ExtensionPreviewBody body)
{
    // A frame already issued keeps the body it was issued with; the next one takes this.
    preview_body_ = body ? std::make_shared<const ExtensionPreviewBody>(std::move(body)) : nullptr;
    preview_disabled_ = false;
    syncPreviewControl();
    // A window bound to its view before it had a body previews now: a preview rendered on request is refreshed when
    // the window opens, whichever of the two came first.
    requestFrame();
}

void ExtensionNativeProcessWindow::setPreviewScaling(const an::ui::PreviewScaling scaling,
                                                     const std::uint32_t regionMargin)
{
    preview_scaling_ = scaling;
    preview_region_margin_ = regionMargin;
}

an::ui::PreviewScaling ExtensionNativeProcessWindow::previewScaling() const
{
    return preview_scaling_;
}

bool ExtensionNativeProcessWindow::bodyAdmitsWorkOfItsOwn() const
{
    // A star search, an image made from an array, any engine call of the SDK is admitted from inside the frame.
    return true;
}

void ExtensionNativeProcessWindow::stopPreviews()
{
    cancelPreviews();
}

void ExtensionNativeProcessWindow::pausePreviews()
{
    cancelPreviews();
    holdPreviewRequests();
    ++previews_paused_;
    // While the GUI thread is about to run the extension itself, an Apply would start a run that takes the extension's
    // lane first and leave the GUI thread waiting on it: the button refuses until the frames resume.
    updateExecuteButtonEnabled();
}

void ExtensionNativeProcessWindow::resumePreviews()
{
    if (previews_paused_ > 0)
    {
        --previews_paused_;
    }
    releasePreviewRequests();
    updateExecuteButtonEnabled();
}

bool ExtensionNativeProcessWindow::previewsPaused() const noexcept
{
    return previews_paused_ > 0;
}

/// What a frame of the window's preview read on the GUI thread when it was issued: what its worker hands the extension.
struct ExtensionNativeProcessWindow::PreviewParams final : ProcessExecutionParams
{
    /// The extension's preview, as it was set when the frame was issued.
    std::shared_ptr<const ExtensionPreviewBody> body;

    /// The parameters the window held.
    QCborMap params;

    /// The manual mask the person drew and uses, at the image's size, or null.
    SharedAsset manualMask;

    /// What the extension knows the image's view as, the image itself and its size.
    std::string viewName;
    std::string fileName;
    SharedAsset image;
    QSize imageSize;

    /// The session the frame's conversions of the mask belong to.
    const an::consumer::EngineSession* session = nullptr;

    /// Whether the preview shows the source's own image: while the person draws the manual mask on it, or shows it.
    bool showsSource = false;

    /// How far the extension's answer at a pixel reads around it, as its manifest states it.
    std::uint32_t regionMargin = 0u;
};

ImageRTPreviewControl::ProcessExecutionParamsPtr ExtensionNativeProcessWindow::getProcessParamsForExecution()
{
    auto snapshot = std::make_shared<PreviewParams>();
    snapshot->body = preview_body_;
    snapshot->params = currentParams();
    snapshot->showsSource =
        manual_mask_editing_enabled_ || (manualMaskDisplayModeSelected() && hasManualMask() && manualMaskUseChecked());
    // A frame that shows the source runs nothing and reads no mask; an Apply reads the mask the person uses.
    snapshot->manualMask = manualMaskImage();
    if (source_view_)
    {
        snapshot->viewName = source_view_->getUniqueName();
        snapshot->fileName = source_view_->getFileName();
        snapshot->image = sourceImage();
        snapshot->imageSize = imageBounds(snapshot->image).size();
    }
    snapshot->session = engineSession();
    snapshot->regionMargin = preview_region_margin_;
    return snapshot;
}

std::optional<ExtensionNativeProcessWindow::RunReading>
ExtensionNativeProcessWindow::runReading(const ProcessExecutionParams* params)
{
    const auto* snapshot = dynamic_cast<const PreviewParams*>(params);
    if (snapshot == nullptr)
    {
        return std::nullopt;
    }
    return RunReading{.params = snapshot->params,
                      .manualMask = snapshot->manualMask,
                      .viewName = snapshot->viewName,
                      .fileName = snapshot->fileName};
}

an::ui::ProcessBodyCoverage ExtensionNativeProcessWindow::processBodyCoverage(const ProcessExecutionParams*) const
{
    // The body replaces the destination with what the extension made, and reads none of it.
    return an::ui::ProcessBodyCoverage::WritesEverySample;
}

bool ExtensionNativeProcessWindow::isIdentity(const ProcessExecutionParams* params) const
{
    // While the manual mask is drawn on the source, or shown over it, the preview shows the source itself and runs
    // nothing.
    const auto* snapshot = dynamic_cast<const PreviewParams*>(params);
    return snapshot != nullptr && snapshot->showsSource;
}

std::uint32_t ExtensionNativeProcessWindow::previewRegionMargin(const ProcessExecutionParams* params) const
{
    const auto* snapshot = dynamic_cast<const PreviewParams*>(params);
    return snapshot != nullptr ? snapshot->regionMargin : 0u;
}

an::Status ExtensionNativeProcessWindow::executeProcessImpl(const an::ui::ProcessSource& source,
                                                            an::ui::ProcessDestination& destination,
                                                            const an::ui::ProcessRun& run,
                                                            const ProcessExecutionParams* params,
                                                            an::ui::ExecutionScope& scope)
{
    // Everything read here is the frame's snapshot: the window itself is not read on the worker.
    const auto* snapshot = dynamic_cast<const PreviewParams*>(params);
    if (snapshot == nullptr || !snapshot->body || !*snapshot->body)
    {
        return an::Status::failure(an::ErrorCode::NotInitialized, "the extension previews nothing");
    }

    // The manual mask laid over the frame's source as the source lies over the image: cut to its part, or reduced by
    // its factor.
    an::consumer::AssetReadLease mask_lease;
    std::optional<an::consumer::ImageAsset> fitted_mask;
    an::consumer::AssetReadLease fitted_lease;
    const an::consumer::AnyImage* mask = nullptr;
    if (snapshot->manualMask)
    {
        an::Result<an::consumer::AssetReadLease> read = snapshot->manualMask->read(scope.execution().stopToken());
        if (!read.hasValue())
        {
            return read.status();
        }
        mask_lease = std::move(*read);
        const std::optional<an::ImageDescriptor> source_described = source.descriptor();
        const std::optional<an::ImageDescriptor> mask_described = snapshot->manualMask->descriptor();
        const bool fits = source_described.has_value() && mask_described.has_value() &&
                          source_described->extent().width() == mask_described->extent().width() &&
                          source_described->extent().height() == mask_described->extent().height();
        const bool cut = run.partOfTarget && !run.part.isEmpty();
        if (fits)
        {
            mask = &mask_lease.image();
        }
        else if (snapshot->session == nullptr || (!cut && run.proxyFactor < 2u))
        {
            return an::Status::failure(an::ErrorCode::IncompatibleArguments,
                                       "the manual mask cannot be laid over the frame's image");
        }
        else
        {
            an::Result<an::consumer::ImageAsset> made =
                cut ? an::ui::cropProcessRegion(*snapshot->session, scope.engine(), scope.execution(),
                                                mask_lease.image(), mask_lease.validity(), run.part)
                    : an::ui::reduceForPreview(*snapshot->session, scope.engine(), scope.execution(), mask_lease,
                                               run.proxyFactor);
            if (!made.hasValue())
            {
                return made.status();
            }
            fitted_mask.emplace(std::move(*made));
            an::Result<an::consumer::AssetReadLease> fitted = fitted_mask->read(scope.execution().stopToken());
            if (!fitted.hasValue())
            {
                return fitted.status();
            }
            fitted_lease = std::move(*fitted);
            mask = &fitted_lease.image();
        }
    }

    ExtensionPreviewFrame frame;
    frame.source = &source.image();
    frame.part = run.partOfTarget ? run.part : QRect();
    if (!frame.part.isEmpty())
    {
        frame.image = snapshot->image;
    }
    frame.imageSize = snapshot->imageSize;
    frame.factor = run.proxyFactor;
    frame.params = snapshot->params;
    frame.mask = mask;
    frame.viewName = snapshot->viewName;
    frame.fileName = snapshot->fileName;
    frame.interactive = run.frameKind == an::ui::PreviewFrameKind::Interactive;
    frame.stop = scope.execution().stopToken();

    an::Result<an::consumer::ImageAsset> made = (*snapshot->body)(frame);
    if (!made.hasValue())
    {
        return made.status();
    }
    if (scope.stopRequested())
    {
        return an::Status::failure(an::ErrorCode::Cancelled, "the frame was stopped");
    }
    return destination.replace(std::move(*made));
}

void ExtensionNativeProcessWindow::setParamActionCallback(ParamActionCallback callback)
{
    if (!callback)
    {
        param_action_callback_ = nullptr;
        return;
    }
    param_action_callback_ = [callback = std::move(callback)](const std::string& action_id, const QCborMap& params,
                                                              ParamActionCompletion done) {
        done(callback(action_id, params));
    };
}

void ExtensionNativeProcessWindow::setParamActionCallback(AsyncParamActionCallback callback)
{
    param_action_callback_ = std::move(callback);
}

void ExtensionNativeProcessWindow::setParamChangedCallback(ParamChangedCallback callback)
{
    param_changed_callback_ = std::move(callback);
}

void ExtensionNativeProcessWindow::setSettingsCallbacks(SettingsGetCallback getCallback,
                                                        SettingsSetCallback setCallback)
{
    settings_get_callback_ = std::move(getCallback);
    settings_set_callback_ = std::move(setCallback);
    refreshExternalToolConfiguration();
}

void ExtensionNativeProcessWindow::setOpenSettingsCallback(OpenSettingsCallback callback)
{
    open_settings_callback_ = std::move(callback);
}

void ExtensionNativeProcessWindow::setParamDefs(const QJsonArray& defs)
{
    manual_preview_invalidating_param_ids_.clear();
    manual_preview_enabled_ = false;
    manual_preview_dirty_ = false;
    manual_preview_refresh_in_flight_ = false;
    manual_preview_refresh_on_open_pending_ = false;
    manual_preview_dirty_generation_ = 0;
    manual_preview_refresh_generation_ = 0;
    manual_preview_refresh_action_id_.clear();
    manual_preview_stale_text_ = tr("Preview update needed.");
    manual_preview_ready_text_ = tr("Preview is up to date.");
    manual_preview_updating_text_ = tr("Updating preview...");
    manual_preview_refresh_button_.clear();
    manual_preview_status_banner_.clear();
    destroyManualMaskCanvas();
    manual_mask_document_.reset();
    manual_mask_group_.clear();
    manual_mask_controls_widget_.clear();
    manual_mask_use_checkbox_.clear();
    manual_mask_edit_button_.clear();
    manual_mask_cancel_button_.clear();
    manual_mask_clear_button_.clear();
    manual_mask_display_combo_.clear();
    manual_mask_edit_snapshot_.reset();
    manual_mask_display_param_id_.clear();
    manual_mask_use_param_id_.clear();
    manual_mask_display_value_ = QVariant();
    manual_mask_previous_display_value_ = QVariant();
    manual_mask_editing_enabled_ = false;
    manual_mask_had_previous_display_value_ = false;
    manual_mask_use_default_ = false;
    manual_mask_edit_snapshot_use_checked_ = false;
    manual_mask_default_brush_size_ = 50;
    manual_mask_copy_.reset();
    manual_mask_copy_document_ = nullptr;
    preview_area_enabled_ = true;
    // The framework's histogram is the first real-time histogram parameter's; new definitions make it again.
    m_UseHistogramPreviewWidget = false;
    m_HistogramPreview.reset();
    configured_preview_autostretch_ = false;
    configured_preview_autostretch_default_ = false;
    preview_autostretch_default_applied_ = false;
    configured_sub_area_size_ = QSize();
    configured_sub_area_label_.clear();
    configured_controls_panel_width_ = 0;
    pending_target_selector_ = false;
    pending_target_channel_filter_.clear();
    pending_target_selector_label_.clear();
    pending_target_selector_position_.clear();
    configured_window_size_ = QSize();
    view_selector_params_.clear();
    available_view_selector_views_.clear();
    setProcessDescription(defaultNativeProcessDescription());
    setHeaderProgressVisible(true);
    if (controls_panel_)
    {
        controls_panel_->setMinimumWidth(0);
    }
    if (!page_shape_)
    {
        setFixedSizeWindow(false);
        setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
        setMinimumSize(0, 0);
    }
    updating_param_defs_ = true;
    resetExternalToolConfiguration();

    // A target selector the panel drew goes with the panel's controls; one the window drew beside them stays.
    if (target_selector_section_ && param_panel_->isAncestorOf(target_selector_section_))
    {
        target_selector_section_ = nullptr;
        target_selector_label_ = nullptr;
        target_selector_ = nullptr;
    }
    param_panel_->clear();

    for (const auto& entry_value : defs)
    {
        const QJsonObject obj = entry_value.toObject();
        const QString type = obj[QStringLiteral("type")].toString().trimmed().toLower();
        if (type != QLatin1String("meta"))
        {
            continue;
        }

        const QJsonValue tool_config_value = obj.value(QStringLiteral("tool_configuration"));
        if (tool_config_value.isObject())
        {
            parseExternalToolConfiguration(tool_config_value.toObject());
        }
    }

    ensureExternalToolConfigurationWidgets();

    // The window's own part of the list - its meta entries and the credits it shows with its description - in the
    // list's order; the panel draws the rest.
    bool enable_sub_area_by_default = false;
    for (const auto& entry_value : defs)
    {
        const QJsonObject obj = entry_value.toObject();
        const QString type = obj[QStringLiteral("type")].toString().trimmed().toLower();
        if (type == QLatin1String("info"))
        {
            const QString id = obj[QStringLiteral("id")].toString();
            const QString label = obj[QStringLiteral("label")].toString(id);
            const QString text = obj[QStringLiteral("text")].toString(label);
            if (isAttributionParam(obj))
            {
                const QString header_description = text.trimmed();
                if (!header_description.isEmpty())
                {
                    setProcessDescription(header_description);
                }
            }
            continue;
        }
        if (type != QLatin1String("meta"))
        {
            continue;
        }

        // "preview_surface" chose between a pane of the preview's own and the source pane showing it: a window that
        // previews now shows its frames in the one view it has, so an extension that still states it changes nothing.
        if (obj.contains(QStringLiteral("preview_area")))
        {
            preview_area_enabled_ = obj[QStringLiteral("preview_area")].toBool(true);
        }
        if (obj.contains(QStringLiteral("preview_area_enabled")))
        {
            preview_area_enabled_ = obj[QStringLiteral("preview_area_enabled")].toBool(true);
        }
        if (obj.contains(QStringLiteral("header_progress")))
        {
            setHeaderProgressVisible(obj[QStringLiteral("header_progress")].toBool(true));
        }
        if (obj.contains(QStringLiteral("show_header_progress")))
        {
            setHeaderProgressVisible(obj[QStringLiteral("show_header_progress")].toBool(true));
        }
        const QString header_description = headerDescriptionFromMeta(obj);
        if (!header_description.isEmpty())
        {
            setProcessDescription(header_description);
        }
        // "preview_hq_default" is read as nothing: every view carries each sample at full precision through its
        // stretch, so an extension that states it changes nothing.
        if (obj.contains(QStringLiteral("preview_autostretch")))
        {
            configured_preview_autostretch_ = obj[QStringLiteral("preview_autostretch")].toBool(false);
        }
        if (obj.contains(QStringLiteral("preview_autostretch_default")))
        {
            configured_preview_autostretch_default_ = obj[QStringLiteral("preview_autostretch_default")].toBool(false);
        }
        if (obj.contains(QStringLiteral("controls_panel_width")))
        {
            configured_controls_panel_width_ =
                (std::max)(1, panel_rows::clampToInt(obj[QStringLiteral("controls_panel_width")].toDouble(0)));
        }
        const auto window_config_value = obj.value(QStringLiteral("window"));
        const QJsonObject window_config =
            window_config_value.isObject() ? window_config_value.toObject() : QJsonObject();
        QSize requested_window_size = jsonSizePair(window_config.value(QStringLiteral("size")));
        if (!requested_window_size.isValid())
        {
            requested_window_size = jsonSizePair(obj.value(QStringLiteral("window_size")));
        }
        if (requested_window_size.isValid())
        {
            configured_window_size_ = requested_window_size;
        }
        // A page is sized by its host: what the extension says of its window's size is the window's alone.
        if (!page_shape_)
        {
            if (window_config.contains(QStringLiteral("fixed_size")))
            {
                setFixedSizeWindow(window_config.value(QStringLiteral("fixed_size")).toBool(false));
            }
            if (window_config.contains(QStringLiteral("fixed")))
            {
                setFixedSizeWindow(window_config.value(QStringLiteral("fixed")).toBool(false));
            }
            if (window_config.contains(QStringLiteral("resizable")))
            {
                setFixedSizeWindow(!window_config.value(QStringLiteral("resizable")).toBool(true));
            }
            if (obj.contains(QStringLiteral("fixed_size")))
            {
                setFixedSizeWindow(obj[QStringLiteral("fixed_size")].toBool(false));
            }
            if (obj.contains(QStringLiteral("resizable")))
            {
                setFixedSizeWindow(!obj[QStringLiteral("resizable")].toBool(true));
            }
        }
        applyNativeWindowSizeConstraints();
        if (preview_area_enabled_ && obj[QStringLiteral("sub_area")].toBool(false))
        {
            const auto size_arr = obj[QStringLiteral("sub_area_size")].toArray();
            const int w = size_arr.size() >= 1 ? size_arr[0].toInt(800) : 800;
            const int h = size_arr.size() >= 2 ? size_arr[1].toInt(600) : 600;
            const QString sub_label = obj[QStringLiteral("sub_area_label")].toString();
            enable_sub_area_by_default = obj[QStringLiteral("sub_area_default_enabled")].toBool(true);
            configured_sub_area_size_ = QSize(w, h);
            configured_sub_area_label_ = sub_label;
        }
        if (obj[QStringLiteral("target_selector")].toBool(false))
        {
            const auto filter_arr = obj[QStringLiteral("target_channel_filter")].toArray();
            pending_target_channel_filter_.clear();
            for (const auto& val : filter_arr)
            {
                pending_target_channel_filter_.push_back(val.toInt());
            }
            pending_target_selector_label_ = obj.value(QStringLiteral("target_selector_label")).toString();
            pending_target_selector_position_ =
                obj.value(QStringLiteral("target_selector_position")).toString().trimmed().toLower();
            pending_target_selector_ = true;
        }
    }

    an::ui::extensions::PanelReadingNotes notes;
    const an::ui::extensions::ExtensionPanelDescription description =
        an::ui::extensions::panelDescriptionFromParamDefs(defs, &notes);
    for (const QString& left_out : notes.leftOut)
    {
        LOG_DEBUG("Native process window: the parameter list's {} is not drawn", left_out.toStdString());
    }
    std::string refusal;
    if (!param_panel_->setDescription(description, &refusal))
    {
        LOG_WARNING("Native process window: the extension's parameters were refused: {}", refusal);
        showProcessMessage(tr("Parameters refused"), QString::fromStdString(refusal), ProcessMessageTone::Error);
    }
    adoptPanelControls();

    if (param_scroll_area_)
    {
        param_scroll_area_->setVisible(param_panel_->hasVisibleContent());
    }

    // A page's column is as wide as its process declares (`processColumnWidth`), the panel's width never more.
    if (controls_panel_ && configured_controls_panel_width_ > 0 && !page_shape_)
    {
        controls_panel_->setMinimumWidth(configured_controls_panel_width_);
        if (main_splitter_ && preview_panel_)
        {
            const int total_width = (std::max)(width(), configured_controls_panel_width_ +
                                                            an::ui::processes::rt_preview_ui::kMinPreviewPaneWidth);
            const int preview_width = (std::max)(an::ui::processes::rt_preview_ui::kMinPreviewPaneWidth,
                                                 total_width - configured_controls_panel_width_);
            main_splitter_->setSizes({configured_controls_panel_width_, preview_width});
            last_splitter_sizes_ = main_splitter_->sizes();
        }
    }

    setPreviewAreaSelectionEnabled(preview_area_enabled_);
    applyPreviewMeta();
    if (!preview_area_enabled_)
    {
        enable_sub_area_by_default = false;
    }
    if (enable_sub_area_by_default)
    {
        enableSubArea(configured_sub_area_size_, configured_sub_area_label_);
    }
    else
    {
        disableSubArea();
    }

    updating_param_defs_ = false;
    param_panel_->updateReadouts();
    param_panel_->updateConditions();
    updateManualPreviewRefreshUi();
    if (m_HistogramPreview && !previewView())
    {
        m_HistogramPreview->setImage(sourceImage());
    }
    syncCurveEditorHistograms();
    refreshTheme();
    fitWindowToVisibleContents();
    requestFrame();
}

QWidget*
ExtensionNativeProcessWindow::buildHostedControl(const an::ui::extensions::PanelNode& node,
                                                 const an::ui::extensions::ExtensionPanelHost::Placement& place)
{
    const QJsonObject obj = node.fields.toJsonObject();
    if (node.kind == QLatin1String("manual_mask_editor"))
    {
        setupManualMaskEditor(obj, place());
        return manual_mask_group_.data();
    }
    if (node.kind != QLatin1String("target_selector") || target_selector_)
    {
        return nullptr;
    }

    const QString id = node.id();
    const QString label = obj[QStringLiteral("label")].toString(id);
    auto* row = new QWidget(param_panel_);
    row->setProperty("anParamId", id);
    auto* row_layout = new QGridLayout(row);
    panel_rows::setUpRowGrid(row_layout);

    auto* title = new QLabel(label, row);
    title->setObjectName(QStringLiteral("anNativeParamLabel"));
    panel_rows::setUpRowLabel(title);
    const QString tooltip = tooltipText(obj);
    applyTooltip(row, tooltip);
    applyTooltip(title, tooltip);
    row_layout->addWidget(title, 0, 0, Qt::AlignLeft | Qt::AlignVCenter);

    auto* combo = new QComboBox(row);
    combo->setObjectName(QStringLiteral("anNativeProcessTargetSelector"));
    applyTooltip(combo, tooltip);
    combo->setMinimumWidth(150);
    combo->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    row_layout->addWidget(combo, 0, 1);

    target_selector_section_ = row;
    target_selector_label_ = title;
    target_selector_ = combo;

    QObject::connect(target_selector_, &QComboBox::currentTextChanged, this, [this](const QString& text) {
        refreshViewSelectorParams();
        if (target_changed_callback_)
        {
            target_changed_callback_(text.toStdString());
        }
    });

    place()->addWidget(row);
    return row;
}

void ExtensionNativeProcessWindow::adoptPanelControls()
{
    const auto& description = param_panel_->description();
    if (!description.has_value())
    {
        return;
    }

    const auto registerRefreshButton = [this](const QString& action_id, const bool refresh_on_open) {
        if (QPushButton* button = param_panel_->actionButton(action_id))
        {
            registerManualPreviewRefreshButton(button, action_id);
            if (refresh_on_open)
            {
                manual_preview_refresh_on_open_pending_ = true;
                updateManualPreviewRefreshUi();
            }
        }
    };

    std::function<void(const std::vector<an::ui::extensions::PanelNode>&)> adopt;
    adopt = [&](const std::vector<an::ui::extensions::PanelNode>& nodes) {
        for (const an::ui::extensions::PanelNode& node : nodes)
        {
            const QString id = node.id();
            const QString& kind = node.kind;
            if (kind == QLatin1String("float") || kind == QLatin1String("int") || kind == QLatin1String("bool") ||
                kind == QLatin1String("choice") || kind == QLatin1String("view_selector") ||
                kind == QLatin1String("string"))
            {
                if (node.field(u"preview_invalidates").toBool(false) && param_panel_->control(id))
                {
                    manual_preview_invalidating_param_ids_.insert(id.toStdString());
                }
            }
            if (kind == QLatin1String("choice"))
            {
                for (const QCborValue& action_value : node.field(u"inline_actions").toArray())
                {
                    const QCborMap action = action_value.toMap();
                    if (action.value(QStringLiteral("preview_refresh_role")).toBool(false))
                    {
                        registerRefreshButton(action.value(QStringLiteral("id")).toString().trimmed(),
                                              action.value(QStringLiteral("preview_refresh_on_open")).toBool(false));
                    }
                }
            }
            if (kind == QLatin1String("action") && node.field(u"preview_refresh_role").toBool(false))
            {
                registerRefreshButton(id, node.field(u"preview_refresh_on_open").toBool(false));
            }
            if (kind == QLatin1String("info") && node.field(u"preview_status").toBool(false))
            {
                if (auto* banner = dynamic_cast<an::ui::AnInfoBanner*>(param_panel_->widgetOf(node)))
                {
                    manual_preview_status_banner_ = banner;
                    const auto text = [&node](const char16_t* name, const QString& fallback) {
                        const QCborValue value = node.field(QStringView(name));
                        return value.isString() ? value.toString() : fallback;
                    };
                    manual_preview_stale_text_ = text(u"stale_text", manual_preview_stale_text_);
                    manual_preview_ready_text_ = text(u"ready_text", manual_preview_ready_text_);
                    manual_preview_updating_text_ = text(u"updating_text", manual_preview_updating_text_);
                    updateManualPreviewRefreshUi();
                }
            }
            if (kind == QLatin1String("view_selector"))
            {
                if (auto* combo = qobject_cast<QComboBox*>(param_panel_->control(id)))
                {
                    ViewSelectorParam selector;
                    selector.id = id;
                    selector.combo = combo;
                    selector.default_value = node.field(u"default").toString();
                    selector.placeholder = combo->count() > 0 ? combo->itemText(0) : tr("Select image");
                    selector.match_target_geometry = node.field(u"match_target_geometry").toBool(false);
                    selector.match_target_channels = node.field(u"match_target_channels").toBool(false);
                    selector.exclude_target = node.field(u"exclude_target").toBool(false);
                    for (const QCborValue& channels : node.field(u"channel_filter").toArray())
                    {
                        selector.channel_filter.push_back(panel_rows::clampToInt(
                            channels.isInteger() ? static_cast<double>(channels.toInteger()) : channels.toDouble()));
                    }
                    view_selector_params_.push_back(std::move(selector));
                }
            }
            adopt(node.children);
        }
    };
    adopt(description->nodes);
}

void ExtensionNativeProcessWindow::onPanelEvent(const QString& id, const QString& name, const QCborValue&)
{
    if (name == QLatin1String("value_changed"))
    {
        // A curve's change has always reached the extension with no key named: its release names it.
        onInteractiveParamChanged(param_panel_->kindOf(id) == QLatin1String("curve_editor") ? QString() : id);
        return;
    }
    if (name == QLatin1String("clicked"))
    {
        onParamActionClicked(id);
        return;
    }
    if (name == QLatin1String("pressed"))
    {
        // A curve dragged is shown in interactive frames, as a dragged slider is, and settled when let go.
        curve_edit_in_progress_ = true;
        curve_edit_pending_preview_ = false;
        controlHeld(param_panel_->control(id), true);
        return;
    }
    if (name == QLatin1String("released"))
    {
        const bool should_refresh = curve_edit_pending_preview_;
        curve_edit_in_progress_ = false;
        curve_edit_pending_preview_ = false;
        controlHeld(param_panel_->control(id), false);
        if (!updating_param_defs_ && should_refresh && param_changed_callback_)
        {
            QStringList changed_keys;
            if (!id.isEmpty())
            {
                changed_keys.push_back(id);
            }
            param_changed_callback_(currentParams(), changed_keys, QStringLiteral("user"));
        }
    }
}

QCborMap ExtensionNativeProcessWindow::currentParams() const
{
    return param_panel_ ? param_panel_->values() : QCborMap();
}

bool ExtensionNativeProcessWindow::hasManualMask() const
{
    return manual_mask_document_ && !manual_mask_document_->isEmpty();
}

bool ExtensionNativeProcessWindow::manualMaskDisplayModeSelected() const
{
    if (manual_mask_display_param_id_.isEmpty() || !manual_mask_display_value_.isValid())
    {
        return false;
    }

    const QCborMap params = currentParams();
    const auto it = params.find(manual_mask_display_param_id_);
    if (it == params.end())
    {
        return false;
    }

    return panel_rows::valuesEqual(it.value().toVariant(), manual_mask_display_value_);
}

ExtensionNativeProcessWindow::SharedAsset ExtensionNativeProcessWindow::manualMaskImage() const
{
    if (!hasManualMask() || !manualMaskUseChecked())
    {
        return nullptr;
    }

    // A copy is taken once per change of the drawing: the frames of a drag read the same one.
    if (manual_mask_copy_ && manual_mask_copy_document_ == manual_mask_document_.get() &&
        manual_mask_copy_revision_ == manual_mask_document_->revision())
    {
        return manual_mask_copy_;
    }
    an::Result<an::consumer::ImageAsset> copy = manual_mask_document_->createOutputMask();
    if (!copy.hasValue())
    {
        LOG_WARNING("Native process window: the manual mask could not be copied: {}", copy.status().message());
        return nullptr;
    }
    manual_mask_copy_ = std::make_shared<const an::consumer::ImageAsset>(std::move(*copy));
    manual_mask_copy_document_ = manual_mask_document_.get();
    manual_mask_copy_revision_ = manual_mask_document_->revision();
    return manual_mask_copy_;
}

void ExtensionNativeProcessWindow::applyParamUpdates(const QCborMap& updates, const QString& reason)
{
    if (updates.isEmpty())
    {
        return;
    }

    updating_param_defs_ = true;
    const QStringList changed_keys = param_panel_->setValues(updates);
    bool changed_manual_preview_invalidating_param = false;
    for (const QString& param_id : changed_keys)
    {
        changed_manual_preview_invalidating_param =
            changed_manual_preview_invalidating_param || paramInvalidatesManualPreview(param_id);
        markManualPreviewDirty(param_id);
    }
    updating_param_defs_ = false;

    if (!changed_keys.isEmpty())
    {
        // The panel's readouts and conditions followed its values.
        updateManualMaskUiState();
        if (!changed_manual_preview_invalidating_param)
        {
            requestFrame();
        }
        if (param_changed_callback_)
        {
            param_changed_callback_(currentParams(), changed_keys, reason);
        }
    }
}

void ExtensionNativeProcessWindow::onParamActionClicked(const QString& actionId)
{
    // An action whose callback has yet to answer refuses the next: two answers would race for the parameters.
    if (!param_action_callback_ || actionId.isEmpty() || param_action_pending_)
    {
        return;
    }

    const bool is_manual_preview_refresh = manual_preview_enabled_ && actionId == manual_preview_refresh_action_id_;
    if (is_manual_preview_refresh)
    {
        manual_preview_refresh_in_flight_ = true;
        updateManualPreviewRefreshUi();
    }

    param_action_pending_ = true;
    const QPointer<ExtensionNativeProcessWindow> guard(this);
    param_action_callback_(actionId.toStdString(), currentParams(),
                           [guard, actionId, is_manual_preview_refresh](QCborMap updates) {
                               if (!guard)
                               {
                                   return;
                               }
                               guard->param_action_pending_ = false;
                               guard->applyParamUpdates(updates, QStringLiteral("param_action:%1").arg(actionId));
                               if (is_manual_preview_refresh)
                               {
                                   guard->manual_preview_refresh_generation_ = guard->manual_preview_dirty_generation_;
                               }
                               if (updates.isEmpty())
                               {
                                   guard->requestFrame();
                               }
                           });
}

void ExtensionNativeProcessWindow::setSourceView(ImageViewControl* view)
{
    if (source_view_ == view && m_image_view_control == view)
    {
        return;
    }

    // The view going is the host's, lent while it was embedded: it goes back parentless, never deleted with the window.
    if (source_view_ && source_view_ != view && view_area_layout_ &&
        source_view_->parent() == view_area_layout_->parentWidget())
    {
        view_area_layout_->removeWidget(source_view_);
        source_view_->setParent(nullptr);
    }

    if (view == nullptr)
    {
        // The frames over the image going away are refused before the window lets it go, and so is its preview.
        stopPreviews();
        destroyPreviewControl();
        if (m_image_view_control)
        {
            m_image_view_control->detachProcess(*this);
            setViewControl(nullptr);
        }
        source_view_ = nullptr;
        syncEmbeddedViewLayout();
        updateExecuteButtonEnabled();
        destroyManualMaskCanvas();
        manual_mask_document_.reset();
        manual_mask_copy_.reset();
        manual_mask_copy_document_ = nullptr;
        updateManualMaskUiState();
        updateManualPreviewRefreshUi();
        return;
    }

    source_view_ = view;
    if (m_image_view_control != view)
    {
        // The view becomes the preview's target: the frames over the one before are refused, and what they made of it
        // is let go.
        changeTargetViewControl(view);
    }

    syncPreviewControl();
    updateExecuteButtonEnabled();
    onSourceImageChanged();
    requestFrame();
    updateManualPreviewRefreshUi();
}

bool ExtensionNativeProcessWindow::wantsPreview() const
{
    return preview_body_ != nullptr && !preview_disabled_ && !closing_ && view_area_visible_ && source_view_ &&
           source_view_->hasImage();
}

bool ExtensionNativeProcessWindow::isPreviewEnabled() const
{
    return wantsPreview();
}

void ExtensionNativeProcessWindow::enablePreview(const bool enabled)
{
    // The framework turns the preview off when its view goes from under it; the window previews again when its body or
    // its image is set again.
    preview_disabled_ = !enabled;
    syncEmbeddedViewLayout();
}

void ExtensionNativeProcessWindow::syncPreviewControl()
{
    const bool wanted = wantsPreview();
    ImageViewControl* preview = previewView();
    if (wanted && !preview)
    {
        // The window keeps the size it has whether it previews or not.
        m_DefaultSizePreview = size();
        createPreviewControl();
    }
    else if (!wanted && preview)
    {
        m_DefaultSizeNoPreview = size();
        stopPreviews();
        destroyPreviewControl();
    }
    syncEmbeddedViewLayout();
}

void ExtensionNativeProcessWindow::onSourceImageChanged()
{
    syncPreviewControl();
    if (previewView())
    {
        // The original a person compares the preview with is the source's image as it is now.
        updateAlternateImage(true);
    }
    else if (m_UseHistogramPreviewWidget && m_HistogramPreview)
    {
        m_HistogramPreview->setImage(sourceImage());
    }
    syncCurveEditorHistograms();
    syncManualMaskDocumentToSource();
    syncManualMaskCanvas();
    updateManualMaskUiState();
    // A preview rendered on request was rendered from the image before this one, whether the view changed or its image
    // changed in place: another process, an undo, or this process's own Apply.
    markManualPreviewDirty();
}

void ExtensionNativeProcessWindow::onImageLoaded(ImageViewControl* ctrl)
{
    ImageRTPreviewControl::onImageLoaded(ctrl);
    if (ctrl != nullptr && ctrl == m_image_view_control)
    {
        onSourceImageChanged();
    }
}

void ExtensionNativeProcessWindow::onImageUpdated(ImageViewControl* ctrl)
{
    ImageRTPreviewControl::onImageUpdated(ctrl);
    if (ctrl != nullptr && ctrl == m_image_view_control)
    {
        onSourceImageChanged();
    }
}

void ExtensionNativeProcessWindow::onAlternateImageEnabled(ImageViewControl* ctrl)
{
    ImageRTPreviewControl::onAlternateImageEnabled(ctrl);
    syncCurveEditorHistograms();
}

void ExtensionNativeProcessWindow::onAlternateImageDisabled(ImageViewControl* ctrl)
{
    ImageRTPreviewControl::onAlternateImageDisabled(ctrl);
    syncCurveEditorHistograms();
}

void ExtensionNativeProcessWindow::syncEmbeddedViewLayout()
{
    if (!view_area_layout_ || !view_area_layout_->parentWidget())
    {
        return;
    }

    // The source view is lent and goes back parentless to its owner when the preview view shows the window's frames;
    // the placeholder is the window's own and stays its child, hidden wherever no slot of the layout shows it.
    if (placeholder_label_)
    {
        view_area_layout_->removeWidget(placeholder_label_);
    }
    if (source_view_ && source_view_->parent() == view_area_layout_->parentWidget())
    {
        view_area_layout_->removeWidget(source_view_);
        source_view_->setParent(nullptr);
    }

    if (ImageViewControl* preview = previewView())
    {
        // The framework put the preview view in the layout; it takes the area.
        view_area_layout_->removeWidget(preview);
        view_area_layout_->addWidget(preview, 1);
        preview->show();
        placeholder_label_->hide();
        return;
    }

    if (page_shape_)
    {
        // A page's source view is the application's own view of the target, never taken into the page: with no
        // preview view the page's canvas says why.
        placeholder_label_->hide();
    }
    else if (source_view_)
    {
        view_area_layout_->addWidget(source_view_, 1);
        source_view_->show();
        placeholder_label_->hide();
    }
    else
    {
        view_area_layout_->addWidget(placeholder_label_, 1);
        placeholder_label_->show();
    }
}

void ExtensionNativeProcessWindow::setViewAreaVisible(const bool visible)
{
    view_area_visible_ = visible;
    setPreviewPaneVisible(visible);
    syncPreviewControl();

    fitWindowToVisibleContents();
}

void ExtensionNativeProcessWindow::setPreviewPaneVisible(const bool visible)
{
    if (!main_splitter_ || !preview_panel_)
    {
        if (preview_panel_)
        {
            preview_panel_->setVisible(visible);
        }
        return;
    }

    if (visible)
    {
        preview_panel_->setMinimumWidth(an::ui::processes::rt_preview_ui::kMinPreviewPaneWidth);
        preview_panel_->setMaximumWidth(QWIDGETSIZE_MAX);
        preview_panel_->setVisible(true);
        if (auto* handle = main_splitter_->handle(1))
        {
            handle->setVisible(true);
        }

        if (last_splitter_sizes_.size() >= 2 && last_splitter_sizes_[0] > 0 && last_splitter_sizes_[1] > 0)
        {
            main_splitter_->setSizes(last_splitter_sizes_);
        }
        else
        {
            const int controls_width = controls_panel_ ? controls_panel_->sizeHint().width() : 320;
            const int preview_width = (std::max)(controls_width, width() - controls_width);
            main_splitter_->setSizes({controls_width, preview_width});
        }
        return;
    }

    const QList<int> current_sizes = main_splitter_->sizes();
    if (current_sizes.size() >= 2 && current_sizes[0] > 0 && current_sizes[1] > 0)
    {
        last_splitter_sizes_ = current_sizes;
    }

    preview_panel_->setVisible(false);
    preview_panel_->setMaximumWidth(0);
    preview_panel_->setMinimumWidth(0);
    if (auto* handle = main_splitter_->handle(1))
    {
        handle->setVisible(false);
    }

    const int total_width =
        controls_panel_ && preview_panel_ ? controls_panel_->width() + preview_panel_->width() : width();
    main_splitter_->setSizes({(std::max)(total_width, controls_panel_ ? controls_panel_->sizeHint().width() : 1), 0});
}

void ExtensionNativeProcessWindow::applyCommittedResult(an::consumer::ImageAsset&& image)
{
    if (!image.valid())
    {
        return;
    }

    // The result is what the next preview and execute start from: the source view shows it, and the preview follows
    // its image.
    showAssetInView(source_view_, std::move(image));
}

void ExtensionNativeProcessWindow::applyPreviewMeta()
{
    ImageViewControl* preview = previewView();
    if (!preview)
    {
        return;
    }

    // The window's preview view takes no snapshot: what the extension previews is only what its Apply commits.
    preview->setRTPreviewTakeSnapshotActionEnabled(false);
    if (!configured_preview_autostretch_)
    {
        preview->forceLinearMode();
    }
    preview->enableRTPreviewAutostretchControls(configured_preview_autostretch_);
    if (configured_preview_autostretch_ && configured_preview_autostretch_default_ &&
        !preview_autostretch_default_applied_)
    {
        selectRtPreviewAutostretchMode(preview);
        preview_autostretch_default_applied_ = true;
    }
}

void ExtensionNativeProcessWindow::previewControlStarted()
{
    preview_autostretch_default_applied_ = false;
    last_histogram_source_.reset();
    applyPreviewMeta();
    syncEmbeddedViewLayout();
    syncCurveEditorHistograms();
    syncManualMaskCanvas();
}

void ExtensionNativeProcessWindow::previewControlClosed()
{
    destroyManualMaskCanvas();
}

void ExtensionNativeProcessWindow::enableProcessControls(const bool enabled)
{
    ImageRTPreviewControl::enableProcessControls(enabled);
    if (ImageViewControl* preview = previewView())
    {
        preview->setRTPreviewTakeSnapshotActionEnabled(false);
    }
}

void ExtensionNativeProcessWindow::closeEvent(QCloseEvent* event)
{
    if (execution_in_progress_)
    {
        if (close_callback_)
        {
            hide();
            close_callback_();
            if (event)
            {
                event->ignore();
            }
            return;
        }
        if (event)
        {
            event->ignore();
        }
        onCancelClicked();
        return;
    }

    if (close_callback_)
    {
        // The session ends: nothing on its way is shown, and the extension's run over it is stopped.
        closing_ = true;
        stopPreviews();
        hide();
        close_callback_();
        if (event)
        {
            event->ignore();
        }
        return;
    }
    ImageRTPreviewControl::closeEvent(event);
}

void ExtensionNativeProcessWindow::onContentLayoutReplaced(QBoxLayout* contentLayout)
{
    // A window of a page's shape handed the framework its main layout, which the shell's wrapper has just replaced: the
    // framework follows it too, as every process of the framework's does.
    if (page_shape_)
    {
        ImageRTPreviewControl::onContentLayoutReplaced(contentLayout);
    }
    root_layout_ = qobject_cast<QHBoxLayout*>(contentLayout);
    if (root_layout_)
    {
        root_layout_->setContentsMargins(0, 0, 0, 0);
        root_layout_->setSpacing(0);
    }
}

void ExtensionNativeProcessWindow::onExecuteClicked()
{
    // While a parameter action or another run the GUI thread makes itself waits for the frames to land, an Apply would
    // take the extension's lane on a worker and leave that wait blocked on it once it took the lane in turn.
    if (execution_in_progress_ || previewsPaused())
    {
        return;
    }

    execution_cancel_requested_ = false;
    close_after_execution_cancel_ = false;
    if (execute_callback_)
    {
        execute_callback_();
    }
}

void ExtensionNativeProcessWindow::onCancelClicked()
{
    if (execution_in_progress_)
    {
        markExecutionCancelRequested();
    }
    if (cancel_callback_)
    {
        cancel_callback_();
    }
}

void ExtensionNativeProcessWindow::updateExecuteButtonEnabled()
{
    // Every change of the tool's resolution ends here.
    externalToolRecordMayHaveChanged();
    if (!execute_button_)
    {
        return;
    }
    if (execution_in_progress_ || previewsPaused())
    {
        execute_button_->setEnabled(false);
        return;
    }

    const bool version_query_pending =
        external_tool_resolved_record_.value(QStringLiteral("resolved_cli_version_pending")).toBool();
    const bool external_tool_ready =
        !external_tool_config_enabled_ || (external_tool_config_valid_ && !version_query_pending);
    const bool applies = page_shape_ ? source_view_ != nullptr && source_view_->hasImage()
                                     : static_cast<bool>(execute_callback_) && source_view_ != nullptr;
    execute_button_->setEnabled(applies && acceptsRuns() && external_tool_ready && !manual_mask_editing_enabled_);
}

void ExtensionNativeProcessWindow::refreshApplyEnabled()
{
    updateExecuteButtonEnabled();
}

void ExtensionNativeProcessWindow::insertAtControlsHead(QWidget* widget)
{
    if (widget == nullptr || controls_layout_ == nullptr)
    {
        return;
    }
    controls_layout_->insertWidget(0, widget, 0);
}

void ExtensionNativeProcessWindow::setControlsEnabled(const bool enabled)
{
    for (QWidget* widget : {static_cast<QWidget*>(param_panel_), target_selector_section_,
                            static_cast<QWidget*>(external_tool_config_button_), static_cast<QWidget*>(reset_button_)})
    {
        if (widget != nullptr)
        {
            widget->setEnabled(enabled);
        }
    }
}

void ExtensionNativeProcessWindow::resetExternalToolConfiguration()
{
    ++external_tool_version_query_generation_;
    if (external_tool_version_process_)
    {
        external_tool_version_process_->disconnect(this);
        external_tool_version_process_->kill();
        external_tool_version_process_->deleteLater();
        external_tool_version_process_ = nullptr;
    }
    external_tool_version_query_executable_.clear();
    external_tool_config_enabled_ = false;
    external_tool_config_valid_ = true;
    external_tool_append_platform_suffix_ = true;
    external_tool_require_executable_ = true;
    external_tool_show_configured_banner_ = true;
    external_tool_open_extension_settings_ = false;
    external_tool_setting_key_.clear();
    external_tool_label_.clear();
    external_tool_button_label_.clear();
    external_tool_dialog_title_.clear();
    external_tool_dialog_size_ = QSize();
    external_tool_not_configured_text_.clear();
    external_tool_configured_text_.clear();
    external_tool_invalid_text_.clear();
    external_tool_download_page_url_.clear();
    external_tool_download_button_label_.clear();
    external_tool_update_page_url_.clear();
    external_tool_update_api_url_.clear();
    external_tool_install_instructions_.clear();
    external_tool_download_note_.clear();
    external_tool_required_files_.clear();
    external_tool_candidate_directories_.clear();
    external_tool_candidate_subdirectories_.clear();
    external_tool_primary_executable_.clear();
    external_tool_executable_candidates_.clear();
    external_tool_version_arguments_.clear();
    external_tool_version_pattern_.clear();
    external_tool_resolved_record_ = QCborMap{};
    external_tool_status_banner_ = nullptr;
    if (external_tool_config_button_)
    {
        external_tool_config_button_->hide();
    }
    updateExecuteButtonEnabled();
}

void ExtensionNativeProcessWindow::parseExternalToolConfiguration(const QJsonObject& config)
{
    const QString setting_key = config.value(QStringLiteral("settings_key")).toString().trimmed();
    if (setting_key.isEmpty())
    {
        return;
    }

    external_tool_config_enabled_ = true;
    external_tool_config_valid_ = false;
    external_tool_setting_key_ = setting_key;
    external_tool_label_ = config.value(QStringLiteral("label")).toString(QStringLiteral("External tool")).trimmed();
    if (external_tool_label_.isEmpty())
    {
        external_tool_label_ = QStringLiteral("External tool");
    }
    external_tool_button_label_ =
        config.value(QStringLiteral("button_label")).toString(QStringLiteral("Configure")).trimmed();
    if (external_tool_button_label_.isEmpty())
    {
        external_tool_button_label_ = QStringLiteral("Configure");
    }
    external_tool_dialog_title_ = config.value(QStringLiteral("dialog_title"))
                                      .toString(QStringLiteral("Select %1 Folder").arg(external_tool_label_))
                                      .trimmed();
    if (external_tool_dialog_title_.isEmpty())
    {
        external_tool_dialog_title_ = QStringLiteral("Select %1 Folder").arg(external_tool_label_);
    }
    external_tool_dialog_size_ = jsonSizePair(config.value(QStringLiteral("dialog_size")));
    external_tool_not_configured_text_ =
        config.value(QStringLiteral("not_configured_text"))
            .toString(QStringLiteral("%1 is not configured. Click Configure to select the installation folder.")
                          .arg(external_tool_label_))
            .trimmed();
    external_tool_configured_text_ =
        config.value(QStringLiteral("configured_text"))
            .toString(QStringLiteral("%1 is configured and ready to use.").arg(external_tool_label_))
            .trimmed();
    external_tool_invalid_text_ = config.value(QStringLiteral("invalid_text")).toString().trimmed();
    external_tool_append_platform_suffix_ =
        config.value(QStringLiteral("append_platform_executable_suffix")).toBool(true);
    external_tool_require_executable_ = config.value(QStringLiteral("require_executable")).toBool(true);
    external_tool_show_configured_banner_ = config.value(QStringLiteral("show_configured_banner")).toBool(true);
    external_tool_open_extension_settings_ = config.value(QStringLiteral("open_extension_settings")).toBool(false);
    external_tool_download_page_url_ = platformStringValue(config, QStringLiteral("platform_download_page_urls"));
    if (external_tool_download_page_url_.isEmpty())
    {
        external_tool_download_page_url_ = platformStringValue(config, QStringLiteral("download_page_url"));
    }
    external_tool_download_button_label_ = config.value(QStringLiteral("download_button_label")).toString().trimmed();
    external_tool_update_page_url_ = platformStringValue(config, QStringLiteral("platform_update_page_urls"));
    if (external_tool_update_page_url_.isEmpty())
    {
        external_tool_update_page_url_ = platformStringValue(config, QStringLiteral("update_page_url"));
    }
    external_tool_update_api_url_ = platformStringValue(config, QStringLiteral("platform_update_api_urls"));
    if (external_tool_update_api_url_.isEmpty())
    {
        external_tool_update_api_url_ = platformStringValue(config, QStringLiteral("update_api_url"));
    }
    external_tool_install_instructions_ = config.value(QStringLiteral("install_instructions")).toString().trimmed();
    external_tool_download_note_ = config.value(QStringLiteral("download_note")).toString().trimmed();

    const auto append_required_files = [this](const QJsonValue& value) {
        const QJsonArray array = value.toArray();
        for (const QJsonValue& item : array)
        {
            const QString file_name = item.toString().trimmed();
            if (!file_name.isEmpty() && !external_tool_required_files_.contains(file_name))
            {
                external_tool_required_files_.push_back(file_name);
            }
        }
    };
    append_required_files(config.value(QStringLiteral("required_files")));
    append_required_files(config.value(QStringLiteral("required_executables")));
    external_tool_candidate_directories_ = platformStringListValue(config, QStringLiteral("candidate_directories"));
    external_tool_candidate_subdirectories_ =
        platformStringListValue(config, QStringLiteral("candidate_subdirectories"));
    for (const QString& directory : platformStringListValue(config, QStringLiteral("platform_candidate_directories")))
    {
        if (!external_tool_candidate_directories_.contains(directory))
        {
            external_tool_candidate_directories_.push_back(directory);
        }
    }
    external_tool_primary_executable_ = config.value(QStringLiteral("primary_executable")).toString().trimmed();
    if (external_tool_primary_executable_.isEmpty() && !external_tool_required_files_.isEmpty())
    {
        external_tool_primary_executable_ = external_tool_required_files_.front();
    }
    external_tool_executable_candidates_ =
        platformStringListValue(config, QStringLiteral("primary_executable_candidates"));
    if (!external_tool_primary_executable_.isEmpty() &&
        !external_tool_executable_candidates_.contains(external_tool_primary_executable_))
    {
        external_tool_executable_candidates_.push_front(external_tool_primary_executable_);
    }
    if (external_tool_executable_candidates_.isEmpty() && !external_tool_primary_executable_.isEmpty())
    {
        external_tool_executable_candidates_.push_back(external_tool_primary_executable_);
    }
    external_tool_version_arguments_ = platformStringListValue(config, QStringLiteral("version_arguments"));
    external_tool_version_pattern_ = config.value(QStringLiteral("version_pattern")).toString().trimmed();

    if (external_tool_config_button_)
    {
        external_tool_config_button_->setText(external_tool_button_label_);
        external_tool_config_button_->setToolTip(external_tool_dialog_title_);
        external_tool_config_button_->show();
    }
}

void ExtensionNativeProcessWindow::ensureExternalToolConfigurationWidgets()
{
    if (!param_panel_ || !external_tool_config_enabled_)
    {
        return;
    }

    external_tool_status_banner_ =
        new an::ui::AnInfoBanner(external_tool_not_configured_text_, an::ui::AnInfoBanner::Tone::Warning, param_panel_);
    external_tool_status_banner_->setObjectName(QStringLiteral("anNativeProcessToolConfigBanner"));
    param_panel_->addLeadingWidget(external_tool_status_banner_);
    refreshExternalToolConfiguration();
}

QString ExtensionNativeProcessWindow::currentExternalToolDirectory() const
{
    if (!external_tool_config_enabled_ || external_tool_setting_key_.isEmpty())
    {
        return {};
    }

    const QCborValue default_value{QString()};
    const QCborValue value =
        settings_get_callback_ ? settings_get_callback_(external_tool_setting_key_, default_value) : default_value;
    if (value.isString())
    {
        return value.toString().trimmed();
    }

    return value.toVariant().toString().trimmed();
}

QString ExtensionNativeProcessWindow::platformAdjustedRequiredFile(const QString& fileName) const
{
    QString adjusted = fileName.trimmed();
#ifdef _WIN32
    if (external_tool_append_platform_suffix_ && !adjusted.endsWith(QStringLiteral(".exe"), Qt::CaseInsensitive) &&
        !adjusted.endsWith(QStringLiteral(".cmd"), Qt::CaseInsensitive) &&
        !adjusted.endsWith(QStringLiteral(".bat"), Qt::CaseInsensitive))
    {
        adjusted += QStringLiteral(".exe");
    }
#endif
    return adjusted;
}

QString ExtensionNativeProcessWindow::externalToolExecutableInDirectory(const QString& directory) const
{
    const QString trimmed_directory = directory.trimmed();
    if (trimmed_directory.isEmpty() || external_tool_executable_candidates_.isEmpty())
    {
        return {};
    }

    QStringList relative_directories{QString()};
    for (const QString& raw_subdirectory : external_tool_candidate_subdirectories_)
    {
        const QString subdirectory = raw_subdirectory.trimmed();
        if (!subdirectory.isEmpty() && !relative_directories.contains(subdirectory))
        {
            relative_directories.push_back(subdirectory);
        }
    }

    for (const QString& relative_directory : relative_directories)
    {
        QDir tool_dir(trimmed_directory);
        if (!relative_directory.isEmpty())
        {
            tool_dir = QDir(tool_dir.filePath(relative_directory));
        }
        if (!tool_dir.exists())
        {
            continue;
        }

        const QStringList entries = tool_dir.entryList(QDir::Files | QDir::NoDotAndDotDot);
        for (const QString& raw_candidate : external_tool_executable_candidates_)
        {
            const QString candidate = platformAdjustedRequiredFile(raw_candidate);
            if (candidate.isEmpty())
            {
                continue;
            }

            const QFileInfo file_info(tool_dir.filePath(candidate));
            if (file_info.exists() && file_info.isFile())
            {
                return file_info.absoluteFilePath();
            }

            if (candidate.contains(QLatin1Char('/')) || candidate.contains(QDir::separator()))
            {
                continue;
            }

            for (const QString& entry : entries)
            {
                if (entry.compare(candidate, Qt::CaseInsensitive) == 0)
                {
                    return QFileInfo(tool_dir.filePath(entry)).absoluteFilePath();
                }
            }
        }
    }

    return {};
}

bool ExtensionNativeProcessWindow::validateExternalToolDirectory(const QString& directory, QString* outMessage) const
{
    const QString trimmed_directory = directory.trimmed();
    if (trimmed_directory.isEmpty())
    {
        if (outMessage)
        {
            *outMessage = external_tool_not_configured_text_;
        }
        return false;
    }

    const QFileInfo dir_info(trimmed_directory);
    if (!dir_info.exists() || !dir_info.isDir())
    {
        if (outMessage)
        {
            *outMessage =
                QStringLiteral("Configured %1 folder does not exist: %2").arg(external_tool_label_, trimmed_directory);
        }
        return false;
    }

    const QString executable_path = externalToolExecutableInDirectory(trimmed_directory);
    if (!external_tool_executable_candidates_.isEmpty())
    {
        if (executable_path.isEmpty())
        {
            if (outMessage)
            {
                const QString missing = external_tool_executable_candidates_.join(QStringLiteral(", "));
                const QString detail = QStringLiteral("Configured %1 folder is missing a supported executable: %2")
                                           .arg(external_tool_label_, missing);
                *outMessage = external_tool_invalid_text_.isEmpty() ? detail : external_tool_invalid_text_.arg(missing);
            }
            return false;
        }

        bool require_executable = external_tool_require_executable_;
#ifdef _WIN32
        const QString executable_name = QFileInfo(executable_path).fileName();
        if (require_executable && !external_tool_append_platform_suffix_ &&
            !executable_name.endsWith(QStringLiteral(".exe"), Qt::CaseInsensitive) &&
            !executable_name.endsWith(QStringLiteral(".cmd"), Qt::CaseInsensitive) &&
            !executable_name.endsWith(QStringLiteral(".bat"), Qt::CaseInsensitive))
        {
            require_executable = false;
        }
#endif
        if (require_executable && !QFileInfo(executable_path).isExecutable())
        {
            if (outMessage)
            {
                *outMessage = QStringLiteral("Configured %1 executable is not runnable: %2")
                                  .arg(external_tool_label_, executable_path);
            }
            return false;
        }
    }

    const QDir tool_dir(trimmed_directory);
    const auto resolved_file_info = [&tool_dir](const QString& required_file) {
        QFileInfo file_info(tool_dir.filePath(required_file));
        if (file_info.exists() && file_info.isFile())
        {
            return file_info;
        }

        if (required_file.contains(QLatin1Char('/')) || required_file.contains(QDir::separator()))
        {
            return file_info;
        }

        const QStringList entries = tool_dir.entryList(QDir::Files | QDir::NoDotAndDotDot);
        for (const QString& entry : entries)
        {
            if (entry.compare(required_file, Qt::CaseInsensitive) == 0)
            {
                return QFileInfo(tool_dir.filePath(entry));
            }
        }

        return file_info;
    };

    QStringList missing_files;
    QStringList not_runnable_files;
    for (const QString& raw_required_file : external_tool_required_files_)
    {
        const QString required_file = platformAdjustedRequiredFile(raw_required_file);
        const QFileInfo file_info = resolved_file_info(required_file);
        if (!file_info.exists() || !file_info.isFile())
        {
            missing_files.push_back(required_file);
            continue;
        }
        bool require_executable = external_tool_require_executable_;
#ifdef _WIN32
        if (require_executable && !external_tool_append_platform_suffix_ &&
            !required_file.endsWith(QStringLiteral(".exe"), Qt::CaseInsensitive))
        {
            // Qt does not classify extensionless files as executable on Windows, even
            // when an exact-name tool declaration intentionally disables .exe suffixing.
            require_executable = false;
        }
#endif
        if (require_executable && !file_info.isExecutable())
        {
            not_runnable_files.push_back(required_file);
        }
    }

    if (!missing_files.isEmpty())
    {
        if (outMessage)
        {
            const QString missing = missing_files.join(QStringLiteral(", "));
            const QString detail = QStringLiteral("Configured %1 folder is missing required executables: %2")
                                       .arg(external_tool_label_, missing);
            *outMessage = external_tool_invalid_text_.isEmpty() ? detail : external_tool_invalid_text_.arg(missing);
        }
        return false;
    }

    if (!not_runnable_files.isEmpty())
    {
        if (outMessage)
        {
            *outMessage = QStringLiteral("Configured %1 executables are not runnable: %2")
                              .arg(external_tool_label_, not_runnable_files.join(QStringLiteral(", ")));
        }
        return false;
    }

    if (outMessage)
    {
        *outMessage = external_tool_configured_text_;
    }
    return true;
}

ExtensionNativeProcessWindow::ExternalToolResolution ExtensionNativeProcessWindow::resolveExternalTool() const
{
    ExternalToolResolution resolution;
    resolution.configured_folder = currentExternalToolDirectory();

    if (!external_tool_config_enabled_)
    {
        resolution.valid = true;
        resolution.source = QStringLiteral("disabled");
        return resolution;
    }

    // The configured folder gets its own diagnostic slot. The candidate and PATH probes below are
    // a silent fallback, and letting them share this variable meant the banner reported whichever
    // location happened to be probed last - typically "Configured <tool> folder does not exist:
    // <some stale PATH entry>" - instead of the reason the folder the user actually chose was
    // rejected.
    QString message;
    QString configured_folder_message;
    if (!resolution.configured_folder.trimmed().isEmpty() &&
        validateExternalToolDirectory(resolution.configured_folder, &configured_folder_message))
    {
        resolution.valid = true;
        resolution.executable = externalToolExecutableInDirectory(resolution.configured_folder);
        resolution.source = QStringLiteral("settings");
        resolution.diagnostic = configured_folder_message;
        return resolution;
    }

    for (const QString& candidate_directory : external_tool_candidate_directories_)
    {
        if (validateExternalToolDirectory(candidate_directory, &message))
        {
            resolution.valid = true;
            resolution.configured_folder = candidate_directory;
            resolution.executable = externalToolExecutableInDirectory(candidate_directory);
            resolution.source = QStringLiteral("candidate_directory");
            resolution.diagnostic = message;
            return resolution;
        }
    }

    const QString path_text = QProcessEnvironment::systemEnvironment().value(QStringLiteral("PATH"));
    const QChar path_separator =
#ifdef _WIN32
        QLatin1Char(';');
#else
        QLatin1Char(':');
#endif
    for (const QString& path_directory : path_text.split(path_separator, Qt::SkipEmptyParts))
    {
        if (validateExternalToolDirectory(path_directory, &message))
        {
            resolution.valid = true;
            resolution.configured_folder = path_directory;
            resolution.executable = externalToolExecutableInDirectory(path_directory);
            resolution.source = QStringLiteral("path");
            resolution.diagnostic = message;
            return resolution;
        }
    }

    // Nothing configured, or the configured folder was rejected: report why that folder failed, not
    // why the fallback search did.
    resolution.diagnostic =
        configured_folder_message.isEmpty() ? external_tool_not_configured_text_ : configured_folder_message;
    return resolution;
}

void ExtensionNativeProcessWindow::startExternalToolVersionQuery(const QString& executablePath)
{
    if (executablePath.trimmed().isEmpty())
    {
        return;
    }

    if (external_tool_version_process_ && external_tool_version_query_executable_ == executablePath &&
        external_tool_version_process_->state() != QProcess::NotRunning)
    {
        external_tool_resolved_record_.insert(QStringLiteral("resolved_cli_version_pending"), true);
        updateExecuteButtonEnabled();
        return;
    }

    const int query_generation = ++external_tool_version_query_generation_;
    if (external_tool_version_process_)
    {
        external_tool_version_process_->disconnect(this);
        external_tool_version_process_->kill();
        external_tool_version_process_->deleteLater();
    }

    auto* process = new QProcess(this);
    external_tool_version_process_ = process;
    external_tool_version_query_executable_ = executablePath;
    external_tool_resolved_record_.insert(QStringLiteral("resolved_cli_version_pending"), true);
    updateExecuteButtonEnabled();
    process->setProgram(executablePath);
    process->setArguments(external_tool_version_arguments_.isEmpty() ? QStringList{QStringLiteral("--version")}
                                                                     : external_tool_version_arguments_);
    QObject::connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
                     [this, process, executablePath, query_generation]() {
                         completeExternalToolVersionQuery(process, executablePath, query_generation);
                     });
    QObject::connect(process, &QProcess::errorOccurred, this,
                     [this, process, executablePath, query_generation](const QProcess::ProcessError process_error) {
                         if (process_error == QProcess::FailedToStart)
                         {
                             completeExternalToolVersionQuery(process, executablePath, query_generation);
                         }
                     });
    QTimer::singleShot(1500, process, [process]() {
        if (process->state() != QProcess::NotRunning)
        {
            process->kill();
        }
    });
    process->start();
}

void ExtensionNativeProcessWindow::completeExternalToolVersionQuery(QProcess* process, const QString& executablePath,
                                                                    const int queryGeneration)
{
    if (!process || process != external_tool_version_process_ ||
        queryGeneration != external_tool_version_query_generation_)
    {
        return;
    }

    QString version;
    if (process->error() == QProcess::UnknownError && process->exitStatus() == QProcess::NormalExit)
    {
        QString output = QString::fromUtf8(process->readAllStandardOutput());
        const QString stderr_output = QString::fromUtf8(process->readAllStandardError());
        if (!stderr_output.trimmed().isEmpty())
        {
            output += QStringLiteral("\n");
            output += stderr_output;
        }
        version = versionFromOutput(output, external_tool_version_pattern_, 200);
    }

    external_tool_version_process_ = nullptr;
    external_tool_version_query_executable_.clear();
    process->deleteLater();
    if (external_tool_resolved_record_.value(QStringLiteral("resolved_cli_executable")).toString() != executablePath)
    {
        return;
    }
    external_tool_resolved_record_.insert(QStringLiteral("resolved_cli_version"), version);
    external_tool_resolved_record_.insert(QStringLiteral("resolved_cli_version_pending"), false);
    updateExecuteButtonEnabled();
}

QCborMap ExtensionNativeProcessWindow::resolvedExternalToolRecord() const
{
    return external_tool_resolved_record_;
}

QCborValue ExtensionNativeProcessWindow::transientExternalToolSettingValue(const QString& key,
                                                                           const QCborValue& defaultValue) const
{
    const auto it = external_tool_resolved_record_.constFind(key);
    return it == external_tool_resolved_record_.constEnd() ? defaultValue : it.value();
}

void ExtensionNativeProcessWindow::setExternalToolConfigurationStatus(const bool valid, const QString& message,
                                                                      const an::ui::AnInfoBanner::Tone tone)
{
    external_tool_config_valid_ = valid;
    if (external_tool_status_banner_)
    {
        if (valid && !external_tool_show_configured_banner_)
        {
            external_tool_status_banner_->hide();
            updateExecuteButtonEnabled();
            return;
        }
        external_tool_status_banner_->setMessage(message);
        external_tool_status_banner_->setTone(tone);
        external_tool_status_banner_->show();
    }
    updateExecuteButtonEnabled();
}

void ExtensionNativeProcessWindow::refreshExternalToolConfiguration()
{
    if (!external_tool_config_enabled_)
    {
        external_tool_config_valid_ = true;
        external_tool_resolved_record_ = QCborMap{};
        if (external_tool_status_banner_)
        {
            external_tool_status_banner_->hide();
        }
        if (external_tool_config_button_)
        {
            external_tool_config_button_->hide();
        }
        updateExecuteButtonEnabled();
        return;
    }

    if (external_tool_config_button_)
    {
        external_tool_config_button_->setText(external_tool_button_label_.isEmpty() ? QStringLiteral("Configure")
                                                                                    : external_tool_button_label_);
        external_tool_config_button_->show();
    }

    const ExternalToolResolution resolution = resolveExternalTool();
    external_tool_resolved_record_ = QCborMap{{QStringLiteral("configured_folder"), resolution.configured_folder},
                                              {QStringLiteral("resolved_cli_executable"), resolution.executable},
                                              {QStringLiteral("resolution_source"), resolution.source},
                                              {QStringLiteral("resolved_cli_version"), resolution.version},
                                              {QStringLiteral("resolver_diagnostic"), resolution.diagnostic}};
    const QString message =
        resolution.diagnostic.isEmpty() ? external_tool_not_configured_text_ : resolution.diagnostic;
    setExternalToolConfigurationStatus(resolution.valid, message,
                                       resolution.valid ? an::ui::AnInfoBanner::Tone::Success
                                                        : an::ui::AnInfoBanner::Tone::Warning);
    if (resolution.valid)
    {
        startExternalToolVersionQuery(resolution.executable);
    }
}

void ExtensionNativeProcessWindow::onExternalToolConfigClicked()
{
    if (!external_tool_config_enabled_ || external_tool_setting_key_.isEmpty())
    {
        return;
    }

    if (external_tool_open_extension_settings_)
    {
        if (!open_settings_callback_)
        {
            showProcessMessage(QStringLiteral("Settings"),
                               tr("The extension settings dialog is unavailable in this host session."),
                               ProcessMessageTone::Error);
            return;
        }

        std::string settings_error;
        if (!open_settings_callback_(&settings_error))
        {
            showProcessMessage(
                QStringLiteral("Settings"),
                QString::fromStdString(settings_error.empty() ? "Could not open extension settings." : settings_error),
                ProcessMessageTone::Error);
            return;
        }

        refreshExternalToolConfiguration();
        return;
    }

    if (external_tool_config_dialog_)
    {
        external_tool_config_dialog_->raise();
        external_tool_config_dialog_->activateWindow();
        return;
    }

    ExternalToolManagementOptions options;
    options.label = external_tool_label_;
    options.dialog_title = external_tool_dialog_title_;
    options.dialog_size = external_tool_dialog_size_;
    options.current_directory = currentExternalToolDirectory();
    options.download_page_url = external_tool_download_page_url_;
    options.download_button_label = external_tool_download_button_label_;
    options.update_page_url = external_tool_update_page_url_;
    options.update_api_url = external_tool_update_api_url_;
    options.install_instructions = external_tool_install_instructions_;
    options.download_note = external_tool_download_note_;
    options.required_files = external_tool_required_files_;
    if (options.required_files.isEmpty())
    {
        options.required_files = external_tool_executable_candidates_;
    }
    options.validate_directory = [this](const QString& directory, QString* message) {
        return validateExternalToolDirectory(directory, message);
    };
    options.save_directory = [this](const QString& directory, std::string* error) {
        if (!settings_set_callback_)
        {
            if (error)
            {
                *error = "Extension settings are not available.";
            }
            return false;
        }

        return settings_set_callback_(external_tool_setting_key_, QCborValue(directory), error);
    };
    options.configuration_changed = [this]() { refreshExternalToolConfiguration(); };

    auto* dialog = new ExternalToolManagementDialog(std::move(options), this);
    external_tool_config_dialog_ = dialog;
    QObject::connect(dialog, &QObject::destroyed, this, [this, dialog]() {
        if (external_tool_config_dialog_ == dialog)
        {
            external_tool_config_dialog_ = nullptr;
        }
    });

    dialog->open();
}

void ExtensionNativeProcessWindow::applyNativeWindowSizeConstraints()
{
    if (page_shape_)
    {
        return;
    }
    const bool fixed_size = property("anFixedSizeWindow").toBool();
    if (!fixed_size)
    {
        setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
        if (configured_window_size_.isValid())
        {
            QSize target_size = configured_window_size_;
            if (QLayout* window_layout = layout())
            {
                window_layout->activate();
                const QSize layout_minimum = window_layout->minimumSize();
                if (layout_minimum.isValid())
                {
                    target_size = target_size.expandedTo(layout_minimum);
                }
                const QSize layout_hint = window_layout->sizeHint();
                if (layout_hint.isValid())
                {
                    target_size = target_size.expandedTo(layout_hint);
                }
            }
            if (size() != target_size)
            {
                resize(target_size);
            }
        }
        return;
    }

    QSize target_size = configured_window_size_.isValid() ? configured_window_size_ : size();
    if (QLayout* window_layout = layout())
    {
        window_layout->activate();
        const QSize layout_minimum = window_layout->minimumSize();
        if (layout_minimum.isValid())
        {
            target_size = target_size.expandedTo(layout_minimum);
        }
        const QSize layout_hint = window_layout->sizeHint();
        if (layout_hint.isValid())
        {
            target_size = target_size.expandedTo(layout_hint);
        }
    }
    if (!configured_window_size_.isValid())
    {
        target_size = target_size.expandedTo(size());
    }
    if (!target_size.isValid())
    {
        return;
    }

    setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
    setMinimumSize(target_size);
    if (size() != target_size)
    {
        resize(target_size);
    }
    setFixedSize(target_size);
}

void ExtensionNativeProcessWindow::fitWindowToVisibleContents()
{
    if (view_area_visible_ || page_shape_)
    {
        return;
    }

    if (QLayout* window_layout = layout())
    {
        window_layout->activate();
    }
    if (root_layout_)
    {
        root_layout_->activate();
    }

    updateGeometry();
    adjustSize();
    applyNativeWindowSizeConstraints();
}

void ExtensionNativeProcessWindow::requestPreviewFrame()
{
    requestFrame();
}

void ExtensionNativeProcessWindow::requestFrame()
{
    if (updating_param_defs_ || !previewView() || !wantsPreview())
    {
        return;
    }

    if (triggerInitialManualPreviewRefreshIfNeeded())
    {
        return;
    }

    // Paced and collapsed by the framework: interactive while a control is held, settled otherwise, and held back
    // while an execute runs.
    schedulePreviewProcessExecution();
}

void ExtensionNativeProcessWindow::settingsUpdatedImpl() {}

void ExtensionNativeProcessWindow::executeProcessStarted(const RTPreviewMode previewMode, const bool)
{
    if (previewMode != RTPreviewMode::AsyncPreview)
    {
        return;
    }
    preview_in_progress_ = true;
    last_frame_committed_ = false;
    updateManualPreviewRefreshUi();
}

void ExtensionNativeProcessWindow::executeProcessCompletedImpl(const RTPreviewMode previewMode, const bool)
{
    if (previewMode == RTPreviewMode::AsyncPreview)
    {
        last_frame_committed_ = true;
    }
}

void ExtensionNativeProcessWindow::executeProcessEndedImpl(const RTPreviewMode previewMode, const bool)
{
    if (previewMode != RTPreviewMode::AsyncPreview)
    {
        return;
    }
    preview_in_progress_ = false;
    markManualPreviewRefreshCompleted(last_frame_committed_);
    // The curve editors' histograms follow what the preview view shows once a frame replaces it.
    syncCurveEditorHistograms();
    updateManualPreviewRefreshUi();
}

void ExtensionNativeProcessWindow::registerManualPreviewRefreshButton(QPushButton* button, const QString& actionId)
{
    if (!button || actionId.isEmpty())
    {
        return;
    }

    manual_preview_enabled_ = true;
    manual_preview_dirty_ = true;
    manual_preview_refresh_action_id_ = actionId;
    manual_preview_refresh_button_ = button;
    updateManualPreviewRefreshUi();
}

bool ExtensionNativeProcessWindow::triggerInitialManualPreviewRefreshIfNeeded()
{
    if (!manual_preview_refresh_on_open_pending_ || !manual_preview_enabled_ ||
        manual_preview_refresh_action_id_.isEmpty() || !param_action_callback_ || !preview_body_ || !source_view_ ||
        preview_in_progress_ || execution_in_progress_ || manual_preview_refresh_in_flight_ ||
        manual_mask_editing_enabled_)
    {
        return false;
    }

    manual_preview_refresh_on_open_pending_ = false;
    onParamActionClicked(manual_preview_refresh_action_id_);
    return true;
}

bool ExtensionNativeProcessWindow::paramInvalidatesManualPreview(const QString& paramId) const
{
    if (!manual_preview_enabled_ || paramId.isEmpty())
    {
        return false;
    }

    const std::string key = paramId.toStdString();
    return manual_preview_invalidating_param_ids_.find(key) != manual_preview_invalidating_param_ids_.end();
}

void ExtensionNativeProcessWindow::markManualPreviewDirty(const QString& paramId)
{
    if (!manual_preview_enabled_)
    {
        return;
    }
    if (!paramId.isEmpty() && !paramInvalidatesManualPreview(paramId))
    {
        return;
    }

    manual_preview_dirty_ = true;
    ++manual_preview_dirty_generation_;
    updateManualPreviewRefreshUi();
}

void ExtensionNativeProcessWindow::updateManualPreviewRefreshUi()
{
    if (!manual_preview_enabled_)
    {
        return;
    }

    const bool refresh_enabled = manual_preview_dirty_ && source_view_ != nullptr && !preview_in_progress_ &&
                                 !execution_in_progress_ && !manual_preview_refresh_in_flight_ &&
                                 !manual_preview_refresh_on_open_pending_ && !manual_mask_editing_enabled_;
    if (manual_preview_refresh_button_)
    {
        manual_preview_refresh_button_->setEnabled(refresh_enabled);
    }

    if (!manual_preview_status_banner_)
    {
        return;
    }

    if (manual_preview_refresh_in_flight_ || preview_in_progress_)
    {
        manual_preview_status_banner_->setTone(an::ui::AnInfoBanner::Tone::Info);
        manual_preview_status_banner_->setMessage(manual_preview_updating_text_);
        return;
    }

    if (manual_preview_dirty_)
    {
        manual_preview_status_banner_->setTone(an::ui::AnInfoBanner::Tone::Warning);
        manual_preview_status_banner_->setMessage(manual_preview_stale_text_);
        return;
    }

    manual_preview_status_banner_->setTone(an::ui::AnInfoBanner::Tone::Success);
    manual_preview_status_banner_->setMessage(manual_preview_ready_text_);
}

void ExtensionNativeProcessWindow::setupManualMaskEditor(const QJsonObject& obj, QVBoxLayout* parentLayout)
{
    if (!parentLayout)
    {
        return;
    }

    const auto& spacing = an::ui::tokens().spacing;
    const QString id = obj.value(QStringLiteral("id")).toString(QStringLiteral("manual_mask"));
    const QString label = obj.value(QStringLiteral("label")).toString(tr("Manual Mask"));
    const QString tooltip = tooltipText(obj);
    manual_mask_use_param_id_ =
        obj.value(QStringLiteral("use_param")).toString(QStringLiteral("use_manual_mask")).trimmed();
    const QString use_label = obj.value(QStringLiteral("use_label")).toString(tr("Use manual mask"));
    manual_mask_use_default_ = obj.value(QStringLiteral("use_default")).toBool(false);

    manual_mask_display_param_id_ = obj.value(QStringLiteral("display_param")).toString().trimmed();
    manual_mask_display_value_ = obj.value(QStringLiteral("display_value")).toVariant();
    manual_mask_default_brush_size_ =
        std::clamp(panel_rows::clampToInt(obj.value(QStringLiteral("brush_size")).toDouble(50.0)), 1, 512);
    const int min_brush_size =
        std::clamp(panel_rows::clampToInt(obj.value(QStringLiteral("min_brush_size")).toDouble(10.0)), 1, 512);
    const int max_brush_size = std::clamp(
        panel_rows::clampToInt(obj.value(QStringLiteral("max_brush_size")).toDouble(200.0)), min_brush_size, 512);
    manual_mask_default_brush_size_ = std::clamp(manual_mask_default_brush_size_, min_brush_size, max_brush_size);

    auto* group = new QGroupBox(label, param_panel_);
    group->setObjectName(QStringLiteral("anNativeManualMaskEditor"));
    group->setProperty("anParamId", id);
    group->setCheckable(false);
    applyTooltip(group, tooltip);

    auto* group_layout = new QVBoxLayout(group);
    group_layout->setContentsMargins(spacing.sm, spacing.sm, spacing.sm, spacing.sm);
    group_layout->setSpacing(spacing.sm);

    auto* use_row = new QWidget(group);
    auto* use_layout = new QGridLayout(use_row);
    panel_rows::setUpRowGrid(use_layout);

    auto* use_label_widget = new QLabel(use_label, use_row);
    use_label_widget->setObjectName(QStringLiteral("anNativeParamLabel"));
    panel_rows::setUpRowLabel(use_label_widget);
    use_layout->addWidget(use_label_widget, 0, 0, Qt::AlignLeft | Qt::AlignVCenter);

    auto* use_checkbox = new QCheckBox(use_row);
    use_checkbox->setObjectName(QStringLiteral("anNativeManualMaskUseCheckBox"));
    use_checkbox->setProperty("anParamId", manual_mask_use_param_id_);
    use_checkbox->setChecked(false);
    use_checkbox->setEnabled(false);
    applyTooltip(use_checkbox, tr("Use the stored manual mask when previewing or applying."));
    use_layout->addWidget(use_checkbox, 0, 1, Qt::AlignLeft | Qt::AlignVCenter);
    group_layout->addWidget(use_row);

    auto* action_row = new QWidget(group);
    auto* action_layout = new QHBoxLayout(action_row);
    action_layout->setContentsMargins(0, 0, 0, 0);
    action_layout->setSpacing(spacing.sm);

    auto* edit_button = new QPushButton(tr("Edit Mask"), action_row);
    edit_button->setObjectName(QStringLiteral("anNativeManualMaskEditButton"));
    edit_button->setProperty("anProcessButtonRole", QStringLiteral("primary"));
    applyTooltip(edit_button, tr("Edit the manual mask on the original source image."));
    action_layout->addWidget(edit_button, 0);

    auto* cancel_button = new QPushButton(tr("Cancel"), action_row);
    cancel_button->setObjectName(QStringLiteral("anNativeManualMaskCancelButton"));
    cancel_button->setProperty("anProcessButtonRole", QStringLiteral("secondary"));
    cancel_button->hide();
    applyTooltip(cancel_button, tr("Discard mask edits made in this editing session."));
    action_layout->addWidget(cancel_button, 0);

    auto* clear_button = new QPushButton(tr("Clear Mask"), action_row);
    clear_button->setObjectName(QStringLiteral("anNativeManualMaskClearButton"));
    clear_button->setProperty("anProcessButtonRole", QStringLiteral("secondary"));
    applyTooltip(clear_button, tr("Remove everything painted on the manual mask."));
    action_layout->addWidget(clear_button, 0);
    action_layout->addStretch(1);
    group_layout->addWidget(action_row);

    auto* controls_widget = new QWidget(group);
    controls_widget->setObjectName(QStringLiteral("anNativeManualMaskToolPanel"));
    auto* controls_layout = new QGridLayout(controls_widget);
    panel_rows::setUpRowGrid(controls_layout);
    applyTooltip(controls_widget, tooltip);

    auto* tool_label = new QLabel(tr("Tool"), controls_widget);
    tool_label->setObjectName(QStringLiteral("anNativeParamLabel"));
    panel_rows::setUpRowLabel(tool_label);
    controls_layout->addWidget(tool_label, 0, 0, Qt::AlignLeft | Qt::AlignVCenter);

    auto* tool_controls = new QWidget(controls_widget);
    auto* tool_layout = new QHBoxLayout(tool_controls);
    tool_layout->setContentsMargins(0, 0, 0, 0);
    tool_layout->setSpacing(spacing.xs);

    auto* tool_group = new QButtonGroup(tool_controls);
    tool_group->setExclusive(true);
    auto* paint_button = new QPushButton(tr("Brush"), tool_controls);
    auto* lasso_button = new QPushButton(tr("Lasso"), tool_controls);
    auto* erase_button = new QPushButton(tr("Eraser"), tool_controls);
    const std::array<QPushButton*, 3> tool_buttons{paint_button, lasso_button, erase_button};
    for (QPushButton* button : tool_buttons)
    {
        button->setCheckable(true);
        button->setObjectName(QStringLiteral("anNativeManualMaskToolButton"));
        tool_layout->addWidget(button, 0);
        applyTooltip(button, tooltip);
    }
    tool_layout->addStretch(1);
    tool_group->addButton(paint_button, 0);
    tool_group->addButton(lasso_button, 1);
    tool_group->addButton(erase_button, 2);
    paint_button->setChecked(true);
    controls_layout->addWidget(tool_controls, 0, 1);

    QObject::connect(tool_group, &QButtonGroup::idClicked, this, [this](const int id) {
        if (!manual_mask_canvas_)
        {
            return;
        }
        if (id == 1)
        {
            manual_mask_canvas_->setTool(an::ui::drawing::DrawingTool::FreehandLasso);
        }
        else if (id == 2)
        {
            manual_mask_canvas_->setTool(an::ui::drawing::DrawingTool::Eraser);
        }
        else
        {
            manual_mask_canvas_->setTool(an::ui::drawing::DrawingTool::Brush);
        }
    });

    auto* size_label = new QLabel(tr("Brush Size"), controls_widget);
    size_label->setObjectName(QStringLiteral("anNativeParamLabel"));
    panel_rows::setUpRowLabel(size_label);
    controls_layout->addWidget(size_label, 1, 0, Qt::AlignLeft | Qt::AlignVCenter);

    auto* size_controls = new QWidget(controls_widget);
    auto* size_layout = new QHBoxLayout(size_controls);
    size_layout->setContentsMargins(0, 0, 0, 0);
    size_layout->setSpacing(spacing.sm);

    auto* size_spin = new QSpinBox(size_controls);
    size_spin->setObjectName(QStringLiteral("anNativeManualMaskBrushSizeSpinBox"));
    size_spin->setRange(min_brush_size, max_brush_size);
    size_spin->setValue(manual_mask_default_brush_size_);
    applyTooltip(size_spin, tooltip);

    auto* size_slider = new QSlider(Qt::Horizontal, size_controls);
    size_slider->setObjectName(QStringLiteral("anNativeManualMaskBrushSizeSlider"));
    size_slider->setRange(min_brush_size, max_brush_size);
    size_slider->setValue(manual_mask_default_brush_size_);
    applyTooltip(size_slider, tooltip);

    auto* size_link = new QSliderSpinBox(size_spin, size_slider, size_controls);
    QObject::connect(size_link, &QSliderSpinBox::valueChanged, this, [this](const int value) {
        manual_mask_default_brush_size_ = value;
        if (manual_mask_canvas_)
        {
            manual_mask_canvas_->setBrushSize(static_cast<float>(value));
        }
    });

    size_layout->addWidget(size_spin, 0);
    size_layout->addWidget(size_slider, 1);
    controls_layout->addWidget(size_controls, 1, 1);

    group_layout->addWidget(controls_widget);
    controls_widget->setEnabled(false);
    controls_widget->hide();

    manual_mask_group_ = group;
    manual_mask_controls_widget_ = controls_widget;
    manual_mask_use_checkbox_ = use_checkbox;
    manual_mask_edit_button_ = edit_button;
    manual_mask_cancel_button_ = cancel_button;
    manual_mask_clear_button_ = clear_button;
    parentLayout->addWidget(group);

    if (!manual_mask_use_param_id_.isEmpty())
    {
        if (obj.value(QStringLiteral("preview_invalidates")).toBool(false))
        {
            manual_preview_invalidating_param_ids_.insert(manual_mask_use_param_id_.toStdString());
        }
        param_panel_->registerHostedValue(manual_mask_use_param_id_, QStringLiteral("bool"), use_checkbox,
                                          QVariant(manual_mask_use_default_));
    }

    QObject::connect(use_checkbox, &QCheckBox::checkStateChanged, this, [this](Qt::CheckState) {
        onInteractiveParamChanged(manual_mask_use_param_id_);
        updateManualMaskUiState();
    });

    QObject::connect(edit_button, &QPushButton::clicked, this,
                     [this]() { setManualMaskEditingEnabled(!manual_mask_editing_enabled_); });
    QObject::connect(cancel_button, &QPushButton::clicked, this, [this]() { cancelManualMaskEditing(); });
    QObject::connect(clear_button, &QPushButton::clicked, this, [this]() { clearManualMask(); });

    syncManualMaskDocumentToSource();
    updateManualMaskUiState();
}

void ExtensionNativeProcessWindow::setManualMaskEditingEnabled(const bool enabled)
{
    if (manual_mask_editing_enabled_ == enabled)
    {
        return;
    }

    manual_mask_editing_enabled_ = enabled;
    updateExecuteButtonEnabled();

    if (enabled)
    {
        if (isSubAreaEnabled())
        {
            disableSubArea();
        }

        syncManualMaskDocumentToSource();
        manual_mask_edit_snapshot_ = nullptr;
        if (hasManualMask())
        {
            an::Result<an::consumer::ImageAsset> snapshot = manual_mask_document_->createOutputMask();
            if (snapshot.hasValue())
            {
                manual_mask_edit_snapshot_ = std::make_shared<const an::consumer::ImageAsset>(std::move(*snapshot));
            }
        }
        manual_mask_edit_snapshot_use_checked_ = manualMaskUseChecked();
        if (!manual_mask_display_param_id_.isEmpty() && manual_mask_display_value_.isValid())
        {
            if (QComboBox* combo = findChoiceParamCombo(manual_mask_display_param_id_))
            {
                manual_mask_display_combo_ = combo;
                manual_mask_previous_display_value_ = combo->currentData();
                manual_mask_had_previous_display_value_ = true;
                combo->setEnabled(false);
            }
        }

        // The mask is drawn on the original: the preview view shows the source's own image while it is edited.
        requestFrame();
        syncManualMaskCanvas();
        updateManualMaskUiState();
        updateManualPreviewRefreshUi();
        return;
    }

    if (!manual_mask_display_param_id_.isEmpty())
    {
        if (QComboBox* combo = findChoiceParamCombo(manual_mask_display_param_id_))
        {
            combo->setEnabled(true);
            if (manual_mask_had_previous_display_value_)
            {
                setChoiceParamValueSilently(manual_mask_display_param_id_, manual_mask_previous_display_value_);
            }
        }
    }
    manual_mask_display_combo_.clear();
    manual_mask_previous_display_value_ = QVariant();
    manual_mask_had_previous_display_value_ = false;
    manual_mask_edit_snapshot_.reset();

    updateManualMaskUiState();
    param_panel_->updateConditions();
    syncManualMaskCanvas();
    updateManualPreviewRefreshUi();
    if (!manual_preview_enabled_)
    {
        requestFrame();
    }
}

void ExtensionNativeProcessWindow::cancelManualMaskEditing()
{
    if (!manual_mask_editing_enabled_)
    {
        return;
    }

    syncManualMaskDocumentToSource();
    if (manual_mask_document_)
    {
        if (const SharedAsset snapshot = manual_mask_edit_snapshot_)
        {
            submitManualMaskEdit([snapshot](an::ui::drawing::DrawingDocument& document,
                                            const an::ExecutionContext& execution) -> an::Status {
                an::Result<an::consumer::AssetReadLease> lease = snapshot->read(execution.stopToken());
                if (!lease.hasValue())
                {
                    return lease.status();
                }
                return document.importMask(execution, *lease);
            });
        }
        else
        {
            submitManualMaskEdit([](an::ui::drawing::DrawingDocument& document, const an::ExecutionContext& execution) {
                return document.clear(execution);
            });
        }
    }
    setManualMaskUseChecked(manual_mask_edit_snapshot_ ? manual_mask_edit_snapshot_use_checked_ : false, false);
    setManualMaskEditingEnabled(false);
    updateManualMaskUiState();
}

void ExtensionNativeProcessWindow::clearManualMask()
{
    syncManualMaskDocumentToSource();
    if (!manual_mask_document_)
    {
        setManualMaskUseChecked(false, true);
        updateManualMaskUiState();
        return;
    }

    const bool had_mask = !manual_mask_document_->isEmpty();
    submitManualMaskEdit([](an::ui::drawing::DrawingDocument& document, const an::ExecutionContext& execution) {
        return document.clear(execution);
    });
    setManualMaskUseChecked(false, true);
    if (had_mask)
    {
        markManualPreviewDirty();
    }
    syncManualMaskCanvas();
    updateManualMaskUiState();
    updateManualPreviewRefreshUi();
}

void ExtensionNativeProcessWindow::updateManualMaskUiState()
{
    const bool has_mask = hasManualMask();
    const bool mask_view_enabled = has_mask && manualMaskUseChecked() && !manual_mask_editing_enabled_;
    bool display_mode_changed = false;
    if (!manual_mask_display_param_id_.isEmpty() && manual_mask_display_value_.isValid())
    {
        setChoiceParamOptionEnabled(manual_mask_display_param_id_, manual_mask_display_value_, mask_view_enabled);
        if (!mask_view_enabled)
        {
            if (QComboBox* combo = findChoiceParamCombo(manual_mask_display_param_id_);
                combo && panel_rows::valuesEqual(combo->currentData(), manual_mask_display_value_))
            {
                display_mode_changed = setChoiceParamToDefaultSilently(manual_mask_display_param_id_);
            }
        }
    }
    if (manual_mask_use_checkbox_)
    {
        manual_mask_use_checkbox_->setEnabled(has_mask && !manual_mask_editing_enabled_);
        if (!has_mask && manual_mask_use_checkbox_->isChecked())
        {
            setManualMaskUseChecked(false, false);
        }
    }
    if (manual_mask_edit_button_)
    {
        manual_mask_edit_button_->setText(manual_mask_editing_enabled_ ? tr("Done") : tr("Edit Mask"));
        manual_mask_edit_button_->setEnabled(sourceImage() != nullptr);
    }
    if (manual_mask_cancel_button_)
    {
        manual_mask_cancel_button_->setVisible(manual_mask_editing_enabled_);
    }
    if (manual_mask_clear_button_)
    {
        manual_mask_clear_button_->setEnabled(has_mask || manual_mask_editing_enabled_);
    }
    if (manual_mask_controls_widget_)
    {
        manual_mask_controls_widget_->setVisible(manual_mask_editing_enabled_);
        manual_mask_controls_widget_->setEnabled(manual_mask_editing_enabled_);
    }
    if (display_mode_changed && !updating_param_defs_ && !manual_mask_editing_enabled_)
    {
        requestFrame();
    }
}

bool ExtensionNativeProcessWindow::manualMaskUseChecked() const
{
    return manual_mask_use_checkbox_ && manual_mask_use_checkbox_->isChecked();
}

void ExtensionNativeProcessWindow::setManualMaskUseChecked(const bool checked, const bool notify)
{
    if (!manual_mask_use_checkbox_)
    {
        return;
    }

    const bool changed = manual_mask_use_checkbox_->isChecked() != checked;
    if (notify)
    {
        manual_mask_use_checkbox_->setChecked(checked);
    }
    else
    {
        const QSignalBlocker blocker(manual_mask_use_checkbox_);
        manual_mask_use_checkbox_->setChecked(checked);
    }
    if (changed && !notify)
    {
        updateManualMaskUiState();
    }
}

void ExtensionNativeProcessWindow::syncManualMaskDocumentToSource()
{
    const QRect bounds = imageBounds(sourceImage());
    const an::consumer::EngineSession* session = engineSession();
    const int width = bounds.width();
    const int height = bounds.height();
    if (session == nullptr || width <= 0 || height <= 0)
    {
        manual_mask_document_.reset();
        if (manual_mask_edits_)
        {
            manual_mask_edits_->setDocument(nullptr);
        }
        updateManualMaskUiState();
        return;
    }

    if (manual_mask_document_ && manual_mask_document_->width() == width && manual_mask_document_->height() == height)
    {
        return;
    }

    const bool had_mask = manual_mask_document_ && !manual_mask_document_->isEmpty();
    an::Result<std::shared_ptr<an::ui::drawing::DrawingDocument>> document =
        an::ui::drawing::DrawingDocument::create(*session, width, height);
    if (!document.hasValue())
    {
        LOG_WARNING("Native process window: the manual mask could not be made: {}", document.status().message());
        manual_mask_document_.reset();
    }
    else
    {
        manual_mask_document_ = std::move(*document);
    }
    if (manual_mask_edits_)
    {
        manual_mask_edits_->setDocument(manual_mask_document_);
    }
    if (manual_mask_canvas_ && manual_mask_document_)
    {
        manual_mask_canvas_->setDocument(manual_mask_document_);
    }
    if (had_mask)
    {
        markManualPreviewDirty();
    }
    updateManualMaskUiState();
}

void ExtensionNativeProcessWindow::syncManualMaskCanvas()
{
    const bool read_only_display_enabled =
        !manual_mask_editing_enabled_ && manualMaskDisplayModeSelected() && hasManualMask() && manualMaskUseChecked();
    if (!manual_mask_editing_enabled_ && !read_only_display_enabled)
    {
        destroyManualMaskCanvas();
        return;
    }

    syncManualMaskDocumentToSource();
    ImageViewControl* view = previewPresentationView();
    if (!view || !view->getScrollArea() || !manual_mask_document_)
    {
        return;
    }

    if (!manual_mask_canvas_)
    {
        manual_mask_canvas_ =
            std::make_unique<an::ui::drawing::DrawingCanvasController>(view->getScrollArea(), manual_mask_document_);
        manual_mask_canvas_->setStrokeCommittedCallback([this]() { onManualMaskChanged(); });
    }
    else
    {
        manual_mask_canvas_->setDocument(manual_mask_document_);
    }

    an::ui::drawing::BrushParams brush;
    brush.size = static_cast<float>(manual_mask_default_brush_size_);
    brush.opacity = 1.0f;
    brush.hardness = 1.0f;
    brush.flow = 1.0f;
    brush.spacing = 0.25f;
    manual_mask_canvas_->setBrushParams(brush);
    manual_mask_canvas_->setPreviewMode(an::ui::drawing::MaskPreviewMode::Amber);
    manual_mask_canvas_->setOverlayVisible(true);
    manual_mask_canvas_->setHideWhileAlternateImageVisible(true);
    manual_mask_canvas_->setListenerEnabled(manual_mask_editing_enabled_);
    if (read_only_display_enabled)
    {
        manual_mask_canvas_->setSelectionEnabled(true);
    }
}

void ExtensionNativeProcessWindow::destroyManualMaskCanvas()
{
    if (!manual_mask_canvas_)
    {
        return;
    }

    manual_mask_canvas_->destroy();
    manual_mask_canvas_.reset();
    if (ImageViewControl* view = previewPresentationView(); view && view->getViewWidget())
    {
        view->getViewWidget()->update();
    }
}

void ExtensionNativeProcessWindow::onManualMaskChanged()
{
    if (hasManualMask())
    {
        setManualMaskUseChecked(true, false);
    }
    markManualPreviewDirty();
    updateManualMaskUiState();
    updateManualPreviewRefreshUi();
}

QComboBox* ExtensionNativeProcessWindow::findChoiceParamCombo(const QString& id) const
{
    if (id.isEmpty() || param_panel_->kindOf(id) != QLatin1String("choice"))
    {
        return nullptr;
    }
    return qobject_cast<QComboBox*>(param_panel_->control(id));
}

bool ExtensionNativeProcessWindow::setChoiceParamValueSilently(const QString& id, const QVariant& value)
{
    QComboBox* combo = findChoiceParamCombo(id);
    if (!combo)
    {
        return false;
    }

    for (int index = 0; index < combo->count(); ++index)
    {
        if (panel_rows::valuesEqual(combo->itemData(index), value))
        {
            const bool changed = combo->currentIndex() != index;
            const QSignalBlocker blocker(combo);
            combo->setCurrentIndex(index);
            return changed;
        }
    }
    return false;
}

void ExtensionNativeProcessWindow::setChoiceParamOptionEnabled(const QString& id, const QVariant& value,
                                                               const bool enabled)
{
    QComboBox* combo = findChoiceParamCombo(id);
    if (!combo)
    {
        return;
    }

    auto* standard_model = qobject_cast<QStandardItemModel*>(combo->model());
    if (!standard_model)
    {
        return;
    }

    for (int index = 0; index < combo->count(); ++index)
    {
        if (!panel_rows::valuesEqual(combo->itemData(index), value))
        {
            continue;
        }

        if (QStandardItem* item = standard_model->item(index))
        {
            item->setEnabled(enabled);
        }
        return;
    }
}

bool ExtensionNativeProcessWindow::setChoiceParamToDefaultSilently(const QString& id)
{
    if (id.isEmpty())
    {
        return false;
    }

    const QVariant default_value =
        param_panel_->kindOf(id) == QLatin1String("choice") ? param_panel_->defaultOf(id) : QVariant();

    if (default_value.isValid() && setChoiceParamValueSilently(id, default_value))
    {
        return true;
    }

    QComboBox* combo = findChoiceParamCombo(id);
    if (!combo || combo->count() <= 0)
    {
        return false;
    }

    auto* standard_model = qobject_cast<QStandardItemModel*>(combo->model());
    for (int index = 0; index < combo->count(); ++index)
    {
        if (standard_model)
        {
            if (QStandardItem* item = standard_model->item(index); item && !item->isEnabled())
            {
                continue;
            }
        }

        const bool changed = combo->currentIndex() != index;
        const QSignalBlocker blocker(combo);
        combo->setCurrentIndex(index);
        return changed;
    }
    return false;
}

void ExtensionNativeProcessWindow::onInteractiveParamChanged(const QString& paramId)
{
    if (!updating_param_defs_)
    {
        const bool manual_preview_invalidated = paramInvalidatesManualPreview(paramId);
        markManualPreviewDirty(paramId);
        param_panel_->updateReadouts();
        if (curve_edit_in_progress_)
        {
            // The curve being dragged is shown in interactive frames; the extension hears of it once it is let go.
            curve_edit_pending_preview_ = true;
            if (!manual_preview_invalidated)
            {
                requestFrame();
            }
            return;
        }
        param_panel_->updateConditions();
        syncManualMaskCanvas();
        if (!manual_preview_invalidated)
        {
            requestFrame();
        }
        if (param_changed_callback_)
        {
            QStringList changed_keys;
            if (!paramId.isEmpty())
            {
                changed_keys.push_back(paramId);
            }
            param_changed_callback_(currentParams(), changed_keys, QStringLiteral("user"));
        }
    }
}

void ExtensionNativeProcessWindow::syncCurveEditorHistograms(const SharedAsset& explicit_source)
{
    // What the window shows: the original while the preview view shows it, else the preview's last result, else the
    // source's image.
    SharedAsset histogram_source = explicit_source;
    if (!histogram_source)
    {
        if (ImageViewControl* view = previewView())
        {
            if (view->getViewWidget() && view->getViewWidget()->isAlternateImageRenderEnabled())
            {
                histogram_source = view->alternateAsset();
            }
            if (!histogram_source)
            {
                histogram_source = view->document().asset();
            }
        }
    }
    if (!histogram_source)
    {
        histogram_source = sourceImage();
    }
    if (!histogram_source || histogram_source == last_histogram_source_)
    {
        return;
    }
    last_histogram_source_ = histogram_source;

    for (CurvesWidget* editor : param_panel_->curveEditors())
    {
        if (editor && editor->getHistogramEnabled())
        {
            if (an::ui::HistogramCountScheduler* counts = histogramCountsFor(editor))
            {
                counts->count(histogram_source);
            }
        }
    }

    // The preview's own histogram is made by its frames; any other real-time histogram counts what is shown.
    const QWidget* frame_histogram =
        m_UseHistogramPreviewWidget && m_HistogramPreview ? m_HistogramPreview->widget() : nullptr;
    for (const auto& param : param_panel_->histograms())
    {
        if (param.histogram && param.histogram.data() != frame_histogram)
        {
            if (an::ui::HistogramCountScheduler* counts = histogramCountsFor(param.histogram.data()))
            {
                counts->count(histogram_source);
            }
        }
    }
}

bool ExtensionNativeProcessWindow::isSubAreaEnabled() const
{
    return m_rt_preview_use_sub_area;
}

void ExtensionNativeProcessWindow::resetParamsToDefaults()
{
    const QCborMap before = currentParams();
    updating_param_defs_ = true;
    bool manual_preview_reset_changed = false;
    for (const QString& param_id : param_panel_->resetToDefaults())
    {
        manual_preview_reset_changed = manual_preview_reset_changed || paramInvalidatesManualPreview(param_id);
    }

    updating_param_defs_ = false;
    // The panel's readouts and conditions followed its defaults.
    updateManualMaskUiState();
    if (manual_preview_reset_changed)
    {
        markManualPreviewDirty();
    }
    syncCurveEditorHistograms();
    if (!manual_preview_reset_changed)
    {
        // The widgets changed with the window's requests held: this is the reset's one frame.
        requestFrame();
    }
    if (param_changed_callback_)
    {
        const QCborMap after = currentParams();
        QStringList changed_keys;
        for (auto it = after.begin(); it != after.end(); ++it)
        {
            const QString key = it.key().toString();
            if (before.value(key) != it.value())
            {
                changed_keys.push_back(key);
            }
        }
        if (!changed_keys.isEmpty())
        {
            param_changed_callback_(after, changed_keys, QStringLiteral("reset"));
        }
    }
}

/// --- Preview area ---

void ExtensionNativeProcessWindow::enableSubArea(const QSize& initial_size, const QString& label)
{
    if (!preview_area_enabled_)
    {
        return;
    }

    configured_sub_area_size_ = sanitizeSubAreaSize(initial_size);
    if (!label.isEmpty())
    {
        configured_sub_area_label_ = label;
    }
    sub_area_wanted_ = true;

    // The framework's preview area, at the configured size and with the configured label: made with the preview view
    // while the window does not preview yet, and turned on now, its checkbox with it, while it does.
    m_InitiaSubAreaSize = configured_sub_area_size_;
    m_SubAreaText = configured_sub_area_label_.isEmpty() ? QStringLiteral("Preview area") : configured_sub_area_label_;
    ImageViewControl* preview = previewView();
    if (!preview)
    {
        m_rt_preview_use_sub_area = true;
        return;
    }
    if (QCheckBox* preview_area = preview->getRTPreviewAreaCheckBox())
    {
        const QSignalBlocker blocker(preview_area);
        preview_area->setChecked(true);
    }
    on_rt_preview_area_toggled(true);
}

void ExtensionNativeProcessWindow::disableSubArea()
{
    sub_area_wanted_ = false;
    ImageViewControl* preview = previewView();
    if (!preview)
    {
        m_rt_preview_use_sub_area = false;
        return;
    }
    if (QCheckBox* preview_area = preview->getRTPreviewAreaCheckBox())
    {
        const QSignalBlocker blocker(preview_area);
        preview_area->setChecked(false);
    }
    on_rt_preview_area_toggled(false);
}

ExtensionNativeProcessWindow::SharedAsset ExtensionNativeProcessWindow::sourceImage() const
{
    return source_view_ ? source_view_->document().asset() : nullptr;
}

void ExtensionNativeProcessWindow::showAssetInView(ImageViewControl* view, an::consumer::ImageAsset&& image)
{
    if (!view || !image.valid())
    {
        return;
    }

    an::ui::ImageDocument& shown = view->document();
    if (!shown.valid())
    {
        an::Result<an::ui::ImageDocument> first = an::ui::ImageDocument::create(std::move(image));
        if (!first.hasValue())
        {
            LOG_WARNING("Native process window: an image could not be shown: {}", first.status().message());
            return;
        }
        view->setDocument(std::move(*first));
        syncCurveEditorHistograms();
        return;
    }

    // Replaced at once when nothing reads the document; otherwise the view takes its writer on a worker and the
    // histograms follow when it has.
    an::Result<an::ui::ImageDocumentWriter> writer = shown.tryWrite();
    if (writer.hasValue() && writer->replace(std::move(image)).ok())
    {
        writer->release();
        view->notifyImageUpdated(true);
        syncCurveEditorHistograms();
        return;
    }
    if (writer.hasValue())
    {
        writer->release();
    }
    const QPointer<ExtensionNativeProcessWindow> guard(this);
    view->replaceImage(std::move(image), true, [guard](const an::Status& status) {
        if (!guard)
        {
            return;
        }
        if (status.failed())
        {
            LOG_WARNING("Native process window: an image could not be shown: {}", status.message());
            return;
        }
        guard->syncCurveEditorHistograms();
    });
}

an::ui::HistogramCountScheduler* ExtensionNativeProcessWindow::histogramCountsFor(HistogramWidget* widget)
{
    if (widget == nullptr)
    {
        return nullptr;
    }
    auto found = histogram_counts_.find(widget);
    if (found == histogram_counts_.end())
    {
        found = histogram_counts_.emplace(widget, std::make_unique<an::ui::HistogramCountScheduler>(widget)).first;
    }
    return found->second.get();
}

void ExtensionNativeProcessWindow::submitManualMaskEdit(
    std::function<an::Status(an::ui::drawing::DrawingDocument&, const an::ExecutionContext&)> edit)
{
    if (!manual_mask_document_)
    {
        return;
    }
    if (manual_mask_canvas_)
    {
        manual_mask_canvas_->submitEdit(std::move(edit));
        return;
    }
    if (!manual_mask_edits_)
    {
        manual_mask_edits_ =
            std::make_unique<an::ui::drawing::DrawingEditQueue>([this](const an::ui::drawing::DrawingEditReport&) {
                if (manual_mask_edits_ && manual_mask_edits_->lastFailure().failed())
                {
                    LOG_WARNING("Native process window: a manual mask edit was refused: {}",
                                manual_mask_edits_->lastFailure().message());
                }
            });
    }
    if (manual_mask_edits_->document() != manual_mask_document_)
    {
        manual_mask_edits_->setDocument(manual_mask_document_);
    }
    manual_mask_edits_->submit(std::move(edit));
}

ImageViewControl* ExtensionNativeProcessWindow::previewPresentationView() const noexcept
{
    // The view the window shows: the preview view while it previews, the source view embedded otherwise.
    if (ImageViewControl* preview = previewView())
    {
        return preview;
    }
    return source_view_;
}

// --- Target image selector ---

bool ExtensionNativeProcessWindow::targetSelectorAcceptsView(const std::map<std::string, QVariant>& view) const
{
    if (!viewHasImage(view) || viewNameFromDescriptor(view).isEmpty())
    {
        return false;
    }

    if (target_channel_filter_.empty())
    {
        return true;
    }

    const auto ch_it = view.find("channels");
    if (ch_it == view.end())
    {
        return true;
    }

    const int channels = ch_it->second.toInt();
    return std::find(target_channel_filter_.begin(), target_channel_filter_.end(), channels) !=
           target_channel_filter_.end();
}

void ExtensionNativeProcessWindow::refreshViewSelectorParams()
{
    if (view_selector_params_.empty())
    {
        return;
    }

    const QString target_name = QString::fromStdString(selectedTargetView());
    int target_width = 0;
    int target_height = 0;
    int target_channels = 0;
    for (const auto& view : available_view_selector_views_)
    {
        if (viewNameFromDescriptor(view) != target_name)
        {
            continue;
        }

        target_width = viewIntField(view, "width");
        target_height = viewIntField(view, "height");
        target_channels = viewIntField(view, "channels");
        break;
    }
    if ((target_width <= 0 || target_height <= 0 || target_channels <= 0) && source_view_)
    {
        if (const std::optional<an::ImageDescriptor> described = source_view_->imageDescriptor(); described)
        {
            target_width = static_cast<int>(described->extent().width());
            target_height = static_cast<int>(described->extent().height());
            target_channels = static_cast<int>(described->extent().channelCount());
        }
    }

    auto accepts_view = [&](const ViewSelectorParam& selector, const std::map<std::string, QVariant>& view) -> bool {
        const QString name = viewNameFromDescriptor(view);
        if (name.isEmpty() || !viewHasImage(view))
        {
            return false;
        }
        if (selector.exclude_target && !target_name.isEmpty() && name == target_name)
        {
            return false;
        }

        const int channels = viewIntField(view, "channels");
        if (!selector.channel_filter.empty() &&
            std::find(selector.channel_filter.begin(), selector.channel_filter.end(), channels) ==
                selector.channel_filter.end())
        {
            return false;
        }
        if (selector.match_target_channels && target_channels > 0 && channels > 0 && channels != target_channels)
        {
            return false;
        }
        if (selector.match_target_geometry && target_width > 0 && target_height > 0)
        {
            const int width = viewIntField(view, "width");
            const int height = viewIntField(view, "height");
            if (width <= 0 || height <= 0 || width != target_width || height != target_height)
            {
                return false;
            }
        }
        return true;
    };

    auto find_data = [](const QComboBox* combo, const QVariant& value) -> int {
        if (!combo)
        {
            return -1;
        }
        for (int index = 0; index < combo->count(); ++index)
        {
            if (panel_rows::valuesEqual(combo->itemData(index), value))
            {
                return index;
            }
        }
        return -1;
    };

    for (const ViewSelectorParam& selector : view_selector_params_)
    {
        QComboBox* combo = selector.combo.data();
        if (!combo)
        {
            continue;
        }

        const QVariant previous_value = combo->currentData();
        const QSignalBlocker blocker(combo);
        combo->clear();
        combo->addItem(selector.placeholder.isEmpty() ? tr("Select image") : selector.placeholder,
                       selector.default_value);

        for (const auto& view : available_view_selector_views_)
        {
            if (!accepts_view(selector, view))
            {
                continue;
            }

            const QString name = viewNameFromDescriptor(view);
            combo->addItem(name, name);
            const int idx = combo->count() - 1;
            combo->setItemData(idx, selectorTooltipFromView(view), Qt::ToolTipRole);
        }

        int next_index = find_data(combo, previous_value);
        if (next_index < 0)
        {
            next_index = find_data(combo, selector.default_value);
        }
        combo->setCurrentIndex(next_index >= 0 ? next_index : 0);
        combo->setEnabled(combo->count() > 1);
    }
}

void ExtensionNativeProcessWindow::enableTargetSelector(const std::vector<std::map<std::string, QVariant>>& views,
                                                        const std::vector<int>& channel_filter)
{
    target_channel_filter_ = channel_filter;

    if (!target_selector_)
    {
        target_selector_section_ = new QWidget(controls_panel_ ? controls_panel_ : this);
        target_selector_section_->setObjectName(QStringLiteral("anNativeProcessTargetSelectorSection"));
        auto* target_selector_layout = new QFormLayout(target_selector_section_);
        target_selector_layout->setContentsMargins(0, 0, 0, 0);
        target_selector_layout->setVerticalSpacing(an::ui::tokens().spacing.xs);
        target_selector_layout->setHorizontalSpacing(an::ui::tokens().spacing.md);

        const QString label =
            pending_target_selector_label_.trimmed().isEmpty() ? tr("Target Image") : pending_target_selector_label_;
        target_selector_label_ = new QLabel(label, target_selector_section_);
        target_selector_label_->setObjectName(QStringLiteral("anNativeProcessTargetSelectorLabel"));
        panel_rows::applyRowLabelTheme(target_selector_label_);
        target_selector_layout->setWidget(0, QFormLayout::LabelRole, target_selector_label_);

        target_selector_ = new QComboBox(target_selector_section_);
        target_selector_->setObjectName(QStringLiteral("anNativeProcessTargetSelector"));
        target_selector_->setMinimumWidth(150);
        target_selector_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        target_selector_layout->setWidget(0, QFormLayout::FieldRole, target_selector_);

        if (controls_layout_)
        {
            const bool top_position = pending_target_selector_position_ == QLatin1String("top");
            // A page's target heads its controls, as every process's does.
            const int insert_idx =
                page_shape_ ? controls_layout_->indexOf(param_panel_)
                : top_position && param_scroll_area_
                    ? controls_layout_->indexOf(param_scroll_area_)
                    : controls_layout_->indexOf(action_bar_ ? action_bar_ : static_cast<QWidget*>(button_box_));
            if (insert_idx >= 0)
            {
                controls_layout_->insertWidget(insert_idx, target_selector_section_);
            }
            else
            {
                controls_layout_->addWidget(target_selector_section_);
            }
        }
        else if (root_layout_)
        {
            const int button_idx =
                root_layout_->indexOf(action_bar_ ? action_bar_ : static_cast<QWidget*>(button_box_));
            if (button_idx >= 0)
            {
                root_layout_->insertWidget(button_idx, target_selector_section_);
            }
            else
            {
                root_layout_->addWidget(target_selector_section_);
            }
        }

        QObject::connect(target_selector_, &QComboBox::currentTextChanged, this, [this](const QString& text) {
            refreshViewSelectorParams();
            if (target_changed_callback_)
            {
                target_changed_callback_(text.toStdString());
            }
        });
    }

    const QString previous_selection = target_selector_->currentText();
    const QSignalBlocker blocker(target_selector_);
    target_selector_->clear();

    for (const auto& view : views)
    {
        addOrUpdateTargetSelectorView(view);
    }
    setAvailableViewSelectorViews(views);

    if (!previous_selection.isEmpty())
    {
        const int previous_idx = target_selector_->findText(previous_selection);
        if (previous_idx != -1)
        {
            target_selector_->setCurrentIndex(previous_idx);
        }
    }
    refreshViewSelectorParams();
}

void ExtensionNativeProcessWindow::addOrUpdateTargetSelectorView(const std::map<std::string, QVariant>& view)
{
    if (!target_selector_ || !targetSelectorAcceptsView(view))
    {
        return;
    }

    const auto name_it = view.find("name");
    if (name_it == view.end())
    {
        return;
    }

    const QString name = name_it->second.toString();
    if (name.isEmpty())
    {
        return;
    }

    const QString tooltip = selectorTooltipFromView(view);

    const QSignalBlocker blocker(target_selector_);
    int idx = target_selector_->findText(name);
    if (idx == -1)
    {
        target_selector_->addItem(name, name);
        idx = target_selector_->count() - 1;
    }

    target_selector_->setItemData(idx, name, Qt::UserRole);
    target_selector_->setItemData(idx, tooltip, Qt::ToolTipRole);
}

void ExtensionNativeProcessWindow::removeTargetSelectorView(const QString& view_name)
{
    if (!target_selector_ || view_name.isEmpty())
    {
        return;
    }

    const int idx = target_selector_->findText(view_name);
    if (idx == -1)
    {
        return;
    }

    const QSignalBlocker blocker(target_selector_);
    target_selector_->removeItem(idx);
}

void ExtensionNativeProcessWindow::renameTargetSelectorView(const QString& old_name, const QString& new_name,
                                                            const QString& file_name, const QString& selector_tooltip)
{
    if (!target_selector_ || old_name.isEmpty() || new_name.isEmpty())
    {
        return;
    }

    const int old_idx = target_selector_->findText(old_name);
    if (old_idx == -1)
    {
        addOrUpdateTargetSelectorView(
            {{"name", new_name}, {"file_name", file_name}, {"selector_tooltip", selector_tooltip}});
        return;
    }

    const QString current_text = target_selector_->currentText();
    QString tooltip = selector_tooltip;
    if (tooltip.isEmpty())
    {
        tooltip = file_name;
    }
    if (tooltip.isEmpty())
    {
        tooltip = target_selector_->itemData(old_idx, Qt::ToolTipRole).toString();
    }

    const QSignalBlocker blocker(target_selector_);
    target_selector_->removeItem(old_idx);
    target_selector_->addItem(new_name, new_name);
    const int new_idx = target_selector_->count() - 1;
    target_selector_->setItemData(new_idx, new_name, Qt::UserRole);
    target_selector_->setItemData(new_idx, tooltip, Qt::ToolTipRole);

    if (current_text == old_name)
    {
        target_selector_->setCurrentIndex(new_idx);
    }
}

void ExtensionNativeProcessWindow::setAvailableViewSelectorViews(
    const std::vector<std::map<std::string, QVariant>>& views)
{
    available_view_selector_views_ = views;
    refreshViewSelectorParams();
}

void ExtensionNativeProcessWindow::addOrUpdateViewSelectorView(const std::map<std::string, QVariant>& view)
{
    const QString name = viewNameFromDescriptor(view);
    if (name.isEmpty())
    {
        return;
    }

    for (auto& existing : available_view_selector_views_)
    {
        if (viewNameFromDescriptor(existing) != name)
        {
            continue;
        }
        existing = view;
        refreshViewSelectorParams();
        return;
    }

    available_view_selector_views_.push_back(view);
    refreshViewSelectorParams();
}

void ExtensionNativeProcessWindow::removeViewSelectorView(const QString& view_name)
{
    if (view_name.isEmpty())
    {
        return;
    }

    const auto old_size = available_view_selector_views_.size();
    available_view_selector_views_.erase(
        std::remove_if(available_view_selector_views_.begin(), available_view_selector_views_.end(),
                       [&view_name](const auto& view) { return viewNameFromDescriptor(view) == view_name; }),
        available_view_selector_views_.end());
    if (available_view_selector_views_.size() != old_size)
    {
        refreshViewSelectorParams();
    }
}

void ExtensionNativeProcessWindow::renameViewSelectorView(const QString& old_name, const QString& new_name,
                                                          const QString& file_name, const QString& selector_tooltip)
{
    if (old_name.isEmpty() || new_name.isEmpty())
    {
        return;
    }

    for (auto& view : available_view_selector_views_)
    {
        if (viewNameFromDescriptor(view) != old_name)
        {
            continue;
        }
        view["name"] = new_name;
        if (!file_name.isEmpty())
        {
            view["file_name"] = file_name;
        }
        if (!selector_tooltip.isEmpty())
        {
            view["selector_tooltip"] = selector_tooltip;
        }
        refreshViewSelectorParams();
        return;
    }

    addOrUpdateViewSelectorView({{"name", new_name}, {"file_name", file_name}, {"selector_tooltip", selector_tooltip}});
}

void ExtensionNativeProcessWindow::setTargetChangedCallback(TargetChangedCallback callback)
{
    target_changed_callback_ = std::move(callback);
}

std::string ExtensionNativeProcessWindow::selectedTargetView() const
{
    if (!target_selector_ || target_selector_->currentIndex() < 0)
    {
        return {};
    }
    return target_selector_->currentText().toStdString();
}

} // namespace an::ui::extensions
