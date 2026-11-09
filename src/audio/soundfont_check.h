#pragma once
#include "core/model.h"
#include <QJsonObject>
namespace piano {
// Uses the production engine offline. Does not open audio devices or use network.
// Writes PCM16 stereo WAV previews and an atomic JSON report to outputDirectory.
// The report identifies the tested bank by SHA-256; it does not certify its origin.
QJsonObject checkSoundfont(const QString &path, const QString &outputDirectory, const Song &demo);
} // namespace piano
