// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "ImageSummaryViewerControl.h"
#include "controls/ImageInspectorContent.h"
#include "controls/ImageViewControl.h"
#include "theme/AnDesignTokens.h"

#include <QGridLayout>
#include <QFrame>
#include <QLabel>
#include <QLayoutItem>
#include <QRegularExpression>
#include <QTextDocumentFragment>
#include <QVBoxLayout>
#include <QClipboard>
#include <QApplication>

namespace
{

struct SummaryField
{
    QString key;
    QString value;
};

struct SummarySection
{
    QString title;
    QVector<SummaryField> fields;
    QStringList notes;
};

QString htmlToPlainText(const QString& html)
{
    return QTextDocumentFragment::fromHtml(html).toPlainText().trimmed();
}

void clearLayout(QLayout* layout)
{
    if (!layout)
    {
        return;
    }

    while (QLayoutItem* item = layout->takeAt(0))
    {
        if (QLayout* childLayout = item->layout())
        {
            clearLayout(childLayout);
            delete childLayout;
        }
        if (QWidget* childWidget = item->widget())
        {
            delete childWidget;
        }
        delete item;
    }
}

QVector<SummarySection> parseSummaryReport(const QString& reportHtml)
{
    QVector<SummarySection> sections;
    if (reportHtml.trimmed().isEmpty())
    {
        return sections;
    }

    static const QRegularExpression sectionRegex(QStringLiteral(R"(<h3[^>]*>(.*?)</h3>\s*(?:<p[^>]*>(.*?)</p>)?)"),
                                                 QRegularExpression::PatternOption::CaseInsensitiveOption |
                                                     QRegularExpression::PatternOption::DotMatchesEverythingOption);
    static const QRegularExpression lineBreakRegex(QStringLiteral(R"(<br\s*/?>)"),
                                                   QRegularExpression::PatternOption::CaseInsensitiveOption);

    QRegularExpressionMatchIterator matchIt = sectionRegex.globalMatch(reportHtml);
    while (matchIt.hasNext())
    {
        const QRegularExpressionMatch match = matchIt.next();
        SummarySection section;
        section.title = htmlToPlainText(match.captured(1));

        const QString paragraphHtml = match.captured(2).trimmed();
        if (!paragraphHtml.isEmpty())
        {
            const QStringList entries = paragraphHtml.split(lineBreakRegex, Qt::SkipEmptyParts);
            for (const QString& entryHtml : entries)
            {
                const QString line = htmlToPlainText(entryHtml);
                if (line.isEmpty() || line == QStringLiteral("\u00a0"))
                {
                    continue;
                }

                const int separatorIndex = line.indexOf(':');
                if (separatorIndex > 0)
                {
                    SummaryField field;
                    field.key = line.left(separatorIndex).trimmed();
                    field.value = line.mid(separatorIndex + 1).trimmed();
                    if (field.value.isEmpty())
                    {
                        field.value = QStringLiteral("-");
                    }
                    section.fields.push_back(std::move(field));
                }
                else
                {
                    section.notes.push_back(line.trimmed());
                }
            }
        }

        if (section.fields.isEmpty() && section.notes.isEmpty())
        {
            const int separatorIndex = section.title.indexOf(':');
            if (separatorIndex > 0)
            {
                SummaryField titleField;
                titleField.key = section.title.left(separatorIndex).trimmed();
                titleField.value = section.title.mid(separatorIndex + 1).trimmed();
                if (titleField.value.isEmpty())
                {
                    titleField.value = QStringLiteral("-");
                }
                section.fields.push_back(std::move(titleField));
                section.title = QStringLiteral("Overview");
            }
        }

        if (!section.title.isEmpty() || !section.fields.isEmpty() || !section.notes.isEmpty())
        {
            sections.push_back(std::move(section));
        }
    }

    if (sections.isEmpty())
    {
        const QString plain = htmlToPlainText(reportHtml);
        if (!plain.isEmpty())
        {
            SummarySection fallbackSection;
            fallbackSection.title = QStringLiteral("Summary");
            const QStringList lines = plain.split('\n', Qt::SkipEmptyParts);
            for (const QString& lineRaw : lines)
            {
                const QString line = lineRaw.trimmed();
                if (line.isEmpty())
                {
                    continue;
                }
                const int separatorIndex = line.indexOf(':');
                if (separatorIndex > 0)
                {
                    SummaryField field;
                    field.key = line.left(separatorIndex).trimmed();
                    field.value = line.mid(separatorIndex + 1).trimmed();
                    fallbackSection.fields.push_back(std::move(field));
                }
                else
                {
                    fallbackSection.notes.push_back(line);
                }
            }
            sections.push_back(std::move(fallbackSection));
        }
    }

    return sections;
}

void appendSummarySection(QVBoxLayout* contentLayout, QWidget* parent, const SummarySection& section)
{
    if (!contentLayout || !parent)
    {
        return;
    }

    const auto& tok = an::ui::tokens();

    auto* sectionWidget = new QWidget(parent);
    sectionWidget->setObjectName(QStringLiteral("imageSummarySection"));
    auto* sectionLayout = new QVBoxLayout(sectionWidget);
    sectionLayout->setContentsMargins(0, 0, 0, 0);
    sectionLayout->setSpacing(8);

    if (!section.title.isEmpty())
    {
        auto* sectionTitleLabel = new QLabel(section.title, sectionWidget);
        sectionTitleLabel->setObjectName(QStringLiteral("imageSummarySectionTitle"));
        sectionTitleLabel->setWordWrap(true);
        sectionTitleLabel->setFont(tok.typography.heading2);
        QPalette titlePalette = sectionTitleLabel->palette();
        titlePalette.setColor(QPalette::WindowText, tok.colors.textPrimary);
        sectionTitleLabel->setPalette(titlePalette);
        sectionTitleLabel->setContentsMargins(0, 2, 0, 2);
        sectionLayout->addWidget(sectionTitleLabel);
    }

    if (!section.fields.isEmpty())
    {
        auto* fieldsLayout = new QGridLayout();
        fieldsLayout->setContentsMargins(0, 0, 0, 0);
        fieldsLayout->setHorizontalSpacing(14);
        fieldsLayout->setVerticalSpacing(6);
        fieldsLayout->setColumnStretch(1, 1);

        QFont keyFont = tok.typography.heading3;

        int row = 0;
        for (const auto& field : section.fields)
        {
            auto* keyLabel = new QLabel(field.key + QStringLiteral(":"), sectionWidget);
            keyLabel->setObjectName(QStringLiteral("imageSummaryFieldKey"));
            keyLabel->setFont(keyFont);
            keyLabel->setProperty("anStatusTone", QStringLiteral("secondary"));
            keyLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
            fieldsLayout->addWidget(keyLabel, row, 0, Qt::AlignTop);

            auto* valueLabel = new QLabel(field.value, sectionWidget);
            valueLabel->setObjectName(QStringLiteral("imageSummaryFieldValue"));
            valueLabel->setWordWrap(true);
            valueLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
            valueLabel->setFont(tok.typography.body);
            fieldsLayout->addWidget(valueLabel, row, 1, Qt::AlignTop);

            ++row;
        }

        sectionLayout->addLayout(fieldsLayout);
    }

    for (const QString& note : section.notes)
    {
        auto* noteLabel = new QLabel(note, sectionWidget);
        noteLabel->setObjectName(QStringLiteral("imageSummarySectionNote"));
        noteLabel->setWordWrap(true);
        noteLabel->setFont(tok.typography.body);
        noteLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
        sectionLayout->addWidget(noteLabel);
    }

    contentLayout->addWidget(sectionWidget);
}

void renderSummaryReport(an::ui::controls::ImageSummaryViewerControlUiElements& ui, const QString& reportHtml)
{
    if (!ui.summaryContentLayout || !ui.summaryContentWidget)
    {
        return;
    }

    clearLayout(ui.summaryContentLayout);
    const QVector<SummarySection> sections = parseSummaryReport(reportHtml);

    if (sections.isEmpty())
    {
        auto* emptyLabel = new QLabel(QObject::tr("No summary information available."), ui.summaryContentWidget);
        emptyLabel->setObjectName(QStringLiteral("imageSummaryEmptyState"));
        emptyLabel->setProperty("anStatusTone", QStringLiteral("secondary"));
        emptyLabel->setWordWrap(true);
        ui.summaryContentLayout->addWidget(emptyLabel);
        ui.summaryContentLayout->addStretch(1);
        return;
    }

    for (const auto& section : sections)
    {
        appendSummarySection(ui.summaryContentLayout, ui.summaryContentWidget, section);

        if (&section != &sections.back())
        {
            auto* separator = new QFrame(ui.summaryContentWidget);
            separator->setObjectName(QStringLiteral("imageSummarySectionSeparator"));
            separator->setFrameShape(QFrame::HLine);
            separator->setFrameShadow(QFrame::Plain);
            separator->setLineWidth(1);

            QPalette pal = separator->palette();
            pal.setColor(QPalette::WindowText, an::ui::tokens().colors.borderSubtle);
            separator->setPalette(pal);
            ui.summaryContentLayout->addWidget(separator);
        }
    }
    ui.summaryContentLayout->addStretch(1);
}

QString summaryTitle(const ImageViewControl& view)
{
    return QStringLiteral("Image Summary Viewer <%1>").arg(QString::fromUtf8(view.getUniqueName()));
}

} // namespace

ImageSummaryViewerControl::ImageSummaryViewerControl(ImageViewControl* control)
    : QDialog(control), ImageViewControlListener(control)
{
    m_ui = an::ui::controls::buildImageSummaryViewerControlUi(this);

    this->setAttribute(Qt::WA_DeleteOnClose);

    QObject::connect(m_ui.closeButton, &QPushButton::released, this, &ImageSummaryViewerControl::on_closeButtonPushed);
    QObject::connect(m_ui.copyButton, &QPushButton::released, this, &ImageSummaryViewerControl::on_copyButtonPushed);

    setWindowTitle(summaryTitle(*m_image_view_control));

    initialize();

    m_image_view_control->enableAction(ImageViewControlAction::execute_summary, false);

    recalculate();
}

void ImageSummaryViewerControl::onImageLoaded(ImageViewControl*)
{
    recalculate();
}

void ImageSummaryViewerControl::onImageUpdated(ImageViewControl*)
{
    recalculate();
}

void ImageSummaryViewerControl::onImageRenamed(ImageViewControl*)
{
    setWindowTitle(summaryTitle(*m_image_view_control));
    recalculate();
}

void ImageSummaryViewerControl::recalculate()
{
    if (!m_image_view_control)
    {
        return;
    }
    const QString reportHtml = an::ui::documentSummaryReport(m_image_view_control->document(),
                                                             QString::fromUtf8(m_image_view_control->getUniqueName()));
    if (reportHtml.isEmpty() && !m_reportPlainText.isEmpty())
    {
        // A writer holds the image: the summary shown stays until the view's next update.
        return;
    }

    m_reportPlainText = QTextDocumentFragment::fromHtml(reportHtml).toPlainText();
    renderSummaryReport(m_ui, reportHtml);
}

ImageSummaryViewerControl::~ImageSummaryViewerControl() {}

void ImageSummaryViewerControl::on_closeButtonPushed()
{
    this->close();
}

void ImageSummaryViewerControl::on_copyButtonPushed()
{
    if (QClipboard* clipboard = QApplication::clipboard())
    {
        clipboard->setText(m_reportPlainText);
    }
}

void ImageSummaryViewerControl::done(int r)
{
    if (m_image_view_control)
    {
        m_image_view_control->enableAction(ImageViewControlAction::execute_summary, true);
    }

    QDialog::done(r);
}

void ImageSummaryViewerControl::initialize() {}
