#include "reader_pipeline.h"

#include "reader_manager.h"
#include "../schema_validation/integrity_check.h"

#include <sstream>

namespace XER::ModelsBuilder::Reader {

namespace {

std::string DetectFormat(const std::string& filepath) {
	const size_t dot = filepath.find_last_of('.');
	if (dot == std::string::npos) {
		return "unknown";
	}
	return filepath.substr(dot + 1);
}

}  // namespace

LoadResult ReaderPipeline::Process(const std::string& filepath) {
	ReaderManager manager;
	return manager.Load(filepath);
}

PipelineQualityResult ReaderPipeline::ProcessWithQuality(const std::string& filepath) {
	PipelineQualityResult out;
	out.load_result = Process(filepath);
	if (!out.load_result.success || !out.load_result.model) {
		out.summary = out.load_result.error.empty() ? "Reader load failed" : out.load_result.error;
		return out;
	}

	Schema::SchemaValidator validator;
	out.validation = validator.Validate(*out.load_result.model);
	out.metadata = Schema::MetadataExtractor::Extract(*out.load_result.model, DetectFormat(filepath));

	std::string integrity_error;
	out.integrity_ok = Schema::IntegrityCheck::VerifyModel(*out.load_result.model, integrity_error);
	out.passed = out.validation.valid && out.integrity_ok;

	std::ostringstream ss;
	ss << "quality=" << (out.passed ? "pass" : "fail")
		 << ", layers=" << out.metadata.layer_count
		 << ", validation_errors=" << out.validation.errors.size();
	if (!out.integrity_ok && !integrity_error.empty()) {
		ss << ", integrity_error=" << integrity_error;
	}
	out.summary = ss.str();
	return out;
}

}  // namespace XER::ModelsBuilder::Reader

