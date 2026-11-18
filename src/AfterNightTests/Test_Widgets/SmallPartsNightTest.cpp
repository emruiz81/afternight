// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Under Night everything on screen is red: every small part, in every state that changes its colours and with an icon
// of another colour handed to it, paints no pixel with green or blue - its colours are the tokens', which Night maps,
// and an icon is drawn in the part's text colour.

#include "RenderTestSupport.h"

#include "components/AnBreadcrumb.h"
#include "components/AnChip.h"
#include "components/AnKeyHint.h"
#include "components/AnLabeledField.h"
#include "components/AnSegmentedControl.h"
#include "components/AnSnackbar.h"
#include "components/AnStageList.h"
#include "components/AnSwitch.h"
#include "components/AnTag.h"

#include <QIcon>
#include <QImage>
#include <QKeySequence>
#include <QLineEdit>
#include <QPixmap>
#include <QStringList>

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace
{

using an::tests::widgets::RenderedPicture;
using an::tests::widgets::showOffScreen;

/// An icon of a colour Night has no place for.
QIcon greenIcon()
{
    QPixmap pixmap(16, 16);
    pixmap.fill(QColor(0, 200, 0));
    return QIcon(pixmap);
}

/// The first pixel of a picture that keeps green or blue, described; empty when there is none.
std::string firstHue(const RenderedPicture& picture)
{
    const QImage& image = picture.image();
    for (int y = 0; y < image.height(); ++y)
    {
        for (int x = 0; x < image.width(); ++x)
        {
            const QColor pixel = image.pixelColor(x, y);
            if (pixel.green() != 0 || pixel.blue() != 0)
            {
                return "(" + std::to_string(x) + ", " + std::to_string(y) + ") is " +
                       an::ui::testing::colourName(pixel);
            }
        }
    }
    return {};
}

/// Every part, in the states that change its colours.
std::vector<std::unique_ptr<QWidget>> everyPart()
{
    std::vector<std::unique_ptr<QWidget>> parts;
    for (const an::ui::AnTag::Tone tone :
         {an::ui::AnTag::Tone::Neutral, an::ui::AnTag::Tone::Accent, an::ui::AnTag::Tone::Success,
          an::ui::AnTag::Tone::Warning, an::ui::AnTag::Tone::Error, an::ui::AnTag::Tone::Info})
    {
        auto tag = std::make_unique<an::ui::AnTag>(QStringLiteral("Tag"), tone);
        tag->setIcon(greenIcon());
        parts.push_back(std::move(tag));
    }
    parts.push_back(std::make_unique<an::ui::AnKeyHint>(QKeySequence(Qt::CTRL | Qt::Key_K)));
    for (const bool checked : {false, true})
    {
        auto chip = std::make_unique<an::ui::AnChip>(QStringLiteral("Grid"));
        chip->setIcon(greenIcon());
        chip->setCountText(QStringLiteral("3"));
        chip->setCheckable(true);
        chip->setChecked(checked);
        parts.push_back(std::move(chip));
        auto toggle = std::make_unique<an::ui::AnSwitch>(QStringLiteral("Night"));
        toggle->setChecked(checked);
        parts.push_back(std::move(toggle));
    }
    auto segments = std::make_unique<an::ui::AnSegmentedControl>(
        QStringList{QStringLiteral("Linear"), QStringLiteral("Autostretch")});
    segments->setCurrentIndex(1);
    parts.push_back(std::move(segments));
    auto field = std::make_unique<an::ui::AnLabeledField>(QStringLiteral("Folder"), new QLineEdit);
    field->setHint(QStringLiteral("Kept with the project"));
    parts.push_back(std::move(field));
    auto crumb = std::make_unique<an::ui::AnBreadcrumb>();
    crumb->setParts(QStringList{QStringLiteral("Workspace 1"), QStringLiteral("M42")});
    parts.push_back(std::move(crumb));
    auto bar = std::make_unique<an::ui::AnSnackbar>();
    bar->setIcon(greenIcon());
    bar->setText(QStringLiteral("Curves Transformation applied"));
    bar->setDetail(QStringLiteral("2.8 s"));
    bar->setActionText(QStringLiteral("Undo"));
    parts.push_back(std::move(bar));
    auto stages = std::make_unique<an::ui::AnStageList>();
    stages->setRows({
        an::ui::StageRow{QStringLiteral("a"), QStringLiteral("Frames"), QStringLiteral("718 frames"),
                         an::ui::StageStatus::Done, std::nullopt},
        an::ui::StageRow{QStringLiteral("b"), QStringLiteral("Calibration"), QStringLiteral("15 masters"),
                         an::ui::StageStatus::Warning, std::nullopt},
        an::ui::StageRow{QStringLiteral("c"),
                         QStringLiteral("Registration"),
                         QStringLiteral("412 / 718"),
                         an::ui::StageStatus::Running,
                         57,
                         QString(),
                         {an::ui::StageStep{QStringLiteral("Detect"), an::ui::StageStatus::Done},
                          an::ui::StageStep{QStringLiteral("Match"), an::ui::StageStatus::Running},
                          an::ui::StageStep{QStringLiteral("Solve"), an::ui::StageStatus::Pending}}},
        an::ui::StageRow{QStringLiteral("d"), QStringLiteral("Stacking"), QStringLiteral("4 groups"),
                         an::ui::StageStatus::Pending, std::nullopt},
        an::ui::StageRow{QStringLiteral("e"), QStringLiteral("Drizzle"), QStringLiteral("no output"),
                         an::ui::StageStatus::Failed, std::nullopt},
        an::ui::StageRow{QStringLiteral("f"), QStringLiteral("Lights"), QStringLiteral("718 frames"),
                         an::ui::StageStatus::Ready, std::nullopt},
        an::ui::StageRow{QStringLiteral("g"), QStringLiteral("Normalization"), QStringLiteral("local"),
                         an::ui::StageStatus::OutOfDate, std::nullopt},
    });
    stages->setSelectedId(QStringLiteral("d"));
    parts.push_back(std::move(stages));
    return parts;
}

} // namespace

TEST(SmallParts, PaintNoGreenOrBlueAtNight)
{
    an::ui::testing::ObservatoryStyleScope scope(an::ui::testing::observatoryLooks()[0]);
    for (const std::size_t look : {std::size_t{0}, std::size_t{5}})
    {
        SCOPED_TRACE(an::ui::testing::observatoryLooks()[look].name);
        scope.setLook(an::ui::testing::observatoryLooks()[look]);
        scope.setNight(true);
        const an::ui::DesignTokens& t = an::ui::tokens();
        for (const std::unique_ptr<QWidget>& part : everyPart())
        {
            SCOPED_TRACE(part->metaObject()->className());
            showOffScreen(*part);
            part->resize(part->sizeHint().expandedTo(QSize(1, 1)));
            if (auto* const stages = dynamic_cast<an::ui::AnStageList*>(part.get()))
            {
                stages->resize(240, stages->sizeHint().height());
            }
            for (const qreal ratio : {1.0, 2.0})
            {
                QWidget& painted = dynamic_cast<an::ui::AnStageList*>(part.get()) != nullptr
                                       ? *static_cast<an::ui::AnStageList*>(part.get())->viewport()
                                       : *part;
                const RenderedPicture picture = RenderedPicture::ofWidget(painted, ratio, t.layers.surface1);
                EXPECT_EQ(firstHue(picture), std::string()) << "at a device pixel ratio of " << ratio;
            }
        }
        scope.setNight(false);
    }
}
