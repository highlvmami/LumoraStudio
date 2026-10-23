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
#ifdef LUMORA_HAVE_ONNXRUNTIME
    if (inputs.size() != d->inputNames.size()) {
        if (error)
            *error = QStringLiteral("the model takes %1 inputs, not %2").arg(d->inputNames.size()).arg(inputs.size());
        return {};
    }
    std::vector<std::pair<std::string, Tensor>> named;
    for (size_t i = 0; i < inputs.size(); ++i)
        named.emplace_back(d->inputNames[i], inputs[i]);
    const size_t count = outputs < 0 ? d->outputNames.size() : std::min<size_t>(outputs, d->outputNames.size());
    return run(named, std::vector<std::string>(d->outputNames.begin(), d->outputNames.begin() + count), error);
#else
    (void)inputs, (void)outputs;
    if (error)
        *error = QStringLiteral("built without ONNX Runtime");
    return {};
#endif
}

std::vector<Tensor> Network::run(const std::vector<std::pair<std::string, Tensor>>& inputs,
                                 const std::vector<std::string>& outputs, QString* error) const {
    std::vector<Tensor> out;
#ifdef LUMORA_HAVE_ONNXRUNTIME
    try {
        const Ort::MemoryInfo mem = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
        std::vector<Ort::Value> in;
        std::vector<const char*> inNames;
        std::vector<std::unique_ptr<bool[]>> flags; // bool tensors need real bools
        // Empty tensors (an empty cache) still need somewhere to point.
        static float noFloat = 0;
        static int64_t noInt = 0;
        for (const auto& [name, t] : inputs) {
            switch (t.type) {
            case Tensor::Type::Float:
                in.push_back(Ort::Value::CreateTensor<float>(mem, t.data.empty() ? &noFloat : const_cast<float*>(t.data.data()),
                                                             t.data.size(), t.shape.data(), t.shape.size()));
                break;
            case Tensor::Type::Int64:
                in.push_back(Ort::Value::CreateTensor<int64_t>(mem, t.ints.empty() ? &noInt : const_cast<int64_t*>(t.ints.data()),
                                                               t.ints.size(), t.shape.data(), t.shape.size()));
                break;
            case Tensor::Type::Bool: {
                auto& b = flags.emplace_back(std::make_unique<bool[]>(std::max<size_t>(1, t.ints.size())));
                for (size_t i = 0; i < t.ints.size(); ++i)
                    b[i] = t.ints[i] != 0;
                in.push_back(Ort::Value::CreateTensor<bool>(mem, b.get(), t.ints.size(), t.shape.data(), t.shape.size()));
                break;
            }
            }
            inNames.push_back(name.c_str());
        }
        std::vector<const char*> names;
        for (const auto& name : outputs)
            names.push_back(name.c_str());
        auto values = d->session->Run(Ort::RunOptions{nullptr}, inNames.data(), in.data(), in.size(), names.data(), names.size());
        for (auto& v : values) {
            Tensor t;
            const auto info = v.GetTensorTypeAndShapeInfo();
            t.shape = info.GetShape();
            const size_t n = info.GetElementCount();
            switch (info.GetElementType()) {
            case ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64: {
                t.type = Tensor::Type::Int64;
                const int64_t* p = v.GetTensorData<int64_t>();
                t.ints.assign(p, p + n);
                break;
            }
            case ONNX_TENSOR_ELEMENT_DATA_TYPE_BOOL: {
                t.type = Tensor::Type::Bool;
                const bool* p = v.GetTensorData<bool>();
                t.ints.assign(p, p + n);
                break;
            }
            default: {
                const float* p = v.GetTensorData<float>();
                t.data.assign(p, p + n);
            }
            }
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

std::vector<std::string> Network::inputNames() const {
#ifdef LUMORA_HAVE_ONNXRUNTIME
    return d->inputNames;
#else
    return {};
#endif
}

std::vector<std::string> Network::outputNames() const {
#ifdef LUMORA_HAVE_ONNXRUNTIME
    return d->outputNames;
#else
    return {};
#endif
}

} // namespace lumora::ai
