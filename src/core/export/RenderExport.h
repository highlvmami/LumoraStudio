#pragma once

#include "export/TimelineExporter.h"

namespace lumora {

// Writes a VideoStrategy::Render plan: every output frame is composited by
// FrameRenderer at full precision (frames without effects are converted
// straight from the source) and encoded at a constant frame rate; audio is
// either copied packet for packet or mixed by AudioMixer and encoded.
ExportResult runRenderExport(const ExportPlan& plan, const ExportSettings& settings,
                             const TimelineExporter::ProgressFn& progress, const std::atomic_bool* cancel);

} // namespace lumora
