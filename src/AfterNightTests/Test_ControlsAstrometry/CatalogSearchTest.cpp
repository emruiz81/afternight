// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The catalog search: the online resolvers' requests and answers, the bundled catalogs searched by name in the
/// session - read on a worker by whoever searches and kept by the service that read them - and the search window.

#include "app/ApplicationStorage.h"
#include "Test_ControlsAstrometry/AstrometryTestSupport.h"

#include "app/CatalogSearchService.h"
#include "controls/settings/SettingsKeys.h"
#include "dialogs/CatalogSearchDialog.h"

#include <QCoreApplication>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QTableWidget>
#include <QThread>
#include <QUrlQuery>
#include <QtTest/QTest>

#include <gtest/gtest.h>

#include <atomic>
#include <memory>
#include <optional>

namespace
{

using an::tests::astrometry::fixedCatalogs;
using an::tests::astrometry::InstalledContext;
using an::tests::astrometry::openViewerContext;
using an::tests::astrometry::pumpUntil;
using an::tests::astrometry::sourceTreeCatalogs;
using an::tests::astrometry::ViewerTestContext;

class CatalogSearchSettingsGuard
{
public:
    CatalogSearchSettingsGuard()
    {
        QSettings settings = an::ui::applicationSettings();
        m_hadValue = settings.contains(QString::fromUtf8(SettingsKeys::PLATESOLVE_RECENT_OBJECTS));
        m_previousValue = settings.value(QString::fromUtf8(SettingsKeys::PLATESOLVE_RECENT_OBJECTS)).toStringList();
        settings.remove(QString::fromUtf8(SettingsKeys::PLATESOLVE_RECENT_OBJECTS));
    }

    ~CatalogSearchSettingsGuard()
    {
        QSettings settings = an::ui::applicationSettings();
        if (m_hadValue)
        {
            settings.setValue(QString::fromUtf8(SettingsKeys::PLATESOLVE_RECENT_OBJECTS), m_previousValue);
        }
        else
        {
            settings.remove(QString::fromUtf8(SettingsKeys::PLATESOLVE_RECENT_OBJECTS));
        }
    }

private:
    bool m_hadValue = false;
    QStringList m_previousValue;
};

/// The local search's cases: a context lending a session, installed for the case.
class LocalCatalogSearchTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        m_context = openViewerContext();
        ASSERT_NE(m_context, nullptr);
        m_installed.emplace(*m_context);
    }

    void TearDown() override
    {
        m_installed.reset();
        m_context.reset();
    }

    /// Searches through a service and turns the event loop until it answers.
    [[nodiscard]] std::optional<an::ui::CatalogSearchResult> searchThrough(an::ui::CatalogSearchService& service,
                                                                           const QString& name)
    {
        std::optional<an::ui::CatalogSearchResult> answered;
        const QMetaObject::Connection connection =
            QObject::connect(&service, &an::ui::CatalogSearchService::searchFinished, &service,
                             [&answered](const an::ui::CatalogSearchResult& result) { answered = result; });
        service.searchByName(name, an::ui::CatalogServer::LocalCatalog);
        EXPECT_TRUE(pumpUntil([&answered]() { return answered.has_value(); }));
        QObject::disconnect(connection);
        return answered;
    }

    std::unique_ptr<ViewerTestContext> m_context;
    std::optional<InstalledContext> m_installed;
};

} // namespace

TEST(CatalogSearchService, SimbadTapResponseParsesCorrectly)
{
    const QByteArray payload(
        "OID\tra\tdec\tmain_id\totype_longname\ttype_codes\taliases\n"
        "123\t10.6847\t41.2687\tM 31\tGalaxy\tG|EmG\t{\"M 31\",\"NAME Andromeda Galaxy\",\"NGC 224\"}\n");

    const auto result = an::ui::catalog_search::parseResponse(an::ui::CatalogServer::Simbad, payload);

    ASSERT_TRUE(result.success);
    ASSERT_EQ(result.entries.size(), 1);
    EXPECT_EQ(result.entries.front().objectName, QStringLiteral("M 31"));
    EXPECT_EQ(result.entries.front().objectType, QStringLiteral("Galaxy"));
    EXPECT_NEAR(result.entries.front().raDeg, 10.6847, 1e-6);
    EXPECT_NEAR(result.entries.front().decDeg, 41.2687, 1e-6);
    EXPECT_EQ(result.entries.front().resolver, QStringLiteral("SIMBAD"));
    EXPECT_TRUE(result.entries.front().description.contains(QStringLiteral("Andromeda Galaxy")));
    EXPECT_TRUE(result.entries.front().description.contains(QStringLiteral("SIMBAD type codes: G, EmG")));
    ASSERT_EQ(result.entries.front().alternateNames.size(), 1);
    EXPECT_EQ(result.entries.front().alternateNames.front(), QStringLiteral("NGC 224"));
}

TEST(CatalogSearchService, SesameXmlResponseParsesCorrectly)
{
    const QByteArray payload(R"xml(
<Sesame>
  <Target>
    <Resolver name="S=Simbad">
      <jpos>00:42:44.33 +41:16:09.1</jpos>
      <jradeg>10.6847</jradeg>
      <jdedeg>41.2687</jdedeg>
      <oname>M 31</oname>
      <otype>G</otype>
      <alias>M 31</alias>
      <alias>NAME Andromeda Galaxy</alias>
      <refPos>2006AJ....131.1163S</refPos>
    </Resolver>
    <Resolver name="V=VizieR">
      <jpos>05:34:31.94 +22:00:52.2</jpos>
      <jradeg>83.6331</jradeg>
      <jdedeg>22.0145</jdedeg>
      <oname>Crab Nebula</oname>
      <otype>SNR</otype>
      <alias>Messier 1</alias>
      <MType>Filamentary</MType>
    </Resolver>
  </Target>
</Sesame>
)xml");

    const auto result = an::ui::catalog_search::parseResponse(an::ui::CatalogServer::CdsSesame, payload);

    ASSERT_TRUE(result.success);
    ASSERT_EQ(result.entries.size(), 2);
    EXPECT_EQ(result.entries.at(0).objectName, QStringLiteral("M 31"));
    EXPECT_EQ(result.entries.at(0).resolver, QStringLiteral("S=Simbad"));
    EXPECT_TRUE(result.entries.at(0).description.contains(QStringLiteral("Andromeda Galaxy")));
    EXPECT_TRUE(result.entries.at(0).description.contains(QStringLiteral("Reference position: 2006AJ....131.1163S")));
    EXPECT_NEAR(result.entries.at(1).raDeg, 83.6331, 1e-6);
    EXPECT_NEAR(result.entries.at(1).decDeg, 22.0145, 1e-6);
    EXPECT_EQ(result.entries.at(1).objectType, QStringLiteral("SNR"));
    EXPECT_TRUE(result.entries.at(1).description.contains(QStringLiteral("Morphology: Filamentary")));
    ASSERT_EQ(result.entries.at(1).alternateNames.size(), 1);
    EXPECT_EQ(result.entries.at(1).alternateNames.front(), QStringLiteral("Messier 1"));
}

TEST(CatalogSearchService, EmptyResponseReturnsNoResults)
{
    const auto simbad = an::ui::catalog_search::parseResponse(an::ui::CatalogServer::Simbad, QByteArray());
    const auto sesame = an::ui::catalog_search::parseResponse(an::ui::CatalogServer::VizierSesame, QByteArray());

    EXPECT_TRUE(simbad.success);
    EXPECT_TRUE(simbad.entries.isEmpty());
    EXPECT_TRUE(sesame.success);
    EXPECT_TRUE(sesame.entries.isEmpty());
}

TEST(CatalogSearchService, MalformedResponseReturnsError)
{
    const auto simbad = an::ui::catalog_search::parseResponse(
        an::ui::CatalogServer::Simbad,
        QByteArray("OID\tra\tdec\tmain_id\totype_longname\n123\tnot-a-number\t41.2\tM 31\tGalaxy\n"));
    const auto sesame = an::ui::catalog_search::parseResponse(
        an::ui::CatalogServer::CdsSesame, QByteArray("<Sesame><Target><Resolver><jpos>bad coords</jpos></Resolver>"));

    EXPECT_FALSE(simbad.success);
    EXPECT_FALSE(simbad.errorMessage.isEmpty());
    EXPECT_FALSE(sesame.success);
    EXPECT_FALSE(sesame.errorMessage.isEmpty());
}

TEST(CatalogSearchService, ObjectNameUrlEncodedCorrectly)
{
    const QUrl url =
        an::ui::catalog_search::buildRequestUrl(an::ui::CatalogServer::CdsSesame, QStringLiteral("NGC 7000 + IC 5070"));

    EXPECT_EQ(QString::fromLatin1(url.toEncoded()),
              QStringLiteral("https://cds.unistra.fr/cgi-bin/nph-sesame/-oxI/A?NGC%207000%20%2B%20IC%205070"));
}

TEST(CatalogSearchService, SimbadRequestBodyUsesValidTapColumns)
{
    const QByteArray body =
        an::ui::catalog_search::buildRequestBody(an::ui::CatalogServer::Simbad, QStringLiteral("m83"));

    const QString decoded = QUrlQuery(QString::fromUtf8(body)).queryItemValue(QStringLiteral("query"));

    EXPECT_EQ(decoded, QStringLiteral("SELECT basic.oid, basic.ra, basic.dec, basic.main_id, "
                                      "otypedef.otype_longname, basic.otype AS type_codes, "
                                      "array_agg(ident_alias.id) AS aliases "
                                      "FROM ident "
                                      "JOIN basic ON ident.oidref = basic.oid "
                                      "JOIN otypedef ON basic.otype = otypedef.otype "
                                      "LEFT JOIN ident AS ident_alias ON ident_alias.oidref = basic.oid "
                                      "WHERE ident.id = 'm83' "
                                      "GROUP BY basic.oid, basic.ra, basic.dec, basic.main_id, "
                                      "otypedef.otype_longname, basic.otype"));
}

TEST_F(LocalCatalogSearchTest, BothBundledCatalogsLoadInTheSession)
{
    const an::ui::catalog_search::LocalCatalogs catalogs =
        an::ui::catalog_search::loadLocalCatalogs(m_context->engineSession(), {}, sourceTreeCatalogs());

    EXPECT_TRUE(catalogs.deepSkyError.isEmpty()) << catalogs.deepSkyError.toStdString();
    EXPECT_TRUE(catalogs.brightStarsError.isEmpty()) << catalogs.brightStarsError.toStdString();
    EXPECT_GT(catalogs.deepSky.entryCount(), 1000u);
    EXPECT_GT(catalogs.brightStars.entryCount(), 1000u);
    EXPECT_TRUE(catalogs.deepSky.hasNameIndex());
    EXPECT_TRUE(catalogs.brightStars.hasNameIndex());
}

TEST_F(LocalCatalogSearchTest, TheBundledCatalogsAnswerByName)
{
    const an::ui::catalog_search::LocalCatalogs catalogs =
        an::ui::catalog_search::loadLocalCatalogs(m_context->engineSession(), {}, sourceTreeCatalogs());
    std::optional<an::consumer::OperationScope> scope = an::tests::viewer::admitted(m_context->engineSession());
    ASSERT_TRUE(scope.has_value());

    const an::ui::CatalogSearchResult deepSky =
        an::ui::catalog_search::searchLocalCatalogs(scope->execution(), catalogs, QStringLiteral("M 31"));
    ASSERT_TRUE(deepSky.success) << deepSky.errorMessage.toStdString();
    ASSERT_FALSE(deepSky.entries.isEmpty());
    EXPECT_EQ(deepSky.entries.front().objectName, QStringLiteral("M 31"));
    EXPECT_NEAR(deepSky.entries.front().raDeg, 10.68, 0.01);

    const an::ui::CatalogSearchResult star =
        an::ui::catalog_search::searchLocalCatalogs(scope->execution(), catalogs, QStringLiteral("Vega"));
    ASSERT_TRUE(star.success) << star.errorMessage.toStdString();
    ASSERT_FALSE(star.entries.isEmpty());
    EXPECT_EQ(star.entries.front().objectName, QStringLiteral("Vega"));
}

TEST_F(LocalCatalogSearchTest, ATypedDesignationIsReadAsTheCatalogSpellsIt)
{
    const an::ui::catalog_search::LocalCatalogs catalogs =
        an::ui::catalog_search::loadLocalCatalogs(m_context->engineSession(), {}, sourceTreeCatalogs());
    std::optional<an::consumer::OperationScope> scope = an::tests::viewer::admitted(m_context->engineSession());
    ASSERT_TRUE(scope.has_value());

    const an::ui::CatalogSearchResult typed =
        an::ui::catalog_search::searchLocalCatalogs(scope->execution(), catalogs, QStringLiteral("m31"));
    ASSERT_TRUE(typed.success) << typed.errorMessage.toStdString();
    ASSERT_FALSE(typed.entries.isEmpty());
    EXPECT_EQ(typed.entries.front().objectName, QStringLiteral("M 31"));
}

TEST_F(LocalCatalogSearchTest, ALocalSearchReadsOnAWorkerAndAnswersOnTheGuiThread)
{
    an::ui::CatalogSearchService service;
    std::atomic<bool> readOnWorker{false};
    an::ui::BundledCatalogSource tree = sourceTreeCatalogs();
    service.setLocalCatalogSource([&readOnWorker, tree](const an::ui::BundledCatalog catalog) {
        readOnWorker = QThread::currentThread() != QCoreApplication::instance()->thread();
        return tree(catalog);
    });

    bool answeredOnGuiThread = false;
    QObject::connect(&service, &an::ui::CatalogSearchService::searchFinished, &service,
                     [&answeredOnGuiThread](const an::ui::CatalogSearchResult&) {
                         answeredOnGuiThread = QThread::currentThread() == QCoreApplication::instance()->thread();
                     });
    const std::optional<an::ui::CatalogSearchResult> result = searchThrough(service, QStringLiteral("M 42"));

    ASSERT_TRUE(result.has_value());
    ASSERT_TRUE(result->success) << result->errorMessage.toStdString();
    ASSERT_FALSE(result->entries.isEmpty());
    EXPECT_EQ(result->entries.front().objectName, QStringLiteral("M 42"));
    EXPECT_TRUE(readOnWorker.load());
    EXPECT_TRUE(answeredOnGuiThread);
}

TEST_F(LocalCatalogSearchTest, TheServiceReadsItsCatalogsOnceAndKeepsThem)
{
    an::ui::CatalogSearchService service;
    std::atomic<int> reads{0};
    an::ui::BundledCatalogSource tree = sourceTreeCatalogs();
    service.setLocalCatalogSource([&reads, tree](const an::ui::BundledCatalog catalog) {
        ++reads;
        return tree(catalog);
    });

    ASSERT_TRUE(searchThrough(service, QStringLiteral("M 31")).has_value());
    ASSERT_TRUE(searchThrough(service, QStringLiteral("Vega")).has_value());
    EXPECT_EQ(reads.load(), 2);
}

TEST_F(LocalCatalogSearchTest, ACatalogThatCannotBeReadIsReportedAndReadAgainNextTime)
{
    an::ui::CatalogSearchService service;
    std::atomic<bool> fail{true};
    an::ui::BundledCatalogSource tree = sourceTreeCatalogs();
    service.setLocalCatalogSource([&fail, tree](const an::ui::BundledCatalog catalog) -> an::Result<QByteArray> {
        if (fail.load())
        {
            return an::Status::failure(an::ErrorCode::ExternalFailure, "the catalog is not there");
        }
        return tree(catalog);
    });

    const std::optional<an::ui::CatalogSearchResult> first = searchThrough(service, QStringLiteral("M 31"));
    ASSERT_TRUE(first.has_value());
    EXPECT_FALSE(first->success);
    EXPECT_TRUE(first->errorMessage.contains(QStringLiteral("not there"))) << first->errorMessage.toStdString();

    fail = false;
    const std::optional<an::ui::CatalogSearchResult> second = searchThrough(service, QStringLiteral("M 31"));
    ASSERT_TRUE(second.has_value());
    EXPECT_TRUE(second->success) << second->errorMessage.toStdString();
}

TEST_F(LocalCatalogSearchTest, AnUnknownNameIsASuccessWithNoEntries)
{
    an::ui::CatalogSearchService service;
    service.setLocalCatalogSource(fixedCatalogs(
        QByteArray("id,common_name,ra_deg,dec_deg,major_axis_arcmin,minor_axis_arcmin,position_angle_deg,object_type,"
                   "magnitude\nNGC 1,,1.0,27.7,1.0,1.0,0,Galaxy,13.0\n"),
        QByteArray("id,common_name,bayer_designation,ra_deg,dec_deg,magnitude,spectral_type\n"
                   "HIP 1,Sirius,alf CMa,101.2872,-16.7161,-1.46,A1V\n")));

    const std::optional<an::ui::CatalogSearchResult> result = searchThrough(service, QStringLiteral("Nothing Here"));
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->success) << result->errorMessage.toStdString();
    EXPECT_TRUE(result->entries.isEmpty());
}

TEST(CatalogSearchDialog, RecentHistoryStoresAndRecalls)
{
    const QStringList updated = an::ui::catalog_search::updatedRecentHistory(
        {QStringLiteral("M 31"), QStringLiteral("vega"), QStringLiteral("M 31")}, QStringLiteral("Vega"), 20);

    ASSERT_EQ(updated.size(), 2);
    EXPECT_EQ(updated.at(0), QStringLiteral("Vega"));
    EXPECT_EQ(updated.at(1), QStringLiteral("M 31"));
}

TEST(CatalogSearchDialog, RecentHistoryLimitedTo20)
{
    QStringList history;
    for (int i = 0; i < 25; ++i)
    {
        history.push_back(QStringLiteral("Object %1").arg(i));
    }

    const QStringList updated =
        an::ui::catalog_search::updatedRecentHistory(history, QStringLiteral("Newest Object"), 20);

    ASSERT_EQ(updated.size(), 20);
    EXPECT_EQ(updated.front(), QStringLiteral("Newest Object"));
    EXPECT_EQ(updated.back(), QStringLiteral("Object 18"));
}

TEST(CatalogSearchDialog, FitsObjectKeywordPreFillsSearchField)
{
    CatalogSearchSettingsGuard settings_guard;
    an::ui::CatalogSearchDialog dialog(QStringLiteral("NGC 7000"));

    auto* query_edit = dialog.findChild<QLineEdit*>(QStringLiteral("catalogQueryEdit"));
    ASSERT_NE(query_edit, nullptr);
    EXPECT_EQ(query_edit->text(), QStringLiteral("NGC 7000"));
}

TEST(CatalogSearchDialog, DialogReturnsSelectedEntry)
{
    CatalogSearchSettingsGuard settings_guard;
    an::ui::CatalogSearchDialog dialog(QStringLiteral("M 31"));
    dialog.show();

    an::ui::CatalogSearchResult result;
    result.success = true;
    result.entries = {
        an::ui::CatalogSearchEntry{QStringLiteral("M 31"), QStringLiteral("Galaxy"), 10.6847, 41.2687,
                                   QStringLiteral("SIMBAD")},
        an::ui::CatalogSearchEntry{QStringLiteral("M 32"), QStringLiteral("Galaxy"), 10.6750, 40.8650,
                                   QStringLiteral("SIMBAD")},
    };
    dialog.applySearchResult(result);

    auto* results_table = dialog.findChild<QTableWidget*>(QStringLiteral("catalogResultsTable"));
    auto* ok_button = dialog.findChild<QPushButton*>(QStringLiteral("catalogOkButton"));
    auto* description_edit = dialog.findChild<QPlainTextEdit*>(QStringLiteral("catalogDescriptionEdit"));
    auto* aliases_edit = dialog.findChild<QPlainTextEdit*>(QStringLiteral("catalogAliasesEdit"));
    ASSERT_NE(results_table, nullptr);
    ASSERT_NE(ok_button, nullptr);
    ASSERT_NE(description_edit, nullptr);
    ASSERT_NE(aliases_edit, nullptr);

    results_table->selectRow(1);
    QTest::qWait(0);
    ASSERT_TRUE(ok_button->isEnabled());
    EXPECT_TRUE(description_edit->toPlainText().contains(QStringLiteral("No description")));
    EXPECT_TRUE(aliases_edit->toPlainText().contains(QStringLiteral("No alternate")));

    QTest::mouseClick(ok_button, Qt::LeftButton);

    EXPECT_EQ(dialog.result(), QDialog::Accepted);
    EXPECT_EQ(dialog.selectedEntry().objectName, QStringLiteral("M 32"));
    EXPECT_NEAR(dialog.selectedEntry().raDeg, 10.6750, 1e-6);
}

TEST(CatalogSearchDialog, TheResultsShowTheirCoordinatesRoundedOnceIntoEveryField)
{
    // 59.996 seconds round once, into every field: the next minute at zero seconds, never 60.00s under the same
    // minute.
    CatalogSearchSettingsGuard settings_guard;
    an::ui::CatalogSearchDialog dialog(QStringLiteral("edge"));
    an::ui::CatalogSearchResult result;
    result.success = true;
    // 10h 29m 59.996s and +41 29' 59.96".
    result.entries = {an::ui::CatalogSearchEntry{QStringLiteral("edge"), QStringLiteral("Star"),
                                                 (10.0 + 29.0 / 60.0 + 59.996 / 3600.0) * 15.0,
                                                 41.0 + 29.0 / 60.0 + 59.96 / 3600.0, QStringLiteral("local")}};
    dialog.applySearchResult(result);

    auto* results_table = dialog.findChild<QTableWidget*>(QStringLiteral("catalogResultsTable"));
    ASSERT_NE(results_table, nullptr);
    ASSERT_EQ(results_table->rowCount(), 1);
    EXPECT_EQ(results_table->item(0, 2)->text(), QStringLiteral("10h 30m 00.00s"));
    EXPECT_EQ(results_table->item(0, 3)->text(), QStringLiteral("+41° 30' 00.0\""));
}

TEST(CatalogSearchDialog, DetailsPaneShowsDescriptionAndAliases)
{
    CatalogSearchSettingsGuard settings_guard;
    an::ui::CatalogSearchDialog dialog(QStringLiteral("M 83"));
    dialog.show();

    an::ui::CatalogSearchResult result;
    result.success = true;
    an::ui::CatalogSearchEntry entry;
    entry.objectName = QStringLiteral("M 83");
    entry.objectType = QStringLiteral("Starburst Galaxy");
    entry.raDeg = 204.25383;
    entry.decDeg = -29.86576111;
    entry.resolver = QStringLiteral("SIMBAD");
    entry.description = QStringLiteral("Southern Pinwheel Galaxy\nSIMBAD type codes: G, SBG");
    entry.alternateNames = {QStringLiteral("NGC 5236"), QStringLiteral("Messier 83")};
    result.entries = {entry};
    dialog.applySearchResult(result);

    auto* results_table = dialog.findChild<QTableWidget*>(QStringLiteral("catalogResultsTable"));
    auto* description_edit = dialog.findChild<QPlainTextEdit*>(QStringLiteral("catalogDescriptionEdit"));
    auto* aliases_edit = dialog.findChild<QPlainTextEdit*>(QStringLiteral("catalogAliasesEdit"));
    ASSERT_NE(results_table, nullptr);
    ASSERT_NE(description_edit, nullptr);
    ASSERT_NE(aliases_edit, nullptr);

    results_table->selectRow(0);
    QTest::qWait(0);

    EXPECT_TRUE(description_edit->toPlainText().contains(QStringLiteral("Southern Pinwheel Galaxy")));
    EXPECT_TRUE(aliases_edit->toPlainText().contains(QStringLiteral("NGC 5236")));
    EXPECT_TRUE(aliases_edit->toPlainText().contains(QStringLiteral("Messier 83")));
}
