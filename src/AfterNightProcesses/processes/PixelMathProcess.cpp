// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "PixelMathProcess.h"
#include "controls/ImageViewControl.h"
#include "controls/PixelMathVocabulary.h"
#include "processes/ProcessCommonUtils.h"
#include "theme/AnDesignTokens.h"

#include <fmt/format.h>

#include <algorithm>
#include <atomic>
#include <memory>
#include <random>

namespace
{

/// The words a run says itself in.
[[nodiscard]] an::ui::processes::GeometryRunWords runWords(const ImageViewControl& view)
{
    return an::ui::processes::GeometryRunWords{
        .title = QStringLiteral("PixelMath"),
        .progressText = QString::fromStdString(fmt::format("Executing PixelMath to sequence {}", view.getUniqueName())),
        .errorTitle = QStringLiteral("Error running PixelMath"),
        .errorLead = "An error occurred when executing the PixelMath process"};
}

} // namespace

PixelMathProcess::PixelMathProcess(ImageViewControl* control) : ImageProcessControl(control, getProcessNameStatic())
{
    setWindowSizeFitMultipliers(1.25, 1.0);
    m_ui = an::ui::processes::buildPixelMathProcessUi(this);

    this->setAttribute(Qt::WA_DeleteOnClose);

    QObject::connect(m_ui.buttonBox, &QDialogButtonBox::clicked, this, &PixelMathProcess::on_dialog_box_button_clicked);

    QObject::connect(m_ui.useSingleRGBKExpression, &QCheckBox::checkStateChanged, this,
                     &PixelMathProcess::on_useSingleRGBKExpression_clicked);
    QObject::connect(m_ui.rescaleResult, &QCheckBox::checkStateChanged, this,
                     &PixelMathProcess::on_rescaleResult_clicked);

    QObject::connect(m_ui.outputModeSelector, &QComboBox::currentTextChanged, this,
                     &PixelMathProcess::on_outputModeSelector_currentTextChanged);
    QObject::connect(m_ui.colorSpaceSelector, &QComboBox::currentTextChanged, this,
                     &PixelMathProcess::on_targetColorSpace_currentTextChanged);

    QObject::connect(m_ui.tabWidget, &QTabWidget::currentChanged, this, &PixelMathProcess::currentTabChanged);

    QObject::connect(m_ui.importButton, &QPushButton::released, this, &PixelMathProcess::on_importButtonPushed);
    QObject::connect(m_ui.exportButton, &QPushButton::released, this, &PixelMathProcess::on_exportButtonPushed);

    const QFont symbolsFont = an::ui::tokens().typography.monospace;
    m_ui.localSymbolsTextEdit->setFont(symbolsFont);
    m_ui.globalSymbolsTextEdit->setFont(symbolsFont);

    updateWindowTitle();
    setProcessDescription(QStringLiteral(
        "Evaluate pixel-wise expressions to transform target images or create new outputs from scripted formulas."));

    m_SupportTargetViewControlSelection = true;
    initializeTargetViewControlComboBox(m_ui.targetImageSelector);

    m_ui.functionsTableWidget1->setListener(this);
    m_ui.operatorsTableWidget1->setListener(this);
    m_ui.symbolsTableWidget1->setListener(this);

    m_ui.functionsTableWidget2->setListener(this);
    m_ui.operatorsTableWidget2->setListener(this);
    m_ui.symbolsTableWidget2->setListener(this);

    m_ui.functionsTableWidget3->setListener(this);
    m_ui.operatorsTableWidget3->setListener(this);
    m_ui.symbolsTableWidget3->setListener(this);

    m_ui.functionsTableWidget4->setListener(this);
    m_ui.operatorsTableWidget4->setListener(this);
    m_ui.symbolsTableWidget4->setListener(this);

    initialize();

    updateTargetImageInEditors();
    updateImageListInEditors(false);
    updateImageSymbolsInEditors(true);
}

std::string PixelMathProcess::getProcessNameStatic()
{
    return "PixelMath";
}

bool PixelMathProcess::requiresExclusiveAccessStatic()
{
    return false;
}

an::ui::processes::PixelMathSettings PixelMathProcess::settings() const
{
    using namespace an::ui::processes;
    PixelMathSettings asked;
    asked.expressions = {
        m_ui.rgbkTextEdit->toPlainText().toStdString(), m_ui.greenTextEdit->toPlainText().toStdString(),
        m_ui.blueTextEdit->toPlainText().toStdString(), m_ui.alphaTextEdit->toPlainText().toStdString()};
    asked.singleExpression = !useSeparateRGBScripts();
    asked.localSymbols = m_ui.localSymbolsTextEdit->toPlainText().toStdString();
    asked.globalSymbols = m_ui.globalSymbolsTextEdit->toPlainText().toStdString();
    asked.rescale = m_ui.rescaleResult->isChecked();
    asked.rescaleLow = m_ui.lowerBoundValue->value();
    asked.rescaleHigh = m_ui.upperBoundValue->value();
    asked.outputMode = m_ui.outputModeSelector->currentIndex() == 1 ? PixelMathOutputMode::CreateNewImage
                                                                    : PixelMathOutputMode::ReplaceTarget;
    asked.newImageId = m_ui.newImadeIdEdit->text().toStdString();
    asked.width = static_cast<std::uint32_t>((std::max)(0, m_ui.widthValue->value()));
    asked.height = static_cast<std::uint32_t>((std::max)(0, m_ui.heightValue->value()));
    asked.colorSpace = static_cast<PixelMathColorSpace>(std::clamp(m_ui.colorSpaceSelector->currentIndex(), 0, 3));
    asked.colorFormat = static_cast<PixelMathColorFormat>(std::clamp(m_ui.colorFormatSelector->currentIndex(), 0, 5));
    asked.singleThreaded = m_ui.singleThreaded->isChecked();
    asked.processIn64Bits = m_ui.processIn64Bits->isChecked();
    asked.codeOptimization = m_ui.codeOptimization->isChecked();
    return asked;
}

void PixelMathProcess::applySettings(const an::ui::processes::PixelMathSettings& settings)
{
    m_ui.useSingleRGBKExpression->setChecked(settings.singleExpression);
    m_ui.singleThreaded->setChecked(settings.singleThreaded);
    m_ui.processIn64Bits->setChecked(true);
    m_ui.codeOptimization->setChecked(settings.codeOptimization);
    m_ui.rescaleResult->setChecked(settings.rescale);
    m_ui.lowerBoundValue->setValue(settings.rescaleLow);
    m_ui.upperBoundValue->setValue(settings.rescaleHigh);

    m_ui.rgbkTextEdit->setPlainText(QString::fromStdString(settings.expressions[0]));
    m_ui.greenTextEdit->setPlainText(QString::fromStdString(settings.expressions[1]));
    m_ui.blueTextEdit->setPlainText(QString::fromStdString(settings.expressions[2]));
    m_ui.alphaTextEdit->setPlainText(QString::fromStdString(settings.expressions[3]));

    m_ui.localSymbolsTextEdit->setPlainText(QString::fromStdString(settings.localSymbols));
    m_ui.globalSymbolsTextEdit->setPlainText(QString::fromStdString(settings.globalSymbols));

    m_ui.outputModeSelector->setCurrentIndex(static_cast<int>(settings.outputMode));
    m_ui.newImadeIdEdit->setText(QString::fromStdString(settings.newImageId));
    m_ui.widthValue->setValue(static_cast<int>(settings.width));
    m_ui.heightValue->setValue(static_cast<int>(settings.height));
    m_ui.colorSpaceSelector->setCurrentIndex(static_cast<int>(settings.colorSpace));
    m_ui.colorFormatSelector->setCurrentIndex(static_cast<int>(settings.colorFormat));

    // The settings' own choice of one program or three is offered only where the output writes three channels.
    updateUseSeparateRGBScriptsCheckbox();
}

an::ui::processes::PixelMathImageSet PixelMathProcess::imageSet() const
{
    an::ui::processes::PixelMathImageSet images;
    images.targetName = m_image_view_control->getUniqueName();
    for (ImageViewControl* view : process_utils::listViewControls())
    {
        if (view == nullptr || view == m_image_view_control || !view->hasImage())
        {
            continue;
        }
        images.others.push_back(
            an::ui::processes::PixelMathNamedImage{.name = view->getUniqueName(), .asset = view->document().asset()});
    }
    ImageViewControl* active = process_utils::currentActiveViewControl();
    if (active != nullptr && active != m_image_view_control && active->hasImage())
    {
        images.active = active->document().asset();
    }
    ImageViewControl* mask = m_image_view_control->isMasked() ? m_image_view_control->getCurrentMask() : nullptr;
    if (mask != nullptr && mask->hasImage())
    {
        images.mask = mask->document().asset();
    }
    return images;
}

std::uint64_t PixelMathProcess::drawRandomSeed() const
{
    std::random_device device;
    return (static_cast<std::uint64_t>(device()) << 32u) ^ static_cast<std::uint64_t>(device());
}

void PixelMathProcess::onImageUpdated(ImageViewControl* ctrl)
{
    ImageProcessControl::onImageUpdated(ctrl);

    updateUseSeparateRGBScriptsCheckbox();
}

void PixelMathProcess::onViewControlCreated(const ImageViewControl* ctrl)
{
    ImageProcessControl::onViewControlCreated(ctrl);

    updateImageListInEditors(true);
}

void PixelMathProcess::onViewControlDestroyed(const ImageViewControl* ctrl)
{
    ImageProcessControl::onViewControlDestroyed(ctrl);

    updateImageListInEditors(true);
}

void PixelMathProcess::onViewControlRenamed(const std::string& newName, const std::string& oldName,
                                            const ImageViewControl* ctrl)
{
    ImageProcessControl::onViewControlRenamed(newName, oldName, ctrl);

    updateImageListInEditors(true);
}

void PixelMathProcess::onViewControlChanged(ImageViewControl* oldCtrl, ImageViewControl* newCtrl)
{
    ImageProcessControl::onViewControlChanged(oldCtrl, newCtrl);

    updateUseSeparateRGBScriptsCheckbox();
    updateTargetImageInEditors();
}

void PixelMathProcess::onRowDoubleClicked(TableWidget* sender, int row)
{
    QTableWidgetItem* item = sender->item(row, 0);
    if (item)
    {
        QString cellText = item->text();

        PixelMathEditControl* control = nullptr;
        if (m_ui.tabWidget->currentIndex() == 0)
        {
            control = m_ui.rgbkTextEdit;
        }
        else if (m_ui.tabWidget->currentIndex() == 1)
        {
            control = m_ui.greenTextEdit;
        }
        else if (m_ui.tabWidget->currentIndex() == 2)
        {
            control = m_ui.blueTextEdit;
        }
        else if (m_ui.tabWidget->currentIndex() == 3)
        {
            control = m_ui.alphaTextEdit;
        }

        if (control)
        {
            control->setFocus();
            control->insertPlainText(cellText);
            if (sender == m_ui.functionsTableWidget1 || sender == m_ui.functionsTableWidget2 ||
                sender == m_ui.functionsTableWidget3 || sender == m_ui.functionsTableWidget4)
            {
                control->insertPlainText("() ");
            }
            else
            {
                control->insertPlainText(" ");
            }
        }
    }
}

PixelMathProcess::~PixelMathProcess() {}

void PixelMathProcess::on_useSingleRGBKExpression_clicked(Qt::CheckState state)
{
    if (state == Qt::Checked)
    {
        m_ui.tabWidget->setTabText(0, "RGB/K");
        m_ui.tabWidget->setTabEnabled(1, false);
        m_ui.tabWidget->setTabEnabled(2, false);
    }
    else
    {
        m_ui.tabWidget->setTabText(0, "R");
        m_ui.tabWidget->setTabEnabled(1, true);
        m_ui.tabWidget->setTabEnabled(2, true);
    }

    updateImageListInEditors();
    updateImageSymbolsInEditors();
}

void PixelMathProcess::on_rescaleResult_clicked(Qt::CheckState state)
{
    if (state == Qt::Checked)
    {
        m_ui.lowerBoundValue->setEnabled(true);
        m_ui.upperBoundValue->setEnabled(true);
    }
    else
    {
        m_ui.lowerBoundValue->setEnabled(false);
        m_ui.upperBoundValue->setEnabled(false);
    }
}

void PixelMathProcess::on_outputModeSelector_currentTextChanged(const QString& str)
{
    if (str == "Create new image")
    {
        m_ui.newImadeIdEdit->setEnabled(true);
        m_ui.widthValue->setEnabled(true);
        m_ui.heightValue->setEnabled(true);
        m_ui.colorSpaceSelector->setEnabled(true);
        m_ui.colorFormatSelector->setEnabled(true);
    }
    else
    {
        m_ui.newImadeIdEdit->setEnabled(false);
        m_ui.widthValue->setEnabled(false);
        m_ui.heightValue->setEnabled(false);
        m_ui.colorSpaceSelector->setEnabled(false);
        m_ui.colorFormatSelector->setEnabled(false);
    }

    updateUseSeparateRGBScriptsCheckbox();
}

void PixelMathProcess::on_targetColorSpace_currentTextChanged(const QString&)
{
    updateUseSeparateRGBScriptsCheckbox();
}

void PixelMathProcess::currentTabChanged(int index)
{
    if (index >= 0 && index < 3)
    {
        updateImageSymbolsInEditors(true);
    }
    else if (index == 5)
    {
        updateImageSymbolsInEditors(true);
        updateSummaryTab();
    }
}

void PixelMathProcess::on_importButtonPushed()
{
    auto selectedFiles =
        process_utils::selectFiles(this, QStringLiteral("Import pixel math expression..."),
                                   QStringLiteral("__PIXEL_MATH_FOLDER__"), {QStringLiteral("Files (*.anpm)")});

    if (!selectedFiles.empty())
    {
        const QString file = selectedFiles.back();

        if (!m_ui.rgbkTextEdit->toPlainText().trimmed().isEmpty() ||
            !m_ui.greenTextEdit->toPlainText().trimmed().isEmpty() ||
            !m_ui.blueTextEdit->toPlainText().trimmed().isEmpty() ||
            !m_ui.alphaTextEdit->toPlainText().trimmed().isEmpty())
        {
            int ret = QMessageBox::No;
            process_utils::messageBoxYesNo(
                this, QStringLiteral("Import pixel math script"),
                QStringLiteral("This will override all the current content. Do you want to continue?"), true, &ret);

            if (ret == QMessageBox::No)
            {
                return;
            }
        }

        const an::Result<an::ui::processes::PixelMathSettings> read =
            an::ui::processes::readPixelMathSettingsFile(file);
        if (read.hasValue())
        {
            applySettings(*read);
            LOG_INFO("PixelMathProcess: Imported expression from file {}", file.toStdString());
        }
        else
        {
            LOG_WARNING("PixelMathProcess: {}", read.status().message());
            process_utils::notifyErrorMessage(this, QStringLiteral("Error importing pixel math script"),
                                              QString::fromStdString(read.status().message()));
        }

        m_ui.tabWidget->setCurrentIndex(0);
        updateImageSymbolsInEditors(true);
        updateSummaryTab();
    }
}

void PixelMathProcess::on_exportButtonPushed()
{
    auto selectedFiles =
        process_utils::saveFiles(this, QStringLiteral("Export pixel math expression..."), QString(),
                                 QStringLiteral("__PIXEL_MATH_FOLDER__"), {QStringLiteral("Files (*.anpm)")});

    if (!selectedFiles.empty())
    {
        const QString file = selectedFiles.back();
        const an::Status written = an::ui::processes::writePixelMathSettingsFile(file, settings());
        if (written.ok())
        {
            LOG_INFO("PixelMathProcess: Exported expression to file {}", file.toStdString());
        }
        else
        {
            LOG_WARNING("PixelMathProcess: {}", written.message());
            process_utils::notifyErrorMessage(this, QStringLiteral("Error exporting pixel math script"),
                                              QString::fromStdString(written.message()));
        }
    }
}

void PixelMathProcess::initialize()
{
    m_ui.tabWidget->setTabText(0, "RGB/K");
    m_ui.tabWidget->setTabEnabled(1, false);
    m_ui.tabWidget->setTabEnabled(2, false);

    m_ui.outputModeSelector->setCurrentText("Replace target image");
    m_ui.newImadeIdEdit->setEnabled(false);
    m_ui.widthValue->setEnabled(false);
    m_ui.heightValue->setEnabled(false);
    m_ui.colorSpaceSelector->setEnabled(false);
    m_ui.colorFormatSelector->setEnabled(false);

    // The expression tabs offered are the output's channels, as a change of target or output mode decides them: the
    // alpha tab only for an output with an alpha channel, since its expression is read for a fourth channel alone.
    updateUseSeparateRGBScriptsCheckbox();

    initializeTables();
}

void PixelMathProcess::initializeFunctionTable(TableWidget* functionTable, const QStringList& functions)
{
    functionTable->setAlternatingRowColors(true);

    functionTable->setColumnCount(1);
    functionTable->setHeaderLabels({"Functions"});
    functionTable->setupSelectionColors();
    functionTable->verticalHeader()->setDefaultSectionSize(10);

    functionTable->clearContents();
    functionTable->setRowCount(static_cast<int>(functions.size()));
    functionTable->setSortingEnabled(false);
    int currentRow = 0;
    for (const QString& function : functions)
    {
        auto item = new QTableWidgetItem(function);
        item->setToolTip(PixelMathEditControl::getDocForFunctionOrOperator(function));
        functionTable->setItem(currentRow, 0, item);

        currentRow++;
    }
}

void PixelMathProcess::initializeOperatorTable(TableWidget* operatorTable, const QStringList& operators)
{
    operatorTable->setAlternatingRowColors(true);

    operatorTable->setColumnCount(1);
    operatorTable->setHeaderLabels({"Operators"});
    operatorTable->setupSelectionColors();
    operatorTable->verticalHeader()->setDefaultSectionSize(10);

    operatorTable->clearContents();
    operatorTable->setRowCount(static_cast<int>(operators.size()));
    operatorTable->setSortingEnabled(false);
    int currentRow = 0;
    for (const QString& op : operators)
    {
        auto item = new QTableWidgetItem(op);
        item->setToolTip(PixelMathEditControl::getDocForFunctionOrOperator(op));
        operatorTable->setItem(currentRow, 0, item);

        currentRow++;
    }
}

std::map<std::string, double> PixelMathProcess::symbolMap(const QString& text) const
{
    std::map<std::string, double> symbols;
    for (const an::ui::processes::PixelMathSymbol& symbol :
         an::ui::processes::parsePixelMathSymbols(text.toStdString()))
    {
        symbols[symbol.name] = symbol.value;
    }
    return symbols;
}

void PixelMathProcess::initializeSymbolTable(TableWidget* symbolTable)
{
    const std::map<std::string, double> localsSymbolsMap = symbolMap(m_ui.localSymbolsTextEdit->toPlainText());
    const std::map<std::string, double> globalsSymbolsMap = symbolMap(m_ui.globalSymbolsTextEdit->toPlainText());

    const std::map<QString, QString> macros = {
        {"$T", "Target image"}, {"$A", "Active image"}, {"$M", "Target image mask"}};

    std::vector<std::string> images;
    for (auto* viewControl : process_utils::listViewControls())
    {
        if (viewControl)
        {
            images.push_back(viewControl->getUniqueName());
        }
    }

    int count = (int)(localsSymbolsMap.size() + globalsSymbolsMap.size() + macros.size() + images.size());

    symbolTable->setAlternatingRowColors(true);

    symbolTable->setColumnCount(2);
    symbolTable->setHeaderLabels({"Symbol", "Type"});
    symbolTable->setupSelectionColors();
    symbolTable->verticalHeader()->setDefaultSectionSize(10);

    symbolTable->clearContents();
    symbolTable->setRowCount(count);
    symbolTable->setSortingEnabled(false);

    int currentRow = 0;

    for (auto& it : macros)
    {
        symbolTable->setItem(currentRow, 0, new QTableWidgetItem(it.first));
        auto typeItem = new QTableWidgetItem(it.second);
        typeItem->setToolTip(it.second);
        symbolTable->setItem(currentRow, 1, typeItem);

        currentRow++;
    }

    for (auto& it : localsSymbolsMap)
    {
        symbolTable->setItem(currentRow, 0, new QTableWidgetItem(QString::fromStdString(it.first)));
        symbolTable->setItem(currentRow, 1, new QTableWidgetItem("Local symbol"));

        currentRow++;
    }

    for (auto& it : globalsSymbolsMap)
    {
        symbolTable->setItem(currentRow, 0, new QTableWidgetItem(QString::fromStdString(it.first)));
        symbolTable->setItem(currentRow, 1, new QTableWidgetItem("Global symbol"));

        currentRow++;
    }

    for (auto& it : images)
    {
        symbolTable->setItem(currentRow, 0, new QTableWidgetItem(QString::fromStdString(it)));
        symbolTable->setItem(currentRow, 1, new QTableWidgetItem("Image"));

        currentRow++;
    }
}

void PixelMathProcess::initializeTables()
{
    // The engine's language: every function name its tables answer to, the special ones beside them, and every
    // operator its syntax defines.
    const an::ui::PixelMathVocabulary vocabulary = an::ui::pixelMathVocabulary();
    QStringList functions = vocabulary.functions;
    functions.append(vocabulary.specialFunctions);
    std::sort(functions.begin(), functions.end(),
              [](const QString& a, const QString& b) { return QString::compare(a, b, Qt::CaseInsensitive) < 0; });

    initializeFunctionTable(m_ui.functionsTableWidget1, functions);
    initializeFunctionTable(m_ui.functionsTableWidget2, functions);
    initializeFunctionTable(m_ui.functionsTableWidget3, functions);
    initializeFunctionTable(m_ui.functionsTableWidget4, functions);

    initializeOperatorTable(m_ui.operatorsTableWidget1, vocabulary.operators);
    initializeOperatorTable(m_ui.operatorsTableWidget2, vocabulary.operators);
    initializeOperatorTable(m_ui.operatorsTableWidget3, vocabulary.operators);
    initializeOperatorTable(m_ui.operatorsTableWidget4, vocabulary.operators);

    initializeSymbolTable(m_ui.symbolsTableWidget1);
    initializeSymbolTable(m_ui.symbolsTableWidget2);
    initializeSymbolTable(m_ui.symbolsTableWidget3);
    initializeSymbolTable(m_ui.symbolsTableWidget4);
}

void PixelMathProcess::updateImageListInEditors(bool rehighlight)
{
    std::vector<std::string> vControls;
    for (auto* viewControl : process_utils::listViewControls())
    {
        if (viewControl)
        {
            vControls.push_back(viewControl->getUniqueName());
        }
    }

    // Add the special macros:
    vControls.push_back("$T");
    vControls.push_back("$A");
    vControls.push_back("$M"); // If any

    m_ui.rgbkTextEdit->getHighlighter()->setImageList(vControls);
    m_ui.alphaTextEdit->getHighlighter()->setImageList(vControls);
    m_ui.summaryTextEdit->getHighlighter()->setImageList(vControls);

    initializeSymbolTable(m_ui.symbolsTableWidget1);
    initializeSymbolTable(m_ui.symbolsTableWidget4);

    if (useSeparateRGBScripts())
    {
        m_ui.greenTextEdit->getHighlighter()->setImageList(vControls);
        m_ui.blueTextEdit->getHighlighter()->setImageList(vControls);

        initializeSymbolTable(m_ui.symbolsTableWidget2);
        initializeSymbolTable(m_ui.symbolsTableWidget3);
    }

    if (rehighlight)
    {
        m_ui.rgbkTextEdit->getHighlighter()->rehighlight();
        m_ui.alphaTextEdit->getHighlighter()->rehighlight();
        m_ui.summaryTextEdit->getHighlighter()->rehighlight();

        if (useSeparateRGBScripts())
        {
            m_ui.greenTextEdit->getHighlighter()->rehighlight();
            m_ui.blueTextEdit->getHighlighter()->rehighlight();
        }
    }
}

void PixelMathProcess::updateImageSymbolsInEditors(bool rehighlight)
{
    const std::map<std::string, double> localsSymbolsMap = symbolMap(m_ui.localSymbolsTextEdit->toPlainText());
    const std::map<std::string, double> globalsSymbolsMap = symbolMap(m_ui.globalSymbolsTextEdit->toPlainText());

    m_ui.rgbkTextEdit->getHighlighter()->setSymbolMap(localsSymbolsMap, globalsSymbolsMap);
    m_ui.alphaTextEdit->getHighlighter()->setSymbolMap(localsSymbolsMap, globalsSymbolsMap);
    m_ui.summaryTextEdit->getHighlighter()->setSymbolMap(localsSymbolsMap, globalsSymbolsMap);

    initializeSymbolTable(m_ui.symbolsTableWidget1);
    initializeSymbolTable(m_ui.symbolsTableWidget4);

    if (useSeparateRGBScripts())
    {
        m_ui.greenTextEdit->getHighlighter()->setSymbolMap(localsSymbolsMap, globalsSymbolsMap);
        m_ui.blueTextEdit->getHighlighter()->setSymbolMap(localsSymbolsMap, globalsSymbolsMap);

        initializeSymbolTable(m_ui.symbolsTableWidget2);
        initializeSymbolTable(m_ui.symbolsTableWidget3);
    }

    if (rehighlight)
    {
        m_ui.rgbkTextEdit->getHighlighter()->rehighlight();
        m_ui.alphaTextEdit->getHighlighter()->rehighlight();
        m_ui.summaryTextEdit->getHighlighter()->rehighlight();

        if (useSeparateRGBScripts())
        {
            m_ui.greenTextEdit->getHighlighter()->rehighlight();
            m_ui.blueTextEdit->getHighlighter()->rehighlight();
        }
    }
}

void PixelMathProcess::updateTargetImageInEditors()
{
    m_ui.rgbkTextEdit->setTargetImageRealName(m_image_view_control->getUniqueName());
    m_ui.alphaTextEdit->setTargetImageRealName(m_image_view_control->getUniqueName());
    m_ui.summaryTextEdit->setTargetImageRealName(m_image_view_control->getUniqueName());

    if (useSeparateRGBScripts())
    {
        m_ui.greenTextEdit->setTargetImageRealName(m_image_view_control->getUniqueName());
        m_ui.blueTextEdit->setTargetImageRealName(m_image_view_control->getUniqueName());
    }
}

void PixelMathProcess::updateSummaryTab()
{
    QString summary;

    if (useSeparateRGBScripts())
    {
        auto tmpText = m_ui.rgbkTextEdit->toPlainText().trimmed();
        if (!tmpText.isEmpty())
        {
            summary += "// *****************************\n";
            summary += "//              Red:            \n";
            summary += "// *****************************\n\n";
            summary += tmpText;

            summary += "\n\n";
        }

        tmpText = m_ui.greenTextEdit->toPlainText().trimmed();
        if (!tmpText.isEmpty())
        {
            summary += "// *****************************\n";
            summary += "//              Green:          \n";
            summary += "// *****************************\n\n";
            summary += tmpText;

            summary += "\n\n";
        }

        tmpText = m_ui.blueTextEdit->toPlainText().trimmed();
        if (!tmpText.isEmpty())
        {
            summary += "// *****************************\n";
            summary += "//              Blue:           \n";
            summary += "// *****************************\n\n";
            summary += tmpText;

            summary += "\n\n";
        }
    }
    else
    {
        auto tmpText = m_ui.rgbkTextEdit->toPlainText().trimmed();
        if (!tmpText.isEmpty())
        {
            summary += "// *****************************\n";
            summary += "//              RGB/K:          \n";
            summary += "// *****************************\n\n";
            summary += tmpText;

            summary += "\n\n";
        }
    }

    auto tmpText = m_ui.alphaTextEdit->toPlainText().trimmed();
    if (!tmpText.isEmpty())
    {
        summary += "// *****************************\n";
        summary += "//              Alpha:          \n";
        summary += "// *****************************\n\n";
        summary += tmpText;

        summary += "\n\n";
    }

    auto tmpLocalSymbolsText = m_ui.localSymbolsTextEdit->toPlainText().trimmed();
    auto tmpglobalSymbolsText = m_ui.globalSymbolsTextEdit->toPlainText().trimmed();
    if (!tmpLocalSymbolsText.isEmpty() || !tmpglobalSymbolsText.isEmpty())
    {
        summary += "// *****************************\n";
        summary += "//              Symbols:          \n";
        summary += "// *****************************\n\n";
        if (!tmpLocalSymbolsText.isEmpty())
        {
            summary += "// Local symbols:\n";
            summary += tmpLocalSymbolsText;
            summary += "\n\n";
        }

        if (!tmpglobalSymbolsText.isEmpty())
        {
            summary += "// Global symbols:\n";
            summary += tmpglobalSymbolsText;
            summary += "\n\n";
        }
    }

    m_ui.summaryTextEdit->setPlainText(summary);
}

bool PixelMathProcess::useSeparateRGBScripts() const
{
    // If this option isn't even enabled, then we can assume the use single script option is not active at all
    if (!m_ui.useSingleRGBKExpression->isEnabled())
        return false;

    return !m_ui.useSingleRGBKExpression->isChecked();
}

std::uint32_t PixelMathProcess::targetChannelCount() const
{
    const std::optional<an::ImageDescriptor> described =
        m_image_view_control != nullptr ? m_image_view_control->imageDescriptor() : std::nullopt;
    return described.has_value() ? described->extent().channelCount() : 1u;
}

void PixelMathProcess::updateUseSeparateRGBScriptsCheckbox()
{
    bool fromTarget = false;
    if (m_ui.outputModeSelector->currentText() == "Create new image")
    {
        auto cSpaceStr = m_ui.colorSpaceSelector->currentText();

        if (cSpaceStr == "As target")
        {
            fromTarget = true;
        }
        else if (cSpaceStr == "Grayscale")
        {
            m_ui.useSingleRGBKExpression->setEnabled(false);

            m_ui.tabWidget->setTabText(0, "RGB/K");
            m_ui.tabWidget->setTabEnabled(1, false);
            m_ui.tabWidget->setTabEnabled(2, false);
            m_ui.tabWidget->setTabEnabled(3, false);
        }
        else
        {
            m_ui.useSingleRGBKExpression->setEnabled(true);

            if (m_ui.useSingleRGBKExpression->isChecked())
            {
                m_ui.tabWidget->setTabText(0, "RGB/K");
                m_ui.tabWidget->setTabEnabled(1, false);
                m_ui.tabWidget->setTabEnabled(2, false);
            }
            else
            {
                m_ui.tabWidget->setTabText(0, "R");
                m_ui.tabWidget->setTabEnabled(1, true);
                m_ui.tabWidget->setTabEnabled(2, true);
            }

            m_ui.tabWidget->setTabEnabled(3, cSpaceStr == "RGBA");
        }
    }
    else
    {
        fromTarget = true;
    }

    if (fromTarget)
    {
        const std::uint32_t channels = targetChannelCount();
        if (channels >= 3u)
        {
            m_ui.useSingleRGBKExpression->setEnabled(true);

            if (m_ui.useSingleRGBKExpression->isChecked())
            {
                m_ui.tabWidget->setTabText(0, "RGB/K");
                m_ui.tabWidget->setTabEnabled(1, false);
                m_ui.tabWidget->setTabEnabled(2, false);
            }
            else
            {
                m_ui.tabWidget->setTabText(0, "R");
                m_ui.tabWidget->setTabEnabled(1, true);
                m_ui.tabWidget->setTabEnabled(2, true);
            }

            m_ui.tabWidget->setTabEnabled(3, channels == 4u);
        }
        else
        {
            m_ui.useSingleRGBKExpression->setChecked(true);
            m_ui.useSingleRGBKExpression->setEnabled(false);

            m_ui.tabWidget->setTabText(0, "RGB/K");
            m_ui.tabWidget->setTabEnabled(1, false);
            m_ui.tabWidget->setTabEnabled(2, false);

            m_ui.tabWidget->setTabEnabled(3, false);
        }
    }
}

void PixelMathProcess::executePixelMath()
{
    using namespace an::ui::processes;
    if (m_ExecutionInProgress || !m_image_view_control->hasImage())
    {
        return;
    }
    // The dialog and the images every view holds are read here, on the GUI thread; the run reads only these, and leases
    // the ones its expressions name before it is admitted.
    const PixelMathSettings asked = settings();
    const PixelMathImageSet images = imageSet();
    const std::uint64_t seed = drawRandomSeed();

    m_ExecutionInProgress = true;
    m_ui.buttonBox->button(QDialogButtonBox::Apply)->setEnabled(false);
    m_ui.buttonBox->button(QDialogButtonBox::Close)->setEnabled(false);

    LOG_INFO("Executing process '{}' on {} '{}' with random seed {}... ", getProcessName(),
             m_image_view_control->isSequence() ? "sequence" : "image", m_image_view_control->getUniqueName(), seed);

    // A sequence's frames are run in order, each drawing its random numbers from a seed of its own.
    auto frames = std::make_shared<std::atomic<std::uint64_t>>(0u);
    const auto runOnce = [asked, images, seed, frames](const an::ui::ExecutionScope& scope,
                                                       const an::ui::ProcessSource& source) {
        const std::uint64_t frame = frames->fetch_add(1u);
        return runPixelMath(scope, source, asked, images, pixelMathFrameSeed(seed, frame));
    };

    QPointer<PixelMathProcess> guard(this);
    const auto done = [guard](const GeometryRunOutcome& outcome) {
        if (guard)
        {
            guard->finishRun(outcome);
        }
    };
    if (asked.outputMode == PixelMathOutputMode::ReplaceTarget)
    {
        InPlaceRunRequest request;
        request.words = runWords(*m_image_view_control);
        request.stepName = kPixelMathStepName;
        request.stepDescription = std::string();
        request.operation = runOnce;
        request.reads = pixelMathImagesRead(asked, images);
        request.inFlight = beginRunInFlight();
        request.admissionHook = admissionHook();
        runInPlace(*m_image_view_control, std::move(request), done);
        return;
    }

    const std::string named = QString::fromStdString(asked.newImageId).trimmed().toStdString();
    NewViewsRunRequest request;
    request.words = runWords(*m_image_view_control);
    request.imageOutputs = {
        NewViewOutput{.name = named.empty() ? kPixelMathDefaultImageName : named, .history = kPixelMathImageHistory}};
    request.sequenceOutputs = {NewViewOutput{.name = named.empty() ? kPixelMathDefaultSequenceName : named,
                                             .history = kPixelMathSequenceHistory}};
    request.operation =
        [runOnce](const an::ui::ExecutionScope& scope,
                  const an::ui::ProcessSource& source) -> an::Result<std::vector<an::consumer::ImageAsset>> {
        an::Result<an::consumer::ImageAsset> made = runOnce(scope, source);
        if (!made.hasValue())
        {
            return made.status();
        }
        std::vector<an::consumer::ImageAsset> one;
        one.push_back(std::move(*made));
        return one;
    };
    request.readDemosaiced = m_image_view_control->isSequence() && m_image_view_control->isSequenceAutodebayerEnabled();
    request.showDemosaiced = false;
    request.reads = pixelMathImagesRead(asked, images);
    request.inFlight = beginRunInFlight();
    request.admissionHook = admissionHook();
    runToNewViews(*m_image_view_control, std::move(request), done);
}

void PixelMathProcess::finishRun(const an::ui::processes::GeometryRunOutcome& outcome)
{
    m_ExecutionInProgress = false;
    if (outcome.published)
    {
        LOG_INFO("Process '{}' executed successfully on '{}'", getProcessName(),
                 m_image_view_control != nullptr ? m_image_view_control->getUniqueName() : std::string());
    }
    if (m_ProcessCloseRequested)
    {
        close();
        return;
    }
    m_ui.buttonBox->button(QDialogButtonBox::Apply)->setEnabled(true);
    m_ui.buttonBox->button(QDialogButtonBox::Close)->setEnabled(true);
}

void PixelMathProcess::on_dialog_box_button_clicked(QAbstractButton* button)
{
    QDialogButtonBox* buttonBox = qobject_cast<QDialogButtonBox*>(sender());
    if (!buttonBox)
        return;

    QDialogButtonBox::StandardButton sb = buttonBox->standardButton(button);

    if (sb == QDialogButtonBox::Apply)
    {
        executePixelMath();
    }
    else if (sb == QDialogButtonBox::Close)
    {
        this->close();
    }
}
