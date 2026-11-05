// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "ExtensionAppContext.h"

#include "app/AfterNightAppListener.h"
#include "app/AfterNightEvent.h"
#include "app/FilePolicies.h"
#include "app/IconManager.h"
#include "app/RuntimeStorageCoordinator.h"
#include "app/ThemeManager.h"
#include "controls/ImageViewControl.h"
#include "session/IEngineSessionProvider.h"
#include "session/ImageDocument.h"

#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/IO/CodecFormat.h>
#include <AfterNightEngine/IO/CodecRegistry.h>
#include <AfterNightEngine/IO/SequenceDescriptor.h>
#include <AfterNightEngine/Image/ImageDescriptor.h>
#include <AfterNightEngine/Image/OutputIntent.h>

#include <QCoreApplication>
#include <QDir>
#include <QMessageBox>

#include <algorithm>

namespace an::ui::extensions
{

namespace
{

std::string normalizedViewName(const std::string& name)
{
    return name.empty() ? std::string("View") : name;
}

/// Thin adapter: delegates to the existing IconManager instance.
class IconManagerAdapter final : public IIconProvider
{
public:
    explicit IconManagerAdapter(IconManagerSptr mgr) : m_mgr(std::move(mgr)) {}

    [[nodiscard]] QIcon getIcon(const QString& icon) override
    {
        return m_mgr->getIcon(icon);
    }

    [[nodiscard]] QPixmap loadSvgFileAsPixmap(const QString& fileName, QSize size, QColor color) override
    {
        return m_mgr->loadSvgFileAsPixmap(fileName, size, color);
    }

    [[nodiscard]] QColor getColorNormal() const override
    {
        return m_mgr->getColorNormal();
    }

    [[nodiscard]] QColor getColorDisabled() const override
    {
        return m_mgr->getColorDisabled();
    }

private:
    IconManagerSptr m_mgr;
};

} // anonymous namespace

class ExtensionAppContext::EventDispatcher final : public QObject
{
public:
    explicit EventDispatcher(ExtensionAppContext* owner) : owner_(owner) {}

protected:
    bool event(QEvent* event) override
    {
        if (!event || event->type() != AfterNightEvent::getEventType())
        {
            return QObject::event(event);
        }

        event->accept();
        auto* after_night_event = static_cast<AfterNightEvent*>(event);
        try
        {
            after_night_event->run();
        }
        catch (const std::exception& exception)
        {
            if (owner_)
            {
                owner_->notifyErrorMessage(
                    owner_->getMainWindow(), QStringLiteral("Async Operation Error"),
                    QStringLiteral("Exception running async operation: %1").arg(QString::fromUtf8(exception.what())));
            }
        }
        catch (...)
        {
            if (owner_)
            {
                owner_->notifyErrorMessage(owner_->getMainWindow(), QStringLiteral("Async Operation Error"),
                                           QStringLiteral("Exception running async operation: unknown error"));
            }
        }

        return true;
    }

private:
    ExtensionAppContext* owner_ = nullptr;
};

ExtensionAppContext::ExtensionAppContext()
    : icon_manager_(std::make_shared<IconManager>()),
      icon_provider_(std::make_shared<IconManagerAdapter>(icon_manager_)), event_dispatcher_(new EventDispatcher(this))
{
}

ExtensionAppContext::~ExtensionAppContext()
{
    delete event_dispatcher_;
    event_dispatcher_ = nullptr;
}

void ExtensionAppContext::setMainWindow(QWidget* main_window) noexcept
{
    main_window_ = main_window;
}

void ExtensionAppContext::setSessionDirectories(const std::filesystem::path& temp_dir,
                                                const std::filesystem::path& artifacts_dir)
{
    temp_dir_path_ = QString::fromStdString(temp_dir.lexically_normal().string());
    artifacts_dir_path_ = QString::fromStdString(artifacts_dir.lexically_normal().string());
    if (!temp_dir_path_.isEmpty())
    {
        QDir().mkpath(temp_dir_path_);
    }
    if (!artifacts_dir_path_.isEmpty())
    {
        QDir().mkpath(artifacts_dir_path_);
    }
}

void ExtensionAppContext::setDialogCallbacks(DialogCallbacks callbacks)
{
    dialog_callbacks_ = std::move(callbacks);
}

void ExtensionAppContext::setViewFactoryCallback(ViewFactoryCallback callback)
{
    view_factory_callback_ = std::move(callback);
}

void ExtensionAppContext::setEngineSessionProvider(IEngineSessionProvider* provider) noexcept
{
    engine_session_provider_ = provider;
}

QString ExtensionAppContext::artifactsDirPath() const
{
    return artifacts_dir_path_;
}

std::size_t ExtensionAppContext::listenerCount() const noexcept
{
    return listeners_.size();
}

std::size_t ExtensionAppContext::trackedViewCount() const noexcept
{
    return view_names_by_address_.size();
}

std::size_t ExtensionAppContext::registeredProcessCount() const noexcept
{
    return processes_.size();
}

void ExtensionAppContext::notifyViewCreated(const ImageViewControl* ctrl)
{
    notifyListeners([ctrl](AfterNightAppListener* listener) { listener->onViewControlCreated(ctrl); });
}

void ExtensionAppContext::notifyViewDestroyed(const ImageViewControl* ctrl)
{
    notifyListeners([ctrl](AfterNightAppListener* listener) { listener->onViewControlDestroyed(ctrl); });
}

void ExtensionAppContext::notifyViewUpdated(const ImageViewControl* ctrl)
{
    notifyListeners([ctrl](AfterNightAppListener* listener) { listener->onViewControlUpdated(ctrl); });
}

void ExtensionAppContext::notifyActiveViewChanged(ImageViewControl* ctrl)
{
    notifyListeners([ctrl](AfterNightAppListener* listener) { listener->onActiveViewControlChanged(ctrl); });
}

void ExtensionAppContext::notifyViewRenamed(const std::string& new_name, const std::string& old_name,
                                            const ImageViewControl* ctrl)
{
    notifyListeners([&new_name, &old_name, ctrl](AfterNightAppListener* listener) {
        listener->onViewControlRenamed(new_name, old_name, ctrl);
    });
}

IIconProviderPtr ExtensionAppContext::getIconProvider()
{
    return icon_provider_;
}

void ExtensionAppContext::reloadIcons(bool dark_mode)
{
    if (!icon_manager_)
    {
        return;
    }

    const auto [normal_color, disabled_color] = an::ui::iconPaletteForTheme(dark_mode);
    const auto& color_tokens = an::ui::tokens().colors;

    icon_manager_->setColors(normal_color, disabled_color);
    icon_manager_->setSemanticColors(color_tokens.statusInfo, color_tokens.statusWarning, color_tokens.statusError,
                                     color_tokens.accentPrimary, color_tokens.statusSuccess);
    icon_manager_->loadIcons(QStringLiteral(":/AfterNight/icons/SVG"));
    icon_manager_->loadIcons(QStringLiteral(":/AfterNight/icons/PNG"));
}

IDialogService* ExtensionAppContext::getDialogService()
{
    return this;
}

IGlobalSettingsProvider* ExtensionAppContext::getGlobalSettings()
{
    return this;
}

IInterfaceSettingsRuntimeService* ExtensionAppContext::getInterfaceSettingsRuntimeService()
{
    return this;
}

IProcessSettingsProvider* ExtensionAppContext::getProcessSettings()
{
    return this;
}

ITempFileService* ExtensionAppContext::getTempFileService()
{
    return this;
}

IPathMemoryService* ExtensionAppContext::getPathMemoryService()
{
    return this;
}

IFileDialogService* ExtensionAppContext::getFileDialogService()
{
    return this;
}

IViewFactory* ExtensionAppContext::getViewFactory()
{
    return this;
}

IViewRegistry* ExtensionAppContext::getViewRegistry()
{
    return this;
}

IAppEventBus* ExtensionAppContext::getAppEventBus()
{
    return this;
}

IProcessRegistry* ExtensionAppContext::getProcessRegistry()
{
    return this;
}

IOutputIntentProvider* ExtensionAppContext::getOutputIntentProvider()
{
    return this;
}

IRecentFilesService* ExtensionAppContext::getRecentFilesService()
{
    return this;
}

IThumbnailPathProvider* ExtensionAppContext::getThumbnailPathProvider()
{
    return this;
}

IViewControlManager* ExtensionAppContext::getViewControlManager()
{
    return this;
}

IStatusBarService* ExtensionAppContext::getStatusBarService()
{
    return this;
}

IActionStateManager* ExtensionAppContext::getActionStateManager()
{
    return this;
}

IEngineSessionProvider* ExtensionAppContext::getEngineSessionProvider()
{
    return engine_session_provider_;
}

IRawOpenPolicyService* ExtensionAppContext::getRawOpenPolicyService()
{
    return nullptr;
}

QWidget* ExtensionAppContext::getMainWindow()
{
    return main_window_;
}

void ExtensionAppContext::openSettingsAt(const QString& pageName)
{
    if (dialog_callbacks_.open_settings_at)
    {
        dialog_callbacks_.open_settings_at(pageName);
    }
}

void ExtensionAppContext::messageBoxYesNo(QWidget* parent, const QString& title, const QString& text, bool warn,
                                          int* ret)
{
    if (dialog_callbacks_.message_box_yes_no)
    {
        dialog_callbacks_.message_box_yes_no(parent, title, text, warn, ret);
        return;
    }

    if (ret)
    {
        *ret = QMessageBox::No;
    }
}

void ExtensionAppContext::notifyErrorMessage(QWidget* parent, const QString& title, const QString& text)
{
    if (dialog_callbacks_.notify_error)
    {
        dialog_callbacks_.notify_error(parent, title, text);
    }
}

void ExtensionAppContext::notifyErrorMessageAsync(QWidget* parent, const QString& title, const QString& text)
{
    if (dialog_callbacks_.notify_error_async)
    {
        dialog_callbacks_.notify_error_async(parent, title, text);
    }
}

void ExtensionAppContext::notifyWarnMessage(QWidget* parent, const QString& title, const QString& text)
{
    if (dialog_callbacks_.notify_warn)
    {
        dialog_callbacks_.notify_warn(parent, title, text);
    }
}

void ExtensionAppContext::notifyWarnMessageAsync(QWidget* parent, const QString& title, const QString& text)
{
    if (dialog_callbacks_.notify_warn_async)
    {
        dialog_callbacks_.notify_warn_async(parent, title, text);
    }
}

void ExtensionAppContext::notifyInfoMessage(QWidget* parent, const QString& title, const QString& text)
{
    if (dialog_callbacks_.notify_info)
    {
        dialog_callbacks_.notify_info(parent, title, text);
    }
}

void ExtensionAppContext::notifyInfoMessageAsync(QWidget* parent, const QString& title, const QString& text)
{
    if (dialog_callbacks_.notify_info_async)
    {
        dialog_callbacks_.notify_info_async(parent, title, text);
    }
}

bool ExtensionAppContext::upsert(const QString& key, const QString& value)
{
    global_settings_.insert(key, value);
    return true;
}

QString ExtensionAppContext::select(const QString& key, const QString& defaultValue)
{
    return global_settings_.value(key, defaultValue);
}

std::optional<QString> ExtensionAppContext::selectOptional(const QString& key)
{
    const auto it = global_settings_.find(key);
    if (it == global_settings_.end())
    {
        return std::nullopt;
    }
    return it.value();
}

void ExtensionAppContext::applyTheme(const QString&) {}

void ExtensionAppContext::refreshAllZoomSettings() {}

bool ExtensionAppContext::upsert(const QString& processName, int win_state, int width, int height,
                                 const QString& json_settings)
{
    process_settings_.insert(processName, ProcessSettingsRecord{win_state, width, height, json_settings});
    return true;
}

bool ExtensionAppContext::select(const QString& processName, int& win_state, int& width, int& height,
                                 QString& json_settings)
{
    const auto it = process_settings_.find(processName);
    if (it == process_settings_.end())
    {
        return false;
    }

    win_state = it->win_state;
    width = it->width;
    height = it->height;
    json_settings = it->json_settings;
    return true;
}

bool ExtensionAppContext::isTempFile(const std::string& fileName)
{
    if (temp_dir_path_.isEmpty())
    {
        return false;
    }

    return QString::fromStdString(fileName).startsWith(temp_dir_path_);
}

std::string ExtensionAppContext::createTempFilePathForNewSequence(const an::SequenceFormat format) const
{
    const int next_id = ++next_temp_sequence_id_;
    const QString base_dir = temp_dir_path_.isEmpty() ? QDir::tempPath() : temp_dir_path_;
    const QString file_path = QStringLiteral("%1/sequence_%2%3")
                                  .arg(base_dir)
                                  .arg(next_id)
                                  .arg(QString::fromStdString(tempSequenceExtensionForFormat(format)));
    return file_path.toStdString();
}

QString ExtensionAppContext::getTempDirPath() const
{
    return temp_dir_path_;
}

QString ExtensionAppContext::selectPath(const QString& contextKey, const QString& defaultValue)
{
    return path_memory_.value(contextKey, defaultValue);
}

bool ExtensionAppContext::upsertPath(const QString& contextKey, const QString& path)
{
    path_memory_.insert(contextKey, path);
    return true;
}

QStringList ExtensionAppContext::selectImageFiles(QWidget*, const QString&, const QString&, const bool)
{
    return {};
}

QStringList ExtensionAppContext::selectFiles(QWidget*, const QString&, const QString&, const QStringList&)
{
    return {};
}

QStringList ExtensionAppContext::saveFiles(QWidget*, const QString&, const QString&, const QString&, const QStringList&)
{
    return {};
}

ImageViewControl* ExtensionAppContext::createNewImageViewControlFromImage(
    ImageViewControl* customViewControl, ImageDocument&& document, QWidget* parent, const std::string& historyDesc,
    ViewControlMode mode, ImageViewControl* neighbour, bool keepFocusedNeighbour, bool pendingChanges)
{
    if (!view_factory_callback_)
    {
        return nullptr;
    }

    return view_factory_callback_(customViewControl, std::move(document), parent, historyDesc, mode, neighbour,
                                  keepFocusedNeighbour, pendingChanges);
}

ImageViewControl* ExtensionAppContext::createNewImageViewControlFromSequence(ImageViewControl*, ImageDocument&&,
                                                                             QWidget*, const bool, const std::string&,
                                                                             ViewControlMode, ImageViewControl*, bool,
                                                                             bool)
{
    return nullptr;
}

ImageViewControl* ExtensionAppContext::getActiveImageViewControl()
{
    return active_view_control_;
}

void ExtensionAppContext::setActiveImageViewControl(ImageViewControl* view)
{
    if (active_view_control_ == view)
    {
        return;
    }

    active_view_control_ = view;
    notifyActiveViewChanged(view);
}

void ExtensionAppContext::subscribeListener(AfterNightAppListener* listener)
{
    if (!listener)
    {
        return;
    }

    if (std::find(listeners_.begin(), listeners_.end(), listener) == listeners_.end())
    {
        listeners_.push_back(listener);
    }
}

void ExtensionAppContext::unsubscribeListener(AfterNightAppListener* listener)
{
    std::erase(listeners_, listener);
}

void ExtensionAppContext::postAfterNightEvent(AfterNightEvent* event, int priority)
{
    if (!event)
    {
        return;
    }

    if (!event_dispatcher_)
    {
        event_dispatcher_ = new EventDispatcher(this);
    }

    if (QCoreApplication::instance())
    {
        QCoreApplication::postEvent(event_dispatcher_, event, priority);
        return;
    }

    delete event;
}

void ExtensionAppContext::notifyProcessOpened(const std::string& processName, const ImageViewControl* targetCtrl)
{
    notifyListeners([&processName, targetCtrl](AfterNightAppListener* listener) {
        listener->onProcessOpened(processName, targetCtrl);
    });
}

void ExtensionAppContext::notifyProcessExecuted(const std::string& processName, const ImageViewControl* targetCtrl)
{
    notifyListeners([&processName, targetCtrl](AfterNightAppListener* listener) {
        listener->onProcessExecuted(processName, targetCtrl);
    });
}

void ExtensionAppContext::registerProcessInstance(TopLevelProcessBaseControl* process)
{
    if (!process)
    {
        return;
    }

    if (std::find(processes_.begin(), processes_.end(), process) == processes_.end())
    {
        processes_.push_back(process);
    }
}

void ExtensionAppContext::unregisterProcessInstance(TopLevelProcessBaseControl* process)
{
    std::erase(processes_, process);
}

an::Result<an::OutputIntent> ExtensionAppContext::buildOutputIntent(const an::ImageDescriptor& source,
                                                                    const an::CodecFormat container) const
{
    // Read when the write is asked for, as the application's persistence reads its own, from the settings the
    // application handed this host.
    const ExportSettings settings = readExportSettings(
        [this](const QString& key, const QString& fallback) { return global_settings_.value(key, fallback); });
    an::Result<std::shared_ptr<const an::CodecRegistry>> registry = createProductCodecRegistry();
    if (!registry.hasValue())
    {
        return registry.status();
    }
    return buildExportIntent(settings, **registry, source, container);
}

std::deque<std::string> ExtensionAppContext::getRecentFilesList() const
{
    return {};
}

void ExtensionAppContext::openRecentSingleFile(const QString&) {}

void ExtensionAppContext::openDroppedFiles(const QStringList&) {}

QString ExtensionAppContext::selectThumbnailPath(const QString&) const
{
    return {};
}

void ExtensionAppContext::addViewControl(ImageViewControl* view)
{
    if (!view)
    {
        return;
    }

    const std::string view_name = normalizedViewName(view->getUniqueName());
    const bool tracked = view_names_by_address_.find(view) != view_names_by_address_.end();
    if (!tracked)
    {
        view_controls_.push_back(view);
        view_names_by_address_.emplace(view, view_name);
        view_controls_by_name_[view_name] = view;
        notifyViewCreated(view);
    }
    else
    {
        renameViewControl(view);
    }

    if (!active_view_control_)
    {
        setActiveImageViewControl(view);
    }
}

void ExtensionAppContext::removeViewControl(ImageViewControl* view, bool)
{
    if (!view)
    {
        return;
    }

    const auto view_name_it = view_names_by_address_.find(view);
    if (view_name_it != view_names_by_address_.end())
    {
        const auto by_name_it = view_controls_by_name_.find(view_name_it->second);
        if (by_name_it != view_controls_by_name_.end() && by_name_it->second == view)
        {
            view_controls_by_name_.erase(by_name_it);
        }
        view_names_by_address_.erase(view_name_it);
    }

    std::erase(view_controls_, view);

    const bool active_view_removed = active_view_control_ == view;
    if (active_view_control_ == view)
    {
        active_view_control_ = view_controls_.empty() ? nullptr : view_controls_.front();
    }

    notifyViewDestroyed(view);
    if (active_view_removed)
    {
        notifyActiveViewChanged(active_view_control_);
    }
}

void ExtensionAppContext::renameViewControl(ImageViewControl* view)
{
    if (!view)
    {
        return;
    }

    const auto it = view_names_by_address_.find(view);
    if (it == view_names_by_address_.end())
    {
        return;
    }

    const std::string old_name = it->second;
    const std::string new_name = normalizedViewName(view->getUniqueName());
    if (old_name == new_name)
    {
        return;
    }

    const auto by_name_it = view_controls_by_name_.find(old_name);
    if (by_name_it != view_controls_by_name_.end() && by_name_it->second == view)
    {
        view_controls_by_name_.erase(by_name_it);
    }

    it->second = new_name;
    view_controls_by_name_[new_name] = view;
    notifyViewRenamed(new_name, old_name, view);
}

void ExtensionAppContext::maximizeViewControl(ImageViewControl*) {}

void ExtensionAppContext::restoreViewControl(ImageViewControl*) {}

void ExtensionAppContext::notifyViewControlUpdated(const ImageViewControl* ctrl)
{
    notifyViewUpdated(ctrl);
}

void ExtensionAppContext::createContextMenuForViewControl(ImageViewControl*, QContextMenuEvent*) {}

bool ExtensionAppContext::supportsDockWidgets() const
{
    return false;
}

ImageViewControl* ExtensionAppContext::getViewControlByName(const std::string& name)
{
    const auto it = view_controls_by_name_.find(normalizedViewName(name));
    return it != view_controls_by_name_.end() ? it->second : nullptr;
}

std::vector<ImageViewControl*> ExtensionAppContext::getListOfViewControls()
{
    return view_controls_;
}

std::string ExtensionAppContext::createViewControlUniqueName(const std::string& name)
{
    const std::string base_name = normalizedViewName(name);
    std::string unique_name = base_name;
    std::size_t suffix = 1;
    while (view_controls_by_name_.find(unique_name) != view_controls_by_name_.end())
    {
        unique_name = base_name + "_" + std::to_string(suffix++);
    }
    return unique_name;
}

void ExtensionAppContext::updateImageInfoInStatusBar() {}

void ExtensionAppContext::updateImageZoomInfoInStatusBar() {}

void ExtensionAppContext::updateImageCelestialCoordsInStatusBar(double, double) {}

void ExtensionAppContext::clearImageCelestialCoordsInStatusBar() {}

void ExtensionAppContext::setCustomImageActionEnabled(ImageViewControl*, GlobalViewControlActions, bool) {}

void ExtensionAppContext::updateActiveImageActions() {}

void ExtensionAppContext::updateActiveImageHistory() {}

void ExtensionAppContext::notifyListeners(const std::function<void(AfterNightAppListener*)>& notifier)
{
    const auto listeners = listeners_;
    for (AfterNightAppListener* listener : listeners)
    {
        if (listener)
        {
            notifier(listener);
        }
    }
}

} // namespace an::ui::extensions