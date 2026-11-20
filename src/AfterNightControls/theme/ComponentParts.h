// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// What the small parts of `components/` are made of beyond the design tokens: the sizes that are no metric of a
/// density - a switch's track and knob, a tag's corner, a chip's padding - and the few things each of them paints the
/// same way: a font at a size of the metrics, a state layer over a hovered or pressed part, the focus ring a keyboard
/// focus draws. A part takes its colours from `tokens()` where it paints and its sizes from `tokens().metrics` in its
/// sizeHint(); these constants are the rest, decided here so that no component writes a size of its own
/// (`THEME_SYSTEM.md`, "Small parts").

#include "AfterNightControlsExport.h"

#include <QColor>
#include <QFont>
#include <QRectF>

class QIcon;
class QPainter;
class QString;

namespace an::ui::parts
{

// A tag: a small square-cornered mark of a tone.
inline constexpr int kTagRadius = 4;
inline constexpr int kTagPadding = 7;
inline constexpr int kTagIcon = 12;
inline constexpr int kTagGap = 4;

/// The property of a menu's action that holds the quiet tag the Observatory profile paints after its text - an
/// extension's process carries "extension" - and the room between the text and the tag.
inline constexpr const char* kMenuTagProperty = "anMenuTag";
inline constexpr int kMenuTagGap = 8;

// A key hint: the keys of a shortcut in the monospace, in a small outlined box.
inline constexpr int kKeyHintRadius = 4;
inline constexpr int kKeyHintPadding = 5;
/// How much of the text's colour a key hint on a coloured part draws its outline in.
inline constexpr qreal kKeyHintOnColourOutline = 0.38;

// A chip: a pill, its text the caption, with an optional icon before it and a count after it.
inline constexpr int kChipPadding = 10;
inline constexpr int kChipPaddingBeforeIcon = 8;
inline constexpr int kChipIcon = 13;
inline constexpr int kChipGap = 6;

// A switch: a track 32 x 18 whose knob grows from 10 px off to 14 px on, where it carries a check.
inline constexpr int kSwitchWidth = 32;
inline constexpr int kSwitchHeight = 18;
inline constexpr int kSwitchKnobOff = 10;
inline constexpr int kSwitchKnobOn = 14;
inline constexpr int kSwitchBorder = 2;
inline constexpr int kSwitchLabelGap = 8;

// A segmented control: segments that share one outline; the chosen one carries a check.
inline constexpr int kSegmentPadding = 12;
inline constexpr int kSegmentPaddingSmall = 7;
inline constexpr int kSegmentCheck = 12;
inline constexpr int kSegmentCheckGap = 6;

// A label beside or above its field.
inline constexpr int kFieldLabelGapAbove = 4;

// A breadcrumb: its parts with a chevron between them.
inline constexpr int kBreadcrumbChevron = 12;
inline constexpr int kBreadcrumbGap = 6;

// A transient message: a dark raised bar with an icon, a text, a detail and one action.
inline constexpr int kSnackbarIcon = 16;
inline constexpr int kSnackbarPaddingStart = 14;
inline constexpr int kSnackbarPaddingEnd = 6;
inline constexpr int kSnackbarGap = 10;
inline constexpr int kSnackbarActionPadding = 10;
/// How much of its colour a transient message's detail is drawn in.
inline constexpr qreal kSnackbarDetailOpacity = 0.72;
/// How far above the status line a transient message stands, at the bottom centre of the centre.
inline constexpr int kSnackbarAboveStatusLine = 16;

// The Settings dialog's navigation column, with its search field above the pages' list; the least width of its page,
// which the Comfortable density's controls need.
inline constexpr int kSettingsNavigationWidth = 208;
inline constexpr int kSettingsPageLeastWidth = 600;
// A colour's swatch on the Interface page: its side, the ring that marks it chosen, the check it carries then.
inline constexpr int kSwatchSize = 28;
inline constexpr int kSwatchRing = 2;
inline constexpr int kSwatchCheck = 12;

// A process's page: its column's widths, standard and wide, and in a window under the narrow width; the run's row.
inline constexpr int kPageColumnStandard = 360;
inline constexpr int kPageColumnWide = 520;
inline constexpr int kPageColumnStandardNarrow = 320;
inline constexpr int kPageColumnWideNarrow = 440;
inline constexpr int kPageNarrowWindow = 1200;
inline constexpr int kPageRunSpinner = 14;
inline constexpr int kPageRunSpinnerStroke = 2;
/// The run's arc turns by a step every interval, in milliseconds.
inline constexpr int kPageRunSpinnerStep = 80;
inline constexpr int kPageRunSpinnerDegrees = 30;
inline constexpr int kPageRunBar = 3;

// A task's header: its target's thumbnail, the state's icon, its paddings; the bar of a run's progress under it.
inline constexpr int kTaskHeaderThumbnail = 22;
inline constexpr int kTaskHeaderThumbnailRadius = 5;
inline constexpr int kTaskHeaderStateIcon = 13;
inline constexpr int kTaskHeaderPaddingStart = 8;
inline constexpr int kTaskHeaderPaddingEnd = 6;
inline constexpr int kTaskHeaderRunBar = 2;
/// The share of the header's width the bar of a run that cannot tell its progress slides across.
inline constexpr qreal kTaskHeaderSlidingShare = 0.3;

// A task's header that says a job: 8 px above and below its three lines - the eyebrow, the name at 22 px, the facts 1
// px under it - 20 px before them and 16 px after its last button; 16 px between the text and what follows it, 6 px
// between the parts at its right. Its measure: a bar of 180 x 4 px with corners of 2, 3 px under its text and 6 px
// before the buttons, left out where the text would keep less than its least width. A hairline 20 px high stands
// before the ways out.
inline constexpr int kJobHeaderPaddingVertical = 8;
inline constexpr int kJobHeaderPaddingStart = 20;
inline constexpr int kJobHeaderPaddingEnd = 16;
inline constexpr int kJobHeaderGap = 16;
inline constexpr int kJobHeaderPartsGap = 6;
inline constexpr int kJobHeaderTitleFont = 22;
inline constexpr int kJobHeaderFactsGap = 1;
inline constexpr int kJobHeaderMeterWidth = 180;
inline constexpr int kJobHeaderMeterBar = 4;
inline constexpr int kJobHeaderMeterRadius = 2;
inline constexpr int kJobHeaderMeterGap = 3;
inline constexpr int kJobHeaderMeterMargin = 6;
inline constexpr int kJobHeaderTextLeast = 260;
inline constexpr int kJobHeaderSeparator = 20;

// A stage list's row: a status mark in a box, a title over one line, an optional progress.
inline constexpr int kStageMark = 14;
inline constexpr int kStageMarkBox = 24;
inline constexpr int kStageMarkBorder = 2;
inline constexpr int kStageGlyph = 10;
inline constexpr int kStageRowPaddingVertical = 7;
inline constexpr int kStageRowPaddingHorizontal = 10;
inline constexpr int kStageRowGap = 10;
inline constexpr int kStageRowSpacing = 4;
inline constexpr int kStageProgress = 3;
inline constexpr int kStageProgressGap = 4;
inline constexpr int kStageConnector = 2;
inline constexpr int kStageStepMark = 8;
inline constexpr int kStageStepGap = 2;
inline constexpr int kStageStepTextGap = 6;

// A job's page in three parts. The stage list's column: 216 px wide on the first surface behind a hairline, 10 px
// above and below and 8 px at its sides, 8 px between its parts - an eyebrow set 10 px in, the list, the foot set 4 px
// in, whose acts stand 4 px apart. The aside a stage's page keeps at its right: 336 px wide as it opens, between
// 288 and 480 px as a person drags it, padded by 8 px.
inline constexpr int kStageColumnWidth = 216;
inline constexpr int kStageColumnPaddingVertical = 10;
inline constexpr int kStageColumnPaddingHorizontal = 8;
inline constexpr int kStageColumnGap = 8;
inline constexpr int kStageColumnEyebrowInset = 10;
inline constexpr int kStageColumnFootInset = 4;
inline constexpr int kStageColumnFootGap = 4;
inline constexpr int kStageAsideWidth = 336;
inline constexpr int kStageAsideMinWidth = 288;
inline constexpr int kStageAsideMaxWidth = 480;
inline constexpr int kStageAsidePadding = 8;

// The rail: place and foot items in 36 px rows, tasks in 42 px rows, 2 px apart, 6 px from the rail's ends.
inline constexpr int kRailPadding = 6;
inline constexpr int kRailGap = 2;
inline constexpr int kRailPlaceRow = 36;
inline constexpr int kRailTaskRow = 42;
inline constexpr int kRailSquare = 32;
inline constexpr int kRailIcon = 20;
/// The selected item's bar at the rail's left edge, its right corners rounded.
inline constexpr int kRailIndicatorWidth = 3;
inline constexpr int kRailIndicatorHeight = 20;
inline constexpr int kRailIndicatorRadius = 3;
/// The short hairline between the places and the tasks, and its margins.
inline constexpr int kRailHairlineWidth = 24;
inline constexpr int kRailHairlineAbove = 5;
inline constexpr int kRailHairlineBelow = 4;
inline constexpr qreal kRailHairlineShare = 0.6;
/// A workspace's digit and the console's count: 15 px high, ringed by the rail's surface.
inline constexpr int kRailBadge = 15;
inline constexpr int kRailBadgeRadius = 5;
inline constexpr int kRailBadgeRing = 2;
inline constexpr int kRailBadgeFont = 9;
inline constexpr int kRailBadgePadding = 2;
inline constexpr int kRailPillPadding = 3;
/// How far a digit badge reaches past its square's lower right, and a count's pill past its upper right.
inline constexpr int kRailBadgeOffset = 4;
inline constexpr int kRailPillOffset = 3;
/// The tooltip at an item's right: 28 px high, 2 px from the rail, 10 px of padding.
inline constexpr int kRailTipHeight = 28;
inline constexpr int kRailTipGap = 2;
inline constexpr int kRailTipPadding = 10;
/// A dragged thumbnail's ghost.
inline constexpr qreal kRailGhostOpacity = 0.7;
/// The line that marks where a dragged task's thumbnail goes back to: in the gap above its row.
inline constexpr int kRailInsertionLine = 2;

// A task's thumbnail: a 38 px box holding a 30 px picture, a ring 2 px thick, the process's glyph on a 14 px disc at
// the lower right and the dot of an unseen result at the upper right.
inline constexpr int kTaskBox = 38;
inline constexpr int kTaskPicture = 30;
inline constexpr int kTaskPictureRadius = 6;
inline constexpr int kTaskRing = 2;
inline constexpr int kTaskGlyphDisc = 14;
inline constexpr int kTaskGlyph = 10;
inline constexpr int kTaskTileGlyph = 16;
inline constexpr int kTaskResultDot = 13;
inline constexpr int kTaskResultGlyph = 9;
inline constexpr int kTaskFailedRadius = 3;
/// A floating task's picture is drawn at this share.
inline constexpr qreal kTaskFloatingOpacity = 0.45;

// The command palette: a popup 640 px wide, 12 px under the point it opens from; a 48 px query row with an 18 px search
// icon and the query at 15 px; a body inset 6 px at the sides and the foot, its groups under caps headers - 9 px above,
// 3 below, 10 at the sides, on a 14 px line - and its rows with 10 px of padding, 12 px gaps and 16 px icons; a 36 px
// footer. The body takes at most 60 % of the window's height.
inline constexpr int kPaletteWidth = 640;
inline constexpr int kPaletteOffset = 12;
inline constexpr int kPaletteQueryRow = 48;
inline constexpr int kPaletteQueryIcon = 18;
inline constexpr int kPaletteQueryFont = 15;
inline constexpr int kPaletteQueryPaddingStart = 16;
inline constexpr int kPaletteQueryPaddingEnd = 12;
inline constexpr int kPaletteQueryGap = 12;
inline constexpr int kPaletteBodyPadding = 6;
inline constexpr qreal kPaletteBodyShare = 0.6;
inline constexpr int kPaletteHeaderPaddingTop = 9;
inline constexpr int kPaletteHeaderPaddingBottom = 3;
inline constexpr int kPaletteHeaderPaddingSide = 10;
inline constexpr int kPaletteHeaderLine = 14;
/// A header's letter spacing, as a share of its font's size.
inline constexpr qreal kPaletteHeaderTracking = 0.08;
inline constexpr int kPaletteRowRadius = 6;
inline constexpr int kPaletteRowPadding = 10;
inline constexpr int kPaletteRowGap = 12;
inline constexpr int kPaletteRowIcon = 16;
inline constexpr int kPaletteHintGap = 6;
/// The accent's bar at the selected row's left edge.
inline constexpr int kPaletteSelectedBar = 3;
inline constexpr int kPaletteStar = 16;
/// The share of the room beside a row's title its subtitle may take at most.
inline constexpr qreal kPaletteSubtitleShare = 0.45;
inline constexpr int kPaletteFooter = 36;
inline constexpr int kPaletteFooterPadding = 14;
inline constexpr int kPaletteFooterGap = 14;
inline constexpr int kPaletteFooterHintGap = 5;
inline constexpr int kPaletteFooterIcon = 14;

// An icon button: an icon a strip's button high and square, its icon 16 px.
inline constexpr int kIconButtonIcon = 16;
inline constexpr int kIconButtonRadius = 6;

// The readout: a column 360 px wide - 320 in a window under 1200 px wide - of cards 8 px apart, each radius 12 with a
// section header; a plot in a well of radius 8 at least 100 px high; a value being measured again drawn at 55 %. The
// image card starts folded in a window under 820 px high.
inline constexpr int kReadoutColumnWidth = 360;
inline constexpr int kReadoutColumnNarrowWidth = 320;
inline constexpr int kReadoutNarrowWindow = 1200;
inline constexpr int kReadoutShortWindow = 820;
inline constexpr int kReadoutCardGap = 8;
/// The readout column's histogram card grows with the column up to this height - its header, its well and its tools -
/// and the room under the last card is left empty, so a tall window does not stretch the plot.
inline constexpr int kReadoutHistogramCardMaximum = 248;
inline constexpr int kReadoutCardRadius = 12;
inline constexpr int kReadoutWellRadius = 8;
inline constexpr int kReadoutWellMinimum = 100;
/// How far a well's plot sits inside it, so that the plot's square corners stay inside the well's rounded ones.
inline constexpr int kReadoutWellInset = 4;
inline constexpr int kReadoutToolGap = 2;
inline constexpr int kReadoutValueGap = 2;
inline constexpr int kReadoutCellGap = 10;
inline constexpr int kReadoutDot = 6;
inline constexpr int kReadoutDotGap = 4;
/// A caps label's letter spacing, as a share of its font's size.
inline constexpr qreal kReadoutCapsTracking = 0.08;
inline constexpr qreal kReadoutStaleOpacity = 0.55;
// The compact readout of a task: padding 6 12 8, a 20 px header, a well 92 px high beside an 84 px column; a column at
// least 480 px wide lays its histogram and its cells side by side.
inline constexpr int kCompactReadoutPaddingTop = 6;
inline constexpr int kCompactReadoutPaddingSide = 12;
inline constexpr int kCompactReadoutPaddingBottom = 8;
inline constexpr int kCompactReadoutHeader = 20;
inline constexpr int kCompactReadoutWell = 92;
inline constexpr int kCompactReadoutSide = 84;
inline constexpr int kCompactReadoutWide = 480;
inline constexpr int kCompactReadoutIcon = 12;
/// A change smaller than this is shown as none.
inline constexpr double kReadoutDeltaFloor = 0.00005;

// A shell's status line: its texts 14 px apart, 12 px from its ends.
inline constexpr int kStatusLineGap = 14;
inline constexpr int kStatusLinePadding = 12;

// A shell's top bar: icon buttons 32 px square with 20 px icons, 12 px from the bar's left, its parts 8 px apart.
inline constexpr int kTopBarButton = 32;
inline constexpr int kTopBarIcon = 20;
inline constexpr int kTopBarPadding = 12;
inline constexpr int kTopBarGap = 8;
inline constexpr int kTopBarLogo = 20;
// The project switcher in the top bar: at most 220 px wide, its name elided in the middle.
inline constexpr int kProjectSwitcherMaxWidth = 220;
// The command palette's field in the top bar: 340 px wide, 240 px in a window under 1200 px, an icon button under
// 1100 px; the bar's height less 14, the small parts' corners of 8, a 16 px search icon.
inline constexpr int kPaletteFieldWide = 340;
inline constexpr int kPaletteFieldNarrow = 240;
inline constexpr int kPaletteFieldNarrowWindow = 1200;
inline constexpr int kPaletteFieldIconWindow = 1100;
inline constexpr int kPaletteFieldInset = 14;
inline constexpr int kPaletteFieldRadius = 8;
inline constexpr int kPaletteFieldIcon = 16;
// A shell's Session Console drawer: resized by a 5 px band along its top edge, never under 120 px nor over 60 % of
// the window's height.
inline constexpr int kConsoleDrawerMinimumHeight = 120;
inline constexpr int kConsoleDrawerMaximumPercent = 60;
inline constexpr int kConsoleDrawerGrip = 5;

// The floating toolbar of a view: a rounded scrim `metrics.floatingToolbar` high with a 1 px edge under it, padding 0
// 5, gap 2; its buttons 10 px less than its height, its choices 12 px less; separators 1 x 20 with 4 px each side. The
// small form, on a floating image window, is 32 px high, radius 8, padding 0 3, its buttons 26 px.
inline constexpr int kFloatingToolbarRadius = 10;
inline constexpr int kFloatingToolbarPadding = 5;
inline constexpr int kFloatingToolbarGap = 2;
inline constexpr int kFloatingToolbarButtonInset = 10;
inline constexpr int kFloatingToolbarChoiceInset = 12;
inline constexpr int kFloatingToolbarIcon = 18;
inline constexpr int kFloatingToolbarSeparatorHeight = 20;
inline constexpr int kFloatingToolbarSeparatorMargin = 4;
inline constexpr int kFloatingToolbarChannelWidth = 104;
inline constexpr int kFloatingToolbarZoomWidth = 62;
inline constexpr int kFloatingToolbarNameMaximum = 220;
inline constexpr int kFloatingToolbarNameMark = 8;
inline constexpr int kFloatingToolbarNameMarkBorder = 2;
inline constexpr int kFloatingToolbarNamePaddingStart = 10;
inline constexpr int kFloatingToolbarNamePaddingEnd = 6;
inline constexpr int kFloatingToolbarNameGap = 6;
inline constexpr int kFloatingToolbarChoicePadding = 10;
inline constexpr int kFloatingToolbarChoiceMargin = 2;
inline constexpr int kFloatingToolbarChevron = 12;
/// How far above the bottom of the area it floats over the toolbar sits, in its full and its small form.
inline constexpr int kFloatingToolbarLift = 12;
inline constexpr int kFloatingToolbarSmallLift = 8;
/// How far above a sequence's player strip the toolbar sits when it would cover it, in either form.
inline constexpr int kFloatingToolbarClearance = 8;
inline constexpr int kFloatingToolbarSmallHeight = 32;
inline constexpr int kFloatingToolbarSmallRadius = 8;
inline constexpr int kFloatingToolbarSmallPadding = 3;
inline constexpr int kFloatingToolbarSmallButton = 26;
inline constexpr int kFloatingToolbarSmallIcon = 15;
inline constexpr int kFloatingToolbarSmallZoomWidth = 54;
/// What over an image a choice's outline and a separator are drawn in: its text at these shares.
inline constexpr qreal kOnImageOutlineShare = 0.34;
inline constexpr qreal kOnImageSeparatorShare = 0.22;

// The history strip: `metrics.strip` high, padding 0 6 0 12, gap 6; the caps label 6 px before the first chip; chips
// `metrics.chip` high, pills, padding 0 10, an icon of 13 px 6 px before the text and pulled 3 px in; chevrons of 12 px
// between them; at its right the history's buttons, a small control square, their icons 16 px.
inline constexpr int kHistoryStripPaddingStart = 12;
inline constexpr int kHistoryStripPaddingEnd = 6;
inline constexpr int kHistoryStripGap = 6;
inline constexpr int kHistoryStripLabelGap = 6;
inline constexpr int kHistoryChipPadding = 10;
inline constexpr int kHistoryChipIcon = 13;
inline constexpr int kHistoryChipIconPull = 3;
inline constexpr int kHistoryChipGap = 6;
inline constexpr int kHistoryChevron = 12;
inline constexpr int kHistoryButtonIcon = 16;
/// The dash of a pending chip's outline, in its pen's widths: dash, then gap.
inline constexpr qreal kHistoryPendingDash = 3.0;
/// How long a turn of the writing chip's spinner takes, and how often it moves.
inline constexpr int kHistorySpinnerTurnMs = 1000;
inline constexpr int kHistorySpinnerFrameMs = 80;

// The notice over a view a run writes: the view under `scrimSoft`, and at its centre a pill 36 px high, radius 10,
// padding 0 5 0 12, gap 8: a 14 px lock in the accent over images, the text, the share done in the monospace, a 48 x 4
// bar, and a button 26 px high, padding 0 10, 4 px after the bar, its outline the text over images at 40 %.
inline constexpr int kNoticePillHeight = 36;
inline constexpr int kNoticePillRadius = 10;
inline constexpr int kNoticePillPaddingStart = 12;
inline constexpr int kNoticePillPaddingEnd = 5;
inline constexpr int kNoticePillGap = 8;
inline constexpr int kNoticeLockIcon = 14;
inline constexpr int kNoticeBarWidth = 48;
inline constexpr int kNoticeBarHeight = 4;
inline constexpr qreal kNoticeBarTrackShare = 0.24;
inline constexpr int kNoticeButtonHeight = 26;
inline constexpr int kNoticeButtonPadding = 10;
inline constexpr int kNoticeButtonGap = 4;
inline constexpr qreal kNoticeButtonOutlineShare = 0.40;

// Home: a page 28 px from the centre's sides and 20 px under the top bar, its sections 20 px apart; the date in caps
// over a 22 px heading; a quick action's shortcut 8 px after its text. The Continue row is 228 px high: the featured
// image takes what the side cards leave, each side card 312 px wide with 14 px of padding and corners of 10, the cards
// 12 px apart. A recent tile is at least 260 px wide, the tiles 12 px apart: a 108 px picture over its name and its
// facts, 10 px of padding, corners of 8; in the list a row 56 px high with a 72 x 44 picture. The welcome is at most
// 1040 px wide: a 72 px logo over a 26 px title, three cards of 248 x 112 with 36 px icons, a drop zone 768 px wide and
// 72 px high outlined in dashes with corners of 12, and the six things AfterNight does in a row, their icons 20 px.
inline constexpr int kHomePagePaddingSide = 28;
inline constexpr int kHomePagePaddingTop = 20;
inline constexpr int kHomeSectionGap = 20;
inline constexpr int kHomeHeadingFont = 22;
inline constexpr int kHomeDisplayFont = 26;
inline constexpr int kHomeSectionFont = 17;
inline constexpr int kHomeActionKeysGap = 8;
inline constexpr int kHomeActionIcon = 16;
inline constexpr int kHomeContinueRowHeight = 228;
inline constexpr int kHomeSideCardWidth = 312;
inline constexpr int kHomeCardPadding = 14;
inline constexpr int kHomeCardRadius = 10;
inline constexpr int kHomeCardGap = 12;
inline constexpr int kHomeTileMinWidth = 260;
inline constexpr int kHomeTileGap = 12;
inline constexpr int kHomeTilePicture = 108;
inline constexpr int kHomeTilePadding = 10;
inline constexpr int kHomeTileRadius = 8;
inline constexpr int kHomeTileTagInset = 8;
inline constexpr int kHomeListRow = 56;
inline constexpr int kHomeListPictureWidth = 72;
inline constexpr int kHomeListPictureHeight = 44;
inline constexpr int kHomeSearchWidth = 280;
inline constexpr int kHomeWelcomeWidth = 1040;
inline constexpr int kHomeWelcomeLogo = 72;
inline constexpr int kHomeWelcomeCardWidth = 248;
inline constexpr int kHomeWelcomeCardHeight = 112;
inline constexpr int kHomeWelcomeCardIcon = 36;
inline constexpr int kHomeDropZoneWidth = 768;
inline constexpr int kHomeDropZoneHeight = 72;
inline constexpr int kHomeDropZoneRadius = 12;
inline constexpr int kHomeDropZoneIcon = 22;
inline constexpr int kHomeFeatureIcon = 20;
/// The dash of the drop zone's outline, in its pen's widths: dash, then gap.
inline constexpr qreal kHomeDropZoneDash = 4.0;
/// The welcome's quiet star field: a star for every so many square pixels, drawn in the tertiary text at most at this
/// share of its opacity.
inline constexpr int kHomeStarArea = 7000;
inline constexpr qreal kHomeStarShare = 0.45;

// The Integration hub, on Home's page: a section's head with its caption 10 px after its title. The three ways side by
// side, 12 px apart, each a card at least 214 px high with 18 px of padding above and below and 20 at the sides, a
// 40 px square for its icon of 22 px, and 10 px between its rows. The four single steps side by side, 12 px apart, each
// a card with 14 px of padding above and below, 16 on the left and 12 on the right, a 20 px icon and a 14 px chevron.
// The jobs: the search 250 px wide and the project's choice 140 px; a row 12 px higher than a list row, and a running
// job's bar 72 px long and 4 px high.
inline constexpr int kHubSectionCaptionGap = 10;
inline constexpr int kHubWayGap = 12;
inline constexpr int kHubWayMinHeight = 214;
inline constexpr int kHubWayPaddingTop = 18;
inline constexpr int kHubWayPaddingSide = 20;
inline constexpr int kHubWayIconBox = 40;
inline constexpr int kHubWayIcon = 22;
inline constexpr int kHubWayRowGap = 10;
/// The gap between the tags of a way's stages.
inline constexpr int kHubWayTagGap = 4;
inline constexpr int kHubStepGap = 12;
inline constexpr int kHubStepPaddingTop = 14;
inline constexpr int kHubStepPaddingLeft = 16;
inline constexpr int kHubStepPaddingRight = 12;
inline constexpr int kHubStepIcon = 20;
inline constexpr int kHubStepChevron = 14;
inline constexpr int kHubSearchWidth = 250;
inline constexpr int kHubProjectChoiceWidth = 140;
inline constexpr int kHubJobRowExtra = 12;
inline constexpr int kHubJobBarWidth = 72;
inline constexpr int kHubJobBarHeight = 4;
/// The jobs' columns after the job's own, which takes what they leave: its project, its frames, its masters, its last
/// run, how long it took, its status and its button.
inline constexpr int kHubJobProjectColumn = 170;
inline constexpr int kHubJobCountColumn = 80;
inline constexpr int kHubJobLastRunColumn = 150;
inline constexpr int kHubJobTookColumn = 90;
inline constexpr int kHubJobStatusColumn = 310;
inline constexpr int kHubJobButtonColumn = 76;
/// The gap between a job's columns.
inline constexpr int kHubJobColumnGap = 12;

// Home's Projects row: four project cards and an Open project... tile of 120 px, 12 px apart; a card has 10 px of
// padding and a 60 px picture with 12 px beside it. Project Home: the page 14 px under the top bar, 28 px from the
// centre's sides and 12 px between its parts; a 64 px picture beside the heading; the cards in two columns, the right
// one 384 px wide; a section card's head 32 px high, its body 10 px from its sides. A workspace's tile: 8 px of
// padding, corners of 8, a miniature of 176 x 110 with corners of 6 and 2 px between its images, a tab strip of 16 px
// in a tabbed one with its tabs 22 x 10 and 3 px apart. A row of the images' table: a 58 x 40 picture. An integration's
// row 7 px above and below. The storage bar 10 px high with 2 px between its parts, each at least 4 px; a legend's
// swatch of 8 px. The notes at least 44 px high.
inline constexpr int kProjectCardsGap = 12;
inline constexpr int kProjectCardPadding = 10;
inline constexpr int kProjectCardPicture = 60;
inline constexpr int kProjectOpenTileWidth = 120;
inline constexpr int kProjectHomePaddingTop = 14;
inline constexpr int kProjectHomeGap = 12;
inline constexpr int kProjectHomePicture = 64;
inline constexpr int kProjectHomeSideColumn = 384;
inline constexpr int kProjectSectionHead = 32;
inline constexpr int kProjectSectionPadding = 10;
inline constexpr int kWorkspaceTilePadding = 8;
inline constexpr int kWorkspaceTileRadius = 8;
inline constexpr int kWorkspaceMiniatureWidth = 176;
inline constexpr int kWorkspaceMiniatureHeight = 110;
inline constexpr int kWorkspaceMiniatureRadius = 6;
inline constexpr int kWorkspaceMiniatureGap = 2;
inline constexpr int kWorkspaceMiniatureTabStrip = 16;
inline constexpr int kWorkspaceMiniatureTabWidth = 22;
inline constexpr int kWorkspaceMiniatureTabHeight = 10;
inline constexpr int kWorkspaceMiniatureTabGap = 3;
/// The images' table: a 58 x 40 picture, the name, which takes what the others leave, then the type, the size, the
/// history, the state and when the file changed.
inline constexpr int kProjectImagePictureWidth = 58;
inline constexpr int kProjectImagePictureHeight = 40;
inline constexpr int kProjectImageTypeColumn = 56;
inline constexpr int kProjectImageSizeColumn = 98;
inline constexpr int kProjectImageHistoryColumn = 134;
inline constexpr int kProjectImageStateColumn = 168;
inline constexpr int kProjectImageModifiedColumn = 124;
inline constexpr int kProjectIntegrationRowPadding = 7;
inline constexpr int kStorageBarHeight = 10;
inline constexpr int kStorageBarGap = 2;
inline constexpr int kStorageBarLeast = 4;
inline constexpr int kStorageSwatch = 8;
inline constexpr int kProjectNotesMinHeight = 44;

// What every part draws the same way.
inline constexpr int kFocusRingWidth = 2;
/// The stroke of a glyph a part draws itself: a check, a cross, a chevron.
inline constexpr qreal kGlyphStroke = 1.5;
inline constexpr qreal kHoverLayer = 0.08;
inline constexpr qreal kPressedLayer = 0.12;
inline constexpr qreal kDisabledOpacity = 0.38;

/// A font of the type scale at a size of the metrics, in logical pixels, set as points at Qt's 96 logical dots per
/// inch as the type scale is set, at a weight.
[[nodiscard]] AFTERNIGHTCONTROLS_API QFont sizedFont(const QFont& base, int logicalPixels,
                                                     QFont::Weight weight = QFont::Normal);

/// The exact width of a text in a font, rounded up: the room a part gives a text it may later elide, so that the
/// text it was measured for is never elided (`horizontalAdvance` rounds to the nearest pixel, which with fractional
/// advances is less than the text needs).
[[nodiscard]] AFTERNIGHTCONTROLS_API int textWidth(const QFont& font, const QString& text);

/// A colour at a share of its own opacity.
[[nodiscard]] AFTERNIGHTCONTROLS_API QColor atShare(const QColor& colour, qreal share);

/// The text colour for a background: `preferred` when it reaches `minimumRatio` against it, otherwise white or black,
/// whichever stands out more - or `preferred` when neither does better.
[[nodiscard]] AFTERNIGHTCONTROLS_API QColor contrastingTextColor(const QColor& background, const QColor& preferred,
                                                                 double minimumRatio);

/// Lays the state colour over a part's shape - 8 % while hovered, 12 % while pressed, nothing otherwise.
AFTERNIGHTCONTROLS_API void paintStateLayer(QPainter& painter, const QRectF& part, qreal radius, bool hovered,
                                            bool pressed);

/// The ring a part draws while it holds the keyboard's focus: 2 px of the focus colour at the part's edge, inside it,
/// as the proxy style draws a control's - a widget paints nothing outside its own rectangle.
AFTERNIGHTCONTROLS_API void paintFocusRing(QPainter& painter, const QRectF& part, qreal radius);

/// Whether a focus reason is the keyboard's - a part draws its ring only then.
[[nodiscard]] AFTERNIGHTCONTROLS_API bool isKeyboardFocusReason(Qt::FocusReason reason);

/// An icon drawn in one colour in a square - its shape kept, its colours replaced - at the painter's device pixel
/// ratio, as a part draws an icon in the colour of its text.
AFTERNIGHTCONTROLS_API void paintIcon(QPainter& painter, const QIcon& icon, const QRectF& square, const QColor& colour);

/// A check mark stroked in a square, as a chosen segment, a switch's knob and a finished stage draw it.
AFTERNIGHTCONTROLS_API void paintCheckMark(QPainter& painter, const QRectF& square, const QColor& colour, qreal width);

/// A cross stroked in a square, as a failed stage draws it.
AFTERNIGHTCONTROLS_API void paintCrossMark(QPainter& painter, const QRectF& square, const QColor& colour, qreal width);

/// An exclamation mark in a square, as a stage that ended with a warning draws it.
AFTERNIGHTCONTROLS_API void paintExclamationMark(QPainter& painter, const QRectF& square, const QColor& colour,
                                                 qreal width);

/// A caps label: the caption in semibold capitals, its letters spaced by the readout's tracking - a card's title, a
/// readout's label, the column's eyebrow.
[[nodiscard]] AFTERNIGHTCONTROLS_API QFont capsFont(const QFont& base, int logicalPixels);

/// A chevron pointing right stroked in a square, as a breadcrumb draws it between its parts.
AFTERNIGHTCONTROLS_API void paintChevronRight(QPainter& painter, const QRectF& square, const QColor& colour,
                                              qreal width);

} // namespace an::ui::parts
