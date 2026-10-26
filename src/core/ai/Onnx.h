#pragma once

#include <QString>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace lumora::ai {

// True when this build runs neural networks (ONNX Runtime is linked in).
bool onnxAvailable();

// A tensor: row-major data and its shape. Float unless `type` says
// otherwise; whole numbers (token ids, masks, flags) are kept in `ints`.
struct Tensor {
    enum class Type { Float, Int64, Bool };
    std::vector<int64_t> shape;
    std::vector<float> data;
    std::vector<int64_t> ints;
    Type type = Type::Float;

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
    // The same for a network with several inputs, in the model's order.
    std::vector<Tensor> run(const std::vector<Tensor>& inputs, int outputs = -1, QString* error = nullptr) const;

    // Runs the network with its inputs given by name and returns the named
    // outputs, in that order.
    std::vector<Tensor> run(const std::vector<std::pair<std::string, Tensor>>& inputs,
                            const std::vector<std::string>& outputs, QString* error = nullptr) const;

    std::vector<int64_t> inputShape() const;
    std::vector<std::string> inputNames() const;
    std::vector<std::string> outputNames() const;

private:
    Network();
    struct Impl;
    std::unique_ptr<Impl> d;
};

} // namespace lumora::ai
