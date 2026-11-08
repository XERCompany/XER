#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace XER::Content::FormatConv {

// Batch 3D-format converter on top of Assimp. Each input file is imported
// with the shared quality post-process preset, then exported to the target
// format through Assimp::Exporter — keeping the full aiScene fidelity
// (animations, skins, cameras) that the neutral ModelScene would drop.
//
// Conversions run in parallel on XER::AsyncIO::IO::Async::IOThreadPool;
// results are returned in input order.

struct ConvertOptions {
	// Target format id/extension without a dot, e.g. "ply", "gltf2",
	// "stl", "x". Must be reported by Assimp's exporter list.
	std::string target_format;

	// Output directory. Empty -> same directory as each input file.
	// The input file name is kept; only the extension is replaced.
	std::string output_dir;

	// Maximum files converted concurrently. 0 -> all inputs at once
	// (bounded only by the shared pool's thread count).
	std::size_t max_parallel = 0;

	// Overwrite existing output files. When false, an existing output
	// produces an error result instead of being clobbered.
	bool overwrite = false;
};

struct ConvertResult {
	std::string input_path;
	std::string output_path;
	bool ok = false;
	std::string error;
	double elapsed_ms = 0.0;
};

class BatchConverter {
public:
	// Converts every input; never throws. Per-file failures are reported
	// in the corresponding ConvertResult, not aborting the batch.
	static std::vector<ConvertResult> ConvertFiles(
	    const std::vector<std::string>& inputs,
	    const ConvertOptions& options);

	// Computes the output path for `input` under `options`
	// (output_dir + filename with target extension).
	static std::string OutputPathFor(const std::string& input,
					 const ConvertOptions& options);

	// True when Assimp's exporter list contains `format`
	// (case-insensitive, with or without a leading dot).
	static bool IsExportFormatSupported(const std::string& format);

	// Extension ids of every Assimp export format, lowercased,
	// dot-stripped, deduplicated (e.g. "ply", "gltf2", "x").
	static std::vector<std::string> SupportedExportFormats();
};

}  // namespace XER::Content::FormatConv
