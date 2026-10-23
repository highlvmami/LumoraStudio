#include "Onnx.h"

#include <QFile>
#include <QHash>
#include <algorithm>
#include <mutex>
#include <thread>

#ifdef LUMORA_HAVE_ONNXRUNTIME
#include <onnxruntime_cxx_api.h>
#endif

namespace lumora::ai {

bool onnxAvailable() {
#ifdef LUMORA_HAVE_ONNXRUNTIME
    return true;
#else
    return false;
#endif
}

#ifdef LUMORA_HAVE_ONNXRUNTIME

namespace {

Ort::Env& env() {
    static Ort::Env e(ORT_LOGGING_LEVEL_ERROR, "LumoraStudio");
    return e;
}

} // namespace

struct Network::Impl {
    std::unique_ptr<Ort::Session> session;
    std::vector<std::string> inputNames;
    std::vector<std::string> outputNames;
    std::vector<int64_t> inputShape;
};

#else

struct Network::Impl {};

#endif

Network::Network() : d(std::make_unique<Impl>()) {}
Network::~Network() = default;

std::shared_ptr<Network> Network::load(const QString& path, QString* error) {
#ifdef LUMORA_HAVE_ONNXRUNTIME
    static std::mutex mutex;
    static QHash<QString, std::shared_ptr<Network>> cache;
    std::lock_guard lock(mutex);
    if (auto it = cache.find(path); it != cache.end())
        return *it;
    if (!QFile::exists(path)) {
        if (error)
            *error = QStringLiteral("model file missing: %1").arg(path);
        return nullptr;
    }
    std::shared_ptr<Network> net(new Network);
    try {
        Ort::SessionOptions options;
        // Leave a core for the user interface and the decoders.
        const int threads = std::max(1, static_cast<int>(std::thread::hardware_concurrency()) - 1);
        options.SetIntraOpNumThreads(threads);
        options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
#ifdef _WIN32
        const std::wstring file = path.toStdWString();
#else
        const std::string file = QFile::encodeName(path).toStdString();
#endif
        net->d->session = std::make_unique<Ort::Session>(env(), file.c_str(), options);
        Ort::AllocatorWithDefaultOptions alloc;
        for (size_t i = 0; i < net->d->session->GetInputCount(); ++i)
            net->d->inputNames.emplace_back(net->d->session->GetInputNameAllocated(i, alloc).get());
        for (size_t i = 0; i < net->d->session->GetOutputCount(); ++i)
            net->d->outputNames.emplace_back(net->d->session->GetOutputNameAllocated(i, alloc).get());
        if (net->d->inputNames.empty() || net->d->outputNames.empty())
            throw Ort::Exception("the model has no inputs or outputs", ORT_INVALID_GRAPH);
        net->d->inputShape = net->d->session->GetInputTypeInfo(0).GetTensorTypeAndShapeInfo().GetShape();
    } catch (const Ort::Exception& e) {
        if (error)
            *error = QString::fromUtf8(e.what());
        return nullptr;
    }
    cache.insert(path, net);
    return net;
#else
    (void)path;
    if (error)
        *error = QStringLiteral("built without ONNX Runtime");
    return nullptr;
#endif
}

std::vector<int64_t> Network::inputShape() const {
#ifdef LUMORA_HAVE_ONNXRUNTIME
    return d->inputShape;
#else
    return {};
#endif
}

std::vector<Tensor> Network::run(const Tensor& input, int outputs, QString* error) const {
    return run(std::vector<Tensor>{input}, outputs, error);
}

std::vector<Tensor> Network::run(const std::vector<Tensor>& inputs, int outputs, QString* error) const {
    std::vector<Tensor> out;
#ifdef LUMORA_HAVE_ONNXRUNTIME
    if (inputs.size() != d->inputNames.size()) {
        if (error)
            *error = QStringLiteral("the model takes %1 inputs, not %2").arg(d->inputNames.size()).arg(inputs.size());
        return out;
    }
    try {
        const Ort::MemoryInfo mem = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
        std::vector<Ort::Value> in;
        std::vector<const char*> inNames;
        for (size_t i = 0; i < inputs.size(); ++i) {
            in.push_back(Ort::Value::CreateTensor<float>(mem, const_cast<float*>(inputs[i].data.data()), inputs[i].data.size(),
                                                         inputs[i].shape.data(), inputs[i].shape.size()));
            inNames.push_back(d->inputNames[i].c_str());
        }
        const size_t count = outputs < 0 ? d->outputNames.size() : std::min<size_t>(outputs, d->outputNames.size());
        std::vector<const char*> names;
        for (size_t i = 0; i < count; ++i)
            names.push_back(d->outputNames[i].c_str());
        auto values = d->session->Run(Ort::RunOptions{nullptr}, inNames.data(), in.data(), in.size(), names.data(), names.size());
        for (auto& v : values) {
            Tensor t;
            const auto info = v.GetTensorTypeAndShapeInfo();
            t.shape = info.GetShape();
            const size_t n = info.GetElementCount();
            const float* p = v.GetTensorData<float>();
            t.data.assign(p, p + n);
            out.push_back(std::move(t));
        }
    } catch (const Ort::Exception& e) {
        if (error)
            *error = QString::fromUtf8(e.what());
        out.clear();
    }
#else
    (void)inputs, (void)outputs;
    if (error)
        *error = QStringLiteral("built without ONNX Runtime");
#endif
    return out;
}

} // namespace lumora::ai
