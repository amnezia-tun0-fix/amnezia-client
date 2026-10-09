// Fork builds (org.amnezia.vpn.strict) look for updates among the fork's own GitHub releases
// instead of asking the Amnezia gateway, which knows nothing about them. Fork-only.
//
// The VPN service checks too (ForkUpdates.kt), since the app's window may stay closed for
// weeks. Both keep what they learn in files/forkUpdate.json, so that whoever checks first
// on a given day answers for the other.

#include "updateController.h"

#include <QDate>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QVersionNumber>

#include "amneziaApplication.h"
#include "logger.h"
#include "version.h"

namespace
{
    Logger logger("UpdateController");

    QString forkStatePath()
    {
        return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/forkUpdate.json";
    }

    // A check is due once per calendar day, local time. Same key as ForkUpdates.dayKey().
    QString forkDayKey()
    {
        return QDate::currentDate().toString(Qt::ISODate);
    }

    QJsonObject readForkState()
    {
        QFile file(forkStatePath());
        if (!file.open(QIODevice::ReadOnly)) {
            return {};
        }
        return QJsonDocument::fromJson(file.readAll()).object();
    }

    void writeForkState(const QJsonObject &state)
    {
        QSaveFile file(forkStatePath());
        if (file.open(QIODevice::WriteOnly)) {
            file.write(QJsonDocument(state).toJson(QJsonDocument::Compact));
            file.commit();
        }
    }

    QString forkVersion()
    {
        return QStringLiteral("%1-strict.%2").arg(QLatin1String(APP_VERSION)).arg(AMNEZIA_FORK_RELEASE);
    }

    // True for a tag vX-strict.N newer than this build.
    bool isNewerForkTag(const QString &tag)
    {
        static const QRegularExpression tagPattern(QStringLiteral("^v(\\d+(?:\\.\\d+)*)-strict\\.(\\d+)$"));
        const QRegularExpressionMatch match = tagPattern.match(tag);
        if (!match.hasMatch()) {
            return false;
        }
        const int baseOrder = QVersionNumber::compare(QVersionNumber::fromString(match.captured(1)),
                                                      QVersionNumber::fromString(QLatin1String(APP_VERSION)));
        return baseOrder > 0 || (baseOrder == 0 && match.captured(2).toInt() > AMNEZIA_FORK_RELEASE);
    }
}

bool UpdateController::isForkCheckDue()
{
    const QJsonObject state = readForkState();
    if (state.value("lastCheckDay").toString() != forkDayKey()) {
        return true;
    }
    logger.info() << "Update check skipped, already done today";
    // Checked today, by the app or by the VPN service: show what was found, without
    // opening the update page again.
    if (setForkRelease(state)) {
        emit updateKnown();
    }
    return false;
}

bool UpdateController::setForkRelease(const QJsonObject &state)
{
    const QString tag = state.value("latestTag").toString();
    if (!isNewerForkTag(tag)) {
        return false;
    }
    m_version = tag.mid(1);
    m_releaseDate = state.value("publishedAt").toString().left(10);
    m_description = state.value("latestName").toString();
    m_tags.clear();
    m_newFeatures.clear();
    m_improvements.clear();
    m_bugFixes.clear();
    m_downloadBaseUrl.clear();
    m_releasePageUrl = state.value("latestUrl").toString();
    setUpdateState(UpdateState::State::Idle);
    return true;
}

void UpdateController::checkForkRelease()
{
    QNetworkRequest request(QUrl(QStringLiteral("https://api.github.com/repos/%1/releases/latest")
                                         .arg(QLatin1String(AMNEZIA_FORK_RELEASES_REPO))));
    request.setTransferTimeout(7000);
    request.setRawHeader("Accept", "application/vnd.github+json");
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("AmneziaVPN/%1").arg(forkVersion()));

    QNetworkReply *reply = amnApp->networkManager()->get(request);

    QObject::connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        setUpdateCheckRunning(false);

        if (reply->error() != QNetworkReply::NoError) {
            logger.error() << "Fork release check failed, network error:" << static_cast<int>(reply->error())
                           << reply->errorString() << "HTTP status:"
                           << reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            emit updateCheckFailed();
            return;
        }

        const QJsonObject release = QJsonDocument::fromJson(reply->readAll()).object();
        QJsonObject state = readForkState();
        state["lastCheckDay"] = forkDayKey();
        state["latestTag"] = release.value("tag_name").toString();
        state["latestName"] = release.value("name").toString();
        state["latestUrl"] = release.value("html_url").toString();
        state["publishedAt"] = release.value("published_at").toString();
        writeForkState(state);

        if (!setForkRelease(state)) {
            logger.info() << "Latest fork release" << state.value("latestTag").toString() << "this build"
                          << forkVersion() << ": up to date";
            emit updateNotFound();
            return;
        }

        logger.info() << "Fork update available:" << m_version << "this build" << forkVersion();
        emit updateFound();
    });
}
