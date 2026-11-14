// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// Every widget of this library that paints a picture, and how it shows it under Night, where everything on screen is
/// red: a picture is shown in its Night form, made off the GUI thread (`theme/NightImage.h`), never laid under an
/// effect. A widget that paints a picture - a `drawImage`, a `drawPixmap`, a label's `setPixmap` - is on this list, and
/// `NightImage.EveryWidgetThatPaintsAPictureIsOnTheList` (`Test_ThemeSystem`) fails the build of a widget that is not,
/// or of an entry whose file paints none. `THEME_SYSTEM.md`, "Night", says what each kind means.

#include <array>
#include <cstdint>

namespace an::ui
{

/// How a widget shows its picture under Night.
enum class NightImageUse : std::uint8_t
{
    /// Presented bytes, mapped as the presentation's last stage on the worker that presents them
    /// (`ViewPresentationSettings::night`).
    Presentation,
    /// A picture shown through `nightImage` or a `NightPicture`: its Night form, made on the thread pool and kept.
    Picture,
    /// A picture its owner hands it, already in the form its owner shows: the view's Night bytes, a preview's Night
    /// form.
    Handed,
    /// An icon of the icon manager, which loads every icon in Night's form: tinted in the tokens' colours, or a
    /// coloured one mapped as it is loaded.
    Icon,
    /// Drawn in colours, not from a picture: the tokens', which Night maps, or a fixed one through `colorAsShown`.
    Colours,
    /// Written to a file, never shown: kept as it is.
    NeverShown
};

/// One widget that paints a picture.
struct NightImageWidget
{
    /// The class.
    const char* widget;
    /// Its source, from the library's folder.
    const char* source;
    /// How it shows its picture under Night.
    NightImageUse use;
    /// What it paints, and how.
    const char* how;
};

/// The widgets that paint a picture, by source.
inline constexpr std::array<NightImageWidget, 19u> kNightImageWidgets{{
    {"ImageViewWidget", "widgets/ImageViewWidget.cpp", NightImageUse::Presentation,
     "The image's presentation, a preview's frames and the mask layer, mapped on the worker that presents them; under "
     "Night the lens magnifies what is painted and reads the image's own values."},
    {"ImageLensWidget", "widgets/ImageLensWidget.cpp", NightImageUse::Handed,
     "Magnifies what its owner painted, which its owner hands it in Night's form."},
    {"ComparisonDisplayModule", "widgets/ComparisonDisplayModule.cpp", NightImageUse::Picture,
     "The comparison's overlay over the view, through nightImage."},
    {"SplitComparisonDisplayModule", "widgets/SplitComparisonDisplayModule.cpp", NightImageUse::Picture,
     "The compared image beside the split, through nightImage."},
    {"ImagePreviewWidget", "widgets/ImagePreviewWidget.cpp", NightImageUse::Picture,
     "A preview its owner hands it, and the crop its lens magnifies, through a NightPicture."},
    {"ImageThumbnailWidget", "widgets/ImageThumbnailWidget.cpp", NightImageUse::Picture,
     "A recent file's thumbnail, through a NightPicture."},
    {"FeaturedFileCard", "widgets/FeaturedFileCard.cpp", NightImageUse::Picture,
     "The featured file's picture behind the card and its softened copy, through NightPictures."},
    {"NightPictureLabel", "widgets/NightPictureLabel.cpp", NightImageUse::Picture,
     "A label's picture, through a NightPicture, shown again when Night is switched."},
    {"TimelineStageWidget", "widgets/TimelineStageWidget.cpp", NightImageUse::Icon,
     "The stage's icon, tinted in its state's colour."},
    {"DashboardIdentityBar", "controls/DashboardIdentityBar.cpp", NightImageUse::Picture,
     "The application's logo, in a NightPictureLabel."},
    {"DashboardOnboardingPanel", "controls/DashboardOnboardingPanel.cpp", NightImageUse::Picture,
     "The welcome's art, through NightPictures, and the logo in a NightPictureLabel; the cards' icons are tinted."},
    {"ImageHistoryControl", "controls/ImageHistoryControl.cpp", NightImageUse::Picture,
     "A step's preview, in an ImagePreviewWidget."},
    {"NewSequenceControl", "controls/NewSequenceControl.cpp", NightImageUse::Picture,
     "A frame's preview, in an ImagePreviewWidget."},
    {"DrawingCanvasController", "controls/drawing/DrawingCanvasController.cpp", NightImageUse::Colours,
     "The mask being painted, drawn in the preview's colours through colorAsShown."},
    {"ExtensionUiWidgetHost", "controls/ExtensionUiWidgetHost.cpp", NightImageUse::Colours,
     "A status mark, drawn in the status's colour."},
    {"PlaceholderSettingsPage", "controls/settings/PlaceholderSettingsPage.cpp", NightImageUse::Icon,
     "The page's icon."},
    {"SettingsPage", "controls/settings/SettingsPage.cpp", NightImageUse::Icon, "A message's icon."},
    {"AnProxyStyle", "theme/AnProxyStyle.cpp", NightImageUse::Icon,
     "A control's icon, in its disabled or chosen form."},
    {"AnnotationPicture", "controls/annotation/AnnotationPicture.cpp", NightImageUse::NeverShown,
     "The annotated picture, written to a file."},
}};

} // namespace an::ui
