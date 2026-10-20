#pragma once

#include <QCoreApplication>
#include <atomic>
#include <functional>

#include "export/ExportSettings.h"

namespace lumora {

// Writes an ExportPlan to disk. One demuxer per segment feeds both streams;
// each stream is either copied packet for packet or decoded and re-encoded,
// as the plan says. Runs synchronously; call it from a worker thread.
class TimelineExporter {
    Q_DECLARE_TR_FUNCTIONS(TimelineExporter)

public:
    using ProgressFn = std::function<void(double fraction)>;

    ExportResult run(const ExportPlan& plan, const ExportSettings& settings, const ProgressFn& progress = {},
                     const std::atomic_bool* cancel = nullptr);
};

} // namespace lumora
