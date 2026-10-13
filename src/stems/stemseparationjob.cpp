#include "stems/stemseparationjob.h"

#include "moc_stemseparationjob.cpp"

namespace mixxx {

StemSeparationJob::StemSeparationJob(QObject* pParent,
        const QString& trackPath,
        const StemSeparator::Settings& settings)
        : QThread(pParent),
          m_trackPath(trackPath),
          m_settings(settings) {
}

void StemSeparationJob::run() {
    QString outputPath;
    QString error;
    const StemSeparator separator(m_settings);
    if (separator.separate(m_trackPath, &outputPath, &error)) {
        emit completed(m_trackPath, outputPath);
    } else {
        emit failed(m_trackPath, error);
    }
}

} // namespace mixxx
