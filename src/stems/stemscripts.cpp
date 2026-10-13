#include "stems/stemscripts.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStringList>

namespace mixxx {
namespace stemscripts {

namespace {

// Kept in step with the res/mixxx.qrc entries. Order does not matter; the three
// are written together because a separation needs the first two and a
// provisioning run needs the third.
const QStringList kScriptNames = {
        QStringLiteral("separate.py"),
        QStringLiteral("stemmp4.py"),
        QStringLiteral("provision.py"),
};

QString resourcePath(const QString& name) {
    return QStringLiteral(":/stems/") + name;
}

/// The file's contents, or a null QByteArray when it cannot be read. Used both
/// for the copy that is already on disk and for the one in the binary.
QByteArray readAll(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return QByteArray();
    }
    return file.readAll();
}

} // namespace

bool materialize(const QString& targetDir, QString* pReason) {
    if (!QDir().mkpath(targetDir)) {
        *pReason = QStringLiteral("cannot create %1").arg(targetDir);
        return false;
    }

    for (const QString& name : kScriptNames) {
        const QString source = resourcePath(name);
        QFile file(source);
        if (!file.open(QIODevice::ReadOnly)) {
            // Not a configuration problem: the resource is compiled in, so its
            // absence means this build was packaged without it.
            *pReason = QStringLiteral(
                    "%1 is missing from this build of Mixxx, so stem separation "
                    "cannot run. This is a packaging fault.")
                               .arg(source);
            return false;
        }
        const QByteArray contents = file.readAll();
        file.close();

        const QString target = QDir(targetDir).filePath(name);
        if (readAll(target) == contents) {
            continue;
        }

        QFile output(target);
        if (!output.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
                output.write(contents) != contents.size()) {
            *pReason = QStringLiteral("cannot write %1").arg(target);
            return false;
        }
        output.close();
        // The interpreter runs these as the current user, and there is no
        // reason for anyone else to write to them.
        output.setPermissions(QFileDevice::ReadOwner |
                QFileDevice::WriteOwner |
                QFileDevice::ReadGroup);
    }
    return true;
}

QString separationScriptName() {
    return kScriptNames.first();
}

QString provisionScriptName() {
    return QStringLiteral("provision.py");
}

} // namespace stemscripts
} // namespace mixxx
