// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// The top bar of the Observatory window: the band across the window under its title bar.

#include "AfterNightShellObservatoryExport.h"

#include <QIcon>
#include <QString>
#include <QWidget>

#include <functional>

class QEvent;
class QHBoxLayout;
class QLabel;
class QMenu;
class QPaintEvent;
class QResizeEvent;
class QToolButton;

namespace an::ui
{
class AnBreadcrumb;
} // namespace an::ui

namespace an::ui::observatory
{

class PaletteField;

/// The band across the top of the window: the first surface with a hairline under it, as high as the density's top
/// bar. From the left, the logo, the project switcher and the breadcrumb of the place and its item; the palette's field
/// centred in the bar; at the right what the window puts in its trailing row - the layout icons of a workspace, Night,
/// Help, Information and More.
///
/// The project switcher reads the open project's name, or "No project", beside a folder - open while a project is -
/// and is at most 220 px wide: a longer name is cut in its middle and said whole in the tooltip. It opens a menu the
/// window fills each time it is about to show: the open project, the ones used last, New project..., Open project...
/// and Close project.
class AFTERNIGHTSHELLOBSERVATORY_API TopBar final : public QWidget
{
public:
    explicit TopBar(QWidget* parent = nullptr);

    /// The row of the bar's leading parts.
    [[nodiscard]] QHBoxLayout* row() const noexcept;
    /// The row at the bar's right the window fills.
    [[nodiscard]] QHBoxLayout* trailing() const noexcept;

    /// What gives the bar its icons, by the names of their files: the application's icons, in the theme's colours.
    using IconProvider = std::function<QIcon(const QString& name)>;
    void setIconProvider(IconProvider provider);
    /// Takes its icons again from the provider: the theme changed.
    void refreshIcons();

    [[nodiscard]] QWidget* logo() const noexcept;
    [[nodiscard]] QToolButton* projectSwitcher() const noexcept;
    /// The project switcher's menu: the open project, the recent ones, New, Open and Close project.
    [[nodiscard]] QMenu* projectMenu() const noexcept;

    /// Says which project is open, by its name; an empty name says "No project".
    void setProject(const QString& name);
    /// The open project's name as it was given, whole; empty with none.
    [[nodiscard]] const QString& projectName() const noexcept;

    /// What fills the switcher's menu: called now, each time the menu is about to show, and by `refreshProjectMenu`.
    using ProjectMenuFiller = std::function<void(QMenu& menu)>;
    void setProjectMenuFiller(ProjectMenuFiller filler);
    void refreshProjectMenu();
    /// Opens the switcher's menu under the switcher.
    void showProjectMenu();
    [[nodiscard]] an::ui::AnBreadcrumb* breadcrumb() const noexcept;
    [[nodiscard]] PaletteField* paletteField() const noexcept;

    /// Gives the palette's field the form the window's width asks for, and lays the bar out again.
    void setWindowWidth(int width);

    [[nodiscard]] QSize sizeHint() const override;
    [[nodiscard]] QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;
    void changeEvent(QEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    void placeField();
    /// Shows the project's name in the switcher, cut in its middle when the switcher has no room for it.
    void fitProjectName();
    void showProjectIcon();

    QHBoxLayout* m_row = nullptr;
    QHBoxLayout* m_trailing = nullptr;
    QWidget* m_logo = nullptr;
    IconProvider m_icons;
    QToolButton* m_switcher = nullptr;
    QMenu* m_projectMenu = nullptr;
    ProjectMenuFiller m_fillProjectMenu;
    QString m_projectName;
    an::ui::AnBreadcrumb* m_breadcrumb = nullptr;
    PaletteField* m_field = nullptr;
};

} // namespace an::ui::observatory
