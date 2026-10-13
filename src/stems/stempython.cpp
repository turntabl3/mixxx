#include "stems/stempython.h"

#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

namespace mixxx {
namespace stempython {

namespace {

/// Stand-alone CPython builds published by python-build-standalone, which is
/// what makes a machine with no Python usable: the archive is a relocatable
/// interpreter, not an installer, so it can be unpacked into Mixxx's own
/// directory and run from there.
///
/// The versions are pinned. Resolving "latest" would mean parsing a GitHub API
/// response in the middle of an install, and would silently change what gets
/// downloaded; a fixed pair is reproducible and is meant to be bumped on
/// purpose. 3.12 is chosen because PyTorch publishes wheels for it across all
/// the architectures Mixxx runs on, and this interpreter is only ever used as
/// the base for an environment whose whole point is PyTorch.
const QString kPythonBuildTag = QStringLiteral("20261009");
const QString kPythonVersion = QStringLiteral("3.12.15");
const QString kPythonReleaseBase = QStringLiteral(
        "https://github.com/astral-sh/python-build-standalone/releases/download");

QString targetTriple() {
#if defined(Q_OS_WIN)
#if defined(Q_PROCESSOR_ARM_64)
    return QStringLiteral("aarch64-pc-windows-msvc");
#else
    return QStringLiteral("x86_64-pc-windows-msvc");
#endif
#elif defined(Q_OS_MACOS)
#if defined(Q_PROCESSOR_ARM_64)
    return QStringLiteral("aarch64-apple-darwin");
#else
    return QStringLiteral("x86_64-apple-darwin");
#endif
#elif defined(Q_PROCESSOR_ARM_64)
    return QStringLiteral("aarch64-unknown-linux-gnu");
#else
    return QStringLiteral("x86_64-unknown-linux-gnu");
#endif
}

/// The tools used to fetch and unpack. Both ship with Windows 10 and later, and
/// with every Linux Mixxx runs on, so in practice one of these pairs is always
/// there; checking means the failure is "install Python yourself" rather than
/// a mystery about a missing binary.
QString fetchTool() {
    for (const QString& candidate : {QStringLiteral("curl"), QStringLiteral("wget")}) {
        const QString found = QStandardPaths::findExecutable(candidate);
        if (!found.isEmpty()) {
            return found;
        }
    }
    return QString();
}

QString unpackTool() {
    return QStandardPaths::findExecutable(QStringLiteral("tar"));
}

} // namespace

QString findInterpreter(const QString& configured) {
    if (!configured.isEmpty() && QFileInfo(configured).isExecutable()) {
        return configured;
    }
#if defined(Q_OS_WIN)
    const QStringList candidates = {
            QStringLiteral("python.exe"), QStringLiteral("python3.exe")};
#else
    const QStringList candidates = {
            QStringLiteral("python3"), QStringLiteral("python")};
#endif
    for (const QString& candidate : candidates) {
        const QString found = QStandardPaths::findExecutable(candidate);
        if (!found.isEmpty()) {
            return found;
        }
    }
    return QString();
}

QString downloadUrl() {
    const QString triple = targetTriple();
    if (triple.isEmpty()) {
        return QString();
    }
    return QStringLiteral("%1/%2/cpython-%3+%2-%4-install_only.tar.gz")
            .arg(kPythonReleaseBase, kPythonBuildTag, kPythonVersion, triple);
}

QString downloadedInterpreter(const QString& environmentDir) {
#if defined(Q_OS_WIN)
    const QString interpreter = QStringLiteral("python/python.exe");
#else
    const QString interpreter = QStringLiteral("python/bin/python3");
#endif
    return QDir(environmentDir).filePath(interpreter);
}

QStringList downloadCommand(const QString& url, const QString& archivePath) {
    const QString tool = fetchTool();
    if (tool.isEmpty()) {
        return QStringList();
    }
    if (QFileInfo(tool).fileName().startsWith(QStringLiteral("wget"))) {
        // -O is wget's output file; curl spells it -o. Both follow redirects of
        // their own accord here.
        return {tool, QStringLiteral("-O"), archivePath, url};
    }
    return {tool,
            QStringLiteral("-fsSL"),
            QStringLiteral("-o"),
            archivePath,
            url};
}

QStringList unpackCommand(const QString& archivePath, const QString& targetDir) {
    const QString tool = unpackTool();
    if (tool.isEmpty()) {
        return QStringList();
    }
    return {tool, QStringLiteral("-xzf"), archivePath, QStringLiteral("-C"), targetDir};
}

} // namespace stempython
} // namespace mixxx
