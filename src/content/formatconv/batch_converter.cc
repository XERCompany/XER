#include "batch_converter.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <filesystem>
#include <system_error>

#include <assimp/Exporter.hpp>
#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <assimp/types.h>

#include "async_io/io_thread_pool.h"

namespace XER::Content::FormatConv {
namespace fs = std::filesystem;
namespace Async = XER::AsyncIO::IO::Async;

namespace {

// Same quality preset the neutral model loader uses, so converted output
// matches what the rest of the content suite produces.
constexpr unsigned int kConvertFlags =
    aiProcess_Triangulate | aiProcess_JoinIdenticalVertices |
    aiProcess_GenSmoothNormals | aiProcess_SortByPType |
    aiProcess_TransformUVCoords | aiProcess_ValidateDataStructure;

std::string NormalizeFormat(std::string format) {
	if (!format.empty() && format.front() == '.') {
		format.erase(format.begin());
	}
	std::transform(format.begin(), format.end(), format.begin(),
	               [](unsigned char c) { return static_cast<char>(
	                   std::tolower(c)); });
	return format;
}

// Resolves an extension or id ("ply", ".gltf2") to the canonical exporter
// id; returns empty when unsupported. Exporter::Export matches on ids,
// not extensions, so callers must pass the resolved id.
std::string ResolveExportId(const std::string& format) {
	const std::string want = NormalizeFormat(format);
	if (want.empty()) return {};
	Assimp::Exporter exporter;
	const std::size_t count = exporter.GetExportFormatCount();
	for (std::size_t i = 0; i < count; ++i) {
		const aiExportFormatDesc* desc =
		    exporter.GetExportFormatDescription(i);
		if (!desc || !desc->id) continue;
		if (NormalizeFormat(desc->id) == want ||
		    (desc->fileExtension &&
		     NormalizeFormat(desc->fileExtension) == want)) {
			return desc->id;
		}
	}
	return {};
}

// One file: import -> export. Runs on a pool worker; touches only its own
// result slot and its own Assimp objects.
void ConvertOne(const std::string& input, const ConvertOptions& options,
                ConvertResult* result) {
	const auto t0 = std::chrono::steady_clock::now();

	Assimp::Importer importer;
	const aiScene* scene = importer.ReadFile(input, kConvertFlags);
	if (!scene) {
		result->error = std::string("import failed: ") +
		                importer.GetErrorString();
	} else {
		Assimp::Exporter exporter;
		const std::string format_id =
		    ResolveExportId(options.target_format);
		if (exporter.Export(scene, format_id.c_str(),
		                    result->output_path.c_str()) != aiReturn_SUCCESS) {
			result->error = std::string("export failed: ") +
			                exporter.GetErrorString();
		} else {
			result->ok = true;
		}
	}

	result->elapsed_ms =
	    std::chrono::duration<double, std::milli>(
	        std::chrono::steady_clock::now() - t0).count();
}

}  // namespace

std::vector<ConvertResult> BatchConverter::ConvertFiles(
    const std::vector<std::string>& inputs, const ConvertOptions& options) {
	std::vector<ConvertResult> results(inputs.size());
	if (inputs.empty()) return results;

	const std::string format = NormalizeFormat(options.target_format);
	if (format.empty()) {
		for (std::size_t i = 0; i < inputs.size(); ++i) {
			results[i].input_path = inputs[i];
			results[i].error = "empty target_format";
		}
		return results;
	}
	if (!IsExportFormatSupported(format)) {
		for (std::size_t i = 0; i < inputs.size(); ++i) {
			results[i].input_path = inputs[i];
			results[i].error = "unsupported export format: " + format;
		}
		return results;
	}

	// Pre-flight: resolve output paths and overwrite policy up front so a
	// bad batch fails fast instead of after the first file converts.
	for (std::size_t i = 0; i < inputs.size(); ++i) {
		results[i].input_path = inputs[i];
		results[i].output_path = OutputPathFor(inputs[i], options);
		if (results[i].output_path.empty()) {
			results[i].error = "cannot resolve output path for: " +
			                   inputs[i];
			continue;
		}
		if (!options.overwrite && fs::exists(results[i].output_path)) {
			results[i].error = "output exists (overwrite=false): " +
			                   results[i].output_path;
		}
	}

	Async::IOThreadPool& pool = Async::IOThreadPool::GetSharedInstance();

	// Waves of at most max_parallel tasks; WaitAll drains each wave before
	// the next is enqueued. max_parallel==0 submits everything at once.
	const std::size_t wave =
	    options.max_parallel == 0 ? inputs.size()
	                              : std::min(options.max_parallel, inputs.size());
	for (std::size_t begin = 0; begin < inputs.size(); begin += wave) {
		const std::size_t end = std::min(begin + wave, inputs.size());
		for (std::size_t i = begin; i < end; ++i) {
			if (!results[i].error.empty()) continue;  // pre-flight failure
			ConvertOptions file_opts = options;
			file_opts.target_format = format;
			pool.Enqueue([&, i, file_opts]() mutable {
				ConvertOne(inputs[i], file_opts, &results[i]);
			});
		}
		pool.WaitAll();
	}
	return results;
}

std::string BatchConverter::OutputPathFor(const std::string& input,
                                          const ConvertOptions& options) {
	std::error_code ec;
	fs::path in(input);
	if (in.empty()) return {};

	fs::path out_dir = options.output_dir.empty()
	                       ? in.parent_path()
	                       : fs::path(options.output_dir);
	if (out_dir.empty()) out_dir = ".";

	fs::path filename = in.filename();
	filename.replace_extension("." + NormalizeFormat(options.target_format));

	const fs::path out = out_dir / filename;
	// Report logical paths without forcing the filesystem to canonicalize.
	return out.lexically_normal().string();
}

bool BatchConverter::IsExportFormatSupported(const std::string& format) {
	return !ResolveExportId(format).empty();
}

std::vector<std::string> BatchConverter::SupportedExportFormats() {
	Assimp::Exporter exporter;
	std::vector<std::string> result;
	const std::size_t count = exporter.GetExportFormatCount();
	for (std::size_t i = 0; i < count; ++i) {
		const aiExportFormatDesc* desc =
		    exporter.GetExportFormatDescription(i);
		if (!desc) continue;
		// Prefer the recommended file extension; fall back to the id.
		const char* ext =
		    (desc->fileExtension && desc->fileExtension[0])
		        ? desc->fileExtension
		        : desc->id;
		const std::string norm = NormalizeFormat(ext ? ext : "");
		if (!norm.empty() &&
		    std::find(result.begin(), result.end(), norm) ==
		        result.end()) {
			result.push_back(norm);
		}
	}
	return result;
}

}  // namespace XER::Content::FormatConv
