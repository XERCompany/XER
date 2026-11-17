#include "flux/v8_console_table.h"

#include <absl/strings/str_cat.h>

#include <algorithm>
#include <limits>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "flux/v8_console_runtime.h"
#include "helper/class_builder.h"

namespace flux::console {
namespace {

constexpr std::size_t kConsoleTableMaxRows = 32;
constexpr std::size_t kConsoleTableMaxColumns = 12;
constexpr std::size_t kConsoleTableMaxCellWidth = 48;

constexpr char kConsoleTableValueColumn[] = "Values";
constexpr char kConsoleTableOverflowColumn[] = "...";

struct TableRow {
	std::string label;
	std::unordered_map<std::string, std::string> cells;
};

std::string CollapseWhitespace(std::string_view text) {
	std::string out;
	out.reserve(text.size());
	bool previous_space = false;
	for (char ch : text) {
		const bool is_space = ch == '\n' || ch == '\r' || ch == '\t' || ch == ' ';
		if (is_space) {
			if (!previous_space) {
				out.push_back(' ');
			}
			previous_space = true;
			continue;
		}
		out.push_back(ch);
		previous_space = false;
	}
	while (!out.empty() && out.front() == ' ') {
		out.erase(out.begin());
	}
	while (!out.empty() && out.back() == ' ') {
		out.pop_back();
	}
	return out;
}

std::string PadRight(std::string text, std::size_t width) {
	if (text.size() < width) {
		text.append(width - text.size(), ' ');
	}
	return text;
}

std::string TruncateCell(std::string text) {
	if (text.size() <= kConsoleTableMaxCellWidth) {
		return text;
	}
	if (kConsoleTableMaxCellWidth <= 3) {
		return text.substr(0, kConsoleTableMaxCellWidth);
	}
	text.resize(kConsoleTableMaxCellWidth - 3);
	text.append("...");
	return text;
}

std::string OverflowSummary(std::size_t count, std::string_view noun) {
	return absl::StrCat("+", count, " more ", std::string(noun));
}

bool ContainsString(const std::vector<std::string>& values, std::string_view needle) {
	return std::find(values.begin(), values.end(), needle) != values.end();
}

void AppendUnique(std::vector<std::string>* values, const std::string& value) {
	if (values == nullptr || ContainsString(*values, value)) {
		return;
	}
	values->push_back(value);
}

std::vector<std::string> ReadStringArray(v8::Isolate* isolate,
					 v8::Local<v8::Context> context,
					 v8::Local<v8::Value> value,
					 std::size_t max_items) {
	std::vector<std::string> out;
	if (!value->IsArray()) {
		return out;
	}
	v8::Local<v8::Array> array = value.As<v8::Array>();
	const std::uint32_t limit = static_cast<std::uint32_t>(std::min<std::size_t>(array->Length(), max_items));
	for (std::uint32_t index = 0; index < limit; ++index) {
		v8::Local<v8::Value> entry;
		if (!array->Get(context, index).ToLocal(&entry)) {
			continue;
		}
		AppendUnique(&out, Utf8(isolate, entry));
	}
	return out;
}

std::vector<std::string> ReadOwnPropertyNames(v8::Isolate* isolate,
					      v8::Local<v8::Context> context,
					      v8::Local<v8::Object> object,
					      std::size_t max_items,
					      std::size_t* total_items = nullptr) {
	std::vector<std::string> out;
	v8::Local<v8::Array> keys;
	if (!object->GetOwnPropertyNames(context).ToLocal(&keys)) {
		if (total_items != nullptr) {
			*total_items = 0;
		}
		return out;
	}
	if (total_items != nullptr) {
		*total_items = static_cast<std::size_t>(keys->Length());
	}
	const std::uint32_t limit = static_cast<std::uint32_t>(std::min<std::size_t>(keys->Length(), max_items));
	for (std::uint32_t index = 0; index < limit; ++index) {
		v8::Local<v8::Value> key;
		if (!keys->Get(context, index).ToLocal(&key)) {
			continue;
		}
		AppendUnique(&out, Utf8(isolate, key));
	}
	return out;
}

bool ParseArrayIndex(std::string_view text, std::uint32_t* index_out) {
	if (index_out == nullptr || text.empty()) {
		return false;
	}
	std::uint64_t value = 0;
	for (char ch : text) {
		if (ch < '0' || ch > '9') {
			return false;
		}
		value = value * 10u + static_cast<std::uint64_t>(ch - '0');
		if (value > std::numeric_limits<std::uint32_t>::max()) {
			return false;
		}
	}
	*index_out = static_cast<std::uint32_t>(value);
	return true;
}

bool IsStructuredTableValue(v8::Local<v8::Value> value) {
	if (value->IsArray()) {
		return true;
	}
	return value->IsObject()
		&& !value->IsFunction()
		&& !value->IsArrayBuffer()
		&& !value->IsTypedArray()
		&& !value->IsDate()
		&& !value->IsRegExp();
}

bool ColumnAllowed(const std::vector<std::string>& filter, const std::string& column) {
	return filter.empty() || ContainsString(filter, column);
}

std::string FormatTableCell(v8::Isolate* isolate,
				    v8::Local<v8::Context> context,
				    v8::Local<v8::Value> value) {
	FormatOptions options;
	options.max_depth = 1;
	options.max_collection_entries = 4;
	options.top_level_plain_strings = true;
	return TruncateCell(CollapseWhitespace(JoinValues(isolate, context, {value}, options)));
}

void MaybeAddDiscoveredColumn(const std::vector<std::string>& filter,
				      std::vector<std::string>* columns,
				      const std::string& column) {
	if (columns == nullptr || !filter.empty()) {
		return;
	}
	AppendUnique(columns, column);
}

void MaybeSetOverflowCell(const std::vector<std::string>& filter,
				 std::vector<std::string>* columns,
				 TableRow* row,
				 std::size_t hidden_count,
				 std::string_view noun) {
	if (row == nullptr || columns == nullptr || !filter.empty() || hidden_count == 0) {
		return;
	}
	AppendUnique(columns, kConsoleTableOverflowColumn);
	row->cells[kConsoleTableOverflowColumn] = OverflowSummary(hidden_count, noun);
}

void AppendOverflowSummaryRow(std::vector<std::string>* columns,
				      std::vector<TableRow>* rows,
				      std::size_t hidden_rows) {
	if (columns == nullptr || rows == nullptr || hidden_rows == 0) {
		return;
	}
	AppendUnique(columns, kConsoleTableOverflowColumn);
	TableRow summary_row;
	summary_row.label = kConsoleTableOverflowColumn;
	summary_row.cells[kConsoleTableOverflowColumn] = OverflowSummary(hidden_rows, "rows");
	rows->push_back(std::move(summary_row));
}

void PopulateTableRow(v8::Isolate* isolate,
			      v8::Local<v8::Context> context,
			      v8::Local<v8::Value> value,
			      const std::vector<std::string>& filter,
			      std::vector<std::string>* columns,
			      TableRow* row) {
	if (row == nullptr || columns == nullptr) {
		return;
	}
	if (!IsStructuredTableValue(value)) {
		if (!ColumnAllowed(filter, kConsoleTableValueColumn)) {
			return;
		}
		MaybeAddDiscoveredColumn(filter, columns, kConsoleTableValueColumn);
		row->cells[kConsoleTableValueColumn] = FormatTableCell(isolate, context, value);
		return;
	}

	if (value->IsArray()) {
		v8::Local<v8::Array> array = value.As<v8::Array>();
		if (!filter.empty()) {
			for (const std::string& column : filter) {
				std::uint32_t parsed_index = 0;
				if (!ParseArrayIndex(column, &parsed_index) || parsed_index >= array->Length()) {
					continue;
				}
				v8::Local<v8::Value> cell_value;
				if (!array->Get(context, parsed_index).ToLocal(&cell_value)) {
					continue;
				}
				row->cells[column] = FormatTableCell(isolate, context, cell_value);
			}
			return;
		}

		const std::uint32_t limit = static_cast<std::uint32_t>(std::min<std::size_t>(array->Length(), kConsoleTableMaxColumns));
		for (std::uint32_t index = 0; index < limit; ++index) {
			const std::string column = absl::StrCat(index);
			v8::Local<v8::Value> cell_value;
			if (!array->Get(context, index).ToLocal(&cell_value)) {
				continue;
			}
			MaybeAddDiscoveredColumn(filter, columns, column);
			row->cells[column] = FormatTableCell(isolate, context, cell_value);
		}
		MaybeSetOverflowCell(filter, columns, row, static_cast<std::size_t>(array->Length()) - limit, "columns");
		return;
	}

	v8::Local<v8::Object> object = value.As<v8::Object>();
	if (!filter.empty()) {
		for (const std::string& column : filter) {
			v8::Local<v8::Value> cell_value;
			if (!object->Get(context, Engine::Helper::ToV8Str(isolate, column)).ToLocal(&cell_value)) {
				continue;
			}
			row->cells[column] = FormatTableCell(isolate, context, cell_value);
		}
		return;
	}

	std::size_t total_columns = 0;
	for (const std::string& column : ReadOwnPropertyNames(isolate, context, object, kConsoleTableMaxColumns, &total_columns)) {
		v8::Local<v8::Value> cell_value;
		if (!object->Get(context, Engine::Helper::ToV8Str(isolate, column)).ToLocal(&cell_value)) {
			continue;
		}
		MaybeAddDiscoveredColumn(filter, columns, column);
		row->cells[column] = FormatTableCell(isolate, context, cell_value);
	}
	MaybeSetOverflowCell(filter, columns, row, total_columns > row->cells.size() ? total_columns - row->cells.size() : 0, "columns");
}

std::string RenderResolvedTable(const std::vector<std::string>& columns, const std::vector<TableRow>& rows) {
	std::vector<std::string> headers;
	headers.reserve(columns.size() + 1);
	headers.push_back("(index)");
	headers.insert(headers.end(), columns.begin(), columns.end());

	std::vector<std::size_t> widths(headers.size(), 0);
	for (std::size_t index = 0; index < headers.size(); ++index) {
		widths[index] = headers[index].size();
	}
	for (const TableRow& row : rows) {
		widths[0] = std::max(widths[0], row.label.size());
		for (std::size_t index = 0; index < columns.size(); ++index) {
			const auto cell_it = row.cells.find(columns[index]);
			if (cell_it == row.cells.end()) {
				continue;
			}
			widths[index + 1] = std::max(widths[index + 1], cell_it->second.size());
		}
	}

	auto append_row = [&](const std::vector<std::string>& cells, std::string* out) {
		absl::StrAppend(out, "|");
		for (std::size_t index = 0; index < cells.size(); ++index) {
			absl::StrAppend(out, " ", PadRight(cells[index], widths[index]), " |");
		}
		absl::StrAppend(out, "\n");
	};

	auto append_separator = [&](std::string* out) {
		absl::StrAppend(out, "|");
		for (std::size_t width : widths) {
			absl::StrAppend(out, " ", std::string(width, '-'), " |");
		}
		absl::StrAppend(out, "\n");
	};

	std::string out;
	append_row(headers, &out);
	append_separator(&out);
	for (const TableRow& row : rows) {
		std::vector<std::string> cells;
		cells.reserve(columns.size() + 1);
		cells.push_back(row.label);
		for (const std::string& column : columns) {
			const auto cell_it = row.cells.find(column);
			cells.push_back(cell_it == row.cells.end() ? std::string() : cell_it->second);
		}
		append_row(cells, &out);
	}
	if (!out.empty()) {
		out.pop_back();
	}
	return out;
}

}  // namespace

std::string RenderTable(v8::Isolate* isolate,
			v8::Local<v8::Context> context,
			v8::Local<v8::Value> input,
			v8::Local<v8::Value> columns_filter) {
	const std::vector<std::string> filter = columns_filter.IsEmpty()
		? std::vector<std::string>()
		: ReadStringArray(isolate, context, columns_filter, kConsoleTableMaxColumns);
	std::vector<std::string> columns = filter;
	std::vector<TableRow> rows;
	std::size_t hidden_rows = 0;

	if (input->IsArray()) {
		v8::Local<v8::Array> array = input.As<v8::Array>();
		const std::uint32_t limit = static_cast<std::uint32_t>(std::min<std::size_t>(array->Length(), kConsoleTableMaxRows));
		for (std::uint32_t index = 0; index < limit; ++index) {
			v8::Local<v8::Value> row_value;
			if (!array->Get(context, index).ToLocal(&row_value)) {
				continue;
			}
			TableRow row;
			row.label = absl::StrCat(index);
			PopulateTableRow(isolate, context, row_value, filter, &columns, &row);
			rows.push_back(std::move(row));
		}
		hidden_rows = static_cast<std::size_t>(array->Length()) - limit;
	} else if (IsStructuredTableValue(input)) {
		v8::Local<v8::Object> object = input.As<v8::Object>();
		std::size_t total_rows = 0;
		for (const std::string& key : ReadOwnPropertyNames(isolate, context, object, kConsoleTableMaxRows, &total_rows)) {
			v8::Local<v8::Value> row_value;
			if (!object->Get(context, Engine::Helper::ToV8Str(isolate, key)).ToLocal(&row_value)) {
				continue;
			}
			TableRow row;
			row.label = key;
			PopulateTableRow(isolate, context, row_value, filter, &columns, &row);
			rows.push_back(std::move(row));
		}
		hidden_rows = total_rows > rows.size() ? total_rows - rows.size() : 0;
	} else {
		TableRow row;
		row.label = "0";
		PopulateTableRow(isolate, context, input, filter, &columns, &row);
		rows.push_back(std::move(row));
	}

	AppendOverflowSummaryRow(&columns, &rows, hidden_rows);
	return rows.empty() ? std::string("| (index) |\n| ------- |") : RenderResolvedTable(columns, rows);
}

}  // namespace flux::console