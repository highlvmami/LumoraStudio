#include "AudioEffects.h"

#include <algorithm>

namespace lumora {

QJsonObject AudioEffects::toJson() const {
    QJsonObject o;
    if (lowCut > 0.0)
        o["lowCut"] = lowCut;
    if (humHz > 0)
        o["humHz"] = humHz;
    if (wind > 0.0)
        o["wind"] = wind;
    if (denoise > 0.0)
        o["denoise"] = denoise;
    if (hasEq()) {
        o["eqLow"] = eqLow;
        o["eqMid"] = eqMid;
        o["eqMidFreq"] = eqMidFreq;
        o["eqHigh"] = eqHigh;
    }
    if (compressor) {
        o["compressor"] = true;
        o["compThreshold"] = compThreshold;
        o["compRatio"] = compRatio;
        o["compMakeup"] = compMakeup;
    }
    if (duckDb != 0.0)
        o["duckDb"] = duckDb;
    return o;
}

AudioEffects AudioEffects::fromJson(const QJsonObject& o) {
    AudioEffects e;
    e.lowCut = std::clamp(o.value("lowCut").toDouble(0.0), 0.0, 1000.0);
    const int hum = o.value("humHz").toInt(0);
    e.humHz = hum == 50 || hum == 60 ? hum : 0;
    e.wind = std::clamp(o.value("wind").toDouble(0.0), 0.0, 100.0);
    e.denoise = std::clamp(o.value("denoise").toDouble(0.0), 0.0, 100.0);
    e.eqLow = std::clamp(o.value("eqLow").toDouble(0.0), -24.0, 24.0);
    e.eqMid = std::clamp(o.value("eqMid").toDouble(0.0), -24.0, 24.0);
    e.eqMidFreq = std::clamp(o.value("eqMidFreq").toDouble(1000.0), 100.0, 10000.0);
    e.eqHigh = std::clamp(o.value("eqHigh").toDouble(0.0), -24.0, 24.0);
    e.compressor = o.value("compressor").toBool(false);
    e.compThreshold = std::clamp(o.value("compThreshold").toDouble(-20.0), -60.0, 0.0);
    e.compRatio = std::clamp(o.value("compRatio").toDouble(3.0), 1.0, 20.0);
    e.compMakeup = std::clamp(o.value("compMakeup").toDouble(0.0), 0.0, 24.0);
    e.duckDb = std::clamp(o.value("duckDb").toDouble(0.0), -40.0, 0.0);
    return e;
}

QJsonObject AudioMaster::toJson() const {
    QJsonObject o;
    if (normalize) {
        o["normalize"] = true;
        o["targetLufs"] = targetLufs;
    }
    if (limiter)
        o["limiter"] = true;
    if (limiterActive())
        o["ceilingDb"] = ceilingDb;
    return o;
}

AudioMaster AudioMaster::fromJson(const QJsonObject& o) {
    AudioMaster m;
    m.normalize = o.value("normalize").toBool(false);
    m.targetLufs = std::clamp(o.value("targetLufs").toDouble(-14.0), -40.0, -5.0);
    m.limiter = o.value("limiter").toBool(false);
    m.ceilingDb = std::clamp(o.value("ceilingDb").toDouble(-1.0), -12.0, 0.0);
    return m;
}

} // namespace lumora
