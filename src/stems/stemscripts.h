#pragma once

#include <QString>

namespace mixxx {
namespace stemscripts {

/// Writes the embedded separation scripts into targetDir.
///
/// The pipeline (separate.py, stemmp4.py, provision.py) is compiled into the
/// executable as a Qt resource rather than shipped as loose files, so an
/// installation consists of the application alone and there is nothing to lose
/// track of. The interpreter still needs them as real files it can read and
/// import, so they are written out next to the environment that runs them.
///
/// A copy that is already byte-identical is left alone; a stale one, e.g. after
/// an application update changed a script, is overwritten. Returns false and
/// sets pReason if a script is missing from the binary or cannot be written.
bool materialize(const QString& targetDir, QString* pReason);

/// Name of the script that performs a separation, inside a materialized
/// directory.
QString separationScriptName();

/// Name of the script that creates the interpreter environment, inside a
/// materialized directory.
QString provisionScriptName();

} // namespace stemscripts
} // namespace mixxx
