// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include <QtCore/QVariant>
#include <QtGui/QAction>
#include <QtWidgets/QApplication>
#include <QtWidgets/QMainWindow>
#include <QtWidgets/QMenu>
#include <QtWidgets/QMenuBar>
#include <QtWidgets/QStackedWidget>
#include <QtWidgets/QStatusBar>
#include <QtWidgets/QToolBar>

namespace an::ui::app
{

class AfterNightAppUiElements
{
public:
    QAction* actionOpen;
    QAction* actionSave_as;
    QAction* actionExit;
    QAction* actionAbout_Qt;
    QAction* actionAbout_AfterNight;
    QAction* actionSystem_Information;
    QAction* actionUser_Guide;
    QAction* actionUndo;
    QAction* actionRedo;
    QAction* actionClose;
    QAction* actionClose_All;
    QAction* actionDuplicate;
    QAction* actionRename;
    QAction* actionShow_In_File_Browser;
    QAction* actionExtract_Red_Channel;
    QAction* actionExtract_Green_Channel;
    QAction* actionExtract_Blue_Channel;
    QAction* actionExtract_RGB_Channels;
    QAction* actionConvert_to_8_bit;
    QAction* actionConvert_to_signed_16_bit;
    QAction* actionConvert_to_unsigned_16_bit;
    QAction* actionConvert_to_signed_32_bit;
    QAction* actionConvert_to_unsigned_32_bit;
    QAction* actionConvert_to_32_bit_float;
    QAction* actionConvert_to_64_bit_float;
    QAction* action;
    QAction* actionHorizontal_Mirror;
    QAction* actionVertical_Mirror;
    QAction* actionArcSinh_Stretch;
    QAction* actionRotate_180;
    QAction* actionRotate_90_Left;
    QAction* actionRotate_90_Right;
    QAction* actionStarnet;
    QAction* actionStar_Reduction;
    QAction* actionAutomatic_MTF_Transform;
    QAction* actionExtract_CIE_L;
    QAction* actionExtract_CIE_a;
    QAction* actionExtract_CIE_b;
    QAction* actionExtract_CIE_a_b;
    QAction* actionExtract_CIE_L_a_b;
    QAction* actionExtract_CIE_L_c_h;
    QAction* actionExtract_CIE_c;
    QAction* actionExtract_CIE_h;
    QAction* actionExtract_CIE_X;
    QAction* actionExtract_CIE_Y;
    QAction* actionExtract_CIE_Z;
    QAction* actionExtract_CIE_X_Y_Z;
    QAction* actionExtract_HSL_Hue;
    QAction* actionExtract_HSL_Saturation;
    QAction* actionExtract_HSL_Lightness;
    QAction* actionExtract_HSL;
    QAction* actionExtract_HSV_Hue;
    QAction* actionExtract_HSV_Saturation;
    QAction* actionExtract_HSV_Value;
    QAction* actionExtract_HSV;
    QAction* actionExtract_HSI_Hue;
    QAction* actionExtract_HSI_Saturation;
    QAction* actionExtract_HSI_Intensity;
    QAction* actionExtract_HSI;
    QAction* actionNegative_Transformation;
    QAction* actionImage_History;
    QAction* actionasd;
    QAction* actionasd_2;
    QAction* actionStatistics;
    QAction* actionHistogram;
    QAction* actionImage_EXIFViewer;
    QAction* actionFITSHeader;
    QAction* actionAstrometryInfo;
    QAction* actionPlateSolve;
    QAction* actionAdjustBrightness_Contrast;
    QAction* actionGamma_Correction;
    QAction* actionAdjustHSL;
    QAction* actionStarmask_Recombination;
    QAction* actionSCNR;
    QAction* actionMedian_Filter;
    QAction* actionClose_2;
    QAction* actionZoom_In;
    QAction* actionZoom_Out;
    QAction* actionZoom_100;
    QAction* actionFit_To_Window;
    QAction* actionClose_All_Others;
    QAction* actionChannelComposition;
    QAction* actionDebayer;
    QAction* actionConvert_to_Grayscale;
    QAction* actionConvert_to_RGB;
    QAction* actionSummary;
    QAction* actionResize;
    QAction* actionCrop;
    QAction* actionRotate;
    QAction* actionHistogram_Transformation;
    QAction* actionColor_Calibration_2;
    QAction* actionPhotometric_Color_Calibration;
    QAction* actionSpectrophotometric_Color_Calibration;
    QAction* actionBackground_Neutralization;
    QAction* actionMask_Drawing;
    QAction* actionRange_Selection;
    QAction* actionSelect_Mask;
    QAction* actionRemove_Mask;
    QAction* actionInvert_Mask;
    QAction* actionEnable_Mask;
    QAction* actionShow_Mask;
    QAction* actionOverlay_Red;
    QAction* actionOverlay_Green;
    QAction* actionOverlay_Blue;
    QAction* actionOverlay_Yellow;
    QAction* actionOverlay_Magenta;
    QAction* actionOverlay_Cyan;
    QAction* actionReplace;
    QAction* actionMultiply;
    QAction* actionIntegerResample;
    QAction* actionCLA_Histogram_Equalization;
    QAction* actionDeepSNR;
    QAction* actionNon_Local_Bayes_Filter;
    QAction* actionBackground_Extraction;
    QAction* actionAuto_Gradient_Removal;
    QAction* actionWavelet_Denoise_Filter;
    QAction* actionPatch_PCA_Filter;
    QAction* actionLocal_Histogram_Equalization;
    QAction* actionLocal_Contrast_Enhancement;
    QAction* actionDark_Structure_Enhance;
    QAction* actionLinear_Match;
    QAction* actionCurves_Transformation;
    QAction* actionExtract_YCbCr_Luminance;
    QAction* actionExtract_YCbCr_Blue_Chrominance;
    QAction* actionExtract_YCbCr_Red_Chrominance;
    QAction* actionExtract_YCbCr;
    QAction* actionMultiscale_Linear_Transform;
    QAction* actionMultiscale_Alpha_Sharpen;
    QAction* actionStar_Mask;
    QAction* actionStarnet_Star_Mask;
    QAction* actionGaussian_Blur;
    QAction* actionHigh_Pass;
    QAction* actionFilters;
    QAction* actionMorphological_Transformation;
    QAction* actionGRAYCstoration_Filter;
    QAction* actionAdjustLightness_Chroma_Hue;
    QAction* actionExposure;
    QAction* actionCosmetic_Correction_Filter;
    QAction* actionBanding_Reduction_Filter;
    QAction* actionStar_Profiler_v2;
    QAction* actionTighten_Edges;
    QAction* actionProfiled_Starmask_Recombination;
    QAction* actionStar_Profiler_v1;
    QAction* actionLock_Layout;
    QAction* actionUnlock_Layout;
    QAction* actionFit_Images_To_Window;
    QAction* actionGroup_All_Images;
    QAction* actionSplit_All_Images;
    QAction* actionUnsharp_Mask;
    QAction* actionNoise_Generator;
    QAction* actionTotal_Variation_Denoise_Filter;
    QAction* actionImage_Blend;
    QAction* actionImage_Compare;
    QAction* actionSplit_CFA;
    QAction* actionMerge_CFA;
    QAction* actionGeneralized_Hyperbolic_Transformation;
    QAction* actionColor_Saturation;
    QAction* actionBilateral_Filter;
    QAction* actionLog_Viewer;
    QAction* actionExtract_Frame;
    QAction* actionExtract_Sequence;
    QAction* actionNew_Sequence;
    QAction* actionConvert_to_SER;
    QAction* actionConvert_to_Image_List;
    QAction* actionConvert_to_FITS;
    QAction* actionCFA_Integer_Resample;
    QAction* actionMaximize_View;
    QAction* actionPixel_Math_2;
    QAction* actionBinarize;
    QAction* actionImage_Calibration;
    QAction* actionImage_Registration;
    QAction* actionImage_Stacking;
    QAction* actionImage_Integration;
    QAction* actionImage_Integration_Lite;
    QAction* actionStatistical_Stretch;
    QAction* actionLRGB_Combination;
    QAction* actionNBRGB_Combination;
    QAction* actionNBColorMapper;
    QAction* actionNarrowbandPaletteComposer;
    QAction* actionSave_File;
    QAction* actionImage_List_Analyzer;
    QAction* actionExport;
    QAction* actionColorize;
    QAction* actionSettings;
    QAction* actionManage_Extensions;
    QStackedWidget* centralWidget;
    QMenuBar* menuBar;
    QMenu* menuFile;
    QMenu* menuOpen_Recent;
    QMenu* menuEdit;
    QMenu* menuHelp;
    QMenu* menuAbout;
    QMenu* menuImage;
    QMenu* menuActions;
    QMenu* menuView;
    QMenu* menuExtract_Channel;
    QMenu* menuConvert_Format;
    QMenu* menuGeometry;
    QMenu* menuInformation;
    QMenu* menuMask;
    QMenu* menuRendering_Mode;
    QMenu* menuSequence;
    QMenu* menuConvert;
    QMenu* menuProcess;
    QMenu* menuFavorite_Processes;
    QMenu* menuRecent_Processes;
    QMenu* menuIntensity_Transformations;
    QMenu* menuStar_Processing;
    QMenu* menuNoise_Reduction;
    QMenu* menuColor_Spaces;
    QMenu* menuGeometry_2;
    QMenu* menuColor_Calibration;
    QMenu* menuMask_Generation;
    QMenu* menuBackground_Modelization;
    QMenu* menuDetails;
    QMenu* menuFilters;
    QMenu* menuMultiscale;
    QMenu* menuMorphology;
    QMenu* menuAdjustments;
    QMenu* menuUtilities;
    QMenu* menuPixel_Math;
    QMenu* menuPreprocess;
    QMenu* menuExtensions;
    QMenu* menuWindow;
    QMenu* menuView_2;
    QToolBar* mainToolBar;
    QStatusBar* statusBar;

    void setupUi(QMainWindow* AfterNightAppClass)
    {
        if (AfterNightAppClass->objectName().isEmpty())
            AfterNightAppClass->setObjectName("AfterNightAppClass");
        AfterNightAppClass->resize(1267, 950);
        actionOpen = new QAction(AfterNightAppClass);
        actionOpen->setObjectName("actionOpen");
        actionSave_as = new QAction(AfterNightAppClass);
        actionSave_as->setObjectName("actionSave_as");
        actionExit = new QAction(AfterNightAppClass);
        actionExit->setObjectName("actionExit");
        actionAbout_Qt = new QAction(AfterNightAppClass);
        actionAbout_Qt->setObjectName("actionAbout_Qt");
        actionAbout_AfterNight = new QAction(AfterNightAppClass);
        actionAbout_AfterNight->setObjectName("actionAbout_AfterNight");
        actionSystem_Information = new QAction(AfterNightAppClass);
        actionSystem_Information->setObjectName("actionSystem_Information");
        actionUser_Guide = new QAction(AfterNightAppClass);
        actionUser_Guide->setObjectName("actionUser_Guide");
        actionUndo = new QAction(AfterNightAppClass);
        actionUndo->setObjectName("actionUndo");
        actionUndo->setEnabled(false);
        actionRedo = new QAction(AfterNightAppClass);
        actionRedo->setObjectName("actionRedo");
        actionRedo->setEnabled(false);
        actionClose = new QAction(AfterNightAppClass);
        actionClose->setObjectName("actionClose");
        actionClose_All = new QAction(AfterNightAppClass);
        actionClose_All->setObjectName("actionClose_All");
        actionDuplicate = new QAction(AfterNightAppClass);
        actionDuplicate->setObjectName("actionDuplicate");
        actionRename = new QAction(AfterNightAppClass);
        actionRename->setObjectName("actionRename");
        actionShow_In_File_Browser = new QAction(AfterNightAppClass);
        actionShow_In_File_Browser->setObjectName("actionShow_In_File_Browser");
        actionShow_In_File_Browser->setEnabled(false);
        actionExtract_Red_Channel = new QAction(AfterNightAppClass);
        actionExtract_Red_Channel->setObjectName("actionExtract_Red_Channel");
        actionExtract_Green_Channel = new QAction(AfterNightAppClass);
        actionExtract_Green_Channel->setObjectName("actionExtract_Green_Channel");
        actionExtract_Blue_Channel = new QAction(AfterNightAppClass);
        actionExtract_Blue_Channel->setObjectName("actionExtract_Blue_Channel");
        actionExtract_RGB_Channels = new QAction(AfterNightAppClass);
        actionExtract_RGB_Channels->setObjectName("actionExtract_RGB_Channels");
        actionConvert_to_8_bit = new QAction(AfterNightAppClass);
        actionConvert_to_8_bit->setObjectName("actionConvert_to_8_bit");
        actionConvert_to_signed_16_bit = new QAction(AfterNightAppClass);
        actionConvert_to_signed_16_bit->setObjectName("actionConvert_to_signed_16_bit");
        actionConvert_to_unsigned_16_bit = new QAction(AfterNightAppClass);
        actionConvert_to_unsigned_16_bit->setObjectName("actionConvert_to_unsigned_16_bit");
        actionConvert_to_signed_32_bit = new QAction(AfterNightAppClass);
        actionConvert_to_signed_32_bit->setObjectName("actionConvert_to_signed_32_bit");
        actionConvert_to_unsigned_32_bit = new QAction(AfterNightAppClass);
        actionConvert_to_unsigned_32_bit->setObjectName("actionConvert_to_unsigned_32_bit");
        actionConvert_to_32_bit_float = new QAction(AfterNightAppClass);
        actionConvert_to_32_bit_float->setObjectName("actionConvert_to_32_bit_float");
        actionConvert_to_64_bit_float = new QAction(AfterNightAppClass);
        actionConvert_to_64_bit_float->setObjectName("actionConvert_to_64_bit_float");
        action = new QAction(AfterNightAppClass);
        action->setObjectName("action");
        actionHorizontal_Mirror = new QAction(AfterNightAppClass);
        actionHorizontal_Mirror->setObjectName("actionHorizontal_Mirror");
        actionVertical_Mirror = new QAction(AfterNightAppClass);
        actionVertical_Mirror->setObjectName("actionVertical_Mirror");
        actionArcSinh_Stretch = new QAction(AfterNightAppClass);
        actionArcSinh_Stretch->setObjectName("actionArcSinh_Stretch");
        actionRotate_180 = new QAction(AfterNightAppClass);
        actionRotate_180->setObjectName("actionRotate_180");
        actionRotate_90_Left = new QAction(AfterNightAppClass);
        actionRotate_90_Left->setObjectName("actionRotate_90_Left");
        actionRotate_90_Right = new QAction(AfterNightAppClass);
        actionRotate_90_Right->setObjectName("actionRotate_90_Right");
        actionStarnet = new QAction(AfterNightAppClass);
        actionStarnet->setObjectName("actionStarnet");
        actionStar_Reduction = new QAction(AfterNightAppClass);
        actionStar_Reduction->setObjectName("actionStar_Reduction");
        actionAutomatic_MTF_Transform = new QAction(AfterNightAppClass);
        actionAutomatic_MTF_Transform->setObjectName("actionAutomatic_MTF_Transform");
        actionExtract_CIE_L = new QAction(AfterNightAppClass);
        actionExtract_CIE_L->setObjectName("actionExtract_CIE_L");
        actionExtract_CIE_a = new QAction(AfterNightAppClass);
        actionExtract_CIE_a->setObjectName("actionExtract_CIE_a");
        actionExtract_CIE_b = new QAction(AfterNightAppClass);
        actionExtract_CIE_b->setObjectName("actionExtract_CIE_b");
        actionExtract_CIE_a_b = new QAction(AfterNightAppClass);
        actionExtract_CIE_a_b->setObjectName("actionExtract_CIE_a_b");
        actionExtract_CIE_L_a_b = new QAction(AfterNightAppClass);
        actionExtract_CIE_L_a_b->setObjectName("actionExtract_CIE_L_a_b");
        actionExtract_CIE_L_c_h = new QAction(AfterNightAppClass);
        actionExtract_CIE_L_c_h->setObjectName("actionExtract_CIE_L_c_h");
        actionExtract_CIE_c = new QAction(AfterNightAppClass);
        actionExtract_CIE_c->setObjectName("actionExtract_CIE_c");
        actionExtract_CIE_h = new QAction(AfterNightAppClass);
        actionExtract_CIE_h->setObjectName("actionExtract_CIE_h");
        actionExtract_CIE_X = new QAction(AfterNightAppClass);
        actionExtract_CIE_X->setObjectName("actionExtract_CIE_X");
        actionExtract_CIE_Y = new QAction(AfterNightAppClass);
        actionExtract_CIE_Y->setObjectName("actionExtract_CIE_Y");
        actionExtract_CIE_Z = new QAction(AfterNightAppClass);
        actionExtract_CIE_Z->setObjectName("actionExtract_CIE_Z");
        actionExtract_CIE_X_Y_Z = new QAction(AfterNightAppClass);
        actionExtract_CIE_X_Y_Z->setObjectName("actionExtract_CIE_X_Y_Z");
        actionExtract_HSL_Hue = new QAction(AfterNightAppClass);
        actionExtract_HSL_Hue->setObjectName("actionExtract_HSL_Hue");
        actionExtract_HSL_Saturation = new QAction(AfterNightAppClass);
        actionExtract_HSL_Saturation->setObjectName("actionExtract_HSL_Saturation");
        actionExtract_HSL_Lightness = new QAction(AfterNightAppClass);
        actionExtract_HSL_Lightness->setObjectName("actionExtract_HSL_Lightness");
        actionExtract_HSL = new QAction(AfterNightAppClass);
        actionExtract_HSL->setObjectName("actionExtract_HSL");
        actionExtract_HSV_Hue = new QAction(AfterNightAppClass);
        actionExtract_HSV_Hue->setObjectName("actionExtract_HSV_Hue");
        actionExtract_HSV_Saturation = new QAction(AfterNightAppClass);
        actionExtract_HSV_Saturation->setObjectName("actionExtract_HSV_Saturation");
        actionExtract_HSV_Value = new QAction(AfterNightAppClass);
        actionExtract_HSV_Value->setObjectName("actionExtract_HSV_Value");
        actionExtract_HSV = new QAction(AfterNightAppClass);
        actionExtract_HSV->setObjectName("actionExtract_HSV");
        actionExtract_HSI_Hue = new QAction(AfterNightAppClass);
        actionExtract_HSI_Hue->setObjectName("actionExtract_HSI_Hue");
        actionExtract_HSI_Saturation = new QAction(AfterNightAppClass);
        actionExtract_HSI_Saturation->setObjectName("actionExtract_HSI_Saturation");
        actionExtract_HSI_Intensity = new QAction(AfterNightAppClass);
        actionExtract_HSI_Intensity->setObjectName("actionExtract_HSI_Intensity");
        actionExtract_HSI = new QAction(AfterNightAppClass);
        actionExtract_HSI->setObjectName("actionExtract_HSI");
        actionNegative_Transformation = new QAction(AfterNightAppClass);
        actionNegative_Transformation->setObjectName("actionNegative_Transformation");
        actionImage_History = new QAction(AfterNightAppClass);
        actionImage_History->setObjectName("actionImage_History");
        actionImage_History->setEnabled(false);
        actionasd = new QAction(AfterNightAppClass);
        actionasd->setObjectName("actionasd");
        actionasd_2 = new QAction(AfterNightAppClass);
        actionasd_2->setObjectName("actionasd_2");
        actionStatistics = new QAction(AfterNightAppClass);
        actionStatistics->setObjectName("actionStatistics");
        actionStatistics->setEnabled(false);
        actionHistogram = new QAction(AfterNightAppClass);
        actionHistogram->setObjectName("actionHistogram");
        actionHistogram->setEnabled(false);
        actionImage_EXIFViewer = new QAction(AfterNightAppClass);
        actionImage_EXIFViewer->setObjectName("actionImage_EXIFViewer");
        actionImage_EXIFViewer->setEnabled(false);
        actionFITSHeader = new QAction(AfterNightAppClass);
        actionFITSHeader->setObjectName("actionFITSHeader");
        actionFITSHeader->setEnabled(false);
        actionAstrometryInfo = new QAction(AfterNightAppClass);
        actionAstrometryInfo->setObjectName("actionAstrometryInfo");
        actionAstrometryInfo->setEnabled(false);
        actionPlateSolve = new QAction(AfterNightAppClass);
        actionPlateSolve->setObjectName("actionPlateSolve");
        actionPlateSolve->setEnabled(false);
        actionAdjustBrightness_Contrast = new QAction(AfterNightAppClass);
        actionAdjustBrightness_Contrast->setObjectName("actionAdjustBrightness_Contrast");
        actionAdjustBrightness_Contrast->setEnabled(true);
        actionGamma_Correction = new QAction(AfterNightAppClass);
        actionGamma_Correction->setObjectName("actionGamma_Correction");
        actionGamma_Correction->setEnabled(true);
        actionAdjustHSL = new QAction(AfterNightAppClass);
        actionAdjustHSL->setObjectName("actionAdjustHSL");
        actionStarmask_Recombination = new QAction(AfterNightAppClass);
        actionStarmask_Recombination->setObjectName("actionStarmask_Recombination");
        actionSCNR = new QAction(AfterNightAppClass);
        actionSCNR->setObjectName("actionSCNR");
        actionMedian_Filter = new QAction(AfterNightAppClass);
        actionMedian_Filter->setObjectName("actionMedian_Filter");
        actionClose_2 = new QAction(AfterNightAppClass);
        actionClose_2->setObjectName("actionClose_2");
        actionZoom_In = new QAction(AfterNightAppClass);
        actionZoom_In->setObjectName("actionZoom_In");
        actionZoom_Out = new QAction(AfterNightAppClass);
        actionZoom_Out->setObjectName("actionZoom_Out");
        actionZoom_100 = new QAction(AfterNightAppClass);
        actionZoom_100->setObjectName("actionZoom_100");
        actionFit_To_Window = new QAction(AfterNightAppClass);
        actionFit_To_Window->setObjectName("actionFit_To_Window");
        actionClose_All_Others = new QAction(AfterNightAppClass);
        actionClose_All_Others->setObjectName("actionClose_All_Others");
        actionChannelComposition = new QAction(AfterNightAppClass);
        actionChannelComposition->setObjectName("actionChannelComposition");
        actionDebayer = new QAction(AfterNightAppClass);
        actionDebayer->setObjectName("actionDebayer");
        actionConvert_to_Grayscale = new QAction(AfterNightAppClass);
        actionConvert_to_Grayscale->setObjectName("actionConvert_to_Grayscale");
        actionConvert_to_RGB = new QAction(AfterNightAppClass);
        actionConvert_to_RGB->setObjectName("actionConvert_to_RGB");
        actionSummary = new QAction(AfterNightAppClass);
        actionSummary->setObjectName("actionSummary");
        actionResize = new QAction(AfterNightAppClass);
        actionResize->setObjectName("actionResize");
        actionCrop = new QAction(AfterNightAppClass);
        actionCrop->setObjectName("actionCrop");
        actionRotate = new QAction(AfterNightAppClass);
        actionRotate->setObjectName("actionRotate");
        actionRotate->setEnabled(true);
        actionHistogram_Transformation = new QAction(AfterNightAppClass);
        actionHistogram_Transformation->setObjectName("actionHistogram_Transformation");
        actionColor_Calibration_2 = new QAction(AfterNightAppClass);
        actionColor_Calibration_2->setObjectName("actionColor_Calibration_2");
        actionPhotometric_Color_Calibration = new QAction(AfterNightAppClass);
        actionPhotometric_Color_Calibration->setObjectName("actionPhotometric_Color_Calibration");
        actionSpectrophotometric_Color_Calibration = new QAction(AfterNightAppClass);
        actionSpectrophotometric_Color_Calibration->setObjectName("actionSpectrophotometric_Color_Calibration");
        actionBackground_Neutralization = new QAction(AfterNightAppClass);
        actionBackground_Neutralization->setObjectName("actionBackground_Neutralization");
        actionMask_Drawing = new QAction(AfterNightAppClass);
        actionMask_Drawing->setObjectName("actionMask_Drawing");
        actionRange_Selection = new QAction(AfterNightAppClass);
        actionRange_Selection->setObjectName("actionRange_Selection");
        actionSelect_Mask = new QAction(AfterNightAppClass);
        actionSelect_Mask->setObjectName("actionSelect_Mask");
        actionRemove_Mask = new QAction(AfterNightAppClass);
        actionRemove_Mask->setObjectName("actionRemove_Mask");
        actionInvert_Mask = new QAction(AfterNightAppClass);
        actionInvert_Mask->setObjectName("actionInvert_Mask");
        actionInvert_Mask->setCheckable(true);
        actionEnable_Mask = new QAction(AfterNightAppClass);
        actionEnable_Mask->setObjectName("actionEnable_Mask");
        actionEnable_Mask->setCheckable(true);
        actionShow_Mask = new QAction(AfterNightAppClass);
        actionShow_Mask->setObjectName("actionShow_Mask");
        actionShow_Mask->setCheckable(true);
        actionOverlay_Red = new QAction(AfterNightAppClass);
        actionOverlay_Red->setObjectName("actionOverlay_Red");
        actionOverlay_Red->setCheckable(true);
        actionOverlay_Red->setChecked(true);
        actionOverlay_Green = new QAction(AfterNightAppClass);
        actionOverlay_Green->setObjectName("actionOverlay_Green");
        actionOverlay_Green->setCheckable(true);
        actionOverlay_Blue = new QAction(AfterNightAppClass);
        actionOverlay_Blue->setObjectName("actionOverlay_Blue");
        actionOverlay_Blue->setCheckable(true);
        actionOverlay_Yellow = new QAction(AfterNightAppClass);
        actionOverlay_Yellow->setObjectName("actionOverlay_Yellow");
        actionOverlay_Yellow->setCheckable(true);
        actionOverlay_Magenta = new QAction(AfterNightAppClass);
        actionOverlay_Magenta->setObjectName("actionOverlay_Magenta");
        actionOverlay_Magenta->setCheckable(true);
        actionOverlay_Cyan = new QAction(AfterNightAppClass);
        actionOverlay_Cyan->setObjectName("actionOverlay_Cyan");
        actionOverlay_Cyan->setCheckable(true);
        actionReplace = new QAction(AfterNightAppClass);
        actionReplace->setObjectName("actionReplace");
        actionReplace->setCheckable(true);
        actionMultiply = new QAction(AfterNightAppClass);
        actionMultiply->setObjectName("actionMultiply");
        actionMultiply->setCheckable(true);
        actionIntegerResample = new QAction(AfterNightAppClass);
        actionIntegerResample->setObjectName("actionIntegerResample");
        actionCLA_Histogram_Equalization = new QAction(AfterNightAppClass);
        actionCLA_Histogram_Equalization->setObjectName("actionCLA_Histogram_Equalization");
        actionDeepSNR = new QAction(AfterNightAppClass);
        actionDeepSNR->setObjectName("actionDeepSNR");
        actionNon_Local_Bayes_Filter = new QAction(AfterNightAppClass);
        actionNon_Local_Bayes_Filter->setObjectName("actionNon_Local_Bayes_Filter");
        actionBackground_Extraction = new QAction(AfterNightAppClass);
        actionBackground_Extraction->setObjectName("actionBackground_Extraction");
        actionAuto_Gradient_Removal = new QAction(AfterNightAppClass);
        actionAuto_Gradient_Removal->setObjectName("actionAuto_Gradient_Removal");
        actionWavelet_Denoise_Filter = new QAction(AfterNightAppClass);
        actionWavelet_Denoise_Filter->setObjectName("actionWavelet_Denoise_Filter");
        actionPatch_PCA_Filter = new QAction(AfterNightAppClass);
        actionPatch_PCA_Filter->setObjectName("actionPatch_PCA_Filter");
        actionLocal_Histogram_Equalization = new QAction(AfterNightAppClass);
        actionLocal_Histogram_Equalization->setObjectName("actionLocal_Histogram_Equalization");
        actionLocal_Contrast_Enhancement = new QAction(AfterNightAppClass);
        actionLocal_Contrast_Enhancement->setObjectName("actionLocal_Contrast_Enhancement");
        actionDark_Structure_Enhance = new QAction(AfterNightAppClass);
        actionDark_Structure_Enhance->setObjectName("actionDark_Structure_Enhance");
        actionLinear_Match = new QAction(AfterNightAppClass);
        actionLinear_Match->setObjectName("actionLinear_Match");
        actionCurves_Transformation = new QAction(AfterNightAppClass);
        actionCurves_Transformation->setObjectName("actionCurves_Transformation");
        actionExtract_YCbCr_Luminance = new QAction(AfterNightAppClass);
        actionExtract_YCbCr_Luminance->setObjectName("actionExtract_YCbCr_Luminance");
        actionExtract_YCbCr_Blue_Chrominance = new QAction(AfterNightAppClass);
        actionExtract_YCbCr_Blue_Chrominance->setObjectName("actionExtract_YCbCr_Blue_Chrominance");
        actionExtract_YCbCr_Red_Chrominance = new QAction(AfterNightAppClass);
        actionExtract_YCbCr_Red_Chrominance->setObjectName("actionExtract_YCbCr_Red_Chrominance");
        actionExtract_YCbCr = new QAction(AfterNightAppClass);
        actionExtract_YCbCr->setObjectName("actionExtract_YCbCr");
        actionMultiscale_Linear_Transform = new QAction(AfterNightAppClass);
        actionMultiscale_Linear_Transform->setObjectName("actionMultiscale_Linear_Transform");
        actionMultiscale_Alpha_Sharpen = new QAction(AfterNightAppClass);
        actionMultiscale_Alpha_Sharpen->setObjectName("actionMultiscale_Alpha_Sharpen");
        actionStar_Mask = new QAction(AfterNightAppClass);
        actionStar_Mask->setObjectName("actionStar_Mask");
        actionStarnet_Star_Mask = new QAction(AfterNightAppClass);
        actionStarnet_Star_Mask->setObjectName("actionStarnet_Star_Mask");
        actionGaussian_Blur = new QAction(AfterNightAppClass);
        actionGaussian_Blur->setObjectName("actionGaussian_Blur");
        actionHigh_Pass = new QAction(AfterNightAppClass);
        actionHigh_Pass->setObjectName("actionHigh_Pass");
        actionFilters = new QAction(AfterNightAppClass);
        actionFilters->setObjectName("actionFilters");
        actionMorphological_Transformation = new QAction(AfterNightAppClass);
        actionMorphological_Transformation->setObjectName("actionMorphological_Transformation");
        actionGRAYCstoration_Filter = new QAction(AfterNightAppClass);
        actionGRAYCstoration_Filter->setObjectName("actionGRAYCstoration_Filter");
        actionAdjustLightness_Chroma_Hue = new QAction(AfterNightAppClass);
        actionAdjustLightness_Chroma_Hue->setObjectName("actionAdjustLightness_Chroma_Hue");
        actionExposure = new QAction(AfterNightAppClass);
        actionExposure->setObjectName("actionExposure");
        actionCosmetic_Correction_Filter = new QAction(AfterNightAppClass);
        actionCosmetic_Correction_Filter->setObjectName("actionCosmetic_Correction_Filter");
        actionBanding_Reduction_Filter = new QAction(AfterNightAppClass);
        actionBanding_Reduction_Filter->setObjectName("actionBanding_Reduction_Filter");
        actionStar_Profiler_v2 = new QAction(AfterNightAppClass);
        actionStar_Profiler_v2->setObjectName("actionStar_Profiler_v2");
        actionTighten_Edges = new QAction(AfterNightAppClass);
        actionTighten_Edges->setObjectName("actionTighten_Edges");
        actionProfiled_Starmask_Recombination = new QAction(AfterNightAppClass);
        actionProfiled_Starmask_Recombination->setObjectName("actionProfiled_Starmask_Recombination");
        actionStar_Profiler_v1 = new QAction(AfterNightAppClass);
        actionStar_Profiler_v1->setObjectName("actionStar_Profiler_v1");
        actionLock_Layout = new QAction(AfterNightAppClass);
        actionLock_Layout->setObjectName("actionLock_Layout");
        actionLock_Layout->setEnabled(true);
        actionUnlock_Layout = new QAction(AfterNightAppClass);
        actionUnlock_Layout->setObjectName("actionUnlock_Layout");
        actionFit_Images_To_Window = new QAction(AfterNightAppClass);
        actionFit_Images_To_Window->setObjectName("actionFit_Images_To_Window");
        actionGroup_All_Images = new QAction(AfterNightAppClass);
        actionGroup_All_Images->setObjectName("actionGroup_All_Images");
        actionSplit_All_Images = new QAction(AfterNightAppClass);
        actionSplit_All_Images->setObjectName("actionSplit_All_Images");
        actionUnsharp_Mask = new QAction(AfterNightAppClass);
        actionUnsharp_Mask->setObjectName("actionUnsharp_Mask");
        actionNoise_Generator = new QAction(AfterNightAppClass);
        actionNoise_Generator->setObjectName("actionNoise_Generator");
        actionTotal_Variation_Denoise_Filter = new QAction(AfterNightAppClass);
        actionTotal_Variation_Denoise_Filter->setObjectName("actionTotal_Variation_Denoise_Filter");
        actionImage_Blend = new QAction(AfterNightAppClass);
        actionImage_Blend->setObjectName("actionImage_Blend");
        actionImage_Compare = new QAction(AfterNightAppClass);
        actionImage_Compare->setObjectName("actionImage_Compare");
        actionSplit_CFA = new QAction(AfterNightAppClass);
        actionSplit_CFA->setObjectName("actionSplit_CFA");
        actionMerge_CFA = new QAction(AfterNightAppClass);
        actionMerge_CFA->setObjectName("actionMerge_CFA");
        actionGeneralized_Hyperbolic_Transformation = new QAction(AfterNightAppClass);
        actionGeneralized_Hyperbolic_Transformation->setObjectName("actionGeneralized_Hyperbolic_Transformation");
        actionColor_Saturation = new QAction(AfterNightAppClass);
        actionColor_Saturation->setObjectName("actionColor_Saturation");
        actionBilateral_Filter = new QAction(AfterNightAppClass);
        actionBilateral_Filter->setObjectName("actionBilateral_Filter");
        actionLog_Viewer = new QAction(AfterNightAppClass);
        actionLog_Viewer->setObjectName("actionLog_Viewer");
        actionLog_Viewer->setCheckable(true);
        actionExtract_Frame = new QAction(AfterNightAppClass);
        actionExtract_Frame->setObjectName("actionExtract_Frame");
        actionExtract_Sequence = new QAction(AfterNightAppClass);
        actionExtract_Sequence->setObjectName("actionExtract_Sequence");
        actionNew_Sequence = new QAction(AfterNightAppClass);
        actionNew_Sequence->setObjectName("actionNew_Sequence");
        actionConvert_to_SER = new QAction(AfterNightAppClass);
        actionConvert_to_SER->setObjectName("actionConvert_to_SER");
        actionConvert_to_Image_List = new QAction(AfterNightAppClass);
        actionConvert_to_Image_List->setObjectName("actionConvert_to_Image_List");
        actionConvert_to_FITS = new QAction(AfterNightAppClass);
        actionConvert_to_FITS->setObjectName("actionConvert_to_FITS");
        actionCFA_Integer_Resample = new QAction(AfterNightAppClass);
        actionCFA_Integer_Resample->setObjectName("actionCFA_Integer_Resample");
        actionMaximize_View = new QAction(AfterNightAppClass);
        actionMaximize_View->setObjectName("actionMaximize_View");
        actionPixel_Math_2 = new QAction(AfterNightAppClass);
        actionPixel_Math_2->setObjectName("actionPixel_Math_2");
        actionBinarize = new QAction(AfterNightAppClass);
        actionBinarize->setObjectName("actionBinarize");
        actionImage_Calibration = new QAction(AfterNightAppClass);
        actionImage_Calibration->setObjectName("actionImage_Calibration");
        actionImage_Registration = new QAction(AfterNightAppClass);
        actionImage_Registration->setObjectName("actionImage_Registration");
        actionImage_Stacking = new QAction(AfterNightAppClass);
        actionImage_Stacking->setObjectName("actionImage_Stacking");
        actionImage_Integration = new QAction(AfterNightAppClass);
        actionImage_Integration->setObjectName("actionImage_Integration");
        actionImage_Integration_Lite = new QAction(AfterNightAppClass);
        actionImage_Integration_Lite->setObjectName("actionImage_Integration_Lite");
        actionStatistical_Stretch = new QAction(AfterNightAppClass);
        actionStatistical_Stretch->setObjectName("actionStatistical_Stretch");
        actionLRGB_Combination = new QAction(AfterNightAppClass);
        actionLRGB_Combination->setObjectName("actionLRGB_Combination");
        actionNBRGB_Combination = new QAction(AfterNightAppClass);
        actionNBRGB_Combination->setObjectName("actionNBRGB_Combination");
        actionNBColorMapper = new QAction(AfterNightAppClass);
        actionNBColorMapper->setObjectName("actionNBColorMapper");
        actionNarrowbandPaletteComposer = new QAction(AfterNightAppClass);
        actionNarrowbandPaletteComposer->setObjectName("actionNarrowbandPaletteComposer");
        actionSave_File = new QAction(AfterNightAppClass);
        actionSave_File->setObjectName("actionSave_File");
        actionImage_List_Analyzer = new QAction(AfterNightAppClass);
        actionImage_List_Analyzer->setObjectName("actionImage_List_Analyzer");
        actionExport = new QAction(AfterNightAppClass);
        actionExport->setObjectName("actionExport");
        actionColorize = new QAction(AfterNightAppClass);
        actionColorize->setObjectName("actionColorize");
        actionSettings = new QAction(AfterNightAppClass);
        actionSettings->setObjectName("actionSettings");
        actionManage_Extensions = new QAction(AfterNightAppClass);
        actionManage_Extensions->setObjectName("actionManage_Extensions");
        centralWidget = new QStackedWidget(AfterNightAppClass);
        centralWidget->setObjectName("centralWidget");
        centralWidget->setAutoFillBackground(true);
        centralWidget->setContentsMargins(0, 0, 0, 0);
        AfterNightAppClass->setCentralWidget(centralWidget);
        menuBar = new QMenuBar(AfterNightAppClass);
        menuBar->setObjectName("menuBar");
        menuFile = new QMenu(menuBar);
        menuFile->setObjectName("menuFile");
        menuOpen_Recent = new QMenu(menuFile);
        menuOpen_Recent->setObjectName("menuOpen_Recent");
        menuOpen_Recent->setEnabled(false);
        menuEdit = new QMenu(menuBar);
        menuEdit->setObjectName("menuEdit");
        menuHelp = new QMenu(menuBar);
        menuHelp->setObjectName("menuHelp");
        menuAbout = new QMenu(menuBar);
        menuAbout->setObjectName("menuAbout");
        menuImage = new QMenu(menuBar);
        menuImage->setObjectName("menuImage");
        menuActions = new QMenu(menuImage);
        menuActions->setObjectName("menuActions");
        menuView = new QMenu(menuImage);
        menuView->setObjectName("menuView");
        menuExtract_Channel = new QMenu(menuImage);
        menuExtract_Channel->setObjectName("menuExtract_Channel");
        menuConvert_Format = new QMenu(menuImage);
        menuConvert_Format->setObjectName("menuConvert_Format");
        menuGeometry = new QMenu(menuImage);
        menuGeometry->setObjectName("menuGeometry");
        menuInformation = new QMenu(menuImage);
        menuInformation->setObjectName("menuInformation");
        menuMask = new QMenu(menuImage);
        menuMask->setObjectName("menuMask");
        menuMask->setEnabled(true);
        menuRendering_Mode = new QMenu(menuMask);
        menuRendering_Mode->setObjectName("menuRendering_Mode");
        menuSequence = new QMenu(menuImage);
        menuSequence->setObjectName("menuSequence");
        menuConvert = new QMenu(menuSequence);
        menuConvert->setObjectName("menuConvert");
        menuProcess = new QMenu(menuBar);
        menuProcess->setObjectName("menuProcess");
        menuFavorite_Processes = new QMenu(menuProcess);
        menuFavorite_Processes->setObjectName("menuFavorite_Processes");
        menuFavorite_Processes->setEnabled(true);
        menuRecent_Processes = new QMenu(menuProcess);
        menuRecent_Processes->setObjectName("menuRecent_Processes");
        menuRecent_Processes->setEnabled(false);
        menuIntensity_Transformations = new QMenu(menuProcess);
        menuIntensity_Transformations->setObjectName("menuIntensity_Transformations");
        menuStar_Processing = new QMenu(menuProcess);
        menuStar_Processing->setObjectName("menuStar_Processing");
        menuNoise_Reduction = new QMenu(menuProcess);
        menuNoise_Reduction->setObjectName("menuNoise_Reduction");
        menuColor_Spaces = new QMenu(menuProcess);
        menuColor_Spaces->setObjectName("menuColor_Spaces");
        menuGeometry_2 = new QMenu(menuProcess);
        menuGeometry_2->setObjectName("menuGeometry_2");
        menuColor_Calibration = new QMenu(menuProcess);
        menuColor_Calibration->setObjectName("menuColor_Calibration");
        menuMask_Generation = new QMenu(menuProcess);
        menuMask_Generation->setObjectName("menuMask_Generation");
        menuBackground_Modelization = new QMenu(menuProcess);
        menuBackground_Modelization->setObjectName("menuBackground_Modelization");
        menuDetails = new QMenu(menuProcess);
        menuDetails->setObjectName("menuDetails");
        menuFilters = new QMenu(menuProcess);
        menuFilters->setObjectName("menuFilters");
        menuMultiscale = new QMenu(menuProcess);
        menuMultiscale->setObjectName("menuMultiscale");
        menuMorphology = new QMenu(menuProcess);
        menuMorphology->setObjectName("menuMorphology");
        menuAdjustments = new QMenu(menuProcess);
        menuAdjustments->setObjectName("menuAdjustments");
        menuUtilities = new QMenu(menuProcess);
        menuUtilities->setObjectName("menuUtilities");
        menuPixel_Math = new QMenu(menuProcess);
        menuPixel_Math->setObjectName("menuPixel_Math");
        menuPreprocess = new QMenu(menuProcess);
        menuPreprocess->setObjectName("menuPreprocess");
        menuExtensions = new QMenu(menuBar);
        menuExtensions->setObjectName("menuExtensions");
        menuWindow = new QMenu(menuBar);
        menuWindow->setObjectName("menuWindow");
        menuWindow->setEnabled(false);
        menuView_2 = new QMenu(menuBar);
        menuView_2->setObjectName("menuView_2");
        AfterNightAppClass->setMenuBar(menuBar);
        mainToolBar = new QToolBar(AfterNightAppClass);
        mainToolBar->setObjectName("mainToolBar");
        AfterNightAppClass->addToolBar(Qt::ToolBarArea::TopToolBarArea, mainToolBar);
        statusBar = new QStatusBar(AfterNightAppClass);
        statusBar->setObjectName("statusBar");
        AfterNightAppClass->setStatusBar(statusBar);

        menuBar->addAction(menuFile->menuAction());
        menuBar->addAction(menuEdit->menuAction());
        menuBar->addAction(menuView_2->menuAction());
        menuBar->addAction(menuImage->menuAction());
        menuBar->addAction(menuProcess->menuAction());
        menuBar->addAction(menuExtensions->menuAction());
        menuBar->addAction(menuWindow->menuAction());
        menuBar->addAction(menuHelp->menuAction());
        menuBar->addAction(menuAbout->menuAction());
        menuFile->addAction(actionOpen);
        menuFile->addAction(menuOpen_Recent->menuAction());
        menuFile->addSeparator();
        menuFile->addAction(actionNew_Sequence);
        menuFile->addSeparator();
        menuFile->addAction(actionSave_as);
        menuFile->addSeparator();
        menuFile->addAction(actionClose);
        menuFile->addAction(actionClose_All);
        menuFile->addAction(actionClose_All_Others);
        menuFile->addSeparator();
        menuFile->addAction(actionExit);
        menuEdit->addAction(actionUndo);
        menuEdit->addAction(actionRedo);
        menuEdit->addSeparator();
        menuEdit->addAction(actionImage_History);
        menuEdit->addSeparator();
        menuEdit->addAction(actionSettings);
        menuHelp->addAction(actionUser_Guide);
        menuAbout->addAction(actionAbout_Qt);
        menuAbout->addAction(actionAbout_AfterNight);
        menuAbout->addAction(actionSystem_Information);
        menuImage->addAction(menuActions->menuAction());
        menuImage->addAction(menuConvert_Format->menuAction());
        menuImage->addAction(menuExtract_Channel->menuAction());
        menuImage->addAction(menuGeometry->menuAction());
        menuImage->addAction(menuInformation->menuAction());
        menuImage->addAction(menuMask->menuAction());
        menuImage->addAction(menuSequence->menuAction());
        menuImage->addAction(menuView->menuAction());
        menuImage->addSeparator();
        menuImage->addAction(actionShow_In_File_Browser);
        menuActions->addAction(actionDuplicate);
        menuActions->addAction(actionRename);
        menuActions->addAction(actionClose_2);
        menuActions->addAction(actionSave_File);
        menuView->addAction(actionZoom_In);
        menuView->addAction(actionZoom_Out);
        menuView->addAction(actionZoom_100);
        menuView->addSeparator();
        menuView->addAction(actionFit_To_Window);
        menuView->addAction(actionMaximize_View);
        menuExtract_Channel->addAction(actionExtract_Red_Channel);
        menuExtract_Channel->addAction(actionExtract_Green_Channel);
        menuExtract_Channel->addAction(actionExtract_Blue_Channel);
        menuExtract_Channel->addAction(actionExtract_RGB_Channels);
        menuExtract_Channel->addSeparator();
        menuExtract_Channel->addAction(actionExtract_CIE_L);
        menuExtract_Channel->addAction(actionExtract_CIE_a);
        menuExtract_Channel->addAction(actionExtract_CIE_b);
        menuExtract_Channel->addAction(actionExtract_CIE_c);
        menuExtract_Channel->addAction(actionExtract_CIE_h);
        menuExtract_Channel->addAction(actionExtract_CIE_a_b);
        menuExtract_Channel->addAction(actionExtract_CIE_L_a_b);
        menuExtract_Channel->addAction(actionExtract_CIE_L_c_h);
        menuExtract_Channel->addSeparator();
        menuExtract_Channel->addAction(actionExtract_CIE_X);
        menuExtract_Channel->addAction(actionExtract_CIE_Y);
        menuExtract_Channel->addAction(actionExtract_CIE_Z);
        menuExtract_Channel->addAction(actionExtract_CIE_X_Y_Z);
        menuExtract_Channel->addSeparator();
        menuExtract_Channel->addAction(actionExtract_HSL_Hue);
        menuExtract_Channel->addAction(actionExtract_HSL_Saturation);
        menuExtract_Channel->addAction(actionExtract_HSL_Lightness);
        menuExtract_Channel->addAction(actionExtract_HSL);
        menuExtract_Channel->addSeparator();
        menuExtract_Channel->addAction(actionExtract_HSV_Hue);
        menuExtract_Channel->addAction(actionExtract_HSV_Saturation);
        menuExtract_Channel->addAction(actionExtract_HSV_Value);
        menuExtract_Channel->addAction(actionExtract_HSV);
        menuExtract_Channel->addSeparator();
        menuExtract_Channel->addAction(actionExtract_HSI_Hue);
        menuExtract_Channel->addAction(actionExtract_HSI_Saturation);
        menuExtract_Channel->addAction(actionExtract_HSI_Intensity);
        menuExtract_Channel->addAction(actionExtract_HSI);
        menuExtract_Channel->addSeparator();
        menuExtract_Channel->addAction(actionExtract_YCbCr_Luminance);
        menuExtract_Channel->addAction(actionExtract_YCbCr_Blue_Chrominance);
        menuExtract_Channel->addAction(actionExtract_YCbCr_Red_Chrominance);
        menuExtract_Channel->addAction(actionExtract_YCbCr);
        menuConvert_Format->addAction(actionConvert_to_8_bit);
        menuConvert_Format->addAction(actionConvert_to_signed_16_bit);
        menuConvert_Format->addAction(actionConvert_to_unsigned_16_bit);
        menuConvert_Format->addAction(actionConvert_to_signed_32_bit);
        menuConvert_Format->addAction(actionConvert_to_unsigned_32_bit);
        menuConvert_Format->addAction(actionConvert_to_32_bit_float);
        menuConvert_Format->addAction(actionConvert_to_64_bit_float);
        menuGeometry->addAction(actionHorizontal_Mirror);
        menuGeometry->addAction(actionVertical_Mirror);
        menuGeometry->addSeparator();
        menuGeometry->addAction(actionRotate_180);
        menuGeometry->addAction(actionRotate_90_Left);
        menuGeometry->addAction(actionRotate_90_Right);
        menuInformation->addAction(actionStatistics);
        menuInformation->addAction(actionHistogram);
        menuInformation->addAction(actionImage_EXIFViewer);
        menuInformation->addAction(actionFITSHeader);
        menuInformation->addAction(actionAstrometryInfo);
        menuInformation->addAction(actionPlateSolve);
        menuInformation->addAction(actionSummary);
        menuMask->addAction(actionSelect_Mask);
        menuMask->addAction(actionRemove_Mask);
        menuMask->addSeparator();
        menuMask->addAction(actionInvert_Mask);
        menuMask->addAction(actionEnable_Mask);
        menuMask->addAction(actionShow_Mask);
        menuMask->addSeparator();
        menuMask->addAction(menuRendering_Mode->menuAction());
        menuRendering_Mode->addAction(actionOverlay_Red);
        menuRendering_Mode->addAction(actionOverlay_Green);
        menuRendering_Mode->addAction(actionOverlay_Blue);
        menuRendering_Mode->addAction(actionOverlay_Yellow);
        menuRendering_Mode->addAction(actionOverlay_Magenta);
        menuRendering_Mode->addAction(actionOverlay_Cyan);
        menuRendering_Mode->addSeparator();
        menuRendering_Mode->addAction(actionReplace);
        menuRendering_Mode->addAction(actionMultiply);
        menuSequence->addAction(menuConvert->menuAction());
        menuSequence->addAction(actionExtract_Frame);
        menuSequence->addAction(actionExtract_Sequence);
        menuSequence->addAction(actionExport);
        menuConvert->addAction(actionConvert_to_SER);
        menuConvert->addAction(actionConvert_to_Image_List);
        menuConvert->addAction(actionConvert_to_FITS);
        menuProcess->addAction(menuFavorite_Processes->menuAction());
        menuProcess->addAction(menuRecent_Processes->menuAction());
        menuProcess->addSeparator();
        menuProcess->addAction(menuAdjustments->menuAction());
        menuProcess->addAction(menuBackground_Modelization->menuAction());
        menuProcess->addAction(menuColor_Calibration->menuAction());
        menuProcess->addAction(menuColor_Spaces->menuAction());
        menuProcess->addAction(menuDetails->menuAction());
        menuProcess->addAction(menuFilters->menuAction());
        menuProcess->addAction(menuGeometry_2->menuAction());
        menuProcess->addAction(menuIntensity_Transformations->menuAction());
        menuProcess->addAction(menuMask_Generation->menuAction());
        menuProcess->addAction(menuMorphology->menuAction());
        menuProcess->addAction(menuMultiscale->menuAction());
        menuProcess->addAction(menuNoise_Reduction->menuAction());
        menuProcess->addAction(menuPixel_Math->menuAction());
        menuProcess->addAction(menuPreprocess->menuAction());
        menuProcess->addAction(menuStar_Processing->menuAction());
        menuProcess->addAction(menuUtilities->menuAction());
        menuIntensity_Transformations->addAction(actionArcSinh_Stretch);
        menuIntensity_Transformations->addAction(actionBinarize);
        menuIntensity_Transformations->addAction(actionCLA_Histogram_Equalization);
        menuIntensity_Transformations->addAction(actionColor_Saturation);
        menuIntensity_Transformations->addAction(actionCurves_Transformation);
        menuIntensity_Transformations->addAction(actionGeneralized_Hyperbolic_Transformation);
        menuIntensity_Transformations->addAction(actionHistogram_Transformation);
        menuIntensity_Transformations->addAction(actionLocal_Histogram_Equalization);
        menuIntensity_Transformations->addAction(actionStatistical_Stretch);
        menuStar_Processing->addAction(actionProfiled_Starmask_Recombination);
        menuStar_Processing->addAction(actionStarmask_Recombination);
        menuStar_Processing->addAction(actionStar_Reduction);
        menuStar_Processing->addAction(actionStarnet);
        menuStar_Processing->addAction(actionStar_Profiler_v1);
        menuStar_Processing->addAction(actionStar_Profiler_v2);
        menuNoise_Reduction->addAction(actionDeepSNR);
        menuNoise_Reduction->addAction(actionGRAYCstoration_Filter);
        menuNoise_Reduction->addAction(actionNon_Local_Bayes_Filter);
        menuNoise_Reduction->addAction(actionPatch_PCA_Filter);
        menuNoise_Reduction->addAction(actionTotal_Variation_Denoise_Filter);
        menuNoise_Reduction->addAction(actionWavelet_Denoise_Filter);
        menuColor_Spaces->addAction(actionChannelComposition);
        menuColor_Spaces->addAction(actionColorize);
        menuColor_Spaces->addAction(actionDebayer);
        menuColor_Spaces->addAction(actionLRGB_Combination);
        menuColor_Spaces->addAction(actionNBRGB_Combination);
        menuColor_Spaces->addAction(actionNBColorMapper);
        menuColor_Spaces->addAction(actionNarrowbandPaletteComposer);
        menuColor_Spaces->addAction(actionMerge_CFA);
        menuColor_Spaces->addAction(actionSplit_CFA);
        menuGeometry_2->addAction(actionResize);
        menuGeometry_2->addAction(actionCrop);
        menuGeometry_2->addAction(actionRotate);
        menuGeometry_2->addAction(actionIntegerResample);
        menuGeometry_2->addAction(actionCFA_Integer_Resample);
        menuColor_Calibration->addAction(actionBackground_Neutralization);
        menuColor_Calibration->addAction(actionSCNR);
        menuColor_Calibration->addAction(actionColor_Calibration_2);
        menuColor_Calibration->addAction(actionPhotometric_Color_Calibration);
        menuColor_Calibration->addAction(actionSpectrophotometric_Color_Calibration);
        menuColor_Calibration->addAction(actionLinear_Match);
        menuMask_Generation->addAction(actionMask_Drawing);
        menuMask_Generation->addAction(actionRange_Selection);
        menuMask_Generation->addAction(actionStar_Mask);
        menuMask_Generation->addAction(actionStarnet_Star_Mask);
        menuBackground_Modelization->addAction(actionBackground_Extraction);
        menuBackground_Modelization->addAction(actionAuto_Gradient_Removal);
        menuDetails->addAction(actionGaussian_Blur);
        menuDetails->addAction(actionHigh_Pass);
        menuDetails->addAction(actionLocal_Contrast_Enhancement);
        menuDetails->addAction(actionDark_Structure_Enhance);
        menuDetails->addAction(actionMultiscale_Alpha_Sharpen);
        menuDetails->addAction(actionUnsharp_Mask);
        menuFilters->addAction(actionGaussian_Blur);
        menuFilters->addAction(actionMedian_Filter);
        menuFilters->addAction(actionBilateral_Filter);
        menuFilters->addAction(actionBanding_Reduction_Filter);
        menuFilters->addAction(actionCosmetic_Correction_Filter);
        menuMultiscale->addAction(actionMultiscale_Linear_Transform);
        menuMorphology->addAction(actionMorphological_Transformation);
        menuMorphology->addAction(actionTighten_Edges);
        menuAdjustments->addAction(actionAdjustBrightness_Contrast);
        menuAdjustments->addAction(actionGamma_Correction);
        menuAdjustments->addAction(actionExposure);
        menuAdjustments->addAction(actionAdjustHSL);
        menuAdjustments->addAction(actionAdjustLightness_Chroma_Hue);
        menuAdjustments->addSeparator();
        menuAdjustments->addAction(actionNegative_Transformation);
        menuAdjustments->addSeparator();
        menuAdjustments->addAction(actionConvert_to_RGB);
        menuAdjustments->addAction(actionConvert_to_Grayscale);
        menuUtilities->addAction(actionImage_Blend);
        menuUtilities->addAction(actionImage_Compare);
        menuUtilities->addAction(actionNoise_Generator);
        menuUtilities->addAction(actionImage_List_Analyzer);
        menuPixel_Math->addAction(actionPixel_Math_2);
        menuPreprocess->addAction(actionImage_Calibration);
        menuPreprocess->addAction(actionImage_Registration);
        menuPreprocess->addAction(actionImage_Stacking);
        menuPreprocess->addAction(actionImage_Integration);
        menuPreprocess->addAction(actionImage_Integration_Lite);
        menuExtensions->addAction(actionManage_Extensions);
        menuWindow->addAction(actionLock_Layout);
        menuWindow->addAction(actionUnlock_Layout);
        menuWindow->addSeparator();
        menuWindow->addAction(actionGroup_All_Images);
        menuWindow->addAction(actionSplit_All_Images);
        menuWindow->addAction(actionFit_Images_To_Window);
        menuView_2->addAction(actionLog_Viewer);
        mainToolBar->addAction(actionOpen);
        mainToolBar->addAction(actionSave_as);
        mainToolBar->addAction(actionClose);

        retranslateUi(AfterNightAppClass);
        QObject::connect(actionExit, &QAction::triggered, AfterNightAppClass, qOverload<>(&QMainWindow::close));

        QMetaObject::connectSlotsByName(AfterNightAppClass);
    } // setupUi

    void retranslateUi(QMainWindow* AfterNightAppClass)
    {
        AfterNightAppClass->setWindowTitle(QCoreApplication::translate("AfterNightAppClass", "AfterNight", nullptr));
        actionOpen->setText(QCoreApplication::translate("AfterNightAppClass", "Open", nullptr));
#if QT_CONFIG(shortcut)
        actionOpen->setShortcut(QCoreApplication::translate("AfterNightAppClass", "Ctrl+O", nullptr));
#endif // QT_CONFIG(shortcut)
        actionSave_as->setText(QCoreApplication::translate("AfterNightAppClass", "Save as...", nullptr));
#if QT_CONFIG(shortcut)
        actionSave_as->setShortcut(QCoreApplication::translate("AfterNightAppClass", "Ctrl+Shift+S", nullptr));
#endif // QT_CONFIG(shortcut)
        actionExit->setText(QCoreApplication::translate("AfterNightAppClass", "Exit", nullptr));
        actionAbout_Qt->setText(QCoreApplication::translate("AfterNightAppClass", "About Qt", nullptr));
        actionAbout_AfterNight->setText(QCoreApplication::translate("AfterNightAppClass", "About AfterNight", nullptr));
        actionSystem_Information->setText(
            QCoreApplication::translate("AfterNightAppClass", "System Information", nullptr));
        actionUser_Guide->setText(QCoreApplication::translate("AfterNightAppClass", "User Guide", nullptr));
#if QT_CONFIG(shortcut)
        actionUser_Guide->setShortcut(QCoreApplication::translate("AfterNightAppClass", "F1", nullptr));
#endif // QT_CONFIG(shortcut)
        actionUser_Guide->setToolTip(
            QCoreApplication::translate("AfterNightAppClass", "Open the AfterNight User Guide.", nullptr));
        actionUser_Guide->setStatusTip(
            QCoreApplication::translate("AfterNightAppClass", "Open the AfterNight User Guide.", nullptr));
        actionUser_Guide->setWhatsThis(
            QCoreApplication::translate("AfterNightAppClass", "Open the AfterNight User Guide.", nullptr));
        actionUndo->setText(QCoreApplication::translate("AfterNightAppClass", "Undo", nullptr));
#if QT_CONFIG(shortcut)
        actionUndo->setShortcut(QCoreApplication::translate("AfterNightAppClass", "Ctrl+Z", nullptr));
#endif // QT_CONFIG(shortcut)
        actionRedo->setText(QCoreApplication::translate("AfterNightAppClass", "Redo", nullptr));
#if QT_CONFIG(shortcut)
        actionRedo->setShortcut(QCoreApplication::translate("AfterNightAppClass", "Ctrl+Y", nullptr));
#endif // QT_CONFIG(shortcut)
        actionClose->setText(QCoreApplication::translate("AfterNightAppClass", "Close", nullptr));
        actionClose_All->setText(QCoreApplication::translate("AfterNightAppClass", "Close All", nullptr));
        actionDuplicate->setText(QCoreApplication::translate("AfterNightAppClass", "Duplicate", nullptr));
        actionRename->setText(QCoreApplication::translate("AfterNightAppClass", "Rename", nullptr));
        actionShow_In_File_Browser->setText(
            QCoreApplication::translate("AfterNightAppClass", "Show in File Browser", nullptr));
        actionExtract_Red_Channel->setText(
            QCoreApplication::translate("AfterNightAppClass", "Extract Red Channel", nullptr));
        actionExtract_Green_Channel->setText(
            QCoreApplication::translate("AfterNightAppClass", "Extract Green Channel", nullptr));
        actionExtract_Blue_Channel->setText(
            QCoreApplication::translate("AfterNightAppClass", "Extract Blue Channel", nullptr));
        actionExtract_RGB_Channels->setText(
            QCoreApplication::translate("AfterNightAppClass", "Extract RGB Channels", nullptr));
        actionConvert_to_8_bit->setText(QCoreApplication::translate("AfterNightAppClass", "Convert to 8 bit", nullptr));
        actionConvert_to_signed_16_bit->setText(
            QCoreApplication::translate("AfterNightAppClass", "Convert to signed 16 bit", nullptr));
        actionConvert_to_unsigned_16_bit->setText(
            QCoreApplication::translate("AfterNightAppClass", "Convert to unsigned 16 bit", nullptr));
        actionConvert_to_signed_32_bit->setText(
            QCoreApplication::translate("AfterNightAppClass", "Convert to signed 32 bit", nullptr));
        actionConvert_to_unsigned_32_bit->setText(
            QCoreApplication::translate("AfterNightAppClass", "Convert to unsigned 32 bit", nullptr));
        actionConvert_to_32_bit_float->setText(
            QCoreApplication::translate("AfterNightAppClass", "Convert to 32 bit float", nullptr));
        actionConvert_to_64_bit_float->setText(
            QCoreApplication::translate("AfterNightAppClass", "Convert to 64 bit float", nullptr));
        action->setText(QCoreApplication::translate("AfterNightAppClass", ".", nullptr));
        actionHorizontal_Mirror->setText(
            QCoreApplication::translate("AfterNightAppClass", "Horizontal Mirror", nullptr));
        actionVertical_Mirror->setText(QCoreApplication::translate("AfterNightAppClass", "Vertical Mirror", nullptr));
        actionArcSinh_Stretch->setText(QCoreApplication::translate("AfterNightAppClass", "ArcSinh Stretch", nullptr));
        actionRotate_180->setText(QCoreApplication::translate("AfterNightAppClass", "Rotate 180\302\272", nullptr));
        actionRotate_90_Left->setText(
            QCoreApplication::translate("AfterNightAppClass", "Rotate 90\302\272 Left", nullptr));
        actionRotate_90_Right->setText(
            QCoreApplication::translate("AfterNightAppClass", "Rotate 90\302\272 Right", nullptr));
        actionStarnet->setText(QCoreApplication::translate("AfterNightAppClass", "Starnet++", nullptr));
        actionStar_Reduction->setText(QCoreApplication::translate("AfterNightAppClass", "Star Reduction", nullptr));
        actionAutomatic_MTF_Transform->setText(
            QCoreApplication::translate("AfterNightAppClass", "Automatic MTF Transform", nullptr));
        actionExtract_CIE_L->setText(QCoreApplication::translate("AfterNightAppClass", "Extract CIE L*", nullptr));
        actionExtract_CIE_a->setText(QCoreApplication::translate("AfterNightAppClass", "Extract CIE a*", nullptr));
        actionExtract_CIE_b->setText(QCoreApplication::translate("AfterNightAppClass", "Extract CIE b*", nullptr));
        actionExtract_CIE_a_b->setText(
            QCoreApplication::translate("AfterNightAppClass", "Extract CIE a*=R / b*=G", nullptr));
        actionExtract_CIE_L_a_b->setText(
            QCoreApplication::translate("AfterNightAppClass", "Extract CIE L*a*b*", nullptr));
        actionExtract_CIE_L_c_h->setText(
            QCoreApplication::translate("AfterNightAppClass", "Extract CIE L*c*h*", nullptr));
        actionExtract_CIE_c->setText(QCoreApplication::translate("AfterNightAppClass", "Extract CIE c*", nullptr));
        actionExtract_CIE_h->setText(QCoreApplication::translate("AfterNightAppClass", "Extract CIE h*", nullptr));
        actionExtract_CIE_X->setText(QCoreApplication::translate("AfterNightAppClass", "Extract CIE X", nullptr));
        actionExtract_CIE_Y->setText(QCoreApplication::translate("AfterNightAppClass", "Extract CIE Y", nullptr));
        actionExtract_CIE_Z->setText(QCoreApplication::translate("AfterNightAppClass", "Extract CIE Z", nullptr));
        actionExtract_CIE_X_Y_Z->setText(QCoreApplication::translate("AfterNightAppClass", "Extract CIE XYZ", nullptr));
        actionExtract_HSL_Hue->setText(QCoreApplication::translate("AfterNightAppClass", "Extract HSL Hue", nullptr));
        actionExtract_HSL_Saturation->setText(
            QCoreApplication::translate("AfterNightAppClass", "Extract HSL Saturation", nullptr));
        actionExtract_HSL_Lightness->setText(
            QCoreApplication::translate("AfterNightAppClass", "Extract HSL Lightness", nullptr));
        actionExtract_HSL->setText(QCoreApplication::translate("AfterNightAppClass", "Extract HSL", nullptr));
        actionExtract_HSV_Hue->setText(QCoreApplication::translate("AfterNightAppClass", "Extract HSV Hue", nullptr));
        actionExtract_HSV_Saturation->setText(
            QCoreApplication::translate("AfterNightAppClass", "Extract HSV Saturation", nullptr));
        actionExtract_HSV_Value->setText(
            QCoreApplication::translate("AfterNightAppClass", "Extract HSV Value", nullptr));
        actionExtract_HSV->setText(QCoreApplication::translate("AfterNightAppClass", "Extract HSV", nullptr));
        actionExtract_HSI_Hue->setText(QCoreApplication::translate("AfterNightAppClass", "Extract HSI Hue", nullptr));
        actionExtract_HSI_Saturation->setText(
            QCoreApplication::translate("AfterNightAppClass", "Extract HSI Saturation", nullptr));
        actionExtract_HSI_Intensity->setText(
            QCoreApplication::translate("AfterNightAppClass", "Extract HSI Intensity", nullptr));
        actionExtract_HSI->setText(QCoreApplication::translate("AfterNightAppClass", "Extract HSI", nullptr));
        actionNegative_Transformation->setText(
            QCoreApplication::translate("AfterNightAppClass", "Negative Transformation", nullptr));
        actionImage_History->setText(QCoreApplication::translate("AfterNightAppClass", "Image History", nullptr));
        actionasd->setText(QCoreApplication::translate("AfterNightAppClass", "asd", nullptr));
        actionasd_2->setText(QCoreApplication::translate("AfterNightAppClass", "asd", nullptr));
        actionStatistics->setText(QCoreApplication::translate("AfterNightAppClass", "Statistics", nullptr));
        actionHistogram->setText(QCoreApplication::translate("AfterNightAppClass", "Histogram", nullptr));
        actionImage_EXIFViewer->setText(QCoreApplication::translate("AfterNightAppClass", "EXIF Viewer", nullptr));
        actionFITSHeader->setText(QCoreApplication::translate("AfterNightAppClass", "FITS Header", nullptr));
        actionAstrometryInfo->setText(QCoreApplication::translate("AfterNightAppClass", "Astrometry Info", nullptr));
        actionPlateSolve->setText(QCoreApplication::translate("AfterNightAppClass", "Plate Solve", nullptr));
        actionAdjustBrightness_Contrast->setText(
            QCoreApplication::translate("AfterNightAppClass", "Brightness/Contrast", nullptr));
        actionGamma_Correction->setText(QCoreApplication::translate("AfterNightAppClass", "Gamma Correction", nullptr));
        actionAdjustHSL->setText(
            QCoreApplication::translate("AfterNightAppClass", "Hue/Saturation/Lightness", nullptr));
        actionStarmask_Recombination->setText(
            QCoreApplication::translate("AfterNightAppClass", "Starmask Recombination", nullptr));
        actionSCNR->setText(QCoreApplication::translate("AfterNightAppClass", "SCNR Filter", nullptr));
        actionMedian_Filter->setText(QCoreApplication::translate("AfterNightAppClass", "Median Filter", nullptr));
        actionClose_2->setText(QCoreApplication::translate("AfterNightAppClass", "Close", nullptr));
        actionZoom_In->setText(QCoreApplication::translate("AfterNightAppClass", "Zoom In", nullptr));
#if QT_CONFIG(shortcut)
        actionZoom_In->setShortcut(QCoreApplication::translate("AfterNightAppClass", "Ctrl++", nullptr));
#endif // QT_CONFIG(shortcut)
        actionZoom_Out->setText(QCoreApplication::translate("AfterNightAppClass", "Zoom Out", nullptr));
#if QT_CONFIG(shortcut)
        actionZoom_Out->setShortcut(QCoreApplication::translate("AfterNightAppClass", "Ctrl+-", nullptr));
#endif // QT_CONFIG(shortcut)
        actionZoom_100->setText(QCoreApplication::translate("AfterNightAppClass", "Zoom 100%", nullptr));
#if QT_CONFIG(shortcut)
        actionZoom_100->setShortcut(QCoreApplication::translate("AfterNightAppClass", "Ctrl+0", nullptr));
#endif // QT_CONFIG(shortcut)
        actionFit_To_Window->setText(QCoreApplication::translate("AfterNightAppClass", "Fit To Window", nullptr));
        actionClose_All_Others->setText(QCoreApplication::translate("AfterNightAppClass", "Close All Others", nullptr));
        actionChannelComposition->setText(
            QCoreApplication::translate("AfterNightAppClass", "Channel Composition", nullptr));
        actionDebayer->setText(QCoreApplication::translate("AfterNightAppClass", "Debayer", nullptr));
        actionConvert_to_Grayscale->setText(
            QCoreApplication::translate("AfterNightAppClass", "Convert to Grayscale", nullptr));
        actionConvert_to_RGB->setText(QCoreApplication::translate("AfterNightAppClass", "Convert to RGB", nullptr));
        actionSummary->setText(QCoreApplication::translate("AfterNightAppClass", "Summary", nullptr));
        actionResize->setText(QCoreApplication::translate("AfterNightAppClass", "Resize", nullptr));
        actionCrop->setText(QCoreApplication::translate("AfterNightAppClass", "Crop", nullptr));
        actionRotate->setText(QCoreApplication::translate("AfterNightAppClass", "Rotate", nullptr));
        actionHistogram_Transformation->setText(
            QCoreApplication::translate("AfterNightAppClass", "Histogram Transformation", nullptr));
        actionColor_Calibration_2->setText(
            QCoreApplication::translate("AfterNightAppClass", "Color Calibration", nullptr));
        actionPhotometric_Color_Calibration->setText(
            QCoreApplication::translate("AfterNightAppClass", "Photometric Color Calibration", nullptr));
        actionSpectrophotometric_Color_Calibration->setText(
            QCoreApplication::translate("AfterNightAppClass", "Spectrophotometric Color Calibration", nullptr));
        actionBackground_Neutralization->setText(
            QCoreApplication::translate("AfterNightAppClass", "Background Neutralization", nullptr));
        actionMask_Drawing->setText(QCoreApplication::translate("AfterNightAppClass", "Mask Drawing", nullptr));
        actionRange_Selection->setText(QCoreApplication::translate("AfterNightAppClass", "Range Selection", nullptr));
        actionSelect_Mask->setText(QCoreApplication::translate("AfterNightAppClass", "Select Mask", nullptr));
        actionRemove_Mask->setText(QCoreApplication::translate("AfterNightAppClass", "Remove Mask", nullptr));
        actionInvert_Mask->setText(QCoreApplication::translate("AfterNightAppClass", "Invert Mask", nullptr));
        actionEnable_Mask->setText(QCoreApplication::translate("AfterNightAppClass", "Enable Mask", nullptr));
        actionShow_Mask->setText(QCoreApplication::translate("AfterNightAppClass", "Show Mask", nullptr));
        actionOverlay_Red->setText(QCoreApplication::translate("AfterNightAppClass", "Overlay Red", nullptr));
        actionOverlay_Green->setText(QCoreApplication::translate("AfterNightAppClass", "Overlay Green", nullptr));
        actionOverlay_Blue->setText(QCoreApplication::translate("AfterNightAppClass", "Overlay Blue", nullptr));
        actionOverlay_Yellow->setText(QCoreApplication::translate("AfterNightAppClass", "Overlay Yellow", nullptr));
        actionOverlay_Magenta->setText(QCoreApplication::translate("AfterNightAppClass", "Overlay Magenta", nullptr));
        actionOverlay_Cyan->setText(QCoreApplication::translate("AfterNightAppClass", "Overlay Cyan", nullptr));
        actionReplace->setText(QCoreApplication::translate("AfterNightAppClass", "Replace", nullptr));
        actionMultiply->setText(QCoreApplication::translate("AfterNightAppClass", "Multiply", nullptr));
        actionIntegerResample->setText(QCoreApplication::translate("AfterNightAppClass", "Integer Resample", nullptr));
        actionCLA_Histogram_Equalization->setText(
            QCoreApplication::translate("AfterNightAppClass", "CLA Histogram Equalization", nullptr));
        actionDeepSNR->setText(QCoreApplication::translate("AfterNightAppClass", "DeepSNR", nullptr));
        actionNon_Local_Bayes_Filter->setText(
            QCoreApplication::translate("AfterNightAppClass", "Non-Local Bayes Filter", nullptr));
        actionBackground_Extraction->setText(
            QCoreApplication::translate("AfterNightAppClass", "Background Extraction", nullptr));
        actionAuto_Gradient_Removal->setText(
            QCoreApplication::translate("AfterNightAppClass", "Auto Gradient Removal", nullptr));
        actionWavelet_Denoise_Filter->setText(
            QCoreApplication::translate("AfterNightAppClass", "Wavelet Denoise Filter", nullptr));
        actionPatch_PCA_Filter->setText(
            QCoreApplication::translate("AfterNightAppClass", "Patch PCA Denoise Filter", nullptr));
        actionLocal_Histogram_Equalization->setText(
            QCoreApplication::translate("AfterNightAppClass", "Local Histogram Equalization", nullptr));
        actionLocal_Contrast_Enhancement->setText(
            QCoreApplication::translate("AfterNightAppClass", "Local Contrast Enhancement", nullptr));
        actionDark_Structure_Enhance->setText(
            QCoreApplication::translate("AfterNightAppClass", "Dark Structure Enhance", nullptr));
        actionLinear_Match->setText(QCoreApplication::translate("AfterNightAppClass", "Linear Match", nullptr));
        actionCurves_Transformation->setText(
            QCoreApplication::translate("AfterNightAppClass", "Curves Transformation", nullptr));
        actionExtract_YCbCr_Luminance->setText(
            QCoreApplication::translate("AfterNightAppClass", "Extract YCbCr Luma", nullptr));
        actionExtract_YCbCr_Blue_Chrominance->setText(
            QCoreApplication::translate("AfterNightAppClass", "Extract YCbCr Blue Chroma", nullptr));
        actionExtract_YCbCr_Red_Chrominance->setText(
            QCoreApplication::translate("AfterNightAppClass", "Extract YCbCr Red Chroma", nullptr));
        actionExtract_YCbCr->setText(QCoreApplication::translate("AfterNightAppClass", "Extract YCbCr", nullptr));
        actionMultiscale_Linear_Transform->setText(
            QCoreApplication::translate("AfterNightAppClass", "Multiscale Linear Transform", nullptr));
        actionMultiscale_Alpha_Sharpen->setText(
            QCoreApplication::translate("AfterNightAppClass", "Multiscale Alpha Sharpen", nullptr));
        actionStar_Mask->setText(QCoreApplication::translate("AfterNightAppClass", "Star Mask", nullptr));
        actionStarnet_Star_Mask->setText(
            QCoreApplication::translate("AfterNightAppClass", "Starnet++ Star Mask", nullptr));
        actionGaussian_Blur->setText(QCoreApplication::translate("AfterNightAppClass", "Gaussian Blur", nullptr));
        actionHigh_Pass->setText(QCoreApplication::translate("AfterNightAppClass", "High Pass", nullptr));
        actionFilters->setText(QCoreApplication::translate("AfterNightAppClass", "Filters", nullptr));
        actionMorphological_Transformation->setText(
            QCoreApplication::translate("AfterNightAppClass", "Morphological Transformation", nullptr));
        actionGRAYCstoration_Filter->setText(
            QCoreApplication::translate("AfterNightAppClass", "GRAYCstoration Filter", nullptr));
        actionAdjustLightness_Chroma_Hue->setText(
            QCoreApplication::translate("AfterNightAppClass", "Lightness/Chroma/Hue", nullptr));
        actionExposure->setText(QCoreApplication::translate("AfterNightAppClass", "Exposure", nullptr));
        actionCosmetic_Correction_Filter->setText(
            QCoreApplication::translate("AfterNightAppClass", "Cosmetic Correction Filter", nullptr));
        actionBanding_Reduction_Filter->setText(
            QCoreApplication::translate("AfterNightAppClass", "Banding Reduction Filter", nullptr));
        actionStar_Profiler_v2->setText(QCoreApplication::translate("AfterNightAppClass", "Star Profiler v2", nullptr));
        actionTighten_Edges->setText(QCoreApplication::translate("AfterNightAppClass", "Tighten Edges", nullptr));
        actionProfiled_Starmask_Recombination->setText(
            QCoreApplication::translate("AfterNightAppClass", "Profiled Starmask Recombination", nullptr));
        actionStar_Profiler_v1->setText(QCoreApplication::translate("AfterNightAppClass", "Star Profiler v1", nullptr));
        actionLock_Layout->setText(QCoreApplication::translate("AfterNightAppClass", "Lock Layout", nullptr));
        actionUnlock_Layout->setText(QCoreApplication::translate("AfterNightAppClass", "Unlock Layout", nullptr));
        actionFit_Images_To_Window->setText(
            QCoreApplication::translate("AfterNightAppClass", "Fit Images To Window", nullptr));
        actionGroup_All_Images->setText(QCoreApplication::translate("AfterNightAppClass", "Group All Images", nullptr));
        actionSplit_All_Images->setText(QCoreApplication::translate("AfterNightAppClass", "Split All Images", nullptr));
        actionUnsharp_Mask->setText(QCoreApplication::translate("AfterNightAppClass", "Unsharp Mask", nullptr));
        actionNoise_Generator->setText(QCoreApplication::translate("AfterNightAppClass", "Noise Generator", nullptr));
        actionTotal_Variation_Denoise_Filter->setText(
            QCoreApplication::translate("AfterNightAppClass", "Total Variation Denoise Filter", nullptr));
        actionImage_Blend->setText(QCoreApplication::translate("AfterNightAppClass", "Image Blend", nullptr));
        actionImage_Compare->setText(QCoreApplication::translate("AfterNightAppClass", "Image Compare", nullptr));
        actionSplit_CFA->setText(QCoreApplication::translate("AfterNightAppClass", "Split CFA", nullptr));
        actionMerge_CFA->setText(QCoreApplication::translate("AfterNightAppClass", "Merge CFA", nullptr));
        actionGeneralized_Hyperbolic_Transformation->setText(
            QCoreApplication::translate("AfterNightAppClass", "Generalized Hyperbolic Transformation", nullptr));
        actionColor_Saturation->setText(QCoreApplication::translate("AfterNightAppClass", "Color Saturation", nullptr));
        actionBilateral_Filter->setText(QCoreApplication::translate("AfterNightAppClass", "Bilateral Filter", nullptr));
        actionLog_Viewer->setText(QCoreApplication::translate("AfterNightAppClass", "Session Console", nullptr));
        actionExtract_Frame->setText(QCoreApplication::translate("AfterNightAppClass", "Extract Frame", nullptr));
#if QT_CONFIG(tooltip)
        actionExtract_Frame->setToolTip(QCoreApplication::translate(
            "AfterNightAppClass", "Extract current sequence frame into a new image", nullptr));
#endif // QT_CONFIG(tooltip)
        actionExtract_Sequence->setText(QCoreApplication::translate("AfterNightAppClass", "Extract Sequence", nullptr));
        actionNew_Sequence->setText(QCoreApplication::translate("AfterNightAppClass", "New Sequence...", nullptr));
        actionConvert_to_SER->setText(QCoreApplication::translate("AfterNightAppClass", "Convert to SER", nullptr));
        actionConvert_to_Image_List->setText(
            QCoreApplication::translate("AfterNightAppClass", "Convert to Image List", nullptr));
        actionConvert_to_FITS->setText(QCoreApplication::translate("AfterNightAppClass", "Convert to FITS", nullptr));
        actionCFA_Integer_Resample->setText(
            QCoreApplication::translate("AfterNightAppClass", "CFA Integer Resample", nullptr));
        actionMaximize_View->setText(QCoreApplication::translate("AfterNightAppClass", "Maximize View", nullptr));
        actionPixel_Math_2->setText(QCoreApplication::translate("AfterNightAppClass", "Pixel Math", nullptr));
        actionBinarize->setText(QCoreApplication::translate("AfterNightAppClass", "Binarize", nullptr));
        actionImage_Calibration->setText(
            QCoreApplication::translate("AfterNightAppClass", "Image Calibration", nullptr));
        actionImage_Registration->setText(
            QCoreApplication::translate("AfterNightAppClass", "Image Registration", nullptr));
        actionImage_Stacking->setText(QCoreApplication::translate("AfterNightAppClass", "Image Stacking", nullptr));
        actionImage_Integration->setText(
            QCoreApplication::translate("AfterNightAppClass", "Image Integration", nullptr));
        actionImage_Integration_Lite->setText(
            QCoreApplication::translate("AfterNightAppClass", "Image Integration Lite", nullptr));
        actionStatistical_Stretch->setText(
            QCoreApplication::translate("AfterNightAppClass", "Statistical Stretch", nullptr));
        actionLRGB_Combination->setText(QCoreApplication::translate("AfterNightAppClass", "LRGB Combination", nullptr));
        actionNBRGB_Combination->setText(
            QCoreApplication::translate("AfterNightAppClass", "NBRGB Combination", nullptr));
        actionNBColorMapper->setText(QCoreApplication::translate("AfterNightAppClass", "NB Color Mapper", nullptr));
        actionNarrowbandPaletteComposer->setText(
            QCoreApplication::translate("AfterNightAppClass", "Narrowband Palette Composer", nullptr));
        actionSave_File->setText(QCoreApplication::translate("AfterNightAppClass", "Save File...", nullptr));
        actionImage_List_Analyzer->setText(
            QCoreApplication::translate("AfterNightAppClass", "Image List Analyzer", nullptr));
        actionExport->setText(QCoreApplication::translate("AfterNightAppClass", "Export...", nullptr));
        actionColorize->setText(QCoreApplication::translate("AfterNightAppClass", "Colorize", nullptr));
        actionSettings->setText(QCoreApplication::translate("AfterNightAppClass", "Settings...", nullptr));
#if QT_CONFIG(shortcut)
        actionSettings->setShortcut(QCoreApplication::translate("AfterNightAppClass", "Ctrl+,", nullptr));
#endif // QT_CONFIG(shortcut)
        actionManage_Extensions->setText(
            QCoreApplication::translate("AfterNightAppClass", "Manage Extensions...", nullptr));
        actionManage_Extensions->setToolTip(QCoreApplication::translate(
            "AfterNightAppClass", "Install, update, remove, and configure extensions.", nullptr));
        actionManage_Extensions->setStatusTip(QCoreApplication::translate(
            "AfterNightAppClass", "Install, update, remove, and configure extensions.", nullptr));
        actionManage_Extensions->setWhatsThis(QCoreApplication::translate(
            "AfterNightAppClass", "Install, update, remove, and configure extensions.", nullptr));
        menuFile->setTitle(QCoreApplication::translate("AfterNightAppClass", "File", nullptr));
        menuOpen_Recent->setTitle(QCoreApplication::translate("AfterNightAppClass", "Open Recent", nullptr));
        menuEdit->setTitle(QCoreApplication::translate("AfterNightAppClass", "Edit", nullptr));
        menuHelp->setTitle(QCoreApplication::translate("AfterNightAppClass", "Help", nullptr));
        menuAbout->setTitle(QCoreApplication::translate("AfterNightAppClass", "About", nullptr));
        menuImage->setTitle(QCoreApplication::translate("AfterNightAppClass", "Image", nullptr));
        menuActions->setTitle(QCoreApplication::translate("AfterNightAppClass", "Actions", nullptr));
        menuView->setTitle(QCoreApplication::translate("AfterNightAppClass", "View", nullptr));
        menuExtract_Channel->setTitle(QCoreApplication::translate("AfterNightAppClass", "Extract Channel", nullptr));
        menuConvert_Format->setTitle(QCoreApplication::translate("AfterNightAppClass", "Convert Format", nullptr));
        menuGeometry->setTitle(QCoreApplication::translate("AfterNightAppClass", "Fast Geometry", nullptr));
        menuInformation->setTitle(QCoreApplication::translate("AfterNightAppClass", "Information", nullptr));
        menuMask->setTitle(QCoreApplication::translate("AfterNightAppClass", "Mask", nullptr));
        menuRendering_Mode->setTitle(QCoreApplication::translate("AfterNightAppClass", "Rendering Mode", nullptr));
        menuSequence->setTitle(QCoreApplication::translate("AfterNightAppClass", "Sequence", nullptr));
        menuConvert->setTitle(QCoreApplication::translate("AfterNightAppClass", "Convert", nullptr));
        menuProcess->setTitle(QCoreApplication::translate("AfterNightAppClass", "Process", nullptr));
        menuFavorite_Processes->setTitle(
            QCoreApplication::translate("AfterNightAppClass", "Favorite Processes", nullptr));
        menuRecent_Processes->setTitle(QCoreApplication::translate("AfterNightAppClass", "Recent Processes", nullptr));
        menuIntensity_Transformations->setTitle(
            QCoreApplication::translate("AfterNightAppClass", "Intensity Transformations", nullptr));
        menuStar_Processing->setTitle(QCoreApplication::translate("AfterNightAppClass", "Star Processing", nullptr));
        menuNoise_Reduction->setTitle(QCoreApplication::translate("AfterNightAppClass", "Noise Reduction", nullptr));
        menuColor_Spaces->setTitle(QCoreApplication::translate("AfterNightAppClass", "Color Spaces", nullptr));
        menuGeometry_2->setTitle(QCoreApplication::translate("AfterNightAppClass", "Geometry", nullptr));
        menuColor_Calibration->setTitle(
            QCoreApplication::translate("AfterNightAppClass", "Color Calibration", nullptr));
        menuMask_Generation->setTitle(QCoreApplication::translate("AfterNightAppClass", "Mask Generation", nullptr));
        menuBackground_Modelization->setTitle(
            QCoreApplication::translate("AfterNightAppClass", "Background Modelization", nullptr));
        menuDetails->setTitle(QCoreApplication::translate("AfterNightAppClass", "Details", nullptr));
        menuFilters->setTitle(QCoreApplication::translate("AfterNightAppClass", "Filters", nullptr));
        menuMultiscale->setTitle(QCoreApplication::translate("AfterNightAppClass", "Multiscale", nullptr));
        menuMorphology->setTitle(QCoreApplication::translate("AfterNightAppClass", "Morphology", nullptr));
        menuAdjustments->setTitle(QCoreApplication::translate("AfterNightAppClass", "Adjustments", nullptr));
        menuUtilities->setTitle(QCoreApplication::translate("AfterNightAppClass", "Utilities", nullptr));
        menuPixel_Math->setTitle(QCoreApplication::translate("AfterNightAppClass", "Pixel Math", nullptr));
        menuPreprocess->setTitle(QCoreApplication::translate("AfterNightAppClass", "Preprocess", nullptr));
        menuExtensions->setTitle(QCoreApplication::translate("AfterNightAppClass", "Extensions", nullptr));
        menuWindow->setTitle(QCoreApplication::translate("AfterNightAppClass", "Window", nullptr));
        menuView_2->setTitle(QCoreApplication::translate("AfterNightAppClass", "View", nullptr));
    } // retranslateUi
};

inline AfterNightAppUiElements buildAfterNightAppUi(QWidget* parent)
{
    AfterNightAppUiElements ui{};
    if (!parent)
    {
        return ui;
    }

    ui.setupUi(qobject_cast<QMainWindow*>(parent));
    return ui;
}

} // namespace an::ui::app
