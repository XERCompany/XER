#pragma once

#include "../model_core/layer.h"
#include <memory>
#include <string>

namespace XER::ModelsBuilder::LayerFactory {

/// @brief Recurrent layer (LSTM or GRU)
class LayerRecurrent : public Core::Layer {
 public:
  enum class RnnType { LSTM, GRU };

  struct Config {
    RnnType rnn_type{RnnType::LSTM};
    int     input_size{1};
    int     hidden_size{128};
    int     num_layers{1};
    bool    bidirectional{false};
    bool    batch_first{true};
    float   dropout{0.0f};
  };

  explicit LayerRecurrent(const std::string& name, Config config);
  const Config& GetConfig() const { return config_; }

 private:
  Config config_;
};

std::shared_ptr<Core::Layer> MakeRecurrent(const std::string& name,
                                              LayerRecurrent::Config config);

}  // namespace XER::ModelsBuilder::LayerFactory
