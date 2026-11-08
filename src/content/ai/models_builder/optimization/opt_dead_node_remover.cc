#include "opt_dead_node_remover.h"

#include "../graph_engine/graph_optimizer.h"

namespace XER::ModelsBuilder::Optimization {

void OptDeadNodeRemover::Apply(GraphEngine::GraphBase& graph) {
  GraphEngine::GraphOptimizer::PruneDeadNodes(graph);
}

}  // namespace XER::ModelsBuilder::Optimization
