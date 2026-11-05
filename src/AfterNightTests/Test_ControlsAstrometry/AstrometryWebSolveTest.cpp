// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The astrometry.net web solver as a state machine on the solve's worker: it logs in, uploads the image with the
/// hints, polls the submission for a job and the job for its answer, and downloads the answer - each step started when
/// the reply before it finishes or its poll timer fires - against a local service that answers as the case says,
/// while the GUI thread only turns its event loop.

#include "Test_ControlsAstrometry/AstrometryTestSupport.h"

#include "app/CancellationToken.h"
#include "app/PlateSolveService.h"

#include <QByteArray>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QUrl>

#include <gtest/gtest.h>

#include <chrono>
#include <functional>
#include <memory>
#include <optional>

namespace
{

using an::tests::astrometry::addCard;
using an::tests::astrometry::InstalledContext;
using an::tests::astrometry::makeSolvedHeaderBytes;
using an::tests::astrometry::metadataWithCards;
using an::tests::astrometry::MockSolverProcessRunner;
using an::tests::astrometry::openViewerContext;
using an::tests::astrometry::pumpUntil;
using an::tests::astrometry::ViewerTestContext;
using an::tests::astrometry::writeFileBytes;

/// One request the service received.
struct ReceivedRequest
{
    QByteArray method;
    QString path;
    QByteArray body;
};

/// What the service answers a request with: a status line and a body.
struct Answer
{
    QByteArray status = QByteArrayLiteral("200 OK");
    QByteArray body;
};

/// A local HTTP service on the GUI thread that answers each request through the case's handler and records it.
class LocalWebService
{
public:
    using Handler = std::function<Answer(const ReceivedRequest&)>;

    explicit LocalWebService(Handler handler) : m_handler(std::move(handler))
    {
        EXPECT_TRUE(m_server.listen(QHostAddress::LocalHost, 0)) << m_server.errorString().toStdString();
        QObject::connect(&m_server, &QTcpServer::newConnection, &m_server, [this]() { accept(); });
    }

    [[nodiscard]] QUrl baseUrl() const
    {
        return QUrl(QStringLiteral("http://127.0.0.1:%1").arg(m_server.serverPort()));
    }

    /// The requests received so far, in order, as "METHOD /path".
    [[nodiscard]] QStringList requestLines() const
    {
        QStringList lines;
        for (const ReceivedRequest& request : m_received)
        {
            lines << QString::fromLatin1(request.method) + QLatin1Char(' ') + request.path;
        }
        return lines;
    }

    [[nodiscard]] const QList<ReceivedRequest>& received() const noexcept
    {
        return m_received;
    }

private:
    void accept()
    {
        while (m_server.hasPendingConnections())
        {
            QTcpSocket* socket = m_server.nextPendingConnection();
            QObject::connect(socket, &QTcpSocket::disconnected, socket, &QTcpSocket::deleteLater);
            auto buffer = std::make_shared<QByteArray>();
            QObject::connect(socket, &QTcpSocket::readyRead, socket, [this, socket, buffer]() {
                buffer->append(socket->readAll());
                const qsizetype headerEnd = buffer->indexOf("\r\n\r\n");
                if (headerEnd < 0)
                {
                    return;
                }
                const QList<QByteArray> lines = buffer->left(headerEnd).split('\n');
                qsizetype contentLength = 0;
                for (const QByteArray& line : lines)
                {
                    if (line.trimmed().toLower().startsWith("content-length:"))
                    {
                        contentLength = line.mid(line.indexOf(':') + 1).trimmed().toLongLong();
                    }
                }
                if (buffer->size() < headerEnd + 4 + contentLength)
                {
                    return;
                }
                const QList<QByteArray> requestLine = lines.front().trimmed().split(' ');
                ReceivedRequest request;
                request.method = requestLine.value(0);
                request.path = QUrl(QString::fromLatin1(requestLine.value(1))).path();
                request.body = buffer->mid(headerEnd + 4, contentLength);
                buffer->clear();
                m_received.push_back(request);
                const Answer answer = m_handler(request);
                QByteArray response = "HTTP/1.1 " + answer.status + "\r\n";
                response += "Content-Type: application/json\r\n";
                response += "Content-Length: " + QByteArray::number(answer.body.size()) + "\r\n";
                response += "Connection: close\r\n\r\n";
                response += answer.body;
                socket->write(response);
                socket->disconnectFromHost();
            });
        }
    }

    Handler m_handler;
    QTcpServer m_server;
    QList<ReceivedRequest> m_received;
};

[[nodiscard]] QByteArray json(const QJsonObject& object)
{
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

/// A service that solves: a job is assigned on the second poll of the submission and solved on the second poll of
/// the job.
[[nodiscard]] LocalWebService::Handler solvingService(const QByteArray& answer)
{
    auto submissionPolls = std::make_shared<int>(0);
    auto jobPolls = std::make_shared<int>(0);
    return [answer, submissionPolls, jobPolls](const ReceivedRequest& request) -> Answer {
        if (request.path == QStringLiteral("/api/login"))
        {
            return {.body = json({{QStringLiteral("status"), QStringLiteral("success")},
                                  {QStringLiteral("session"), QStringLiteral("session-1")}})};
        }
        if (request.path == QStringLiteral("/api/upload"))
        {
            return {.body =
                        json({{QStringLiteral("status"), QStringLiteral("success")}, {QStringLiteral("subid"), 7}})};
        }
        if (request.path == QStringLiteral("/api/submissions/7"))
        {
            return {.body = ++*submissionPolls < 2 ? json({{QStringLiteral("jobs"), QJsonArray{}}})
                                                   : json({{QStringLiteral("jobs"), QJsonArray{QJsonValue(), 42}}})};
        }
        if (request.path == QStringLiteral("/api/jobs/42"))
        {
            return {.body = json({{QStringLiteral("status"),
                                   ++*jobPolls < 2 ? QStringLiteral("solving") : QStringLiteral("success")}})};
        }
        if (request.path == QStringLiteral("/wcs_file/42"))
        {
            return {.body = answer};
        }
        return {.status = QByteArrayLiteral("404 Not Found")};
    };
}

/// The web solver's cases: an installed context, an image file to upload, and settings that prefer the web service.
class AstrometryWebSolveTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        ASSERT_TRUE(m_directory.isValid());
        m_context = openViewerContext();
        ASSERT_NE(m_context, nullptr);
        m_installed.emplace(*m_context);
        m_image = QDir(m_directory.path()).filePath(QStringLiteral("upload.fits"));
        ASSERT_TRUE(writeFileBytes(m_image, QByteArray(2880, ' ')));
        m_settings.defaultSolver = QStringLiteral("astrometry_web");
        m_settings.astrometryApiKey = QStringLiteral("key-123");
        m_settings.gaiaCatalogDirectory = QDir(m_directory.path()).filePath(QStringLiteral("no_catalog"));
        m_settings.downsample = 2;
        m_settings.timeoutSeconds = 30;
    }

    void TearDown() override
    {
        m_installed.reset();
        m_context.reset();
    }

    /// A request about the uploaded image, its cards stating a target and optics.
    [[nodiscard]] an::ui::PlateSolveRequest request() const
    {
        std::vector<an::CodecCard> cards;
        addCard(cards, "OBJCTRA", "'08 02 00'");
        addCard(cards, "OBJCTDEC", "'+15 00 00'");
        addCard(cards, "FOCALLEN", "500");
        addCard(cards, "XPIXSZ", "3.8");
        an::ui::PlateSolveRequest made;
        made.imagePath = m_image;
        made.existingMetadata = metadataWithCards(std::move(cards));
        made.imageWidth = 640;
        made.imageHeight = 480;
        made.workDirectoryRoot = m_directory.path();
        return made;
    }

    /// Runs a solve on the thread pool and turns the event loop until it reports.
    [[nodiscard]] std::optional<an::ui::PlateSolveResult> solveAgainst(an::ui::PlateSolveService& service,
                                                                       const LocalWebService& web,
                                                                       const std::function<void()>& whileRunning = {})
    {
        service.setAstrometryWebEndpoint(web.baseUrl(), std::chrono::milliseconds(10));
        std::optional<an::ui::PlateSolveResult> finished;
        QObject::connect(&service, &an::ui::PlateSolveService::solveFinished, &service,
                         [&finished](const an::ui::PlateSolveResult& result) { finished = result; });
        service.solveAsync(request(), m_settings);
        if (whileRunning)
        {
            whileRunning();
        }
        EXPECT_TRUE(pumpUntil([&finished]() { return finished.has_value(); }));
        return finished;
    }

    QTemporaryDir m_directory;
    QString m_image;
    an::ui::PlateSolveSettings m_settings;
    std::unique_ptr<ViewerTestContext> m_context;
    std::optional<InstalledContext> m_installed;
};

} // namespace

TEST_F(AstrometryWebSolveTest, TheMachineLogsInUploadsPollsAndDownloadsTheAnswer)
{
    LocalWebService web(solvingService(makeSolvedHeaderBytes(320.0, 240.0, 120.5, 15.0, 0.0005)));
    an::ui::PlateSolveService service(std::make_unique<MockSolverProcessRunner>());

    const std::optional<an::ui::PlateSolveResult> result = solveAgainst(service, web);

    ASSERT_TRUE(result.has_value());
    ASSERT_TRUE(result->succeeded) << result->errorMessage.toStdString();
    EXPECT_EQ(result->solverUsed, QStringLiteral("Astrometry.net (web)"));
    ASSERT_TRUE(result->solution.has_value());
    EXPECT_NEAR(result->solution->referencePoint().rightAscensionDegrees, 120.5, 1e-9);
    EXPECT_NEAR(result->imageScaleArcsecPerPixel, 1.8, 1e-9);
    EXPECT_EQ(web.requestLines(),
              (QStringList{QStringLiteral("POST /api/login"), QStringLiteral("POST /api/upload"),
                           QStringLiteral("GET /api/submissions/7"), QStringLiteral("GET /api/submissions/7"),
                           QStringLiteral("GET /api/jobs/42"), QStringLiteral("GET /api/jobs/42"),
                           QStringLiteral("GET /wcs_file/42")}));
}

TEST_F(AstrometryWebSolveTest, TheLoginCarriesTheKeyAndTheUploadTheHints)
{
    LocalWebService web(solvingService(makeSolvedHeaderBytes(320.0, 240.0, 120.5, 15.0, 0.0005)));
    an::ui::PlateSolveService service(std::make_unique<MockSolverProcessRunner>());

    const std::optional<an::ui::PlateSolveResult> result = solveAgainst(service, web);
    ASSERT_TRUE(result.has_value());
    ASSERT_GE(web.received().size(), 2);

    const QByteArray login = QByteArray::fromPercentEncoding(web.received().at(0).body.mid(13));
    EXPECT_EQ(QJsonDocument::fromJson(login).object().value(QStringLiteral("apikey")).toString(),
              QStringLiteral("key-123"));

    const QByteArray upload = web.received().at(1).body;
    const qsizetype start = upload.indexOf("{\"");
    const qsizetype end = upload.indexOf("}", start);
    ASSERT_GE(start, 0);
    ASSERT_GT(end, start);
    const QJsonObject settings = QJsonDocument::fromJson(upload.mid(start, end - start + 1)).object();
    EXPECT_EQ(settings.value(QStringLiteral("session")).toString(), QStringLiteral("session-1"));
    EXPECT_NEAR(settings.value(QStringLiteral("center_ra")).toDouble(), 120.5, 1e-9);
    EXPECT_NEAR(settings.value(QStringLiteral("center_dec")).toDouble(), 15.0, 1e-9);
    const double scale = 3.8 / 500.0 * 206.26480624709636;
    EXPECT_NEAR(settings.value(QStringLiteral("scale_lower")).toDouble(), scale * 0.8, 1e-9);
    EXPECT_NEAR(settings.value(QStringLiteral("scale_upper")).toDouble(), scale * 1.2, 1e-9);
    EXPECT_EQ(settings.value(QStringLiteral("downsample_factor")).toInt(), 2);
    EXPECT_TRUE(upload.contains("filename=\"upload.fits\""));
}

TEST_F(AstrometryWebSolveTest, ARefusedLoginFailsTheWebSolverAndTheSolveGoesOn)
{
    LocalWebService web([](const ReceivedRequest&) -> Answer {
        return {.body = json({{QStringLiteral("status"), QStringLiteral("error")},
                              {QStringLiteral("errormessage"), QStringLiteral("bad apikey")}})};
    });
    an::ui::PlateSolveService service(std::make_unique<MockSolverProcessRunner>());

    const std::optional<an::ui::PlateSolveResult> result = solveAgainst(service, web);

    ASSERT_TRUE(result.has_value());
    EXPECT_FALSE(result->succeeded);
    EXPECT_TRUE(result->errorMessage.contains(QStringLiteral("login failed - bad apikey")))
        << result->errorMessage.toStdString();
    // The internal solver came next, and its catalog is not installed.
    EXPECT_TRUE(result->errorMessage.contains(QStringLiteral("Internal Gaia"))) << result->errorMessage.toStdString();
    EXPECT_EQ(web.requestLines(), QStringList{QStringLiteral("POST /api/login")});
}

TEST_F(AstrometryWebSolveTest, AJobTheServiceFailsIsReported)
{
    LocalWebService web([](const ReceivedRequest& request) -> Answer {
        if (request.path == QStringLiteral("/api/login"))
        {
            return {.body = json({{QStringLiteral("status"), QStringLiteral("success")},
                                  {QStringLiteral("session"), QStringLiteral("s")}})};
        }
        if (request.path == QStringLiteral("/api/upload"))
        {
            return {.body =
                        json({{QStringLiteral("status"), QStringLiteral("success")}, {QStringLiteral("subid"), 7}})};
        }
        if (request.path == QStringLiteral("/api/submissions/7"))
        {
            return {.body = json({{QStringLiteral("jobs"), QJsonArray{42}}})};
        }
        return {.body = json({{QStringLiteral("status"), QStringLiteral("failure")}})};
    });
    an::ui::PlateSolveService service(std::make_unique<MockSolverProcessRunner>());

    const std::optional<an::ui::PlateSolveResult> result = solveAgainst(service, web);

    ASSERT_TRUE(result.has_value());
    EXPECT_FALSE(result->succeeded);
    EXPECT_TRUE(result->errorMessage.contains(QStringLiteral("plate solving failed on server")))
        << result->errorMessage.toStdString();
}

TEST_F(AstrometryWebSolveTest, AnAnswerTheHeaderReaderRefusesFailsTheWebSolver)
{
    QByteArray truncated = makeSolvedHeaderBytes(320.0, 240.0, 120.5, 15.0, 0.0005);
    truncated.replace("END\n", "");
    LocalWebService web(solvingService(truncated));
    an::ui::PlateSolveService service(std::make_unique<MockSolverProcessRunner>());

    const std::optional<an::ui::PlateSolveResult> result = solveAgainst(service, web);

    ASSERT_TRUE(result.has_value());
    EXPECT_FALSE(result->succeeded);
    EXPECT_TRUE(result->errorMessage.contains(QStringLiteral("WCS file could not be parsed")))
        << result->errorMessage.toStdString();
}

TEST_F(AstrometryWebSolveTest, CancellingWhileWaitingForAJobEndsTheSolveCancelled)
{
    // The service never assigns a job; the solve is cancelled once the submission is being polled.
    LocalWebService web([](const ReceivedRequest& request) -> Answer {
        if (request.path == QStringLiteral("/api/login"))
        {
            return {.body = json({{QStringLiteral("status"), QStringLiteral("success")},
                                  {QStringLiteral("session"), QStringLiteral("s")}})};
        }
        if (request.path == QStringLiteral("/api/upload"))
        {
            return {.body =
                        json({{QStringLiteral("status"), QStringLiteral("success")}, {QStringLiteral("subid"), 7}})};
        }
        return {.body = json({{QStringLiteral("jobs"), QJsonArray{}}})};
    });
    an::ui::PlateSolveService service(std::make_unique<MockSolverProcessRunner>());

    const std::optional<an::ui::PlateSolveResult> result = solveAgainst(service, web, [&]() {
        ASSERT_TRUE(pumpUntil([&web]() { return web.received().size() >= 4; }));
        service.cancel();
    });

    ASSERT_TRUE(result.has_value());
    EXPECT_FALSE(result->succeeded);
    EXPECT_TRUE(result->cancelled);
    EXPECT_TRUE(result->errorMessage.contains(QStringLiteral("waiting for job"))) << result->errorMessage.toStdString();
}
