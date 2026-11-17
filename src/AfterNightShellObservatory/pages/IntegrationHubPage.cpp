// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"

#include "pages/IntegrationHubPage.h"

#include "components/AnTag.h"
#include "controls/HomeParts.h"
#include "controls/IAppContext.h"
#include "processes/ImageIntegration/ui_models/IntegrationJobsModel.h"
#include "theme/AnDesignTokens.h"
#include "theme/ComponentParts.h"

#include <QAbstractButton>
#include <QAccessible>
#include <QComboBox>
#include <QEnterEvent>
#include <QFocusEvent>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QLineEdit>
#include <QPainter>
#include <QPointer>
#include <QPushButton>
#include <QShowEvent>
#include <QVBoxLayout>
#include <verdigris/wobjectimpl.h>

#include <algorithm>
#include <utility>

W_OBJECT_IMPL(an::ui::observatory::IntegrationHubPage)

namespace an::ui::observatory
{

namespace
{

using an::ui::HomeText;
using ImageIntegration::IntegrationJob;
using ImageIntegration::IntegrationJobStatus;

QIcon providedIcon(const QString& name)
{
    if (an::ui::IAppContext* const context = an::ui::appContext())
    {
        if (const an::ui::IIconProviderPtr icons = context->getIconProvider())
        {
            return icons->getIcon(name);
        }
    }
    return {};
}

/// A card of the hub: the second surface inside a hairline, its corners rounded, as Home's cards are.
class HubCard final : public QWidget
{
public:
    explicit HubCard(QWidget* parent) : QWidget(parent) {}

protected:
    void paintEvent(QPaintEvent* /*event*/) override
    {
        const an::ui::DesignTokens& t = an::ui::tokens();
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setPen(QPen(t.colors.borderSubtle, 1.0));
        painter.setBrush(t.layers.surface2);
        painter.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), an::ui::parts::kHomeCardRadius,
                                an::ui::parts::kHomeCardRadius);
    }
};

/// The square a way's icon sits in: the accent's container with its icon in the container's text, or the fourth
/// surface with the secondary text for a way that is planned.
class WayIcon final : public QWidget
{
public:
    WayIcon(QString iconName, const bool planned, QWidget* parent)
        : QWidget(parent), m_name(std::move(iconName)), m_planned(planned)
    {
        setFixedSize(an::ui::parts::kHubWayIconBox, an::ui::parts::kHubWayIconBox);
        refreshIcon();
    }

    void refreshIcon()
    {
        m_icon = providedIcon(m_name);
        update();
    }

protected:
    void paintEvent(QPaintEvent* /*event*/) override
    {
        const an::ui::DesignTokens& t = an::ui::tokens();
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setPen(Qt::NoPen);
        painter.setBrush(m_planned ? t.layers.surface4 : t.colors.accentContainer);
        painter.drawRoundedRect(QRectF(rect()), t.radius.md, t.radius.md);
        const qreal side = an::ui::parts::kHubWayIcon;
        an::ui::parts::paintIcon(painter, m_icon, QRectF((width() - side) / 2.0, (height() - side) / 2.0, side, side),
                                 m_planned ? t.colors.textSecondary : t.colors.textOnAccentContainer);
    }

private:
    QString m_name;
    bool m_planned = false;
    QIcon m_icon;
};

/// A single step's card: its icon, its title over its line, and a chevron - a button that launches its process.
class StepCard final : public QAbstractButton
{
public:
    StepCard(QString iconName, const QString& title, const QString& line, QWidget* parent)
        : QAbstractButton(parent), m_name(std::move(iconName)), m_line(line)
    {
        setText(title);
        setAccessibleName(title);
        setAccessibleDescription(line);
        setToolTip(line);
        setFocusPolicy(Qt::StrongFocus);
        setCursor(Qt::PointingHandCursor);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        refreshIcon();
    }

    void refreshIcon()
    {
        m_icon = providedIcon(m_name);
        m_chevron = providedIcon(QStringLiteral("chevron_right"));
        update();
    }

    [[nodiscard]] QSize sizeHint() const override
    {
        const QFontMetrics title(HomeText::fontFor(HomeText::Role::Strong));
        const QFontMetrics line(HomeText::fontFor(HomeText::Role::Caption));
        const int height = 2 * an::ui::parts::kHubStepPaddingTop + title.height() + line.height();
        return {an::ui::parts::kHomeTileMinWidth, (std::max)(height, an::ui::parts::kHubStepIcon)};
    }

    [[nodiscard]] QSize minimumSizeHint() const override
    {
        return {an::ui::parts::kHubStepPaddingLeft + an::ui::parts::kHubStepIcon + an::ui::parts::kHubStepChevron +
                    an::ui::parts::kHubStepPaddingRight + 2 * an::ui::parts::kHubWayRowGap,
                sizeHint().height()};
    }

protected:
    void paintEvent(QPaintEvent* /*event*/) override
    {
        const an::ui::DesignTokens& t = an::ui::tokens();
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        const QRectF card = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        const qreal radius = an::ui::parts::kHomeCardRadius;
        painter.setPen(QPen(t.colors.borderSubtle, 1.0));
        painter.setBrush(t.layers.surface2);
        painter.drawRoundedRect(card, radius, radius);
        an::ui::parts::paintStateLayer(painter, card, radius, underMouse(), isDown());
        if (hasFocus() && m_keyboardFocus)
        {
            an::ui::parts::paintFocusRing(painter, QRectF(rect()), radius);
        }

        const qreal icon = an::ui::parts::kHubStepIcon;
        const qreal chevron = an::ui::parts::kHubStepChevron;
        const qreal left = an::ui::parts::kHubStepPaddingLeft;
        const qreal right = width() - an::ui::parts::kHubStepPaddingRight;
        an::ui::parts::paintIcon(painter, m_icon, QRectF(left, (height() - icon) / 2.0, icon, icon),
                                 t.colors.textSecondary);
        an::ui::parts::paintIcon(painter, m_chevron,
                                 QRectF(right - chevron, (height() - chevron) / 2.0, chevron, chevron),
                                 t.colors.textTertiary);
        const qreal textLeft = left + icon + an::ui::parts::kHubWayRowGap;
        const qreal textWidth = right - chevron - an::ui::parts::kHubWayRowGap - textLeft;
        const QFont titleFont = HomeText::fontFor(HomeText::Role::Strong);
        const QFont lineFont = HomeText::fontFor(HomeText::Role::Caption);
        const QFontMetricsF title(titleFont);
        const QFontMetricsF line(lineFont);
        const qreal top = (height() - title.height() - line.height()) / 2.0;
        painter.setFont(titleFont);
        painter.setPen(HomeText::colourFor(HomeText::Role::Strong));
        painter.drawText(QRectF(textLeft, top, textWidth, title.height()), Qt::AlignLeft | Qt::AlignVCenter,
                         title.elidedText(text(), Qt::ElideRight, textWidth));
        painter.setFont(lineFont);
        painter.setPen(t.colors.textSecondary);
        painter.drawText(QRectF(textLeft, top + title.height(), textWidth, line.height()),
                         Qt::AlignLeft | Qt::AlignVCenter, line.elidedText(m_line, Qt::ElideRight, textWidth));
    }

    void focusInEvent(QFocusEvent* event) override
    {
        m_keyboardFocus = an::ui::parts::isKeyboardFocusReason(event->reason());
        QAbstractButton::focusInEvent(event);
    }

    void enterEvent(QEnterEvent* event) override
    {
        QAbstractButton::enterEvent(event);
        update();
    }

    void leaveEvent(QEvent* event) override
    {
        QAbstractButton::leaveEvent(event);
        update();
    }

private:
    QString m_name;
    QString m_line;
    QIcon m_icon;
    QIcon m_chevron;
    bool m_keyboardFocus = false;
};

/// A running job's thin bar: the accent's track, and the accent over the part it has done.
class JobBar final : public QWidget
{
public:
    explicit JobBar(QWidget* parent) : QWidget(parent)
    {
        setFixedSize(an::ui::parts::kHubJobBarWidth, an::ui::parts::kHubJobBarHeight);
    }

    void setFraction(const double fraction)
    {
        m_fraction = std::clamp(fraction, 0.0, 1.0);
        update();
    }

protected:
    void paintEvent(QPaintEvent* /*event*/) override
    {
        const an::ui::DesignTokens& t = an::ui::tokens();
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setPen(Qt::NoPen);
        const qreal radius = height() / 2.0;
        painter.setBrush(t.colors.accentTrack);
        painter.drawRoundedRect(QRectF(rect()), radius, radius);
        painter.setBrush(t.colors.accentPrimary);
        painter.drawRoundedRect(QRectF(0, 0, width() * m_fraction, height()), radius, radius);
    }

private:
    double m_fraction = 0.0;
};

/// The tone a job's status is told in.
an::ui::AnTag::Tone toneOf(const IntegrationJobStatus status)
{
    switch (status)
    {
    case IntegrationJobStatus::Running:
        return an::ui::AnTag::Tone::Accent;
    case IntegrationJobStatus::Completed:
        return an::ui::AnTag::Tone::Success;
    case IntegrationJobStatus::CompletedWithWarnings:
    case IntegrationJobStatus::Partial:
        return an::ui::AnTag::Tone::Warning;
    case IntegrationJobStatus::Failed:
    case IntegrationJobStatus::Cancelled:
        return an::ui::AnTag::Tone::Error;
    case IntegrationJobStatus::NotRun:
    default:
        return an::ui::AnTag::Tone::Neutral;
    }
}

/// Lays a row of the jobs out in the table's columns: the job's own, which stretches, then the others at their
/// widths.
void addJobColumns(QHBoxLayout& row, QWidget* job, QWidget* project, QWidget* frames, QWidget* masters,
                   QWidget* lastRun, QWidget* took, QWidget* status, QWidget* button)
{
    row.setContentsMargins(0, 0, 0, 0);
    row.setSpacing(an::ui::parts::kHubJobColumnGap);
    row.addWidget(job, 1);
    const auto fixed = [&row](QWidget* cell, const int width) {
        cell->setFixedWidth(width);
        row.addWidget(cell, 0);
    };
    fixed(project, an::ui::parts::kHubJobProjectColumn);
    fixed(frames, an::ui::parts::kHubJobCountColumn);
    fixed(masters, an::ui::parts::kHubJobCountColumn);
    fixed(lastRun, an::ui::parts::kHubJobLastRunColumn);
    fixed(took, an::ui::parts::kHubJobTookColumn);
    fixed(status, an::ui::parts::kHubJobStatusColumn);
    fixed(button, an::ui::parts::kHubJobButtonColumn);
}

/// A row of the jobs' table, with a hairline over it.
class JobRow final : public QWidget
{
public:
    explicit JobRow(QWidget* parent) : QWidget(parent) {}

    [[nodiscard]] QSize sizeHint() const override
    {
        QSize size = QWidget::sizeHint();
        size.setHeight((std::max)(size.height(), an::ui::tokens().metrics.rowTable + an::ui::parts::kHubJobRowExtra));
        return size;
    }

protected:
    void paintEvent(QPaintEvent* /*event*/) override
    {
        QPainter painter(this);
        painter.setPen(QPen(an::ui::tokens().colors.borderSubtle, 1.0));
        painter.drawLine(QPointF(0, 0.5), QPointF(width(), 0.5));
    }
};

/// A section's head: its title and, 10 px after it, its caption or its count.
QHBoxLayout* sectionHead(QWidget* parent, const QString& title, HomeText* caption)
{
    auto* head = new QHBoxLayout();
    head->setContentsMargins(0, 0, 0, 0);
    head->setSpacing(an::ui::parts::kHubSectionCaptionGap);
    auto* text = new HomeText(HomeText::Role::Section, title, parent);
    text->setElideMode(Qt::ElideNone);
    head->addWidget(text, 0, Qt::AlignVCenter);
    if (caption != nullptr)
    {
        head->addWidget(caption, 0, Qt::AlignVCenter);
    }
    return head;
}

} // namespace

struct IntegrationHubPage::Impl
{
    explicit Impl(ImageIntegration::IntegrationJobsModel& model) : jobs(model) {}

    ImageIntegration::IntegrationJobsModel& jobs;

    QPushButton* guidedStart = nullptr;
    an::ui::AnTag* guidedTag = nullptr;
    QPushButton* newIntegration = nullptr;
    QPushButton* newQuickIntegration = nullptr;
    std::vector<WayIcon*> wayIcons;
    std::vector<StepCard*> steps;

    HomeText* jobsCount = nullptr;
    QLineEdit* search = nullptr;
    QComboBox* projects = nullptr;
    QWidget* table = nullptr;
    QVBoxLayout* rows = nullptr;
    HomeText* empty = nullptr;

    /// A shown row: its job, its button and its tag.
    struct ShownJob
    {
        IntegrationJob job;
        QPointer<QPushButton> button;
        QPointer<an::ui::AnTag> tag;
    };
    std::vector<ShownJob> shown;

    /// The choice of project the person made, which the list of projects is filled again around.
    QString chosenProject;
    bool fillingProjects = false;
};

IntegrationHubPage::IntegrationHubPage(ImageIntegration::IntegrationJobsModel& jobs, QWidget* parent)
    : QScrollArea(parent), m_impl(std::make_unique<Impl>(jobs))
{
    setObjectName(QStringLiteral("observatoryIntegrationHub"));
    setFrameShape(QFrame::NoFrame);
    setWidgetResizable(true);
    auto* content = new QWidget(this);
    content->setObjectName(QStringLiteral("observatoryIntegrationHubContent"));
    auto* column = new QVBoxLayout(content);
    column->setContentsMargins(an::ui::parts::kHomePagePaddingSide, an::ui::parts::kHomePagePaddingTop,
                               an::ui::parts::kHomePagePaddingSide, an::ui::parts::kHomePagePaddingTop);
    column->setSpacing(an::ui::parts::kHomeSectionGap);

    // The heading and its line.
    auto* titles = new QVBoxLayout();
    titles->setContentsMargins(0, 0, 0, 0);
    titles->setSpacing(0);
    auto* heading = new HomeText(HomeText::Role::Heading, tr("Integration"), content);
    heading->setObjectName(QStringLiteral("observatoryIntegrationHeading"));
    heading->setElideMode(Qt::ElideNone);
    titles->addWidget(heading);
    titles->addWidget(new HomeText(HomeText::Role::Secondary, tr("Calibrate, register and stack."), content));
    column->addLayout(titles);

    // Three ways to stack.
    auto* ways = new QVBoxLayout();
    ways->setContentsMargins(0, 0, 0, 0);
    ways->setSpacing(an::ui::parts::kHubWayRowGap);
    ways->addLayout(
        sectionHead(content, tr("Three ways to stack"),
                    new HomeText(HomeText::Role::Secondary,
                                 tr("The same engine behind each; they differ in how much you decide."), content)));
    auto* wayRow = new QGridLayout();
    wayRow->setContentsMargins(0, 0, 0, 0);
    wayRow->setHorizontalSpacing(an::ui::parts::kHubWayGap);
    struct Way
    {
        QString icon;
        QString title;
        QString line;
        QStringList stages;
        QString action;
        bool planned;
        bool primary;
    };
    const std::vector<Way> wayList = {
        {QStringLiteral("help"),
         tr("Guided integration"),
         tr("Answer a few questions; AfterNight sets up calibration, registration and stacking."),
         {tr("Calibration"), tr("Registration"), tr("Stacking")},
         tr("Start"),
         true,
         false},
        {QStringLiteral("layers"),
         tr("Image Integration"),
         tr("The full workflow: frame sources, calibration, registration, stacking and the summary, every setting in "
            "reach."),
         {tr("Frame sources"), tr("Calibration"), tr("Registration"), tr("Stacking"), tr("Summary")},
         tr("New integration"),
         false,
         true},
        {QStringLiteral("play"),
         tr("Image Integration Lite"),
         tr("Lights and optional masters on one page, sensible defaults, one button."),
         {tr("Lights"), tr("Optional masters"), tr("One page")},
         tr("New quick integration"),
         false,
         false},
    };
    for (std::size_t index = 0; index < wayList.size(); ++index)
    {
        const Way& way = wayList[index];
        auto* card = new HubCard(content);
        card->setObjectName(QStringLiteral("integrationWay%1").arg(index + 1));
        card->setMinimumHeight(an::ui::parts::kHubWayMinHeight);
        auto* body = new QVBoxLayout(card);
        body->setContentsMargins(an::ui::parts::kHubWayPaddingSide, an::ui::parts::kHubWayPaddingTop,
                                 an::ui::parts::kHubWayPaddingSide, an::ui::parts::kHubWayPaddingTop);
        body->setSpacing(an::ui::parts::kHubWayRowGap);
        auto* head = new QHBoxLayout();
        head->setContentsMargins(0, 0, 0, 0);
        head->setSpacing(an::ui::parts::kHubWayGap);
        auto* icon = new WayIcon(way.icon, way.planned, card);
        m_impl->wayIcons.push_back(icon);
        head->addWidget(icon, 0, Qt::AlignVCenter);
        auto* title = new HomeText(HomeText::Role::Section, way.title, card);
        head->addWidget(title, 1, Qt::AlignVCenter);
        if (way.planned)
        {
            m_impl->guidedTag = new an::ui::AnTag(tr("Planned"), an::ui::AnTag::Tone::Neutral, card);
            m_impl->guidedTag->setObjectName(QStringLiteral("integrationGuidedTag"));
            head->addWidget(m_impl->guidedTag, 0, Qt::AlignVCenter);
        }
        body->addLayout(head);
        auto* line = new HomeText(HomeText::Role::Body, way.line, card);
        line->setWordWrap(true);
        body->addWidget(line);
        auto* tags = new QHBoxLayout();
        tags->setContentsMargins(0, 0, 0, 0);
        tags->setSpacing(an::ui::parts::kHubWayTagGap);
        for (const QString& stage : way.stages)
        {
            tags->addWidget(new an::ui::AnTag(stage, an::ui::AnTag::Tone::Outlined, card));
        }
        tags->addStretch(1);
        body->addLayout(tags);
        body->addStretch(1);
        auto* foot = new QHBoxLayout();
        foot->setContentsMargins(0, 0, 0, 0);
        foot->setSpacing(an::ui::parts::kHubWayRowGap);
        auto* button = new QPushButton(way.action, card);
        button->setProperty("anButtonVariant", way.primary
                                                   ? QStringLiteral("primary")
                                                   : (way.planned ? QStringLiteral("quiet") : QStringLiteral("tonal")));
        foot->addWidget(button, 0);
        if (way.planned)
        {
            button->setEnabled(false);
            button->setObjectName(QStringLiteral("integrationGuidedStart"));
            foot->addWidget(new HomeText(HomeText::Role::Secondary, tr("Not in this version"), card), 0);
            m_impl->guidedStart = button;
        }
        else if (way.primary)
        {
            button->setObjectName(QStringLiteral("integrationNew"));
            m_impl->newIntegration = button;
            QObject::connect(button, &QPushButton::clicked, this,
                             [this]() { emit processRequested(QString::fromLatin1(kImageIntegration)); });
        }
        else
        {
            button->setObjectName(QStringLiteral("integrationNewQuick"));
            m_impl->newQuickIntegration = button;
            QObject::connect(button, &QPushButton::clicked, this,
                             [this]() { emit processRequested(QString::fromLatin1(kImageIntegrationLite)); });
        }
        foot->addStretch(1);
        body->addLayout(foot);
        wayRow->addWidget(card, 0, static_cast<int>(index));
        wayRow->setColumnStretch(static_cast<int>(index), 1);
    }
    ways->addLayout(wayRow);
    column->addLayout(ways);

    // The single steps.
    auto* steps = new QVBoxLayout();
    steps->setContentsMargins(0, 0, 0, 0);
    steps->setSpacing(an::ui::parts::kHubWayRowGap);
    steps->addLayout(sectionHead(content, tr("Single steps"),
                                 new HomeText(HomeText::Role::Secondary,
                                              tr("One stage on a list of frames, as a process of its own."), content)));
    auto* stepRow = new QGridLayout();
    stepRow->setContentsMargins(0, 0, 0, 0);
    stepRow->setHorizontalSpacing(an::ui::parts::kHubStepGap);
    struct Step
    {
        QString icon;
        QString title;
        QString line;
        const char* descriptor;
    };
    const std::vector<Step> stepList = {
        {QStringLiteral("exposure"), tr("Image Calibration"),
         tr("Apply bias, dark and flat masters to a list of frames"), kImageCalibration},
        {QStringLiteral("star"), tr("Image Registration"), tr("Detect stars and align a list of frames"),
         kImageRegistration},
        {QStringLiteral("stack"), tr("Image Stacking"), tr("Combine registered frames with rejection"), kImageStacking},
        {QStringLiteral("view_list"), tr("Image List Analyzer"), tr("Inspect, blink and export a list of frames"),
         kImageListAnalyzer},
    };
    for (std::size_t index = 0; index < stepList.size(); ++index)
    {
        const Step& step = stepList[index];
        auto* card = new StepCard(step.icon, step.title, step.line, content);
        card->setObjectName(QStringLiteral("integrationStep%1").arg(index + 1));
        const QString descriptor = QString::fromLatin1(step.descriptor);
        QObject::connect(card, &QAbstractButton::clicked, this,
                         [this, descriptor]() { emit processRequested(descriptor); });
        m_impl->steps.push_back(card);
        stepRow->addWidget(card, 0, static_cast<int>(index));
        stepRow->setColumnStretch(static_cast<int>(index), 1);
    }
    steps->addLayout(stepRow);
    column->addLayout(steps);

    // The integration jobs.
    auto* jobsSection = new QVBoxLayout();
    jobsSection->setContentsMargins(0, 0, 0, 0);
    jobsSection->setSpacing(an::ui::parts::kHubWayRowGap);
    m_impl->jobsCount = new HomeText(HomeText::Role::Mono, QString(), content);
    m_impl->jobsCount->setObjectName(QStringLiteral("integrationJobsCount"));
    m_impl->jobsCount->setElideMode(Qt::ElideNone);
    QHBoxLayout* jobsHead = sectionHead(content, tr("Integration jobs"), m_impl->jobsCount);
    jobsHead->addStretch(1);
    m_impl->search = new QLineEdit(content);
    m_impl->search->setObjectName(QStringLiteral("integrationJobSearch"));
    m_impl->search->setPlaceholderText(tr("Search jobs by name or project..."));
    m_impl->search->setClearButtonEnabled(true);
    m_impl->search->setFixedWidth(an::ui::parts::kHubSearchWidth);
    m_impl->search->addAction(providedIcon(QStringLiteral("search")), QLineEdit::LeadingPosition);
    jobsHead->addWidget(m_impl->search, 0, Qt::AlignVCenter);
    jobsHead->addWidget(new HomeText(HomeText::Role::Secondary, tr("Show"), content), 0, Qt::AlignVCenter);
    m_impl->projects = new QComboBox(content);
    m_impl->projects->setObjectName(QStringLiteral("integrationJobProjects"));
    m_impl->projects->setMinimumWidth(an::ui::parts::kHubProjectChoiceWidth);
    m_impl->projects->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    jobsHead->addWidget(m_impl->projects, 0, Qt::AlignVCenter);
    jobsSection->addLayout(jobsHead);

    m_impl->table = new QWidget(content);
    m_impl->table->setObjectName(QStringLiteral("integrationJobsTable"));
    auto* tableLayout = new QVBoxLayout(m_impl->table);
    tableLayout->setContentsMargins(0, 0, 0, 0);
    tableLayout->setSpacing(0);
    auto* header = new QWidget(m_impl->table);
    auto* headerRow = new QHBoxLayout(header);
    const auto caps = [header](const QString& text) {
        auto* label = new HomeText(HomeText::Role::Caps, text, header);
        label->setElideMode(Qt::ElideNone);
        return label;
    };
    addJobColumns(*headerRow, caps(tr("Job")), caps(tr("Project")), caps(tr("Frames")), caps(tr("Masters")),
                  caps(tr("Last run")), caps(tr("Took")), caps(tr("Status")), new QWidget(header));
    tableLayout->addWidget(header);
    m_impl->rows = new QVBoxLayout();
    m_impl->rows->setContentsMargins(0, 0, 0, 0);
    m_impl->rows->setSpacing(0);
    tableLayout->addLayout(m_impl->rows);
    m_impl->empty = new HomeText(HomeText::Role::Secondary,
                                 tr("No integration job yet: a session of Image Integration is one."), m_impl->table);
    m_impl->empty->setObjectName(QStringLiteral("integrationJobsEmpty"));
    tableLayout->addWidget(m_impl->empty);
    jobsSection->addWidget(m_impl->table);
    column->addLayout(jobsSection);
    column->addStretch(1);
    setWidget(content);

    QObject::connect(&m_impl->jobs, &ImageIntegration::IntegrationJobsModel::jobsChanged, this, [this]() {
        refreshProjectChoices();
        rebuildJobs();
    });
    QObject::connect(m_impl->search, &QLineEdit::textChanged, this, [this]() { rebuildJobs(); });
    QObject::connect(m_impl->projects, &QComboBox::currentIndexChanged, this, [this]() {
        if (!m_impl->fillingProjects)
        {
            m_impl->chosenProject = m_impl->projects->currentData().toString();
            rebuildJobs();
        }
    });
    refreshProjectChoices();
    rebuildJobs();
}

IntegrationHubPage::~IntegrationHubPage() = default;

void IntegrationHubPage::refreshIcons()
{
    for (WayIcon* icon : m_impl->wayIcons)
    {
        icon->refreshIcon();
    }
    for (StepCard* step : m_impl->steps)
    {
        step->refreshIcon();
    }
    const QList<QAction*> actions = m_impl->search->actions();
    if (!actions.isEmpty())
    {
        actions.front()->setIcon(providedIcon(QStringLiteral("search")));
    }
}

QAbstractButton* IntegrationHubPage::guidedStart() const
{
    return m_impl->guidedStart;
}

QAbstractButton* IntegrationHubPage::newIntegration() const
{
    return m_impl->newIntegration;
}

QAbstractButton* IntegrationHubPage::newQuickIntegration() const
{
    return m_impl->newQuickIntegration;
}

an::ui::AnTag* IntegrationHubPage::guidedTag() const
{
    return m_impl->guidedTag;
}

std::vector<QAbstractButton*> IntegrationHubPage::steps() const
{
    return {m_impl->steps.begin(), m_impl->steps.end()};
}

QLineEdit* IntegrationHubPage::jobSearch() const
{
    return m_impl->search;
}

QComboBox* IntegrationHubPage::jobProjects() const
{
    return m_impl->projects;
}

QString IntegrationHubPage::jobsCountText() const
{
    return m_impl->jobsCount->text();
}

QStringList IntegrationHubPage::shownJobs() const
{
    QStringList names;
    for (const Impl::ShownJob& shown : m_impl->shown)
    {
        names.append(shown.job.name);
    }
    return names;
}

QAbstractButton* IntegrationHubPage::jobButton(const QString& databasePath, const QString& session) const
{
    for (const Impl::ShownJob& shown : m_impl->shown)
    {
        if (shown.job.name == session && ImageIntegration::sameSessionDatabase(shown.job.databasePath, databasePath))
        {
            return shown.button.data();
        }
    }
    return nullptr;
}

QString IntegrationHubPage::jobStatusText(const QString& databasePath, const QString& session) const
{
    for (const Impl::ShownJob& shown : m_impl->shown)
    {
        if (shown.job.name == session && ImageIntegration::sameSessionDatabase(shown.job.databasePath, databasePath))
        {
            return shown.tag.isNull() ? QString() : shown.tag->text();
        }
    }
    return {};
}

void IntegrationHubPage::showEvent(QShowEvent* event)
{
    QScrollArea::showEvent(event);
    // The jobs' records change while the page is not shown - a run ends in another window, a session is saved: they
    // are read again as the page is shown, on the thread pool.
    m_impl->jobs.refresh();
}

void IntegrationHubPage::refreshProjectChoices()
{
    QStringList named;
    for (const IntegrationJob& job : m_impl->jobs.jobs())
    {
        if (!job.project.isEmpty() && !named.contains(job.project))
        {
            named.append(job.project);
        }
    }
    named.sort(Qt::CaseInsensitive);
    m_impl->fillingProjects = true;
    m_impl->projects->clear();
    // The data of each choice: empty for all, a single space for none, else the project's name.
    m_impl->projects->addItem(tr("All projects"), QString());
    m_impl->projects->addItem(tr("No project"), QStringLiteral(" "));
    for (const QString& project : named)
    {
        m_impl->projects->addItem(project, project);
    }
    int chosen = m_impl->projects->findData(m_impl->chosenProject);
    if (chosen < 0)
    {
        m_impl->chosenProject.clear();
        chosen = 0;
    }
    m_impl->projects->setCurrentIndex(chosen);
    m_impl->fillingProjects = false;
}

void IntegrationHubPage::rebuildJobs()
{
    Impl& impl = *m_impl;
    // The rows are made again whole: a handful of jobs, read when they change.
    impl.shown.clear();
    while (QLayoutItem* item = impl.rows->takeAt(0))
    {
        if (QWidget* const row = item->widget())
        {
            row->hide();
            row->deleteLater();
        }
        delete item;
    }

    const QString search = impl.search->text().trimmed();
    const QString project = impl.chosenProject;
    int running = 0;
    for (const IntegrationJob& job : impl.jobs.jobs())
    {
        running += job.status == IntegrationJobStatus::Running ? 1 : 0;
        if (!project.isEmpty() && (project == QLatin1String(" ") ? !job.project.isEmpty() : job.project != project))
        {
            continue;
        }
        if (!search.isEmpty() && !job.name.contains(search, Qt::CaseInsensitive) &&
            !job.project.contains(search, Qt::CaseInsensitive))
        {
            continue;
        }
        auto* row = new JobRow(impl.table);
        row->setObjectName(QStringLiteral("integrationJobRow"));
        auto* line = new QHBoxLayout(row);
        auto* name = new HomeText(HomeText::Role::Strong, job.name, row);
        name->setToolTip(job.name);
        auto* projectText = new HomeText(job.project.isEmpty() ? HomeText::Role::Caption : HomeText::Role::Body,
                                         job.project.isEmpty() ? tr("No project") : job.project, row);
        const auto mono = [row](const QString& text, const Qt::Alignment alignment) {
            auto* label = new HomeText(HomeText::Role::Mono, text, row);
            label->setAlignment(alignment | Qt::AlignVCenter);
            return label;
        };
        const bool unknown = job.unreadable;
        auto* frames = mono(unknown ? QStringLiteral("—") : QString::number(job.frames), Qt::AlignRight);
        auto* masters = mono(unknown || job.status == IntegrationJobStatus::NotRun ? QStringLiteral("—")
                                                                                   : QString::number(job.masters),
                             Qt::AlignRight);
        auto* lastRun = mono(ImageIntegration::integrationJobLastRunText(job.lastRun), Qt::AlignLeft);
        auto* took = mono(ImageIntegration::integrationJobDurationText(job.durationMs), Qt::AlignLeft);
        auto* status = new QWidget(row);
        auto* statusLine = new QHBoxLayout(status);
        statusLine->setContentsMargins(0, 0, 0, 0);
        statusLine->setSpacing(an::ui::parts::kHubJobColumnGap / 2);
        auto* tag = new an::ui::AnTag(ImageIntegration::integrationJobStatusText(job), toneOf(job.status), status);
        tag->setObjectName(QStringLiteral("integrationJobStatus"));
        statusLine->addWidget(tag, 0, Qt::AlignVCenter);
        if (job.status == IntegrationJobStatus::Running && job.progress.has_value())
        {
            auto* bar = new JobBar(status);
            bar->setFraction(*job.progress);
            statusLine->addWidget(bar, 0, Qt::AlignVCenter);
        }
        statusLine->addStretch(1);
        const bool runs = job.status == IntegrationJobStatus::Running;
        auto* button = new QPushButton(runs ? tr("Show") : tr("Open"), row);
        button->setObjectName(QStringLiteral("integrationJobOpen"));
        button->setProperty("anButtonVariant", QStringLiteral("quiet"));
        button->setEnabled(!unknown || runs);
        const QString databasePath = job.databasePath;
        const QString session = job.name;
        QObject::connect(button, &QPushButton::clicked, this, [this, databasePath, session, runs]() {
            if (runs)
            {
                emit showJobRequested(databasePath, session);
            }
            else
            {
                emit openJobRequested(databasePath, session);
            }
        });
        addJobColumns(*line, name, projectText, frames, masters, lastRun, took, status, button);
        impl.rows->addWidget(row);
        impl.shown.push_back(Impl::ShownJob{job, button, tag});
    }
    const int count = static_cast<int>(impl.jobs.jobs().size());
    impl.jobsCount->setText(running > 0 ? tr("%1 · %2 running").arg(count).arg(running) : QString::number(count));
    impl.empty->setVisible(impl.shown.empty());
    impl.empty->setText(count == 0 ? tr("No integration job yet: a session of Image Integration is one.")
                                   : tr("No job is named so, or is of that project."));
}

} // namespace an::ui::observatory
