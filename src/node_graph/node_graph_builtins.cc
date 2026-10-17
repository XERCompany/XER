#include "node_graph/node_graph_executor.h"
#include "node_graph/node_graph_registry.h"
 
#include <algorithm>
#include <chrono>
#include <cmath>
#include <functional>
#include <numeric>
#include <random>
 
namespace XER::NodeGraph {
 
namespace {
 
constexpr double kPi = 3.14159265358979323846;
 
PortSpec In(const std::string& name, ValueType type, Value def = Value()) { return {name, type, std::move(def)}; }
PortSpec Out(const std::string& name, ValueType type) { return {name, type, Value()}; }
 
NodeTypeDescriptor Make(std::string type, std::string title, std::string category, std::string description,
                        std::vector<PortSpec> inputs, std::vector<PortSpec> outputs, EvaluateFn fn,
                        std::vector<ParamSpec> params = {}, bool pure = true) {
    NodeTypeDescriptor d;
    d.type = std::move(type);
    d.title = std::move(title);
    d.category = std::move(category);
    d.description = std::move(description);
    d.inputs = std::move(inputs);
    d.outputs = std::move(outputs);
    d.evaluate = std::move(fn);
    d.params = std::move(params);
    d.pure = pure;
    return d;
}
 
void Binary(NodeTypeRegistry& r, const std::string& type, const std::string& title, const std::string& desc,
            std::function<double(double, double)> op, double default_b = 0.0) {
    r.Register(Make(type, title, "Math", desc,
                    {In("a", ValueType::kNumber, Value(0.0)), In("b", ValueType::kNumber, Value(default_b))},
                    {Out("result", ValueType::kNumber)}, [op](EvalContext& c) {
                        c.Output("result", op(c.Input("a").AsNumber(), c.Input("b").AsNumber()));
                        return true;
                    }));
}
 
void Unary(NodeTypeRegistry& r, const std::string& type, const std::string& title, const std::string& desc,
           std::function<double(double)> op) {
    r.Register(Make(type, title, "Math", desc, {In("x", ValueType::kNumber, Value(0.0))},
                    {Out("result", ValueType::kNumber)}, [op](EvalContext& c) {
                        c.Output("result", op(c.Input("x").AsNumber()));
                        return true;
                    }));
}
 
}  // namespace
 
void RegisterBuiltinNodes(NodeTypeRegistry& r) {
    using VT = ValueType;
 
    // ── Constants ─────────────────────────────────────────
    r.Register(Make("const.number", "Number", "Constant", "Constant number", {}, {Out("value", VT::kNumber)},
                    [](EvalContext& c) { c.Output("value", c.Param("value").AsNumber()); return true; },
                    {{"value", Value(0.0), {}}}));
    r.Register(Make("const.bool", "Boolean", "Constant", "Constant boolean", {}, {Out("value", VT::kBool)},
                    [](EvalContext& c) { c.Output("value", c.Param("value").AsBool()); return true; },
                    {{"value", Value(false), {}}}));
    r.Register(Make("const.string", "Text", "Constant", "Constant string", {}, {Out("value", VT::kString)},
                    [](EvalContext& c) { c.Output("value", c.Param("value").AsString()); return true; },
                    {{"value", Value(""), {}}}));
 
    // ── Math ──────────────────────────────────────────────
    Binary(r, "math.add", "Add", "a + b", [](double a, double b) { return a + b; });
    Binary(r, "math.subtract", "Subtract", "a - b", [](double a, double b) { return a - b; });
    Binary(r, "math.multiply", "Multiply", "a * b", [](double a, double b) { return a * b; }, 1.0);
    Binary(r, "math.divide", "Divide", "a / b (0 when b == 0)",
           [](double a, double b) { return b == 0.0 ? 0.0 : a / b; }, 1.0);
    Binary(r, "math.power", "Power", "a ^ b", [](double a, double b) { return std::pow(a, b); }, 1.0);
    Binary(r, "math.modulo", "Modulo", "a mod b",
           [](double a, double b) { return b == 0.0 ? 0.0 : std::fmod(a, b); }, 1.0);
    Binary(r, "math.min", "Min", "min(a, b)", [](double a, double b) { return std::min(a, b); });
    Binary(r, "math.max", "Max", "max(a, b)", [](double a, double b) { return std::max(a, b); });
    Unary(r, "math.abs", "Abs", "|x|", [](double x) { return std::fabs(x); });
    Unary(r, "math.sqrt", "Sqrt", "sqrt(x)", [](double x) { return x < 0 ? 0.0 : std::sqrt(x); });
    Unary(r, "math.sin", "Sin", "sin(x)", [](double x) { return std::sin(x); });
    Unary(r, "math.cos", "Cos", "cos(x)", [](double x) { return std::cos(x); });
    Unary(r, "math.floor", "Floor", "floor(x)", [](double x) { return std::floor(x); });
    Unary(r, "math.negate", "Negate", "-x", [](double x) { return -x; });
 
    r.Register(Make("math.clamp", "Clamp", "Math", "Clamp x into [min, max]",
                    {In("x", VT::kNumber, Value(0.0)), In("min", VT::kNumber, Value(0.0)),
                     In("max", VT::kNumber, Value(1.0))},
                    {Out("result", VT::kNumber)}, [](EvalContext& c) {
                        const double lo = c.Input("min").AsNumber(), hi = c.Input("max").AsNumber();
                        c.Output("result", std::clamp(c.Input("x").AsNumber(), std::min(lo, hi), std::max(lo, hi)));
                        return true;
                    }));
    r.Register(Make("math.lerp", "Lerp", "Math", "a + (b - a) * t",
                    {In("a", VT::kNumber, Value(0.0)), In("b", VT::kNumber, Value(1.0)),
                     In("t", VT::kNumber, Value(0.5))},
                    {Out("result", VT::kNumber)}, [](EvalContext& c) {
                        const double a = c.Input("a").AsNumber();
                        c.Output("result", a + (c.Input("b").AsNumber() - a) * c.Input("t").AsNumber());
                        return true;
                    }));
    r.Register(Make("math.compare", "Compare", "Logic", "Compares a and b",
                    {In("a", VT::kNumber, Value(0.0)), In("b", VT::kNumber, Value(0.0))},
                    {Out("result", VT::kBool)}, [](EvalContext& c) {
                        const double a = c.Input("a").AsNumber(), b = c.Input("b").AsNumber();
                        const std::string op = c.Param("op").AsString();
                        bool res = false;
                        if (op == "<") res = a < b;
                        else if (op == "<=") res = a <= b;
                        else if (op == ">") res = a > b;
                        else if (op == ">=") res = a >= b;
                        else if (op == "!=") res = a != b;
                        else res = a == b;
                        c.Output("result", res);
                        return true;
                    },
                    {{"op", Value("=="), {"==", "!=", "<", "<=", ">", ">="}}}));
    r.Register(Make("math.random", "Random", "Math", "Uniform random number in [min, max)",
                    {In("min", VT::kNumber, Value(0.0)), In("max", VT::kNumber, Value(1.0))},
                    {Out("value", VT::kNumber)}, [](EvalContext& c) {
                        thread_local std::mt19937_64 rng{std::random_device{}()};
                        const double seed = c.Param("seed").AsNumber();
                        std::mt19937_64 seeded(static_cast<uint64_t>(seed) * 6364136223846793005ULL + c.node->id);
                        std::uniform_real_distribution<double> dist(c.Input("min").AsNumber(),
                                                                    c.Input("max").AsNumber());
                        c.Output("value", seed != 0.0 ? dist(seeded) : dist(rng));
                        return true;
                    },
                    {{"seed", Value(0.0), {}}}, /*pure=*/false));
 
    // ── Logic ─────────────────────────────────────────────
    r.Register(Make("logic.and", "And", "Logic", "a && b",
                    {In("a", VT::kBool, Value(false)), In("b", VT::kBool, Value(false))}, {Out("result", VT::kBool)},
                    [](EvalContext& c) { c.Output("result", c.Input("a").AsBool() && c.Input("b").AsBool()); return true; }));
    r.Register(Make("logic.or", "Or", "Logic", "a || b",
                    {In("a", VT::kBool, Value(false)), In("b", VT::kBool, Value(false))}, {Out("result", VT::kBool)},
                    [](EvalContext& c) { c.Output("result", c.Input("a").AsBool() || c.Input("b").AsBool()); return true; }));
    r.Register(Make("logic.not", "Not", "Logic", "!x", {In("x", VT::kBool, Value(false))}, {Out("result", VT::kBool)},
                    [](EvalContext& c) { c.Output("result", !c.Input("x").AsBool()); return true; }));
    r.Register(Make("logic.select", "Select", "Logic", "condition ? a : b",
                    {In("condition", VT::kBool, Value(false)), In("a", VT::kAny, Value(0.0)),
                     In("b", VT::kAny, Value(0.0))},
                    {Out("result", VT::kAny)}, [](EvalContext& c) {
                        c.Output("result", c.Input("condition").AsBool() ? c.Input("a") : c.Input("b"));
                        return true;
                    }));
 
    // ── String ────────────────────────────────────────────
    r.Register(Make("string.concat", "Concat", "String", "a + separator + b",
                    {In("a", VT::kString, Value("")), In("b", VT::kString, Value(""))}, {Out("result", VT::kString)},
                    [](EvalContext& c) {
                        c.Output("result", c.Input("a").AsString() + c.Param("separator").AsString() +
                                               c.Input("b").AsString());
                        return true;
                    },
                    {{"separator", Value(""), {}}}));
    r.Register(Make("string.length", "Length", "String", "Number of bytes", {In("text", VT::kString, Value(""))},
                    {Out("length", VT::kNumber)}, [](EvalContext& c) {
                        c.Output("length", static_cast<double>(c.Input("text").AsString().size()));
                        return true;
                    }));
    r.Register(Make("string.upper", "Upper Case", "String", "ASCII upper case", {In("text", VT::kString, Value(""))},
                    {Out("result", VT::kString)}, [](EvalContext& c) {
                        std::string s = c.Input("text").AsString();
                        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char ch) { return std::toupper(ch); });
                        c.Output("result", s);
                        return true;
                    }));
    r.Register(Make("string.format", "Format", "String", "Replaces {0} and {1} in the template",
                    {In("arg0", VT::kAny, Value("")), In("arg1", VT::kAny, Value(""))}, {Out("result", VT::kString)},
                    [](EvalContext& c) {
                        std::string s = c.Param("template").AsString();
                        const std::string args[2] = {c.Input("arg0").AsString(), c.Input("arg1").AsString()};
                        for (int i = 0; i < 2; ++i) {
                            const std::string token = "{" + std::to_string(i) + "}";
                            for (size_t pos = s.find(token); pos != std::string::npos; pos = s.find(token, pos + args[i].size())) {
                                s.replace(pos, token.size(), args[i]);
                            }
                        }
                        c.Output("result", s);
                        return true;
                    },
                    {{"template", Value("{0} {1}"), {}}}));
 
    // ── Array ─────────────────────────────────────────────
    r.Register(Make("array.range", "Range", "Array", "start, start+step, ... (count items)",
                    {In("start", VT::kNumber, Value(0.0)), In("count", VT::kNumber, Value(8.0)),
                     In("step", VT::kNumber, Value(1.0))},
                    {Out("array", VT::kArray)}, [](EvalContext& c) {
                        const double count = std::clamp(c.Input("count").AsNumber(), 0.0, 1e7);
                        Value::Array a(static_cast<size_t>(count));
                        const double start = c.Input("start").AsNumber(), step = c.Input("step").AsNumber();
                        for (size_t i = 0; i < a.size(); ++i) a[i] = start + step * static_cast<double>(i);
                        c.Output("array", std::move(a));
                        return true;
                    }));
    r.Register(Make("array.scale", "Scale", "Array", "array * factor + offset",
                    {In("array", VT::kArray, Value(Value::Array{})), In("factor", VT::kNumber, Value(1.0)),
                     In("offset", VT::kNumber, Value(0.0))},
                    {Out("array", VT::kArray)}, [](EvalContext& c) {
                        Value::Array a = c.Input("array").AsArray();
                        const double f = c.Input("factor").AsNumber(), o = c.Input("offset").AsNumber();
                        for (double& v : a) v = v * f + o;
                        c.Output("array", std::move(a));
                        return true;
                    }));
    r.Register(Make("array.stats", "Statistics", "Array", "sum / mean / min / max / length",
                    {In("array", VT::kArray, Value(Value::Array{}))},
                    {Out("sum", VT::kNumber), Out("mean", VT::kNumber), Out("min", VT::kNumber),
                     Out("max", VT::kNumber), Out("length", VT::kNumber)},
                    [](EvalContext& c) {
                        const Value::Array a = c.Input("array").AsArray();
                        const double sum = std::accumulate(a.begin(), a.end(), 0.0);
                        c.Output("sum", sum);
                        c.Output("mean", a.empty() ? 0.0 : sum / static_cast<double>(a.size()));
                        c.Output("min", a.empty() ? 0.0 : *std::min_element(a.begin(), a.end()));
                        c.Output("max", a.empty() ? 0.0 : *std::max_element(a.begin(), a.end()));
                        c.Output("length", static_cast<double>(a.size()));
                        return true;
                    }));
    r.Register(Make("array.get", "Get Item", "Array", "array[index] (clamped)",
                    {In("array", VT::kArray, Value(Value::Array{})), In("index", VT::kNumber, Value(0.0))},
                    {Out("value", VT::kNumber)}, [](EvalContext& c) {
                        const Value::Array a = c.Input("array").AsArray();
                        if (a.empty()) { c.Output("value", 0.0); return true; }
                        const auto i = static_cast<size_t>(std::clamp(c.Input("index").AsNumber(), 0.0,
                                                                      static_cast<double>(a.size() - 1)));
                        c.Output("value", a[i]);
                        return true;
                    }));
 
    // ── Audio (block-based DSP on number arrays) ──────────
    r.Register(Make("audio.oscillator", "Oscillator", "Audio", "Generates a block of samples",
                    {In("frequency", VT::kNumber, Value(440.0)), In("amplitude", VT::kNumber, Value(1.0))},
                    {Out("signal", VT::kArray)}, [](EvalContext& c) {
                        const double sr = std::max(1.0, c.Param("sampleRate").AsNumber());
                        const auto n = static_cast<size_t>(std::clamp(c.Param("samples").AsNumber(), 1.0, 1e7));
                        const double f = c.Input("frequency").AsNumber(), amp = c.Input("amplitude").AsNumber();
                        const std::string wave = c.Param("waveform").AsString();
                        Value::Array out(n);
                        for (size_t i = 0; i < n; ++i) {
                            const double phase = std::fmod(f * static_cast<double>(i) / sr, 1.0);
                            double s;
                            if (wave == "square") s = phase < 0.5 ? 1.0 : -1.0;
                            else if (wave == "saw") s = 2.0 * phase - 1.0;
                            else if (wave == "triangle") s = 1.0 - 4.0 * std::fabs(phase - 0.5);
                            else s = std::sin(2.0 * kPi * phase);
                            out[i] = s * amp;
                        }
                        c.Output("signal", std::move(out));
                        return true;
                    },
                    {{"waveform", Value("sine"), {"sine", "square", "saw", "triangle"}},
                     {"sampleRate", Value(48000.0), {}},
                     {"samples", Value(256.0), {}}}));
    r.Register(Make("audio.gain", "Gain", "Audio", "Applies gain in decibels",
                    {In("signal", VT::kArray, Value(Value::Array{})), In("db", VT::kNumber, Value(0.0))},
                    {Out("signal", VT::kArray)}, [](EvalContext& c) {
                        Value::Array a = c.Input("signal").AsArray();
                        const double g = std::pow(10.0, c.Input("db").AsNumber() / 20.0);
                        for (double& v : a) v *= g;
                        c.Output("signal", std::move(a));
                        return true;
                    }));
    r.Register(Make("audio.mix", "Mix", "Audio", "Sums two signals",
                    {In("a", VT::kArray, Value(Value::Array{})), In("b", VT::kArray, Value(Value::Array{}))},
                    {Out("signal", VT::kArray)}, [](EvalContext& c) {
                        Value::Array a = c.Input("a").AsArray();
                        const Value::Array b = c.Input("b").AsArray();
                        if (b.size() > a.size()) a.resize(b.size(), 0.0);
                        for (size_t i = 0; i < b.size(); ++i) a[i] += b[i];
                        c.Output("signal", std::move(a));
                        return true;
                    }));
    r.Register(Make("audio.rms", "RMS / Peak", "Audio", "Level meter",
                    {In("signal", VT::kArray, Value(Value::Array{}))},
                    {Out("rms", VT::kNumber), Out("peak", VT::kNumber), Out("db", VT::kNumber)},
                    [](EvalContext& c) {
                        const Value::Array a = c.Input("signal").AsArray();
                        double sq = 0.0, peak = 0.0;
                        for (double v : a) {
                            sq += v * v;
                            peak = std::max(peak, std::fabs(v));
                        }
                        const double rms = a.empty() ? 0.0 : std::sqrt(sq / static_cast<double>(a.size()));
                        c.Output("rms", rms);
                        c.Output("peak", peak);
                        c.Output("db", rms > 0 ? 20.0 * std::log10(rms) : -120.0);
                        return true;
                    }));
 
    // ── Utility ───────────────────────────────────────────
    r.Register(Make("util.print", "Print", "Utility", "Logs the value and passes it through",
                    {In("value", VT::kAny, Value(""))}, {Out("value", VT::kAny)},
                    [](EvalContext& c) {
                        const std::string label = c.Param("label").AsString();
                        c.logs.push_back((label.empty() ? "" : label + ": ") + c.Input("value").AsString());
                        c.Output("value", c.Input("value"));
                        return true;
                    },
                    {{"label", Value(""), {}}}, /*pure=*/false));
    r.Register(Make("util.time", "Time", "Utility", "Seconds since epoch", {}, {Out("seconds", VT::kNumber)},
                    [](EvalContext& c) {
                        const auto now = std::chrono::system_clock::now().time_since_epoch();
                        c.Output("seconds", std::chrono::duration<double>(now).count());
                        return true;
                    },
                    {}, /*pure=*/false));
 
    // ── Graph (subgraphs) ─────────────────────────────────
    r.Register(Make("graph.input", "Graph Input", "Graph", "Value supplied by the parent graph", {},
                    {Out("value", VT::kAny)},
                    [](EvalContext& c) {
                        Value v = c.executor ? c.executor->ExternalInput(c.Param("name").AsString()) : Value();
                        if (v.IsNone()) v = c.Param("default");
                        c.Output("value", v.ConvertTo(ValueTypeFromName(c.Param("type").AsString())));
                        return true;
                    },
                    {{"name", Value("in"), {}},
                     {"type", Value("any"), {"any", "number", "bool", "string", "array"}},
                     {"default", Value(0.0), {}}},
                    /*pure=*/false));
    r.Register(Make("graph.output", "Graph Output", "Graph", "Value returned to the parent graph",
                    {In("value", VT::kAny, Value(0.0))}, {Out("value", VT::kAny)},
                    [](EvalContext& c) { c.Output("value", c.Input("value")); return true; },
                    {{"name", Value("out"), {}},
                     {"type", Value("any"), {"any", "number", "bool", "string", "array"}}}));
    r.Register(Make("graph.subgraph", "Subgraph", "Graph", "Evaluates a nested graph", {}, {},
                    [](EvalContext& c) {
                        if (!c.node->subgraph) {
                            c.error = "subgraph is missing";
                            return false;
                        }
                        Graph inner(*c.node->subgraph);
                        Executor::Options opts;
                        opts.parallel = false;
                        Executor ex(inner, opts);
                        for (const auto& [name, value] : c.inputs) ex.SetExternalInput(name, value);
                        ExecutionResult res = ex.Run();
                        for (auto& l : res.logs) c.logs.push_back(std::move(l));
                        if (!res.ok) {
                            c.error = "subgraph: " + res.error;
                            return false;
                        }
                        for (const auto& [name, value] : ex.external_outputs()) c.Output(name, value);
                        return true;
                    },
                    {}, /*pure=*/false));
}
 
}  // namespace XER::NodeGraph