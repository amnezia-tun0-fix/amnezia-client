// Fork builds (org.amnezia.vpn.strict) look for updates among the fork's own GitHub releases
// instead of asking the Amnezia gateway, which knows nothing about them. Fork-only.

#include "updateController.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QVersionNumber>

#include "amneziaApplication.h"
#include "logger.h"
#include "version.h"

namespace
{
    Logger logger("UpdateController");

    constexpr qint64 kForkCheckIntervalSecs = 24 * 60 * 60;
    const QLatin1String kLastForkCheckKey("lastUpdateCheck");

    QString forkSettingsPath()
    {
        return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/forkUpdateCheck.ini";
    }

    QString forkVersion()
    {
        return QStringLiteral("%1-strict.%2").arg(QLatin1String(APP_VERSION)).arg(AMNEZIA_FORK_RELEASE);
    }
}

bool UpdateController::isForkCheckDue() const
{
    QSettings settings(forkSettingsPath(), QSettings::IniFormat);
    const QDateTime lastCheck = settings.value(kLastForkCheckKey).toDateTime();
    const QDateTime now = QDateTime::currentDateTimeUtc();
    if (lastCheck.isValid() && lastCheck <= now && lastCheck.secsTo(now) < kForkCheckIntervalSecs) {
        logger.info() << "Update check skipped, last one at" << lastCheck.toString(Qt::ISODate);
        return false;
    }
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

        QSettings settings(forkSettingsPath(), QSettings::IniFormat);
        settings.setValue(kLastForkCheckKey, QDateTime::currentDateTimeUtc());

        const QJsonObject release = QJsonDocument::fromJson(reply->readAll()).object();
        const QString tag = release.value("tag_name").toString();

        static const QRegularExpression tagPattern(QStringLiteral("^v(\\d+(?:\\.\\d+)*)-strict\\.(\\d+)$"));
        const QRegularExpressionMatch match = tagPattern.match(tag);
        if (!match.hasMatch()) {
            logger.warning() << "Latest fork release" << tag << "is not named vX-strict.N, ignored";
            emit updateNotFound();
            return;
        }

        const int baseOrder = QVersionNumber::compare(QVersionNumber::fromString(match.captured(1)),
                                                      QVersionNumber::fromString(QLatin1String(APP_VERSION)));
        if (baseOrder < 0 || (baseOrder == 0 && match.captured(2).toInt() <= AMNEZIA_FORK_RELEASE)) {
            logger.info() << "Latest fork release" << tag << "this build" << forkVersion() << ": up to date";
            emit updateNotFound();
            return;
        }

        m_version = tag.mid(1);
        m_releaseDate = release.value("published_at").toString().left(10);
        m_description = release.value("name").toString();
        m_tags.clear();
        m_newFeatures.clear();
        m_improvements.clear();
        m_bugFixes.clear();
        m_downloadBaseUrl.clear();
        m_releasePageUrl = release.value("html_url").toString();

        logger.info() << "Fork update available:" << m_version << "this build" << forkVersion();
        setUpdateState(UpdateState::State::Idle);
        emit updateFound();
    });
}
