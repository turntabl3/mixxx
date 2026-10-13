#pragma once

#include <QString>
#include <QStringList>

namespace mixxx {
namespace stempython {

/// Interpreter to run the installer with.
///
/// Not the environment's own interpreter: on a machine where the environment
/// has never been created that one does not exist yet, so this looks for
/// something the machine already has. Returns the configured path when it is
/// real, otherwise one from PATH, otherwise an empty string.
QString findInterpreter(const QString& configured);

/// URL of a stand-alone CPython build for this platform, or empty when this
/// platform is not one we know how to fetch for.
///
/// Used when a machine has no Python at all, which is the normal state of a
/// Windows install and possible anywhere. The build is pinned rather than
/// resolved to "latest" so that a provisioning run is reproducible: bump
/// kPythonBuildTag and kPythonVersion together.
QString downloadUrl();

/// Where downloadUrl()'s archive puts the interpreter once unpacked.
QString downloadedInterpreter(const QString& environmentDir);

/// Command lines for fetching and unpacking, using tools the platform already
/// has (curl or wget, and tar). Empty when neither is available, in which case
/// the caller should tell the user to install Python by hand rather than
/// pretend it can proceed.
QStringList downloadCommand(const QString& url, const QString& archivePath);
QStringList unpackCommand(const QString& archivePath, const QString& targetDir);

} // namespace stempython
} // namespace mixxx
