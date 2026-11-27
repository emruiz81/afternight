// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"

#include "controls/ProcessPageLayout.h"

#include "components/AnCollapsibleSection.h"
#include "components/AnIconButton.h"
#include "components/AnInfoBanner.h"
#include "components/AnTag.h"
#include "components/AnTaskHeader.h"
#include "controls/IAppContext.h"
#include "controls/ImageViewControl.h"
#include "widgets/ImageViewWidget.h"
#include "controls/ProcessPreviewLayout.h"
#include "controls/ViewChrome.h"
#include "theme/AnDesignTokens.h"
#include "controls/AccessiblePage.h"
#include "theme/AnimationUtils.h"
#include "theme/ComponentParts.h"

#include <QAbstractSpinBox>
#include <QAction>
#include <QApplication>
#include <QBoxLayout>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QEvent>
#include <QFontMetrics>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSpacerItem>
#include <QSplitter>
#include <QTabWidget>
#include <QTimer>

#include <algorithm>
#include <cmath>
#include <map>
#include <utility>

namespace an::ui
{

namespace
{

[[nodiscard]] QString translated(const char* text)
{
    return QCoreApplication::translate("ProcessPageLayout", text);
}

/// The column: the first surface, a hairline at its left.
class PageColumn final : public QWidget
{
public:
    explicit PageColumn(QWidget* parent) : QWidget(parent)
    {
        setObjectName(QStringLiteral("pageColumn"));
    }

protected:
    void paintEvent(QPaintEvent* /*event*/) override
    {
        const DesignTokens& t = tokens();
        QPainter painter(this);
        painter.fillRect(rect(), t.layers.surface1);
        painter.fillRect(QRect(0, 0, 1, height()), t.colors.borderSubtle);
    }
};

/// The canvas: the colour behind an image; with no view in it, it says why - the images still to choose, or that the
/// preview is off.
class PageCanvas final : public QWidget
{
public:
    explicit PageCanvas(QWidget* parent) : QWidget(parent)
    {
        setObjectName(QStringLiteral("pageCanvas"));
    }

    /// The images still to choose, by their names in the controls.
    QStringList missing;

    /// Whether the canvas shows something: a view, or a widget that marks itself as the canvas's content.
    [[nodiscard]] bool showsAView() const
    {
        for (const QWidget* child : findChildren<QWidget*>())
        {
            const bool content =
                dynamic_cast<const ImageViewControl*>(child) != nullptr || child->property("anCanvasContent").toBool();
            if (content && child->isVisibleTo(this))
            {
                return true;
            }
        }
        return false;
    }

    /// What the canvas says with no view: its heading and, with images still to choose, the line that names them.
    [[nodiscard]] QStringList message() const
    {
        if (missing.isEmpty())
        {
            return {translated("The preview is off")};
        }
        return {translated("Choose the images to see the result"),
                translated("Still to choose: %1").arg(missing.join(QStringLiteral(", ")))};
    }

protected:
    void paintEvent(QPaintEvent* /*event*/) override
    {
        const DesignTokens& t = tokens();
        QPainter painter(this);
        painter.fillRect(rect(), t.colors.bgCanvas);
        if (showsAView())
        {
            return;
        }
        const QStringList lines = message();
        if (lines.size() == 1)
        {
            painter.setPen(t.colors.textSecondary);
            painter.setFont(parts::sizedFont(t.typography.body, t.metrics.fontSmall));
            painter.drawText(rect(), Qt::AlignCenter, lines.front());
            return;
        }
        // The heading over the images still to choose, the two centred together.
        const QFont heading = parts::sizedFont(t.typography.body, t.metrics.fontBody, QFont::DemiBold);
        const QFont detail = parts::sizedFont(t.typography.body, t.metrics.fontSmall);
        const int headingHeight = QFontMetrics(heading).height();
        const int detailHeight = QFontMetrics(detail).height();
        const int top = (height() - headingHeight - t.metrics.gap - detailHeight) / 2;
        const QRect headingRect(t.metrics.pad, top, width() - 2 * t.metrics.pad, headingHeight);
        const QRect detailRect(t.metrics.pad, top + headingHeight + t.metrics.gap, width() - 2 * t.metrics.pad,
                               detailHeight);
        painter.setPen(t.colors.textPrimary);
        painter.setFont(heading);
        painter.drawText(headingRect, Qt::AlignCenter, lines[0]);
        painter.setPen(t.colors.textSecondary);
        painter.setFont(detail);
        painter.drawText(detailRect, Qt::AlignCenter,
                         QFontMetrics(detail).elidedText(lines[1], Qt::ElideRight, detailRect.width()));
    }
};

/// A part whose look - its labels' fonts and colours - is applied where it is polished and told of a change of style,
/// never where it is made.
class LookedPart : public QWidget
{
public:
    using QWidget::QWidget;

protected:
    bool event(QEvent* event) override
    {
        const QEvent::Type type = event->type();
        if (type == QEvent::Polish || type == QEvent::StyleChange || type == QEvent::PaletteChange)
        {
            applyLook();
        }
        return QWidget::event(event);
    }

    virtual void applyLook() {}
};

/// The column's header: the category as a caps eyebrow, the process's name as a heading - its description the tooltip -
/// and Close.
class PageHeader final : public LookedPart
{
public:
    explicit PageHeader(QWidget* parent) : LookedPart(parent)
    {
        setObjectName(QStringLiteral("pageHeader"));
        auto* layout = new QGridLayout(this);
        eyebrow = new QLabel(this);
        eyebrow->setObjectName(QStringLiteral("pageEyebrow"));
        title = new QLabel(this);
        title->setObjectName(QStringLiteral("pageTitle"));
        title->setTextInteractionFlags(Qt::NoTextInteraction);
        close = new AnIconButton(QStringLiteral("close"), translated("Close (Esc)"), this);
        close->setObjectName(QStringLiteral("pageClose"));
        // The tag follows the title, as quiet as a fact is; the page shows it once a process states one.
        tag = new AnTag(QString(), AnTag::Tone::Outlined, this);
        tag->setObjectName(QStringLiteral("pageTag"));
        tag->hide();
        layout->addWidget(eyebrow, 0, 0, 1, 3);
        layout->addWidget(title, 1, 0);
        layout->addWidget(tag, 1, 1, Qt::AlignLeft | Qt::AlignVCenter);
        layout->addWidget(close, 0, 3, 2, 1, Qt::AlignRight | Qt::AlignVCenter);
        layout->setColumnStretch(2, 1);
    }

    QLabel* eyebrow = nullptr;
    QLabel* title = nullptr;
    AnTag* tag = nullptr;
    AnIconButton* close = nullptr;

protected:
    void applyLook() override
    {
        const DesignTokens& t = tokens();
        auto* grid = static_cast<QGridLayout*>(layout());
        grid->setContentsMargins(t.metrics.pad, t.metrics.gap, t.metrics.pad / 2, t.metrics.gap / 2);
        grid->setHorizontalSpacing(t.metrics.gap);
        grid->setVerticalSpacing(0);
        eyebrow->setFont(parts::capsFont(t.typography.body, t.metrics.fontCaption));
        QPalette secondary = eyebrow->palette();
        secondary.setColor(QPalette::WindowText, t.colors.textTertiary);
        eyebrow->setPalette(secondary);
        title->setFont(parts::sizedFont(t.typography.body, t.metrics.fontBody + 2, QFont::DemiBold));
        QPalette primary = title->palette();
        primary.setColor(QPalette::WindowText, t.colors.textPrimary);
        title->setPalette(primary);
    }
};

/// A row of the column with the panel's padding: the toggles, the footer.
class PaddedRow final : public LookedPart
{
public:
    PaddedRow(const QString& name, QWidget* parent) : LookedPart(parent)
    {
        setObjectName(name);
        new QHBoxLayout(this);
    }

protected:
    void applyLook() override
    {
        const DesignTokens& t = tokens();
        layout()->setContentsMargins(t.metrics.pad, t.metrics.gap, t.metrics.pad, t.metrics.gap);
        layout()->setSpacing(t.metrics.gap);
    }
};

/// The head of a full page during a run: the run's row, and Cancel at its end, level with the row's container.
class RunHead final : public LookedPart
{
public:
    explicit RunHead(QWidget* parent) : LookedPart(parent)
    {
        setObjectName(QStringLiteral("pageRunHead"));
        new QHBoxLayout(this);
    }

protected:
    void applyLook() override
    {
        const DesignTokens& t = tokens();
        // The row pads itself at both sides; Cancel is as far from the page's side as the row's container is.
        layout()->setContentsMargins(0, 0, t.metrics.pad, 0);
        layout()->setSpacing(0);
    }
};

/// The run's row in the banner's place: the accent's container, a turning arc, what the run does and how far it has
/// come in the monospace, a thin bar; and under it, that the task keeps running when the page is left. Over a sequence
/// the container holds two bars under what the run does, each with its caption and its share: Overall, how far the
/// frames are, and Processing, how far the frame on its way is - a slice that slides while the frame tells nothing.
class RunRow final : public QWidget
{
public:
    explicit RunRow(QWidget* parent) : QWidget(parent)
    {
        setObjectName(QStringLiteral("pageRunRow"));
        m_turn.setInterval(parts::kPageRunSpinnerStep);
        QObject::connect(&m_turn, &QTimer::timeout, this, [this]() {
            // Animations turned off while it turned: the arc stops where an arc drawn with them off stands.
            if (!AnimationUtils::animationsEnabled())
            {
                m_turn.stop();
                m_angle = 0;
                update();
                return;
            }
            m_angle = (m_angle + parts::kPageRunSpinnerDegrees) % 360;
            update();
        });
    }

    void setRun(const QString& text, const std::optional<double> progress, const bool sequence = false,
                const std::optional<double> frameProgress = std::nullopt)
    {
        const bool reshaped = sequence != m_sequence;
        m_text = text;
        m_progress = progress;
        m_sequence = sequence;
        m_frameProgress = frameProgress;
        if (reshaped)
        {
            updateGeometry();
        }
        update();
    }

    /// How many bars the row shows: two over a sequence, one otherwise.
    [[nodiscard]] int bars() const noexcept
    {
        return m_sequence ? 2 : 1;
    }

    [[nodiscard]] QString text() const
    {
        return m_text;
    }

    [[nodiscard]] QSize sizeHint() const override
    {
        const DesignTokens& t = tokens();
        const QFontMetrics line(parts::sizedFont(t.typography.body, t.metrics.fontCaption));
        return QSize(parts::kPageColumnStandard,
                     t.metrics.controlSmall + barsHeight(t) + line.height() + t.metrics.gap);
    }

    [[nodiscard]] QSize minimumSizeHint() const override
    {
        return QSize(0, sizeHint().height());
    }

protected:
    void showEvent(QShowEvent* event) override
    {
        QWidget::showEvent(event);
        if (AnimationUtils::animationsEnabled())
        {
            m_turn.start();
        }
    }

    void hideEvent(QHideEvent* event) override
    {
        m_turn.stop();
        QWidget::hideEvent(event);
    }

    void paintEvent(QPaintEvent* /*event*/) override
    {
        const DesignTokens& t = tokens();
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const int pad = t.metrics.pad;
        const QRect row(pad, t.metrics.gap / 2, width() - 2 * pad, t.metrics.controlSmall);
        // Over a sequence the container holds the two bars under the row.
        const QRect container = row.adjusted(0, 0, 0, barsHeight(t));
        QPainterPath fill;
        fill.addRoundedRect(QRectF(container), t.radius.sm, t.radius.sm);
        painter.fillPath(fill, t.colors.accentContainer);

        // The turning arc.
        const int spinner = parts::kPageRunSpinner;
        const QRectF arc(row.left() + t.metrics.gap, row.center().y() - spinner / 2.0, spinner, spinner);
        painter.setPen(QPen(t.colors.textOnAccentContainer, parts::kPageRunSpinnerStroke, Qt::SolidLine, Qt::RoundCap));
        painter.drawArc(arc, -m_angle * 16, 270 * 16);

        // What the run does, and how far it has come.
        const int textLeft = static_cast<int>(arc.right()) + t.metrics.gap;
        painter.setFont(parts::sizedFont(t.typography.body, t.metrics.fontSmall, QFont::DemiBold));
        painter.setPen(t.colors.textOnAccentContainer);
        const QFontMetrics metrics(painter.font());
        const QString label = metrics.elidedText(m_text, Qt::ElideRight, row.width() / 2);
        painter.drawText(QRect(textLeft, row.top(), row.width(), row.height()), Qt::AlignVCenter | Qt::AlignLeft,
                         label);
        int barLeft = textLeft + metrics.horizontalAdvance(label) + t.metrics.gap;
        if (m_sequence)
        {
            paintSequenceBars(painter, t, container, row.bottom() + 1);
        }
        else if (m_progress.has_value())
        {
            painter.setFont(parts::sizedFont(t.typography.monospace, t.metrics.fontSmall));
            const QString percent = QStringLiteral("%1 %").arg(static_cast<int>(std::lround(*m_progress * 100.0)));
            painter.drawText(QRect(barLeft, row.top(), row.width(), row.height()), Qt::AlignVCenter | Qt::AlignLeft,
                             percent);
            barLeft += QFontMetrics(painter.font()).horizontalAdvance(percent) + t.metrics.gap;
        }
        const int barRight = row.right() - t.metrics.gap;
        if (!m_sequence && barRight > barLeft)
        {
            const QRect track(barLeft, row.center().y() - parts::kPageRunBar / 2, barRight - barLeft,
                              parts::kPageRunBar);
            painter.fillRect(track, t.colors.accentTrack);
            const double share = m_progress.value_or(0.0);
            painter.fillRect(QRect(track.left(), track.top(), static_cast<int>(track.width() * share), track.height()),
                             t.colors.accentPrimary);
        }

        // The line under it.
        painter.setFont(parts::sizedFont(t.typography.body, t.metrics.fontCaption));
        painter.setPen(t.colors.textSecondary);
        painter.drawText(QRect(pad, container.bottom() + 1, width() - 2 * pad, height() - container.bottom() - 1),
                         Qt::AlignVCenter | Qt::AlignLeft,
                         translated("The task keeps running if you leave this page."));
    }

private:
    /// The height the two bars of a run over a sequence add to the container: a line each, and a gap under them.
    [[nodiscard]] int barsHeight(const DesignTokens& t) const
    {
        if (!m_sequence)
        {
            return 0;
        }
        const QFontMetrics small(parts::sizedFont(t.typography.body, t.metrics.fontSmall));
        return 2 * small.height() + t.metrics.gap;
    }

    /// Overall and Processing, each a caption, a bar and its share, a line each under the row.
    void paintSequenceBars(QPainter& painter, const DesignTokens& t, const QRect& container, const int top) const
    {
        const QFont captionFont = parts::sizedFont(t.typography.body, t.metrics.fontSmall);
        const QFont shareFont = parts::sizedFont(t.typography.monospace, t.metrics.fontSmall);
        const QFontMetrics captions(captionFont);
        const QFontMetrics shares(shareFont);
        const QString overall = translated("Overall");
        const QString processing = translated("Processing");
        const int captionWidth =
            (std::max)(captions.horizontalAdvance(overall), captions.horizontalAdvance(processing));
        const int shareWidth = shares.horizontalAdvance(QStringLiteral("100 %"));
        const int left = container.left() + t.metrics.gap;
        const int right = container.right() - t.metrics.gap;
        const int lineHeight = captions.height();
        const auto line = [&](const int index, const QString& caption, const std::optional<double> share) {
            const QRect bounds(left, top + index * lineHeight, right - left, lineHeight);
            painter.setFont(captionFont);
            painter.setPen(t.colors.textOnAccentContainer);
            painter.drawText(QRect(bounds.left(), bounds.top(), captionWidth, bounds.height()),
                             Qt::AlignVCenter | Qt::AlignLeft, caption);
            const int barLeft = bounds.left() + captionWidth + t.metrics.gap;
            const int barRight = bounds.right() - shareWidth - t.metrics.gap;
            if (barRight > barLeft)
            {
                const QRect track(barLeft, bounds.center().y() - parts::kPageRunBar / 2, barRight - barLeft,
                                  parts::kPageRunBar);
                painter.fillRect(track, t.colors.accentTrack);
                if (share.has_value())
                {
                    painter.fillRect(
                        QRect(track.left(), track.top(), static_cast<int>(track.width() * *share), track.height()),
                        t.colors.accentPrimary);
                }
                else
                {
                    // Nothing told yet: a slice slides along the track as the arc turns.
                    const int slice = track.width() / 3;
                    const int offset = static_cast<int>((track.width() - slice) * (m_angle / 360.0));
                    painter.fillRect(QRect(track.left() + offset, track.top(), slice, track.height()),
                                     t.colors.accentPrimary);
                }
            }
            if (share.has_value())
            {
                painter.setFont(shareFont);
                painter.drawText(QRect(bounds.right() - shareWidth, bounds.top(), shareWidth, bounds.height()),
                                 Qt::AlignVCenter | Qt::AlignRight,
                                 QStringLiteral("%1 %").arg(static_cast<int>(std::lround(*share * 100.0))));
            }
        };
        line(0, overall, m_progress);
        line(1, processing, m_frameProgress);
    }

    QString m_text;
    std::optional<double> m_progress;
    bool m_sequence = false;
    std::optional<double> m_frameProgress;
    QTimer m_turn;
    int m_angle = 0;
};

/// Where a widget lies in a layout, the layout's children searched too: the box layout that holds it and its index.
struct Slot
{
    QBoxLayout* layout = nullptr;
    int index = -1;
};

/// Whether a widget is a plain container of the controls - a `QWidget` that only lays out what it holds - whose layout
/// is searched as the controls' own.
[[nodiscard]] bool isPlainContainer(const QWidget& widget)
{
    return QString::fromLatin1(widget.metaObject()->className()) == QLatin1String("QWidget");
}

/// Where a widget lies in a layout: its child layouts searched, and the layouts of the plain containers it lays out,
/// as the controls' groups are collected.
[[nodiscard]] Slot slotOf(QLayout* layout, const QWidget* widget)
{
    if (layout == nullptr)
    {
        return {};
    }
    for (int index = 0; index < layout->count(); ++index)
    {
        QLayoutItem* const item = layout->itemAt(index);
        if (item->widget() == widget)
        {
            return Slot{qobject_cast<QBoxLayout*>(layout), index};
        }
        QLayout* searched = item->layout();
        if (searched == nullptr && item->widget() != nullptr && isPlainContainer(*item->widget()))
        {
            searched = item->widget()->layout();
        }
        if (searched != nullptr)
        {
            const Slot found = slotOf(searched, widget);
            if (found.layout != nullptr || found.index >= 0)
            {
                return found;
            }
        }
    }
    return {};
}

/// The layout that holds a widget, the layout's children and the layouts of the widgets it lays out - a group box's -
/// searched: null when none does.
[[nodiscard]] QLayout* holderOf(QLayout* layout, const QWidget* widget)
{
    if (layout == nullptr)
    {
        return nullptr;
    }
    for (int index = 0; index < layout->count(); ++index)
    {
        QLayoutItem* const item = layout->itemAt(index);
        if (item->widget() == widget)
        {
            return layout;
        }
        if (QLayout* const found = holderOf(item->layout(), widget))
        {
            return found;
        }
        if (QWidget* const laidOut = item->widget(); laidOut != nullptr)
        {
            if (QLayout* const found = holderOf(laidOut->layout(), widget))
            {
                return found;
            }
        }
    }
    return nullptr;
}

/// The top-level group boxes of the controls: those a walk of the controls' layout meets, into its child layouts and
/// into the layouts of plain containers, never into a group, a tab widget, a scroll area or a splitter.
void collectGroups(QLayout* layout, std::vector<QGroupBox*>& groups)
{
    if (layout == nullptr)
    {
        return;
    }
    for (int index = 0; index < layout->count(); ++index)
    {
        QLayoutItem* const item = layout->itemAt(index);
        if (QLayout* const child = item->layout())
        {
            collectGroups(child, groups);
            continue;
        }
        QWidget* const widget = item->widget();
        if (widget == nullptr)
        {
            continue;
        }
        if (auto* const group = qobject_cast<QGroupBox*>(widget))
        {
            groups.push_back(group);
            continue;
        }
        if (qobject_cast<QTabWidget*>(widget) != nullptr || qobject_cast<QAbstractScrollArea*>(widget) != nullptr ||
            qobject_cast<QSplitter*>(widget) != nullptr)
        {
            continue;
        }
        if (isPlainContainer(*widget))
        {
            collectGroups(widget->layout(), groups);
        }
    }
}

/// The footer's place of a button, by its role in the box: Reset and Help at the left, then a stretch, then the
/// others, Close, and the primary at the right.
[[nodiscard]] int footerRank(const QDialogButtonBox::ButtonRole role)
{
    switch (role)
    {
    case QDialogButtonBox::ResetRole:
    case QDialogButtonBox::HelpRole:
        return 0;
    case QDialogButtonBox::ActionRole:
    case QDialogButtonBox::DestructiveRole:
        return 1;
    case QDialogButtonBox::RejectRole:
    case QDialogButtonBox::NoRole:
        return 2;
    case QDialogButtonBox::ApplyRole:
    case QDialogButtonBox::AcceptRole:
    case QDialogButtonBox::YesRole:
        return 3;
    default:
        return 1;
    }
}

[[nodiscard]] const char* footerRole(const QDialogButtonBox::ButtonRole role)
{
    switch (footerRank(role))
    {
    case 0:
        return "neutral";
    case 3:
        return "primary";
    default:
        return "secondary";
    }
}

} // namespace

int pageColumnWidth(const ProcessColumnWidth width, const int windowWidth)
{
    const bool narrow = windowWidth > 0 && windowWidth < parts::kPageNarrowWindow;
    if (width == ProcessColumnWidth::Wide)
    {
        return narrow ? parts::kPageColumnWideNarrow : parts::kPageColumnWide;
    }
    return narrow ? parts::kPageColumnStandardNarrow : parts::kPageColumnStandard;
}

struct ProcessPageLayout::Impl : public QObject
{
    Impl(QWidget& process, const ProcessStandardParts& standardParts, Description stated)
        : processWidget(process), standard(standardParts), description(std::move(stated))
    {
    }

    QWidget& processWidget;
    ProcessStandardParts standard;
    Description description;
    Hooks hooks;

    QWidget* page = nullptr;
    QScrollArea* fullArea = nullptr;
    QPointer<QMenuBar> menuBar;
    PageCanvas* canvas = nullptr;
    QWidget* canvasFoot = nullptr;
    PageColumn* column = nullptr;
    QWidget* readoutSlot = nullptr;
    PageHeader* header = nullptr;
    PaddedRow* toggles = nullptr;
    AnInfoBanner* banner = nullptr;
    RunRow* runRow = nullptr;

    /// A full page's head during a run: the run's row and Cancel; null on a page with a column, and where the process
    /// shows its run in itself.
    QWidget* runHead = nullptr;
    QScrollArea* body = nullptr;
    QWidget* bodyContent = nullptr;
    QVBoxLayout* bodyLayout = nullptr;
    PaddedRow* footer = nullptr;
    QPushButton* cancel = nullptr;
    QCheckBox* previewArea = nullptr;
    QPushButton* compare = nullptr;

    /// What Compare names the image the canvas shows and its alternate: the process's result and the image before it.
    QString compareShown = translated("Processed");
    QString compareAlternate = translated("Original");

    /// The controls: the pane of a splitter, or the widget the row's controls layout was moved into; and the layout
    /// whose side margins are the body's padding, with the margins it came with.
    QWidget* controls = nullptr;
    QLayout* controlsLayout = nullptr;
    QMargins controlsMargins;

    /// The footer's buttons, by the box's button they press.
    std::vector<std::pair<QPointer<QAbstractButton>, QPushButton*>> footerButtons;
    QPushButton* primary = nullptr;

    /// The footer's button for the box's own Cancel, which closes the process without a run; null for a box with none.
    /// During a run it gives way, as the primary button does, to the Cancel that stops the run: the footer shows one
    /// Cancel.
    QPushButton* boxCancel = nullptr;

    std::vector<AnCollapsibleSection*> sections;
    std::map<const AnCollapsibleSection*, std::uint64_t> lastUse;
    std::uint64_t useClock = 0;
    bool fitting = false;
    bool fitQueued = false;

    QPointer<ImageViewControl> preview;
    std::vector<QMetaObject::Connection> previewConnections;
    std::function<void(ImageViewControl*)> previewListener;
    std::optional<JobHeading> job;
    std::function<void()> jobListener;
    ProcessRunState run;
    QString message;

    // ---- Building

    void build()
    {
        if (description.fullPage)
        {
            buildFullPage();
            return;
        }
        QLayout* const root = processWidget.layout();
        // A process with no preview - a mirror page's - is its window's contents whole: they are the controls.
        QWidget* const windowContents = standard.previewLayout == nullptr ? takeWindowContents(*root) : nullptr;
        // Everything the window laid out is hidden; its layout is kept, and holds the page alone.
        for (int index = 0; index < root->count(); ++index)
        {
            if (QWidget* const widget = root->itemAt(index)->widget())
            {
                widget->hide();
            }
        }
        root->setContentsMargins(0, 0, 0, 0);
        root->setSpacing(0);
        root->setSizeConstraint(QLayout::SetNoConstraint);

        page = new QWidget(&processWidget);
        page->setObjectName(QStringLiteral("processPage"));
        auto* pageLayout = new QHBoxLayout(page);
        pageLayout->setContentsMargins(0, 0, 0, 0);
        pageLayout->setSpacing(0);

        auto* canvasPane = new QWidget(page);
        canvasPane->setObjectName(QStringLiteral("pageCanvasPane"));
        auto* canvasPaneLayout = new QVBoxLayout(canvasPane);
        canvasPaneLayout->setContentsMargins(0, 0, 0, 0);
        canvasPaneLayout->setSpacing(0);
        canvas = new PageCanvas(canvasPane);
        auto* canvasLayout = new QVBoxLayout(canvas);
        canvasLayout->setContentsMargins(0, 0, 0, 0);
        canvasLayout->setSpacing(0);
        canvasPaneLayout->addWidget(canvas, 1);
        canvasFoot = new QWidget(canvasPane);
        canvasFoot->setObjectName(QStringLiteral("pageCanvasFoot"));
        auto* footLayout = new QVBoxLayout(canvasFoot);
        footLayout->setContentsMargins(0, 0, 0, 0);
        footLayout->setSpacing(0);
        canvasPaneLayout->addWidget(canvasFoot);
        pageLayout->addWidget(canvasPane, 1);

        column = new PageColumn(page);
        column->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
        auto* columnLayout = new QVBoxLayout(column);
        columnLayout->setContentsMargins(0, 0, 0, 0);
        columnLayout->setSpacing(0);
        pageLayout->addWidget(column);

        readoutSlot = new QWidget(column);
        readoutSlot->setObjectName(QStringLiteral("pageReadoutSlot"));
        auto* slotLayout = new QVBoxLayout(readoutSlot);
        slotLayout->setContentsMargins(0, 0, 0, 0);
        slotLayout->setSpacing(0);
        columnLayout->addWidget(readoutSlot);

        header = new PageHeader(column);
        header->title->setText(description.title);
        header->title->setToolTip(description.description);
        header->eyebrow->setVisible(false);
        QObject::connect(header->close, &QAbstractButton::clicked, this, [this]() {
            if (hooks.close)
            {
                hooks.close();
            }
        });
        columnLayout->addWidget(header);

        toggles = new PaddedRow(QStringLiteral("pageToggles"), column);
        columnLayout->addWidget(toggles);
        banner = new AnInfoBanner(QString(), AnInfoBanner::Tone::Info, column);
        banner->setObjectName(QStringLiteral("pageBanner"));
        banner->hide();
        columnLayout->addWidget(banner);
        runRow = new RunRow(column);
        runRow->hide();
        columnLayout->addWidget(runRow);

        body = new QScrollArea(column);
        body->setObjectName(QStringLiteral("pageBody"));
        body->setFrameShape(QFrame::NoFrame);
        body->setWidgetResizable(true);
        body->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        bodyContent = new QWidget();
        bodyContent->setObjectName(QStringLiteral("pageBodyContent"));
        bodyLayout = new QVBoxLayout(bodyContent);
        bodyLayout->setContentsMargins(0, 0, 0, 0);
        bodyLayout->setSpacing(0);
        body->setWidget(bodyContent);
        body->viewport()->installEventFilter(this);
        columnLayout->addWidget(body, 1);

        footer = new PaddedRow(QStringLiteral("pageFooter"), column);
        columnLayout->addWidget(footer);

        if (windowContents != nullptr)
        {
            controls = windowContents;
            controls->setParent(bodyContent);
            bodyLayout->addWidget(controls, description.controlsFillTheBody ? 1 : 0);
            controls->show();
            controlsLayout = controls->layout();
            controlsMargins = controlsLayout->contentsMargins();
        }
        else
        {
            arrangeCanvasAndControls(*canvasLayout);
        }
        if (controls != nullptr)
        {
            controls->installEventFilter(this);
        }
        if (!description.controlsFillTheBody)
        {
            keepTheRowsTogether();
        }
        leadWithSelectors();
        makeSections();
        makeToggles();
        makeFooter();

        root->addWidget(page);
        page->installEventFilter(this);
        QObject::connect(qApp, &QApplication::focusChanged, this,
                         [this](QWidget* /*old*/, QWidget* now) { focusMoved(now); });
        columnResized();
    }

    /// The process whole in a scroll area that fills the page: what its window laid out is moved into it, its menu bar
    /// hidden there and its menus offered by the page.
    void buildFullPage()
    {
        QLayout* const root = processWidget.layout();
        menuBar = qobject_cast<QMenuBar*>(root->menuBar());
        QWidget* const contents = takeWindowContents(*root);
        contents->setObjectName(QStringLiteral("pageFullContents"));
        if (menuBar.isNull())
        {
            const QList<QMenuBar*> bars = contents->findChildren<QMenuBar*>();
            menuBar = bars.isEmpty() ? nullptr : bars.front();
        }
        if (menuBar)
        {
            menuBar->hide();
        }
        root->setContentsMargins(0, 0, 0, 0);
        root->setSpacing(0);
        root->setSizeConstraint(QLayout::SetNoConstraint);

        page = new QWidget(&processWidget);
        page->setObjectName(QStringLiteral("processPage"));
        auto* pageLayout = new QVBoxLayout(page);
        pageLayout->setContentsMargins(0, 0, 0, 0);
        pageLayout->setSpacing(0);
        // The process's message, and during a run the run's own row with Cancel, over the area.
        banner = new AnInfoBanner(QString(), AnInfoBanner::Tone::Info, page);
        banner->setObjectName(QStringLiteral("pageBanner"));
        banner->hide();
        pageLayout->addWidget(banner);
        if (description.runAtHead)
        {
            runHead = new RunHead(page);
            auto* const headLayout = static_cast<QHBoxLayout*>(runHead->layout());
            runRow = new RunRow(runHead);
            headLayout->addWidget(runRow, 1);
            cancel = new QPushButton(translated("Cancel"), runHead);
            cancel->setObjectName(QStringLiteral("pageCancel"));
            cancel->setToolTip(translated("Stop the run"));
            cancel->setProperty("anProcessButtonRole", QStringLiteral("secondary"));
            QObject::connect(cancel, &QAbstractButton::clicked, this, [this]() {
                if (hooks.cancelRun)
                {
                    hooks.cancelRun();
                }
            });
            headLayout->addWidget(cancel, 0, Qt::AlignTop);
            runHead->hide();
            pageLayout->addWidget(runHead);
        }
        // The area gives the process all the page's room, and scrolls only when the page is smaller than the least the
        // process takes.
        fullArea = new QScrollArea(page);
        fullArea->setObjectName(QStringLiteral("pageFullArea"));
        fullArea->setFrameShape(QFrame::NoFrame);
        fullArea->setWidgetResizable(true);
        fullArea->setWidget(contents);
        contents->show();
        pageLayout->addWidget(fullArea, 1);
        root->addWidget(page);
    }

    /// What a window with no preview laid out, moved whole, in order and with its stretches, into a widget of its own:
    /// the window's layout is left empty, and its margins become the controls' own, which are the body's padding. A
    /// widget keeps whether it was hidden. Nothing is deleted but the items that held the widgets.
    [[nodiscard]] QWidget* takeWindowContents(QLayout& root)
    {
        auto* contents = new QWidget(&processWidget);
        contents->setObjectName(QStringLiteral("pageControls"));
        auto* box = new QVBoxLayout(contents);
        box->setContentsMargins(root.contentsMargins());
        if (root.spacing() >= 0)
        {
            box->setSpacing(root.spacing());
        }
        auto* const rootBox = qobject_cast<QBoxLayout*>(&root);
        while (root.count() > 0)
        {
            const int stretch = rootBox != nullptr ? rootBox->stretch(0) : 0;
            QLayoutItem* const item = root.takeAt(0);
            if (QWidget* const widget = item->widget())
            {
                const Qt::Alignment alignment = item->alignment();
                delete item;
                box->addWidget(widget, stretch, alignment);
            }
            else if (QLayout* const layout = item->layout())
            {
                layout->setParent(nullptr);
                box->addLayout(layout, stretch);
            }
            else
            {
                box->addItem(item);
                box->setStretch(box->count() - 1, stretch);
            }
        }
        return contents;
    }

    /// The preview to the canvas and the controls into the body: a splitter's panes reparented, a row's two layouts
    /// moved into widgets of their own. Nothing is deleted.
    void arrangeCanvasAndControls(QVBoxLayout& canvasLayout)
    {
        auto* const mainBox = qobject_cast<QBoxLayout*>(standard.mainLayout);
        QSplitter* const splitter = findMainProcessSplitter(mainBox);
        if (splitter != nullptr && splitter->count() >= 2)
        {
            QWidget* const previewPane = splitter->widget(1);
            controls = splitter->widget(0);
            previewPane->setParent(canvas);
            previewPane->setMinimumWidth(0);
            previewPane->setMaximumWidth(QWIDGETSIZE_MAX);
            canvasLayout.addWidget(previewPane);
            previewPane->show();
            controls->setParent(bodyContent);
            controls->setMinimumWidth(0);
            controls->setMaximumWidth(QWIDGETSIZE_MAX);
            bodyLayout->addWidget(controls);
            controls->show();
            controlsLayout = controls->layout();
        }
        else if (mainBox != nullptr && standard.previewLayout != nullptr)
        {
            // A row: its first item is the controls' layout, the preview's layout another of its items.
            QLayout* rowControls = nullptr;
            for (int index = 0; index < mainBox->count(); ++index)
            {
                QLayout* const child = mainBox->itemAt(index)->layout();
                if (child != nullptr && child != standard.previewLayout)
                {
                    rowControls = child;
                    break;
                }
            }
            const int previewIndex = mainBox->indexOf(standard.previewLayout);
            if (previewIndex >= 0)
            {
                (void)mainBox->takeAt(previewIndex);
                standard.previewLayout->setParent(nullptr);
                canvasLayout.addLayout(standard.previewLayout);
            }
            controls = new QWidget(bodyContent);
            controls->setObjectName(QStringLiteral("pageControls"));
            auto* wrap = new QVBoxLayout(controls);
            const DesignTokens& t = tokens();
            wrap->setContentsMargins(t.metrics.pad, t.metrics.gap, t.metrics.pad, t.metrics.gap);
            if (rowControls != nullptr)
            {
                (void)mainBox->takeAt(mainBox->indexOf(rowControls));
                rowControls->setParent(nullptr);
                wrap->addLayout(rowControls);
            }
            bodyLayout->addWidget(controls);
            controlsLayout = wrap;
        }
        // The preview's own margins are the canvas's to give up: the image takes the whole canvas.
        if (standard.previewLayout != nullptr)
        {
            standard.previewLayout->setContentsMargins(0, 0, 0, 0);
        }
        if (QWidget* const previewPanel = findPreviewPanelWidget(standard.previewLayout))
        {
            previewPanel->setMinimumWidth(0);
            previewPanel->setMaximumWidth(QWIDGETSIZE_MAX);
            previewPanel->setVisible(true);
        }
        if (controlsLayout != nullptr)
        {
            controlsMargins = controlsLayout->contentsMargins();
        }
    }

    /// The controls keep the height they ask for at the body's top, and the room the body has to spare lies under them:
    /// given to the controls it would lie among their rows - between a label and what it labels - wherever a layout
    /// shares it out. A spacer a builder put among the rows to push them apart in a window taller than they need asks
    /// for no room in the body. Controls that scroll themselves take the room, the scroll area they scroll in growing
    /// with it: kept to the height it asks for, it would scroll them inside a body with room to spare.
    void keepTheRowsTogether()
    {
        if (controls != nullptr)
        {
            for (QLayout* const layout : controls->findChildren<QLayout*>())
            {
                for (int index = 0; index < layout->count(); ++index)
                {
                    QSpacerItem* const spacer = layout->itemAt(index)->spacerItem();
                    if (spacer == nullptr || (spacer->sizePolicy().verticalPolicy() & QSizePolicy::ExpandFlag) == 0 ||
                        spacer->sizeHint().height() == 0)
                    {
                        continue;
                    }
                    spacer->changeSize(spacer->sizeHint().width(), 0, spacer->sizePolicy().horizontalPolicy(),
                                       spacer->sizePolicy().verticalPolicy());
                    layout->invalidate();
                }
            }
        }
        if (controlsScrollThemselves())
        {
            bodyLayout->setStretchFactor(controls, 1);
            return;
        }
        bodyLayout->addStretch(1);
    }

    /// Whether the controls scroll themselves: their pane is a scroll area, or their layout stretches one of its own -
    /// what their window scrolls them in.
    [[nodiscard]] bool controlsScrollThemselves() const
    {
        if (controls == nullptr)
        {
            return false;
        }
        if (qobject_cast<QAbstractScrollArea*>(controls) != nullptr)
        {
            return true;
        }
        const auto* const box = qobject_cast<QBoxLayout*>(controlsLayout);
        if (box == nullptr)
        {
            return false;
        }
        for (int index = 0; index < box->count(); ++index)
        {
            if (box->stretch(index) > 0 && qobject_cast<QAbstractScrollArea*>(box->itemAt(index)->widget()) != nullptr)
            {
                return true;
            }
        }
        return false;
    }

    /// Target image and Channel at the body's head, side by side, each with its label.
    void leadWithSelectors()
    {
        QLayout* const searched = controlsLayout;
        // The labels in the first row, the fields in the second: two fields lie side by side whether each has a label.
        auto* row = new QGridLayout();
        const DesignTokens& t = tokens();
        row->setContentsMargins(controlsMargins.left(), t.metrics.gap, controlsMargins.right(), 0);
        row->setHorizontalSpacing(t.metrics.gap);
        row->setVerticalSpacing(parts::kFieldLabelGapAbove);
        int moved = 0;
        for (QComboBox* selector : {standard.targetSelector, standard.channelSelector})
        {
            if (selector == nullptr)
            {
                continue;
            }
            QLayout* const holder = holderOf(searched, selector);
            if (holder == nullptr)
            {
                continue;
            }
            QWidget* label = nullptr;
            if (auto* form = qobject_cast<QFormLayout*>(holder))
            {
                label = form->labelForField(selector);
                const QFormLayout::TakeRowResult taken = form->takeRow(selector);
                delete taken.labelItem;
                delete taken.fieldItem;
            }
            else if (auto* box = qobject_cast<QBoxLayout*>(holder))
            {
                const int index = box->indexOf(selector);
                if (index > 0 && qobject_cast<QLabel*>(box->itemAt(index - 1)->widget()) != nullptr)
                {
                    label = box->itemAt(index - 1)->widget();
                    box->removeWidget(label);
                }
                box->removeWidget(selector);
            }
            else if (auto* grid = qobject_cast<QGridLayout*>(holder))
            {
                // A row of a grid - the images a process reads, grouped: the label is the cell before the field's.
                int gridRow = 0;
                int gridColumn = 0;
                int rowSpan = 0;
                int columnSpan = 0;
                grid->getItemPosition(grid->indexOf(selector), &gridRow, &gridColumn, &rowSpan, &columnSpan);
                QLayoutItem* const before = gridColumn > 0 ? grid->itemAtPosition(gridRow, gridColumn - 1) : nullptr;
                if (before != nullptr && qobject_cast<QLabel*>(before->widget()) != nullptr)
                {
                    label = before->widget();
                    grid->removeWidget(label);
                }
                grid->removeWidget(selector);
            }
            if (label != nullptr)
            {
                row->addWidget(label, 0, moved);
                label->show();
            }
            row->addWidget(selector, 1, moved);
            row->setColumnStretch(moved, 1);
            ++moved;
        }
        if (moved == 0)
        {
            delete row;
            return;
        }
        bodyLayout->insertLayout(0, row);
    }

    // ---- Sections

    void makeSections()
    {
        std::vector<QGroupBox*> groups;
        collectGroups(controlsLayout, groups);
        if (groups.size() < 2)
        {
            return;
        }
        const QStringList closed = storedClosedSections();
        for (QGroupBox* group : groups)
        {
            const Slot slot = slotOf(controlsLayout, group);
            if (slot.layout == nullptr)
            {
                continue;
            }
            const int stretch = slot.layout->stretch(slot.index);
            const QString title = group->title();
            auto* section = new AnCollapsibleSection(title, group->parentWidget());
            section->setObjectName(QStringLiteral("pageSection"));
            slot.layout->removeWidget(group);
            slot.layout->insertWidget(slot.index, section, stretch);
            auto* inside = new QVBoxLayout();
            inside->setContentsMargins(0, 0, 0, 0);
            inside->addWidget(group);
            section->setContentLayout(inside);
            group->setProperty("anPageSectionTitle", title);
            group->setTitle(QString());
            section->setExpanded(!closed.contains(title));
            lastUse[section] = ++useClock;
            QObject::connect(section->toggleButton(), &QAbstractButton::clicked, this,
                             [this, section](const bool open) { sectionToggled(*section, open); });
            sections.push_back(section);
        }
    }

    [[nodiscard]] QStringList storedClosedSections() const
    {
        IAppContext* const context = appContext();
        IProcessSettingsProvider* const store = context != nullptr ? context->getProcessSettings() : nullptr;
        if (store == nullptr || description.settingsKey.isEmpty())
        {
            return {};
        }
        int state = 0;
        int width = 0;
        int height = 0;
        QString json;
        if (!store->select(description.settingsKey, state, width, height, json))
        {
            return {};
        }
        QStringList closed;
        for (const QJsonValue& value :
             QJsonDocument::fromJson(json.toUtf8()).object().value(QStringLiteral("closed")).toArray())
        {
            closed.append(value.toString());
        }
        return closed;
    }

    void keepSections() const
    {
        IAppContext* const context = appContext();
        IProcessSettingsProvider* const store = context != nullptr ? context->getProcessSettings() : nullptr;
        if (store == nullptr || description.settingsKey.isEmpty())
        {
            return;
        }
        QJsonArray closed;
        for (const AnCollapsibleSection* section : sections)
        {
            if (!section->isExpanded())
            {
                closed.append(section->title());
            }
        }
        QJsonObject kept;
        kept.insert(QStringLiteral("closed"), closed);
        (void)store->upsert(description.settingsKey, 0, -1, -1,
                            QString::fromUtf8(QJsonDocument(kept).toJson(QJsonDocument::Compact)));
    }

    /// How high a section's content is when it is open.
    [[nodiscard]] static int openHeight(const AnCollapsibleSection& section)
    {
        const QWidget* const content = section.contentWidget();
        const QLayout* const inside = content != nullptr ? content->layout() : nullptr;
        return inside != nullptr ? inside->sizeHint().height()
                                 : (content != nullptr ? content->sizeHint().height() : 0);
    }

    /// How high the body would be with every section at the height its state asks: the body's own hint, every section
    /// still moving taken at its end.
    [[nodiscard]] int bodyHeightOnceSettled() const
    {
        // A section that changed invalidates its own parent's layout at once and the ones above it - and the size each
        // widget's item in its parent's layout keeps - only when the event loop turns: the hints up to the body are
        // read again now.
        for (AnCollapsibleSection* section : sections)
        {
            for (QWidget* at = section; at != nullptr; at = at->parentWidget())
            {
                if (QLayout* const layout = at->layout())
                {
                    layout->invalidate();
                }
                at->updateGeometry();
                if (at == bodyContent)
                {
                    break;
                }
            }
        }
        int height = bodyContent->sizeHint().height();
        for (const AnCollapsibleSection* section : sections)
        {
            const QWidget* const content = section->contentWidget();
            const int target = section->isExpanded() ? openHeight(*section) : 0;
            const int hinted = content != nullptr && !content->isHidden()
                                   ? (std::min)(content->maximumHeight(), openHeight(*section))
                                   : 0;
            height += target - hinted;
        }
        return height;
    }

    void sectionToggled(AnCollapsibleSection& opened, const bool open)
    {
        lastUse[&opened] = ++useClock;
        if (open)
        {
            // A section that would push the body past the column's height closes the one used longest ago.
            const int room = body->viewport()->height();
            while (bodyHeightOnceSettled() > room)
            {
                AnCollapsibleSection* oldest = nullptr;
                for (AnCollapsibleSection* section : sections)
                {
                    if (section != &opened && section->isExpanded() &&
                        (oldest == nullptr || lastUse[section] < lastUse[oldest]))
                    {
                        oldest = section;
                    }
                }
                if (oldest == nullptr)
                {
                    break;
                }
                oldest->setExpanded(false);
            }
        }
        keepSections();
    }

    void focusMoved(QWidget* now)
    {
        if (now == nullptr || !bodyContent->isAncestorOf(now))
        {
            return;
        }
        for (AnCollapsibleSection* section : sections)
        {
            if (section->isAncestorOf(now))
            {
                lastUse[section] = ++useClock;
            }
        }
    }

    // ---- Toggles and footer

    void makeToggles()
    {
        auto* row = static_cast<QHBoxLayout*>(toggles->layout());
        if (standard.previewCheckbox != nullptr)
        {
            standard.previewCheckbox->setParent(toggles);
            row->addWidget(standard.previewCheckbox);
            standard.previewCheckbox->show();
        }
        previewArea = new QCheckBox(translated("Preview area"), toggles);
        previewArea->setObjectName(QStringLiteral("pagePreviewArea"));
        previewArea->hide();
        row->addWidget(previewArea);
        row->addStretch(1);
        compare = new QPushButton(compareShown, toggles);
        compare->setObjectName(QStringLiteral("pageCompare"));
        compare->setCheckable(true);
        compare->setToolTip(translated("Compare with the original (Space)"));
        compare->hide();
        row->addWidget(compare);
        toggles->setVisible(description.hasPreview);
    }

    void makeFooter()
    {
        auto* row = static_cast<QHBoxLayout*>(footer->layout());
        QDialogButtonBox* const box = standard.buttonBox;
        std::vector<std::pair<int, QAbstractButton*>> ranked;
        if (box != nullptr)
        {
            box->hide();
            for (QAbstractButton* button : box->buttons())
            {
                ranked.emplace_back(footerRank(box->buttonRole(button)), button);
            }
            std::stable_sort(ranked.begin(), ranked.end(),
                             [](const auto& left, const auto& right) { return left.first < right.first; });
        }
        bool stretched = false;
        for (const auto& [rank, button] : ranked)
        {
            if (rank > 0 && !stretched)
            {
                row->addStretch(1);
                stretched = true;
            }
            auto* mirror = new QPushButton(button->text(), footer);
            // The box's Close, or the Cancel that stands in its place, closes the task on a page: a window's tooltip
            // speaks of a dialog, and the window keeps it.
            const bool closes = box->buttonRole(button) == QDialogButtonBox::RejectRole;
            mirror->setToolTip(closes ? translated("Close this task without applying any changes.")
                                      : button->toolTip());
            mirror->setIcon(button->icon());
            mirror->setProperty("anProcessButtonRole", QString::fromLatin1(footerRole(box->buttonRole(button))));
            mirror->setEnabled(button->isEnabled());
            mirror->setVisible(!button->isHidden());
            QObject::connect(mirror, &QAbstractButton::clicked, button, [button]() { button->click(); });
            button->installEventFilter(this);
            row->addWidget(mirror);
            footerButtons.emplace_back(button, mirror);
            if (rank == 3 && primary == nullptr)
            {
                primary = mirror;
            }
            if (box->standardButton(button) == QDialogButtonBox::Cancel)
            {
                boxCancel = mirror;
            }
        }
        if (!stretched)
        {
            row->addStretch(1);
        }
        cancel = new QPushButton(translated("Cancel"), footer);
        cancel->setObjectName(QStringLiteral("pageCancel"));
        cancel->setToolTip(translated("Stop the run: the image keeps the state it had before Apply"));
        cancel->setProperty("anProcessButtonRole", QStringLiteral("secondary"));
        cancel->hide();
        QObject::connect(cancel, &QAbstractButton::clicked, this, [this]() {
            if (hooks.cancelRun)
            {
                hooks.cancelRun();
            }
        });
        row->addWidget(cancel);
    }

    /// Whether a footer button is hidden during a run, the run's Cancel shown in its stead: the primary button, and the
    /// box's own Cancel.
    [[nodiscard]] bool givesWayDuringRun(const QPushButton* mirror) const noexcept
    {
        return mirror == primary || mirror == boxCancel;
    }

    void mirrorButton(QAbstractButton& button)
    {
        for (auto& [source, mirror] : footerButtons)
        {
            if (source == &button)
            {
                mirror->setEnabled(button.isEnabled() && !(run.running() && mirror != primary));
                if (!givesWayDuringRun(mirror) || !run.running())
                {
                    mirror->setVisible(!button.isHidden());
                }
                mirror->setText(button.text());
            }
        }
    }

    // ---- The preview's toggles

    void bindPreview(ImageViewControl* view)
    {
        for (const QMetaObject::Connection& connection : previewConnections)
        {
            QObject::disconnect(connection);
        }
        previewConnections.clear();
        preview = view;
        const ViewChromeActions* actions = view != nullptr ? &view->chromeActions() : nullptr;
        auto* area = actions != nullptr ? qobject_cast<QCheckBox*>(actions->previewArea) : nullptr;
        previewArea->setVisible(area != nullptr);
        if (area != nullptr)
        {
            {
                const QSignalBlocker blocker(previewArea);
                previewArea->setChecked(area->isChecked());
            }
            previewArea->setEnabled(area->isEnabled());
            previewConnections.push_back(
                QObject::connect(previewArea, &QAbstractButton::clicked, area, [area](const bool checked) {
                    if (area->isChecked() != checked)
                    {
                        area->click();
                    }
                }));
            previewConnections.push_back(
                QObject::connect(area, &QAbstractButton::toggled, previewArea, [this](const bool checked) {
                    const QSignalBlocker blocker(previewArea);
                    previewArea->setChecked(checked);
                }));
            area->installEventFilter(this);
        }
        QAction* const switchImage = actions != nullptr ? actions->switchImage : nullptr;
        compare->setVisible(switchImage != nullptr);
        if (switchImage != nullptr)
        {
            previewConnections.push_back(QObject::connect(compare, &QAbstractButton::clicked, switchImage, [this]() {
                if (preview != nullptr && preview->chromeActions().switchImage != nullptr)
                {
                    preview->chromeActions().switchImage->trigger();
                }
                refreshCompare();
            }));
            previewConnections.push_back(
                QObject::connect(switchImage, &QAction::changed, compare, [this]() { refreshCompare(); }));
        }
        refreshCompare();
    }

    void refreshCompare()
    {
        const bool original = preview != nullptr && preview->getViewWidget() != nullptr &&
                              preview->getViewWidget()->isAlternateImageRenderEnabled();
        const QSignalBlocker blocker(compare);
        compare->setChecked(original);
        compare->setText(original ? compareAlternate : compareShown);
    }

    // ---- Fitting the column

    void columnResized()
    {
        if (column == nullptr)
        {
            return;
        }
        const int windowWidth = page->window() != nullptr ? page->window()->width() : 0;
        const int wanted = pageColumnWidth(description.columnWidth, windowWidth);
        if (column->width() != wanted || column->minimumWidth() != wanted)
        {
            column->setFixedWidth(wanted);
        }
    }

    /// The controls' side margins give way when the controls would be wider than the body.
    void bodyResized()
    {
        if (controlsLayout == nullptr || fitting)
        {
            return;
        }
        fitting = true;
        const int available = body->viewport()->width();
        const QMargins current = controlsLayout->contentsMargins();
        const int least = controlsLayout->minimumSize().width() - current.left() - current.right();
        const int room = (std::max)(0, (available - least) / 2);
        const int side = (std::min)(room, (std::min)(controlsMargins.left(), controlsMargins.right()));
        if (current.left() != side || current.right() != side)
        {
            controlsLayout->setContentsMargins(side, current.top(), side, current.bottom());
        }
        // A combo box the body gives less than its longest item - a narrow column's - elides its chosen one, its list
        // showing each whole, and takes its row's rest.
        if (available > 0 && body->widget() != nullptr)
        {
            bool elided = false;
            for (QComboBox* const combo : body->widget()->findChildren<QComboBox*>())
            {
                const QComboBox::SizeAdjustPolicy policy = combo->sizeAdjustPolicy();
                const bool squeezed =
                    combo->isVisibleTo(body) && combo->width() > 0 && combo->width() < combo->sizeHint().width();
                if ((policy == QComboBox::AdjustToContents || policy == QComboBox::AdjustToContentsOnFirstShow) &&
                    (squeezed || (least > available && combo->sizeHint().width() > available * 2 / 3)))
                {
                    combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
                    combo->setMinimumContentsLength(parts::kPageComboLeastCharacters);
                    QSizePolicy sizing = combo->sizePolicy();
                    sizing.setHorizontalPolicy(QSizePolicy::Expanding);
                    combo->setSizePolicy(sizing);
                    elided = true;
                }
            }
            // A spin box stated wider than it needs - a process's builder states its least width in pixels; its own is
            // its widest value's - gives way to what it needs where the controls are wider than the body even without
            // their side margins.
            if (least > available)
            {
                for (QAbstractSpinBox* const field : body->widget()->findChildren<QAbstractSpinBox*>())
                {
                    const int needs = field->minimumSizeHint().width();
                    if (field->isVisibleTo(body) && needs > 0 && field->minimumWidth() > needs)
                    {
                        field->setMinimumWidth(needs);
                        elided = true;
                    }
                }
            }
            if (elided)
            {
                queueBodyFit();
            }
        }
        fitting = false;
    }

    /// Fits the margins once the layout in progress is done. The controls' least width changes with no change of the
    /// body - a style, a font or a text that settles after the first fit - and the body then has no resize to say so.
    void queueBodyFit()
    {
        if (fitQueued)
        {
            return;
        }
        fitQueued = true;
        QMetaObject::invokeMethod(
            this,
            [this]() {
                fitQueued = false;
                bodyResized();
            },
            Qt::QueuedConnection);
    }

    bool eventFilter(QObject* watched, QEvent* event) override
    {
        const QEvent::Type type = event->type();
        if (watched == page && (type == QEvent::Resize || type == QEvent::ParentChange || type == QEvent::Show))
        {
            columnResized();
        }
        else if (body != nullptr && watched == body->viewport() && type == QEvent::Resize)
        {
            bodyResized();
        }
        else if (type == QEvent::LayoutRequest && controls != nullptr && watched == controls)
        {
            queueBodyFit();
        }
        else if (type == QEvent::EnabledChange || type == QEvent::ShowToParent || type == QEvent::HideToParent)
        {
            if (auto* const button = qobject_cast<QAbstractButton*>(watched))
            {
                if (preview != nullptr && preview->chromeActions().previewArea == button)
                {
                    previewArea->setEnabled(button->isEnabled() && !run.running());
                }
                else
                {
                    mirrorButton(*button);
                }
            }
        }
        return QObject::eventFilter(watched, event);
    }

    // ---- The run

    /// Whether the banner shows the process's message now: there is one, and the run's row does not take its place. A
    /// page with no row of its own for a run - a full page whose process shows its run in itself - says the message
    /// during a run too.
    [[nodiscard]] bool bannerShown() const
    {
        return !message.isEmpty() && !(run.running() && runRow != nullptr);
    }

    void applyRun(const ProcessRunState& state)
    {
        run = state;
        const bool running = state.running();
        if (column == nullptr)
        {
            // A full page shows its run at its head, in its message's place; one whose process shows its run in itself
            // shows none. What the process laid out stays as the process leaves it.
            if (runHead != nullptr)
            {
                runHead->setVisible(running);
                if (running)
                {
                    runRow->setRun(state.text.isEmpty() ? applyingRunText() : state.text, state.progress,
                                   state.sequence, state.frameProgress);
                }
                cancel->setEnabled(running);
            }
            banner->setVisible(bannerShown());
            return;
        }
        runRow->setVisible(running);
        if (running)
        {
            runRow->setRun(state.text.isEmpty() ? applyingRunText() : state.text, state.progress, state.sequence,
                           state.frameProgress);
        }
        banner->setVisible(bannerShown());
        toggles->setEnabled(!running);
        bodyContent->setEnabled(!running);
        header->close->setEnabled(!running);
        for (auto& [source, mirror] : footerButtons)
        {
            if (source.isNull())
            {
                continue;
            }
            if (mirror == primary)
            {
                mirror->setVisible(!running && !source->isHidden());
                mirror->setEnabled(source->isEnabled());
            }
            else
            {
                if (mirror == boxCancel)
                {
                    mirror->setVisible(!running && !source->isHidden());
                }
                mirror->setEnabled(!running && source->isEnabled());
            }
        }
        cancel->setVisible(running);
        cancel->setEnabled(running);
    }
};

ProcessPageLayout::ProcessPageLayout(QWidget& process, const ProcessStandardParts& parts, Description description)
    : m_impl(std::make_unique<Impl>(process, parts, std::move(description)))
{
    m_impl->build();
    // Whatever the process's builder did, the page's controls are reached by Tab and named for assistive technology.
    prepareForKeyboard(*m_impl->page);
}

ProcessPageLayout::~ProcessPageLayout() = default;

QWidget* ProcessPageLayout::page() const noexcept
{
    return m_impl->page;
}

QWidget* ProcessPageLayout::canvas() const noexcept
{
    return m_impl->canvas;
}

QWidget* ProcessPageLayout::canvasFoot() const noexcept
{
    return m_impl->canvasFoot;
}

QWidget* ProcessPageLayout::column() const noexcept
{
    return m_impl->column;
}

QWidget* ProcessPageLayout::readoutSlot() const noexcept
{
    return m_impl->readoutSlot;
}

QWidget* ProcessPageLayout::header() const noexcept
{
    return m_impl->header;
}

QWidget* ProcessPageLayout::toggles() const noexcept
{
    return m_impl->toggles;
}

AnInfoBanner* ProcessPageLayout::banner() const noexcept
{
    return m_impl->banner;
}

int ProcessPageLayout::runBars() const
{
    const Impl& impl = *m_impl;
    if (impl.runRow == nullptr || !impl.run.running())
    {
        return 0;
    }
    return impl.runRow->bars();
}

QWidget* ProcessPageLayout::runBanner() const noexcept
{
    return m_impl->runRow;
}

QScrollArea* ProcessPageLayout::body() const noexcept
{
    return m_impl->body;
}

QWidget* ProcessPageLayout::bodyContent() const noexcept
{
    return m_impl->bodyContent;
}

QWidget* ProcessPageLayout::footer() const noexcept
{
    return m_impl->footer;
}

const std::vector<AnCollapsibleSection*>& ProcessPageLayout::sections() const noexcept
{
    return m_impl->sections;
}

const ProcessStandardParts& ProcessPageLayout::parts() const noexcept
{
    return m_impl->standard;
}

QScrollArea* ProcessPageLayout::fullArea() const noexcept
{
    return m_impl->fullArea;
}

QList<QMenu*> ProcessPageLayout::menus() const
{
    QList<QMenu*> menus;
    if (m_impl->menuBar.isNull())
    {
        return menus;
    }
    for (QAction* const action : m_impl->menuBar->actions())
    {
        if (QMenu* const menu = action->menu())
        {
            menus.append(menu);
        }
    }
    return menus;
}

void ProcessPageLayout::setJobHeading(const JobHeading& job)
{
    if (m_impl->job.has_value() && *m_impl->job == job)
    {
        return;
    }
    m_impl->job = job;
    if (m_impl->jobListener)
    {
        m_impl->jobListener();
    }
}

const JobHeading* ProcessPageLayout::jobHeading() const noexcept
{
    return m_impl->job.has_value() ? &*m_impl->job : nullptr;
}

void ProcessPageLayout::setJobHeadingListener(std::function<void()> listener)
{
    m_impl->jobListener = std::move(listener);
}

QAbstractButton* ProcessPageLayout::footerButton(const QDialogButtonBox::StandardButton which) const
{
    QDialogButtonBox* const box = m_impl->standard.buttonBox;
    QAbstractButton* const source = box != nullptr ? box->button(which) : nullptr;
    for (const auto& [held, mirror] : m_impl->footerButtons)
    {
        if (held == source && source != nullptr)
        {
            return mirror;
        }
    }
    return nullptr;
}

QAbstractButton* ProcessPageLayout::cancelButton() const noexcept
{
    return m_impl->cancel;
}

QAbstractButton* ProcessPageLayout::headerCloseButton() const noexcept
{
    return m_impl->header != nullptr ? m_impl->header->close : nullptr;
}

QAbstractButton* ProcessPageLayout::previewAreaToggle() const noexcept
{
    return m_impl->previewArea;
}

QAbstractButton* ProcessPageLayout::compareToggle() const noexcept
{
    return m_impl->compare;
}

int ProcessPageLayout::columnWidth() const
{
    return m_impl->column != nullptr ? m_impl->column->width() : 0;
}

void ProcessPageLayout::setTitle(const QString& title)
{
    if (m_impl->header == nullptr)
    {
        return;
    }
    m_impl->header->title->setText(title);
}

QString ProcessPageLayout::title() const
{
    return m_impl->header != nullptr ? m_impl->header->title->text() : QString();
}

void ProcessPageLayout::setDescription(const QString& description)
{
    if (m_impl->header == nullptr)
    {
        return;
    }
    m_impl->header->title->setToolTip(description);
}

QString ProcessPageLayout::description() const
{
    return m_impl->header != nullptr ? m_impl->header->title->toolTip() : QString();
}

void ProcessPageLayout::setCategory(const QString& category)
{
    if (m_impl->header == nullptr)
    {
        return;
    }
    m_impl->header->eyebrow->setText(category);
    m_impl->header->eyebrow->setVisible(!category.isEmpty());
}

QString ProcessPageLayout::category() const
{
    return m_impl->header != nullptr ? m_impl->header->eyebrow->text() : QString();
}

void ProcessPageLayout::setTag(const QString& text, const QString& toolTip)
{
    if (m_impl->header == nullptr)
    {
        return;
    }
    m_impl->header->tag->setText(text);
    m_impl->header->tag->setToolTip(toolTip);
    m_impl->header->tag->setAccessibleDescription(toolTip);
    m_impl->header->tag->setVisible(!text.isEmpty());
}

QString ProcessPageLayout::tag() const
{
    return m_impl->header != nullptr ? m_impl->header->tag->text() : QString();
}

QString ProcessPageLayout::tagToolTip() const
{
    return m_impl->header != nullptr ? m_impl->header->tag->toolTip() : QString();
}

void ProcessPageLayout::setCompareNames(const QString& shown, const QString& alternate, const QString& toolTip)
{
    m_impl->compareShown = shown;
    m_impl->compareAlternate = alternate;
    if (m_impl->compare == nullptr)
    {
        return;
    }
    m_impl->compare->setToolTip(toolTip);
    m_impl->refreshCompare();
}

void ProcessPageLayout::setPreviewView(ImageViewControl* preview)
{
    if (m_impl->canvas == nullptr)
    {
        return;
    }
    if (preview != nullptr)
    {
        // A canvas is narrower than a window's preview at the least window: the view takes a small preview's least.
        preview->setMinimumSize(parts::kPageCanvasViewLeastWidth, parts::kPageCanvasViewLeastHeight);
    }
    m_impl->bindPreview(preview);
    m_impl->canvas->update();
    if (m_impl->previewListener)
    {
        m_impl->previewListener(preview);
    }
}

void ProcessPageLayout::setCanvasView(ImageViewControl* view)
{
    if (m_impl->canvas == nullptr)
    {
        return;
    }
    QLayout* const canvasLayout = m_impl->canvas->layout();
    if (ImageViewControl* const shown = m_impl->preview.data(); shown != nullptr && shown != view)
    {
        canvasLayout->removeWidget(shown);
    }
    if (view != nullptr && canvasLayout->indexOf(view) < 0)
    {
        view->setParent(m_impl->canvas);
        canvasLayout->addWidget(view);
        view->show();
    }
    setPreviewView(view);
}

ImageViewControl* ProcessPageLayout::previewView() const
{
    return m_impl->preview.data();
}

void ProcessPageLayout::setPreviewViewListener(std::function<void(ImageViewControl*)> listener)
{
    m_impl->previewListener = std::move(listener);
}

void ProcessPageLayout::setMissingInputs(const QStringList& inputs)
{
    if (m_impl->canvas == nullptr || m_impl->canvas->missing == inputs)
    {
        return;
    }
    m_impl->canvas->missing = inputs;
    // What the canvas holds is hidden while images are still to choose, so the canvas says which.
    QLayout* const canvasLayout = m_impl->canvas->layout();
    for (int index = 0; index < canvasLayout->count(); ++index)
    {
        if (QWidget* const held = canvasLayout->itemAt(index)->widget())
        {
            held->setVisible(inputs.isEmpty());
        }
    }
    m_impl->canvas->update();
}

QString ProcessPageLayout::canvasMessage() const
{
    if (m_impl->canvas == nullptr || m_impl->canvas->showsAView())
    {
        return {};
    }
    return m_impl->canvas->message().join(QLatin1Char('\n'));
}

void ProcessPageLayout::showMessage(const QString& text, const PageMessageTone tone,
                                    const std::optional<double> progressPercent)
{
    Impl& impl = *m_impl;
    impl.message = text;
    if (impl.banner == nullptr)
    {
        return;
    }
    impl.banner->setActions({});
    if (text.isEmpty())
    {
        impl.banner->hide();
        return;
    }
    QString shown = text;
    if (progressPercent.has_value() && std::isfinite(*progressPercent))
    {
        shown = QStringLiteral("%1 (%2 %)").arg(text).arg(static_cast<int>(std::lround(*progressPercent)));
    }
    impl.banner->setMessage(shown);
    impl.banner->setTone(tone == PageMessageTone::Error     ? AnInfoBanner::Tone::Error
                         : tone == PageMessageTone::Warning ? AnInfoBanner::Tone::Warning
                                                            : AnInfoBanner::Tone::Info);
    impl.banner->setVisible(impl.bannerShown());
}

QString ProcessPageLayout::message() const
{
    return m_impl->message;
}

void ProcessPageLayout::setMessageActions(std::vector<ProcessMessageAction> actions)
{
    if (m_impl->banner == nullptr)
    {
        return;
    }
    std::vector<AnInfoBanner::Action> offered;
    offered.reserve(actions.size());
    for (ProcessMessageAction& action : actions)
    {
        offered.push_back(AnInfoBanner::Action{.text = action.text, .act = std::move(action.act)});
    }
    m_impl->banner->setActions(std::move(offered));
}

QStringList ProcessPageLayout::messageActions() const
{
    return m_impl->banner != nullptr ? m_impl->banner->actionTexts() : QStringList();
}

void ProcessPageLayout::setRunState(const ProcessRunState& state)
{
    m_impl->applyRun(state);
}

void ProcessPageLayout::setHooks(Hooks hooks)
{
    m_impl->hooks = std::move(hooks);
}

void ProcessPageLayout::windowResized()
{
    m_impl->columnResized();
}

} // namespace an::ui
