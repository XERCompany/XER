#pragma once

#include <string>
#include <vector>
#include <memory>
#include <map>

namespace XER::ModelsBuilder::Reader::Onnx {

/// @brief ONNX graph node representation
/// 
/// Minimal node structure for graph traversal and optimization
struct OnnxNode {
    std::string op_type;              ///< Operation type (Conv, ReLU, etc.)
    std::string name;                 ///< Node name
    std::vector<std::string> inputs;  ///< Input tensor names
    std::vector<std::string> outputs; ///< Output tensor names
};

/// @brief ONNX graph optimizer
/// 
/// Applies optimization passes to ONNX computation graph:
/// - Operator fusion (Conv+ReLU, Add+ReLU, etc.)
/// - Constant folding (compute constants at load time)
/// - Dead code elimination (remove unused ops)
/// - Shape inference (validate/propagate shapes)
class OnnxOptimizer {
public:
    /// Optimize ONNX graph (all passes)
    /// @param nodes Vector of graph nodes to optimize (in-place)
    /// @return true if optimization succeeded
    static bool Optimize(std::vector<OnnxNode>& nodes);
    
    /// Apply operator fusion pass
    /// @param nodes Graph nodes
    /// @return Number of fusions applied
    static int FuseOperators(std::vector<OnnxNode>& nodes);
    
    /// Apply constant folding pass
    /// @param nodes Graph nodes
    /// @return Number of constants folded
    static int FoldConstants(std::vector<OnnxNode>& nodes);
    
    /// Apply dead code elimination
    /// @param nodes Graph nodes
    /// @param outputs Nodes that must be kept (output names)
    /// @return Number of nodes removed
    static int EliminatDeadCode(std::vector<OnnxNode>& nodes,
                                 const std::vector<std::string>& outputs);
    
    /// Infer shapes throughout graph
    /// @param nodes Graph nodes
    /// @param input_shapes Map of input name → shape
    /// @return true if shape inference succeeded
    static bool InferShapes(const std::vector<OnnxNode>& nodes,
                            std::map<std::string, std::vector<int>>& input_shapes);
};

} // namespace XER::ModelsBuilder::Reader::Onnx
