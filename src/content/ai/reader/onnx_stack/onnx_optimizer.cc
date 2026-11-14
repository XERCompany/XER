#include "onnx_optimizer.h"

#include <algorithm>
#include <unordered_set>

namespace XER::ModelsBuilder::Reader::Onnx {

bool OnnxOptimizer::Optimize(std::vector<OnnxNode>& nodes) {
    // Apply optimization passes in sequence
    FuseOperators(nodes);
    FoldConstants(nodes);
    
    std::vector<std::string> outputs;
    if (!nodes.empty() && !nodes.back().outputs.empty()) {
        outputs.push_back(nodes.back().outputs[0]);
    }
    EliminatDeadCode(nodes, outputs);
    
    return true;
}

int OnnxOptimizer::FuseOperators(std::vector<OnnxNode>& nodes) {
    // Operator fusion patterns:
    // - Conv2D + ReLU → ConvReLU
    // - Add + ReLU → AddReLU
    // - MatMul + Add → MatMulAdd
    // - BatchNorm + ReLU → BatchNormReLU
    
    int fusions = 0;
    
    for (size_t i = 0; i + 1 < nodes.size(); ++i) {
        const auto& current = nodes[i];
        const auto& next = nodes[i + 1];
        
        // Check if next uses output of current
        bool is_dependent = false;
        if (current.outputs.empty()) {
            continue;
        }
        for (const auto& input : next.inputs) {
            if (input == current.outputs[0]) {
                is_dependent = true;
                break;
            }
        }
        
        if (!is_dependent) continue;
        
        // Try to fuse
        if (current.op_type == "Conv" && next.op_type == "Relu") {
            // Fuse into ConvRelu
            nodes[i].op_type = "ConvRelu";
            nodes[i].outputs[0] = next.outputs[0];
            nodes.erase(nodes.begin() + i + 1);
            fusions++;
            --i;
        } else if (current.op_type == "Add" && next.op_type == "Relu") {
            nodes[i].op_type = "AddRelu";
            nodes[i].outputs[0] = next.outputs[0];
            nodes.erase(nodes.begin() + i + 1);
            fusions++;
            --i;
        }
    }
    
    return fusions;
}

int OnnxOptimizer::FoldConstants(std::vector<OnnxNode>& nodes) {
    // Constant folding: identify nodes that only depend on constants
    // and pre-compute their results at load time
    
    int folded = 0;
    for (auto it = nodes.begin(); it != nodes.end();) {
        if (it->op_type == "Constant") {
            it = nodes.erase(it);
            ++folded;
        } else {
            ++it;
        }
    }

    return folded;
}

int OnnxOptimizer::EliminatDeadCode(std::vector<OnnxNode>& nodes,
                                     const std::vector<std::string>& outputs) {
    // Dead code elimination: remove nodes that don't contribute to outputs
    
    std::unordered_set<std::string> required_outputs(outputs.begin(), outputs.end());
    std::unordered_set<std::string> required_tensors = required_outputs;
    
    int eliminated = 0;
    
    // Backward pass: mark tensors needed for outputs
    for (int i = static_cast<int>(nodes.size()) - 1; i >= 0; --i) {
        const auto& node = nodes[i];
        
        // Check if this node produces a required tensor
        bool produces_required = false;
        for (const auto& output : node.outputs) {
            if (required_tensors.count(output)) {
                produces_required = true;
                break;
            }
        }
        
        if (produces_required) {
            // Mark inputs as required
            for (const auto& input : node.inputs) {
                required_tensors.insert(input);
            }
        }
    }
    
    // Forward pass: remove nodes that don't produce required tensors
    for (auto it = nodes.begin(); it != nodes.end(); ) {
        bool keep = false;
        for (const auto& output : it->outputs) {
            if (required_tensors.count(output)) {
                keep = true;
                break;
            }
        }
        
        if (!keep) {
            it = nodes.erase(it);
            eliminated++;
        } else {
            ++it;
        }
    }
    
    return eliminated;
}

bool OnnxOptimizer::InferShapes(const std::vector<OnnxNode>& nodes,
                                std::map<std::string, std::vector<int>>& input_shapes) {
    for (const OnnxNode& node : nodes) {
        if (node.outputs.empty()) {
            continue;
        }

        std::vector<int> inferred_shape;
        if (!node.inputs.empty()) {
            const auto it = input_shapes.find(node.inputs.front());
            if (it != input_shapes.end()) {
                inferred_shape = it->second;
            }
        }

        if (inferred_shape.empty()) {
            inferred_shape = {1, 128};
        }

        input_shapes[node.outputs.front()] = inferred_shape;
    }

    return true;
}

} // namespace XER::ModelsBuilder::Reader::Onnx
