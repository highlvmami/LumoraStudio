#pragma once

#include <QString>
#include <cstdint>
#include <memory>
#include <vector>

namespace lumora::ai {

// True when this build runs neural networks (ONNX Runtime is linked in).
bool onnxAvailable();

// A float tensor: row-major data and its shape.
struct Tensor {
    std::vector<int64_t> shape;
    std::vector<float> data;

    int64_t dim(int i) const { return i < static_cast<int>(shape.size()) ? shape[i] : 0; }
};

// An ONNX model loaded for inference on the CPU. Loading takes a moment, so
// each file is loaded once and shared; running is thread safe.
class Network {
public:
    ~Network();
    Network(const Network&) = delete;
    Network& operator=(const Network&) = delete;

    // nullptr (with `error` set) when the file cannot be loaded.
    static std::shared_ptr<Network> load(const QString& path, QString* error = nullptr);

    // Runs the network on one input and returns its first `outputs` outputs
    // (all of them when negative). Empty on failure, with `error` set.
    std::vector<Tensor> run(const Tensor& input, int outputs = -1, QString* error = nullptr) const;

    std::vector<int64_t> inputShape() const;

private:
    Network();
    struct Impl;
    std::unique_ptr<Impl> d;
};

} // namespace lumora::ai
