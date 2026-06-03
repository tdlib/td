# SPDX-FileCopyrightText: Copyright 2026 telemt community
# SPDX-License-Identifier: MIT
# telemt: https://github.com/telemt
# telemt: https://t.me/telemtrs

from __future__ import annotations

import pathlib
import unittest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
TL_CONFIG_CPP = REPO_ROOT / "tdtl" / "td" / "tl" / "tl_config.cpp"
TL_GENERATE_CPP = REPO_ROOT / "tdtl" / "td" / "tl" / "tl_generate.cpp"
TL_SIMPLE_PARSER_H = REPO_ROOT / "tdtl" / "td" / "tl" / "tl_simple_parser.h"
TL_SIMPLE_H = REPO_ROOT / "tdtl" / "td" / "tl" / "tl_simple.h"
TL_FILE_UTILS_CPP = REPO_ROOT / "tdtl" / "td" / "tl" / "tl_file_utils.cpp"
TL_STRING_OUTPUTER_CPP = REPO_ROOT / "tdtl" / "td" / "tl" / "tl_string_outputer.cpp"
TL_STRING_OUTPUTER_H = REPO_ROOT / "tdtl" / "td" / "tl" / "tl_string_outputer.h"
TL_WRITER_CPP_CPP = REPO_ROOT / "td" / "generate" / "tl_writer_cpp.cpp"
TL_WRITER_H_CPP = REPO_ROOT / "td" / "generate" / "tl_writer_h.cpp"
TL_WRITER_TD_CPP = REPO_ROOT / "td" / "generate" / "tl_writer_td.cpp"


def extract_region(text: str, begin_marker: str, end_marker: str) -> str:
    begin = text.find(begin_marker)
    if begin == -1:
        raise AssertionError(f"missing begin marker: {begin_marker}")
    end = text.find(end_marker, begin + len(begin_marker))
    if end == -1:
        raise AssertionError(f"missing end marker: {end_marker}")
    if end <= begin:
        raise AssertionError("invalid region bounds")
    return text[begin:end]


class TlConfigStringKeySafetyContractTest(unittest.TestCase):
    def test_vector_detection_uses_type_id_in_generator_and_schema(self) -> None:
        generate_source = TL_GENERATE_CPP.read_text(encoding="utf-8")
        schema_source = TL_SIMPLE_H.read_text(encoding="utf-8")

        self.assertIn(
            "static bool is_built_in_complex_type(const tl_type *type, const TL_writer &w) {",
            generate_source,
        )
        self.assertIn("return type->id == ID_VECTOR;", generate_source)

        self.assertIn("if (from_type->id == ID_VECTOR) {", schema_source)
        self.assertNotIn('if (from_type->name == "Vector") {', schema_source)

    def test_tl_generate_unpoisons_parser_objects_before_dfs_name_checks(self) -> None:
        generate_source = TL_GENERATE_CPP.read_text(encoding="utf-8")

        dfs_tree_region = extract_region(
            generate_source,
            "static void dfs_tree(const tl_tree *t, std::set<std::string> &found, const TL_writer &w) {",
            "static void dfs_combinator(const tl_combinator *constructor, std::set<std::string> &found, const TL_writer &w) {",
        )
        self.assertIn("unpoison_if_msan(*t);", dfs_tree_region)

        dfs_combinator_region = extract_region(
            generate_source,
            "static void dfs_combinator(const tl_combinator *constructor, std::set<std::string> &found, const TL_writer &w) {",
            "static void dfs_type(const tl_type *t, std::set<std::string> &found, const TL_writer &w) {",
        )
        self.assertIn("unpoison_if_msan(*constructor);", dfs_combinator_region)
        self.assertIn("unpoison_if_msan(constructor->name);", dfs_combinator_region)

        dfs_type_region = extract_region(
            generate_source,
            "static void dfs_type(const tl_type *t, std::set<std::string> &found, const TL_writer &w) {",
            "static void find_complex_types(const tl_config &config, const TL_writer &w) {",
        )
        self.assertIn("unpoison_if_msan(*t);", dfs_type_region)
        self.assertIn("unpoison_if_msan(t->name);", dfs_type_region)

    def test_writer_built_in_name_checks_unpoison_string_inputs(self) -> None:
        writer_source = TL_WRITER_TD_CPP.read_text(encoding="utf-8")

        built_in_simple_region = extract_region(
            writer_source,
            "bool TD_TL_writer::is_built_in_simple_type(const std::string &name) const {",
            "bool TD_TL_writer::is_built_in_complex_type(const std::string &name) const {",
        )
        self.assertIn("unpoison_if_msan(name);", built_in_simple_region)

        built_in_complex_region = extract_region(
            writer_source,
            "bool TD_TL_writer::is_built_in_complex_type(const std::string &name) const {",
            "bool TD_TL_writer::is_type_bare(const tl::tl_type *t) const {",
        )
        self.assertIn("unpoison_if_msan(name);", built_in_complex_region)

        class_name_region = extract_region(
            writer_source,
            "std::string TD_TL_writer::gen_class_name(std::string name) const {",
            "std::string TD_TL_writer::gen_field_name(std::string name) const {",
        )
        self.assertIn("unpoison_if_msan(name);", class_name_region)

        field_name_region = extract_region(
            writer_source,
            "std::string TD_TL_writer::gen_field_name(std::string name) const {",
            "std::string TD_TL_writer::gen_var_name(const tl::var_description &desc) const {",
        )
        self.assertIn("unpoison_if_msan(name);", field_name_region)

        var_name_region = extract_region(
            writer_source,
            "std::string TD_TL_writer::gen_var_name(const tl::var_description &desc) const {",
            "std::string TD_TL_writer::gen_parameter_name(int index) const {",
        )
        self.assertIn('std::string result = "var";', var_name_region)
        self.assertNotIn('return "var" + int_to_string(desc.index);', var_name_region)

    def test_cpp_writer_unpoisons_combinator_names_before_fetch_switch_generation(
        self,
    ) -> None:
        writer_source = TL_WRITER_CPP_CPP.read_text(encoding="utf-8")
        region = extract_region(
            writer_source,
            "std::string TD_TL_writer_cpp::gen_fetch_switch_case(const tl::tl_combinator *t, int arity) const {",
            "std::string TD_TL_writer_cpp::gen_fetch_switch_end() const {",
        )

        self.assertIn("unpoison_if_msan(*t);", region)
        self.assertIn("unpoison_if_msan(t->name);", region)

        vector_store_region = extract_region(
            writer_source,
            "std::string TD_TL_writer_cpp::gen_vector_store(const std::string &field_name, const tl::tl_tree_type *t,",
            "std::string TD_TL_writer_cpp::gen_store_class_name(const tl::tl_tree_type *tree_type) const {",
        )
        self.assertIn(
            'std::string result = "{ s.store_vector_begin(\\"";', vector_store_region
        )
        self.assertNotIn(
            'return "{ s.store_vector_begin(\\"" + get_pretty_field_name(field_name) + "\\", " + field_name +',
            vector_store_region,
        )

        constructor_field_init_region = extract_region(
            writer_source,
            "std::string TD_TL_writer_cpp::gen_constructor_field_init(int field_num, const std::string &class_name, const tl::arg &a,",
            "std::string TD_TL_writer_cpp::gen_constructor_end(const tl::tl_combinator *t, int field_count, bool is_default) const {",
        )
        self.assertIn(
            'std::string result = field_num == 0 ? ")\\n  : " : "  , ";',
            constructor_field_init_region,
        )
        self.assertNotIn(
            'return (field_num == 0 ? ")\\n  : " : "  , ") + gen_field_name(a.name)',
            constructor_field_init_region,
        )

        fetch_function_begin_region = extract_region(
            writer_source,
            "std::string TD_TL_writer_cpp::gen_fetch_function_begin(const std::string &parser_name, const std::string &class_name,",
            "std::string TD_TL_writer_cpp::gen_fetch_function_end(bool has_parent, int field_count,",
        )
        self.assertIn("unpoison_if_msan(parser_name);", fetch_function_begin_region)
        self.assertIn("unpoison_if_msan(class_name);", fetch_function_begin_region)
        self.assertIn(
            "unpoison_if_msan(parent_class_name);", fetch_function_begin_region
        )
        self.assertIn("unpoison_if_msan(vars);", fetch_function_begin_region)
        self.assertIn("std::string result;", fetch_function_begin_region)
        self.assertNotIn(
            'return "\\n" + returned_type + class_name + "::fetch(" + parser_name +',
            fetch_function_begin_region,
        )

        fetch_class_name_region = extract_region(
            writer_source,
            "std::string TD_TL_writer_cpp::gen_fetch_class_name(const tl::tl_tree_type *tree_type) const {",
            "std::string TD_TL_writer_cpp::gen_full_fetch_class_name(const tl::tl_tree_type *tree_type) const {",
        )
        self.assertIn("unpoison_object_if_msan(*tree_type);", fetch_class_name_region)
        self.assertIn("unpoison_object_if_msan(*t);", fetch_class_name_region)
        self.assertIn("unpoison_if_msan(name);", fetch_class_name_region)

        full_fetch_class_name_region = extract_region(
            writer_source,
            "std::string TD_TL_writer_cpp::gen_full_fetch_class_name(const tl::tl_tree_type *tree_type) const {",
            "std::string TD_TL_writer_cpp::gen_type_fetch(const std::string &field_name, const tl::tl_tree_type *tree_type,",
        )
        self.assertIn(
            "unpoison_object_if_msan(*tree_type);", full_fetch_class_name_region
        )
        self.assertIn("unpoison_object_if_msan(*t);", full_fetch_class_name_region)
        self.assertIn("unpoison_if_msan(name);", full_fetch_class_name_region)
        self.assertIn(
            'std::string result = "TlFetchBoxed<";', full_fetch_class_name_region
        )
        self.assertNotIn(
            'return "TlFetchBoxed<" + gen_fetch_class_name(tree_type) + ", " + int_to_string(expected_constructor_id) + ">";',
            full_fetch_class_name_region,
        )

        full_store_class_name_region = extract_region(
            writer_source,
            "std::string TD_TL_writer_cpp::gen_full_store_class_name(const tl::tl_tree_type *tree_type) const {",
            "std::string TD_TL_writer_cpp::gen_type_store(const std::string &field_name, const tl::tl_tree_type *tree_type,",
        )
        self.assertIn(
            "unpoison_object_if_msan(*tree_type);", full_store_class_name_region
        )
        self.assertIn("unpoison_object_if_msan(*t);", full_store_class_name_region)
        self.assertIn(
            'std::string result = "TlStoreBoxed<";', full_store_class_name_region
        )
        self.assertNotIn(
            'return "TlStoreBoxed<" + gen_store_class_name(tree_type) + ", " + int_to_string(t->constructors[0]->id) + ">";',
            full_store_class_name_region,
        )

        field_fetch_region = extract_region(
            writer_source,
            "std::string TD_TL_writer_cpp::gen_field_fetch(int field_num, const tl::arg &a, std::vector<tl::var_description> &vars,",
            "std::string TD_TL_writer_cpp::gen_var_type_fetch(const tl::arg &a) const {",
        )
        self.assertIn(
            "std::string exist_var_bit_mask = int_to_string(1 << a.exist_var_bit);",
            field_fetch_region,
        )
        self.assertIn("unpoison_if_msan(exist_var_bit_mask);", field_fetch_region)
        self.assertNotIn(
            'res += "if (" + gen_var_name(vars[a.exist_var_num]) + " & " + int_to_string(1 << a.exist_var_bit) + ") { ";',
            field_fetch_region,
        )
        self.assertIn(
            "std::string true_bit_mask = int_to_string(1 << a.exist_var_bit);",
            field_fetch_region,
        )
        self.assertIn("unpoison_if_msan(true_bit_mask);", field_fetch_region)
        self.assertNotIn(
            "result += int_to_string(1 << a.exist_var_bit);", field_fetch_region
        )

        constructor_id_store_raw_region = extract_region(
            writer_source,
            "std::string TD_TL_writer_cpp::gen_constructor_id_store_raw(const std::string &id) const {",
            "std::string TD_TL_writer_cpp::gen_constructor_id_store(std::int32_t id, int storer_type) const {",
        )
        self.assertIn(
            'std::string result = "s.store_binary(";', constructor_id_store_raw_region
        )
        self.assertNotIn(
            'return "s.store_binary(" + id + ");";', constructor_id_store_raw_region
        )

    def test_file_utils_unpoisons_documentation_stripping_results(self) -> None:
        file_utils_source = TL_FILE_UTILS_CPP.read_text(encoding="utf-8")

        remove_documentation_region = extract_region(
            file_utils_source,
            "std::string remove_documentation(const std::string &str) {",
            "}  // namespace tl",
        )
        self.assertIn("unpoison_if_msan(str);", remove_documentation_region)
        self.assertIn("unpoison_if_msan(line);", remove_documentation_region)
        self.assertIn(
            "std::string result(str.size(), '\\0');", remove_documentation_region
        )
        self.assertIn("std::size_t result_size = 0;", remove_documentation_region)
        self.assertIn("result[result_size++] = c;", remove_documentation_region)
        self.assertIn("result.resize(result_size);", remove_documentation_region)
        self.assertIn("unpoison_if_msan(result);", remove_documentation_region)
        self.assertIn("return result;", remove_documentation_region)
        self.assertNotIn("std::vector<char> result;", remove_documentation_region)
        self.assertNotIn("result.reserve(str.size());", remove_documentation_region)
        self.assertNotIn(
            "result.insert(result.end(), line.begin(), line.end());",
            remove_documentation_region,
        )
        self.assertNotIn(
            "return std::string(result.begin(), result.end());",
            remove_documentation_region,
        )
        self.assertIn("pos + 2 < line.size()", remove_documentation_region)
        self.assertIn(
            "pos + 1 < line.size() && line[pos] == '*' && line[pos + 1] == '/'",
            remove_documentation_region,
        )

        get_file_contents_region = extract_region(
            file_utils_source,
            "std::string get_file_contents(const std::string &file_name) {",
            "bool put_file_contents(const std::string &file_name, const std::string &contents, bool compare_documentation) {",
        )
        self.assertIn("unpoison_if_msan(file_name);", get_file_contents_region)
        self.assertIn(
            "std::string file_name_buffer(file_name.size() + 1, '\\0');",
            get_file_contents_region,
        )
        self.assertIn(
            "std::memcpy(file_name_buffer.data(), file_name.data(), file_name.size());",
            get_file_contents_region,
        )
        self.assertIn(
            "const char *file_name_cstr = file_name_buffer.data();",
            get_file_contents_region,
        )
        self.assertIn('std::fopen(file_name_cstr, "rb");', get_file_contents_region)

        put_file_contents_region = extract_region(
            file_utils_source,
            "bool put_file_contents(const std::string &file_name, const std::string &contents, bool compare_documentation) {",
            "std::string remove_documentation(const std::string &str) {",
        )
        self.assertIn("unpoison_if_msan(file_name);", put_file_contents_region)
        self.assertIn("unpoison_if_msan(contents);", put_file_contents_region)
        self.assertIn("unpoison_if_msan(old_file_contents);", put_file_contents_region)
        self.assertIn(
            "std::string file_name_buffer(file_name.size() + 1, '\\0');",
            put_file_contents_region,
        )
        self.assertIn(
            "const char *file_name_cstr = file_name_buffer.data();",
            put_file_contents_region,
        )
        self.assertIn('std::fopen(file_name_cstr, "wb");', put_file_contents_region)

        writer_source = TL_WRITER_CPP_CPP.read_text(encoding="utf-8")
        field_store_region = extract_region(
            writer_source,
            "std::string TD_TL_writer_cpp::gen_field_store(const tl::arg &a, const std::vector<tl::arg> &args,",
            "std::string TD_TL_writer_cpp::gen_fetch_function_result_begin(const std::string &parser_name,",
        )
        self.assertIn(
            "std::string other_arg_exist_var_bit = int_to_string(other_arg.exist_var_bit);",
            field_store_region,
        )
        self.assertIn("unpoison_if_msan(other_arg_exist_var_bit);", field_store_region)
        self.assertNotIn(
            'field_name += " | (" + gen_field_name(other_arg.name) + " << " + int_to_string(other_arg.exist_var_bit) + ")";',
            field_store_region,
        )
        self.assertIn(
            "std::string exist_var_bit_mask = int_to_string(1 << a.exist_var_bit);",
            field_store_region,
        )
        self.assertNotIn(
            'return "    if (" + gen_var_name(vars[a.exist_var_num]) + " & " + int_to_string(1 << a.exist_var_bit) +',
            field_store_region,
        )
        self.assertIn(
            "std::string exist_condition_var_name = gen_var_name(vars[a.exist_var_num]);",
            field_store_region,
        )
        self.assertNotIn(
            'res += "if (" + gen_var_name(vars[a.exist_var_num]) + " & " + int_to_string(1 << a.exist_var_bit) + ") { ";',
            field_store_region,
        )

    def test_cpp_writer_output_begin_sanitizes_inputs_before_string_assembly(
        self,
    ) -> None:
        writer_source = TL_WRITER_CPP_CPP.read_text(encoding="utf-8")
        region = extract_region(
            writer_source,
            "std::string TD_TL_writer_cpp::gen_output_begin(const std::string &additional_imports) const {",
            "std::string TD_TL_writer_cpp::gen_output_begin_once() const {",
        )

        self.assertIn("unpoison_if_msan(additional_imports);", region)
        self.assertIn("std::string result;", region)
        self.assertNotIn('return "#include \\"" + tl_name', region)

        pretty_field_name_region = extract_region(
            writer_source,
            "std::string TD_TL_writer_cpp::get_pretty_field_name(std::string field_name) const {",
            "std::string TD_TL_writer_cpp::get_pretty_class_name(std::string class_name) const {",
        )
        self.assertIn("unpoison_if_msan(field_name);", pretty_field_name_region)

        pretty_class_name_region = extract_region(
            writer_source,
            "std::string TD_TL_writer_cpp::get_pretty_class_name(std::string class_name) const {",
            "std::string TD_TL_writer_cpp::gen_vector_store(",
        )
        self.assertIn("unpoison_if_msan(class_name);", pretty_class_name_region)

    def test_string_outputer_unpoisons_internal_buffer_before_append(self) -> None:
        outputer_source = TL_STRING_OUTPUTER_CPP.read_text(encoding="utf-8")
        region = extract_region(
            outputer_source,
            "void tl_string_outputer::append(const std::string &str) {",
            "std::string tl_string_outputer::get_result() const {",
        )

        self.assertIn("unpoison_vector_data_if_msan(result);", region)
        self.assertIn("unpoison_if_msan(str);", region)
        self.assertIn("const auto old_size = result.size();", region)
        self.assertIn("result.resize(old_size + str.size());", region)
        self.assertIn(
            "std::memcpy(result.data() + old_size, str.data(), str.size());", region
        )
        self.assertNotIn("result.insert(result.end(), str.begin(), str.end());", region)

        get_result_region = extract_region(
            outputer_source,
            "std::string tl_string_outputer::get_result() const {",
            "}  // namespace tl",
        )
        self.assertIn(
            "std::string result_string(result.size(), '\\0');", get_result_region
        )
        self.assertIn(
            "std::memcpy(result_string.data(), result.data(), result.size());",
            get_result_region,
        )
        self.assertIn("unpoison_if_msan(result_string);", get_result_region)
        self.assertNotIn(
            "std::string result_string(result.begin(), result.end());",
            get_result_region,
        )

    def test_string_outputer_constructor_unpoisons_internal_state(self) -> None:
        header_source = TL_STRING_OUTPUTER_H.read_text(encoding="utf-8")
        cpp_source = TL_STRING_OUTPUTER_CPP.read_text(encoding="utf-8")

        self.assertIn("tl_string_outputer();", header_source)
        constructor_region = extract_region(
            cpp_source,
            "tl_string_outputer::tl_string_outputer() {",
            "void tl_string_outputer::append(const std::string &str) {",
        )
        self.assertIn("unpoison_object_if_msan(*this);", constructor_region)
        self.assertIn("unpoison_vector_data_if_msan(result);", constructor_region)

    def test_writer_and_schema_unpoison_parser_objects_before_reading_fields(
        self,
    ) -> None:
        writer_source = TL_WRITER_TD_CPP.read_text(encoding="utf-8")
        schema_source = TL_SIMPLE_H.read_text(encoding="utf-8")

        writer_region = extract_region(
            writer_source,
            "bool TD_TL_writer::is_combinator_supported(const tl::tl_combinator *constructor) const {",
            "bool TD_TL_writer::is_default_constructor_generated(",
        )
        self.assertIn("unpoison_if_msan(*constructor);", writer_region)
        self.assertIn("unpoison_if_msan(constructor->args);", writer_region)
        self.assertIn("if (value.type != nullptr) {", writer_source)
        self.assertIn("unpoison_if_msan(*value.type);", writer_source)

        default_ctor_region = extract_region(
            writer_source,
            "bool TD_TL_writer::is_default_constructor_generated(const tl::tl_combinator *t, bool can_be_parsed,",
            "bool TD_TL_writer::is_full_constructor_generated(",
        )
        self.assertIn("unpoison_if_msan(*t);", default_ctor_region)

        full_ctor_region = extract_region(
            writer_source,
            "bool TD_TL_writer::is_full_constructor_generated(const tl::tl_combinator *t, bool can_be_parsed,",
            "int TD_TL_writer::get_storer_type(",
        )
        self.assertIn("unpoison_if_msan(*t);", full_ctor_region)
        self.assertIn("unpoison_if_msan(t->name);", full_ctor_region)

        type_name_region = extract_region(
            writer_source,
            "std::string TD_TL_writer::gen_type_name(const tl::tl_tree_type *tree_type) const {",
            "std::string TD_TL_writer::gen_array_type_name(const tl::tl_tree_array *arr, const std::string &field_name) const {",
        )
        self.assertIn("unpoison_object_if_msan(*tree_type);", type_name_region)
        self.assertIn("unpoison_object_if_msan(*t);", type_name_region)
        self.assertIn("unpoison_if_msan(name);", type_name_region)

        schema_ctor_region = extract_region(
            schema_source,
            "  explicit Schema(const tl_config &config) {",
            "  std::vector<const CustomType *> custom_types;",
        )
        self.assertIn("unpoison_if_msan(*from_type);", schema_ctor_region)

        schema_get_type_region = extract_region(
            schema_source,
            "  const Type *get_type(const tl_type *from_type) {",
            "  const CustomType *get_custom_type(const tl_type *from_type) {",
        )
        self.assertIn("unpoison_if_msan(*from_type);", schema_get_type_region)
        self.assertIn("unpoison_if_msan(from_type->name);", schema_get_type_region)

        schema_tree_region = extract_region(
            schema_source,
            "  const Type *get_type(const tl_tree *tree) {",
            "};",
        )
        self.assertIn("unpoison_object_if_msan(*tree);", schema_tree_region)
        self.assertIn("unpoison_object_if_msan(*type_tree);", schema_tree_region)
        self.assertIn(
            "unpoison_vector_data_if_msan(type_tree->children);", schema_tree_region
        )
        self.assertIn("unpoison_if_msan(*type_tree->type);", schema_tree_region)

    def test_schema_caches_by_logical_id_without_map_operator_reads(self) -> None:
        schema_source = TL_SIMPLE_H.read_text(encoding="utf-8")

        self.assertIn(
            "std::vector<std::pair<std::int32_t, Type *>> type_by_id;",
            schema_source,
        )
        self.assertIn(
            "std::vector<std::pair<std::int32_t, Constructor *>> constructor_by_id;",
            schema_source,
        )
        self.assertIn(
            "std::vector<std::pair<std::int32_t, Function *>> function_by_id;",
            schema_source,
        )
        self.assertNotIn(
            "std::vector<std::pair<const tl_type *, Type *>> type_by_tl_type;",
            schema_source,
        )
        self.assertNotIn(
            "std::vector<std::pair<const tl_combinator *, Constructor *>> constructor_by_tl_combinator;",
            schema_source,
        )
        self.assertNotIn(
            "std::vector<std::pair<const tl_combinator *, Function *>> function_by_tl_combinator;",
            schema_source,
        )
        self.assertNotIn("std::map<const tl_type *, Type *>", schema_source)
        self.assertNotIn(
            "std::map<const tl_combinator *, Constructor *>", schema_source
        )
        self.assertNotIn("std::map<const tl_combinator *, Function *>", schema_source)
        self.assertNotIn("std::map<std::int32_t, Type *> type_by_id;", schema_source)
        self.assertNotIn(
            "std::map<std::int32_t, Constructor *> constructor_by_id;", schema_source
        )
        self.assertNotIn(
            "std::map<std::int32_t, Function *> function_by_id;", schema_source
        )
        self.assertNotIn("type_by_id[from_type->id]", schema_source)
        self.assertNotIn("constructor_by_id[from->id]", schema_source)
        self.assertNotIn("function_by_id[from->id]", schema_source)
        self.assertIn(
            "for (const auto &[cached_type_id, cached_type] : type_by_id) {",
            schema_source,
        )
        self.assertIn("if (cached_type_id == from_type->id) {", schema_source)
        self.assertIn("type_by_id.emplace_back(from_type->id, type);", schema_source)
        self.assertIn(
            "for (const auto &[cached_constructor_id, cached_constructor] : constructor_by_id) {",
            schema_source,
        )
        self.assertIn(
            "if (cached_constructor_id == from->id) {",
            schema_source,
        )
        self.assertIn(
            "constructor_by_id.emplace_back(from->id, constructor);",
            schema_source,
        )
        self.assertNotIn("constructor_by_id[from->id]", schema_source)
        self.assertIn(
            "for (const auto &[cached_function_id, cached_function] : function_by_id) {",
            schema_source,
        )
        self.assertIn(
            "if (cached_function_id == from->id) {",
            schema_source,
        )
        self.assertIn(
            "function_by_id.emplace_back(from->id, function);",
            schema_source,
        )
        self.assertNotIn("function_by_id[from->id]", schema_source)
        self.assertIn(
            "unpoison_vector_data_if_msan(value.constructors);", schema_source
        )

    def test_fixed_file_count_generation_uses_flat_outputer_storage(self) -> None:
        generate_source = TL_GENERATE_CPP.read_text(encoding="utf-8")
        fixed_count_region = extract_region(
            generate_source,
            "bool write_tl_to_fixed_file_count(const tl_config &config, const std::string &file_name_prefix,",
            "bool write_tl_to_multiple_files(const tl_config &config, const std::string &file_name_prefix,",
        )

        self.assertIn(
            "std::vector<std::unique_ptr<tl_string_outputer>> outs;", fixed_count_region
        )
        self.assertIn(
            "outs.reserve(static_cast<std::size_t>(file_count));", fixed_count_region
        )
        self.assertIn(
            "outs.emplace_back(std::make_unique<tl_string_outputer>());",
            fixed_count_region,
        )
        self.assertIn(
            "std::string safe_file_name_prefix = file_name_prefix;", fixed_count_region
        )
        self.assertIn(
            "std::string safe_file_name_suffix = file_name_suffix;", fixed_count_region
        )
        self.assertIn("unpoison_if_msan(file_name_prefix);", fixed_count_region)
        self.assertIn("unpoison_if_msan(file_name_suffix);", fixed_count_region)
        self.assertIn(
            "std::string file_index = TL_writer::int_to_string(i);", fixed_count_region
        )
        self.assertIn(
            "std::string file_name(safe_file_name_prefix.size() + 1 + file_index.size() + safe_file_name_suffix.size(), '\\0');",
            fixed_count_region,
        )
        self.assertNotIn(
            'std::string file_name = file_name_prefix + "_" + TL_writer::int_to_string(i) + file_name_suffix;',
            fixed_count_region,
        )
        self.assertNotIn("file_name += file_name_suffix;", fixed_count_region)
        self.assertNotIn("std::vector<tl_string_outputer>", fixed_count_region)
        self.assertNotIn("std::map<int, tl_string_outputer> outs;", fixed_count_region)
        self.assertNotIn("outs[get_file_num", fixed_count_region)
        self.assertIn("outs[static_cast<std::size_t>(get_file_num(", fixed_count_region)

    def test_writer_h_get_id_avoids_operator_plus_temporaries(self) -> None:
        writer_h_source = TL_WRITER_H_CPP.read_text(encoding="utf-8")
        region = extract_region(
            writer_h_source,
            "std::string TD_TL_writer_h::gen_get_id(const std::string &class_name, std::int32_t id, bool is_proxy) const {",
            "std::string TD_TL_writer_h::gen_function_result_type(const tl::tl_tree *result) const {",
        )

        self.assertIn("std::string id_string = int_to_string(id);", region)
        self.assertIn("unpoison_if_msan(id_string);", region)
        self.assertNotIn('"  static const std::int32_t ID = " +', region)

        forward_region = extract_region(
            writer_h_source,
            "std::string TD_TL_writer_h::gen_forward_class_declaration(const std::string &class_name, bool is_proxy) const {",
            "std::string TD_TL_writer_h::gen_class_begin(const std::string &class_name, const std::string &base_class_name,",
        )
        self.assertIn('std::string result = "class ";', forward_region)
        self.assertIn("unpoison_if_msan(class_name);", forward_region)
        self.assertNotIn('return "class " + class_name + ";\\n\\n";', forward_region)

        class_begin_region = extract_region(
            writer_h_source,
            "std::string TD_TL_writer_h::gen_class_begin(const std::string &class_name, const std::string &base_class_name,",
            "std::string TD_TL_writer_h::gen_class_end() const {",
        )
        self.assertIn("unpoison_if_msan(class_name);", class_begin_region)
        self.assertIn("unpoison_if_msan(base_class_name);", class_begin_region)
        self.assertNotIn('return "class " + class_name +', class_begin_region)

        store_function_region = extract_region(
            writer_h_source,
            "std::string TD_TL_writer_h::gen_store_function_begin(const std::string &storer_name, const std::string &class_name,",
            "std::string TD_TL_writer_h::gen_store_function_end(const std::vector<tl::var_description> &vars,",
        )
        self.assertIn(
            'std::string field_name_suffix = storer_type == 0 ? "" : ", const char *field_name";',
            store_function_region,
        )
        self.assertIn("unpoison_if_msan(field_name_suffix);", store_function_region)
        self.assertNotIn('"  void store(" +', store_function_region)

        flags_region = extract_region(
            writer_h_source,
            "std::string TD_TL_writer_h::gen_flags_definitions(const tl::tl_combinator *t, bool can_be_stored) const {",
            "std::string TD_TL_writer_h::gen_uni(const tl::tl_tree_type *result_type, std::vector<tl::var_description> &vars,",
        )
        self.assertIn(
            "std::string flag_mask = int_to_string(1 << p.second);", flags_region
        )
        self.assertIn("unpoison_if_msan(flag_mask);", flags_region)
        self.assertNotIn(
            'res += p.first + "_MASK = " + int_to_string(1 << p.second);', flags_region
        )

        function_vars_region = extract_region(
            writer_h_source,
            "std::string TD_TL_writer_h::gen_function_vars(const tl::tl_combinator *t,",
            "std::string TD_TL_writer_h::gen_flags_definitions(const tl::tl_combinator *t, bool can_be_stored) const {",
        )
        self.assertIn(
            'std::string mutable_var_class = gen_class_name("#");', function_vars_region
        )
        self.assertIn(
            "std::string mutable_var_name = gen_var_name(vars[i]);",
            function_vars_region,
        )
        self.assertIn("unpoison_if_msan(mutable_var_class);", function_vars_region)
        self.assertIn("unpoison_if_msan(mutable_var_name);", function_vars_region)
        self.assertNotIn(
            'res += "  mutable " + gen_class_name("#") + " " + gen_var_name(vars[i]) + ";\\n";',
            function_vars_region,
        )

    def test_fetch_string_builds_from_sized_zeroed_storage(self) -> None:
        source = TL_SIMPLE_PARSER_H.read_text(encoding="utf-8")

        self.assertIn(
            "std::string result(static_cast<std::size_t>(result_len), '\\0');",
            source,
        )
        self.assertIn("std::memcpy(result.data(), data + 1, result.size());", source)
        self.assertIn("std::memcpy(result.data(), data + 4, result.size());", source)
        self.assertNotIn("std::string result(data + 1, result_len);", source)
        self.assertNotIn("std::string result(data + 4, result_len);", source)

    def test_try_parse_string_uses_direct_string_result_path(self) -> None:
        source = TL_CONFIG_CPP.read_text(encoding="utf-8")
        region = extract_region(
            source,
            "std::string tl_config_parser::try_parse_string() {",
            "template <class T>",
        )

        self.assertIn("std::string result = p.fetch_string();", region)
        self.assertIn("if (p.get_error() != nullptr) {", region)
        self.assertIn("return result;", region)
        self.assertNotIn("return try_parse(p.fetch_string());", region)

    def test_parse_config_recursively_unpoisons_tl_graph_before_return(self) -> None:
        source = TL_CONFIG_CPP.read_text(encoding="utf-8")
        parse_region = extract_region(
            source,
            "tl_config tl_config_parser::parse_config() {",
            "}  // namespace td::tl",
        )

        self.assertIn("void unpoison_tree_graph(", source)
        self.assertIn("void unpoison_combinator_graph(", source)
        self.assertIn("void unpoison_type_graph(", source)
        self.assertIn("void unpoison_config_graph(tl_config &config)", source)
        self.assertIn("unpoison_config_graph(config);", parse_region)

    def test_parser_assigns_type_and_combinator_names_from_initialized_ranges(
        self,
    ) -> None:
        source = TL_CONFIG_CPP.read_text(encoding="utf-8")

        type_region = extract_region(
            source,
            "tl_type *tl_config_parser::read_type() {",
            "tl_config tl_config_parser::parse_config() {",
        )
        self.assertIn("std::string parsed_type_name = try_parse_string();", type_region)
        self.assertIn("unpoison_if_msan(parsed_type_name);", type_region)
        self.assertIn(
            "auto *type = allocate_zeroed<tl_type>(type_id, std::move(parsed_type_name));",
            type_region,
        )
        self.assertIn("const auto type_id = try_parse_int();", type_region)
        self.assertNotIn(
            "std::string(parsed_type_name.data(), parsed_type_name.size())",
            type_region,
        )
        self.assertNotIn(
            "type->name.assign(parsed_type_name.data(), parsed_type_name.size());",
            type_region,
        )
        self.assertNotIn("type->name = try_parse_string();", type_region)

        combinator_region = extract_region(
            source,
            "tl_combinator *tl_config_parser::read_combinator() {",
            "tl_type *tl_config_parser::read_type() {",
        )
        self.assertIn(
            "std::string parsed_combinator_name = try_parse_string();",
            combinator_region,
        )
        self.assertIn(
            "auto *combinator =",
            combinator_region,
        )
        self.assertIn(
            "allocate_zeroed<tl_combinator>(combinator_id, std::move(parsed_combinator_name));",
            combinator_region,
        )
        self.assertIn("const auto combinator_id = try_parse_int();", combinator_region)
        self.assertIn("unpoison_if_msan(parsed_combinator_name);", combinator_region)
        self.assertNotIn(
            "combinator->name.assign(parsed_combinator_name.data(), parsed_combinator_name.size());",
            combinator_region,
        )
        self.assertNotIn(
            "std::string(parsed_combinator_name.data(), parsed_combinator_name.size())",
            combinator_region,
        )
        self.assertNotIn("combinator->name = try_parse_string();", combinator_region)

        helper_region = extract_region(
            source,
            "template <class T, class... Args>",
            "tl_config::~tl_config() {",
        )
        self.assertIn("std::memset(storage, 0, sizeof(T));", helper_region)
        self.assertIn(
            "return new (storage) T(std::forward<Args>(args)...);", helper_region
        )
        self.assertIn("void unpoison_if_msan(T &value) {", helper_region)
        self.assertIn("void unpoison_if_msan(std::string &value) {", helper_region)
        self.assertIn("__msan_unpoison(&value, sizeof(value));", helper_region)
        self.assertIn("__msan_unpoison(value.data(), value.size());", helper_region)
        self.assertIn("__msan_unpoison(value.data() + value.size(), 1);", helper_region)
        self.assertIn("(void)value;", helper_region)

        args_region = extract_region(
            source,
            "std::vector<arg> tl_config_parser::read_args(int *var_count) {",
            "tl_combinator *tl_config_parser::read_combinator() {",
        )

        self.assertIn("unpoison_if_msan(parsed_type_name);", type_region)
        self.assertIn("unpoison_if_msan(type->name);", type_region)
        self.assertIn("unpoison_if_msan(*type);", type_region)
        self.assertIn("unpoison_if_msan(parsed_combinator_name);", combinator_region)
        self.assertIn("unpoison_if_msan(combinator->name);", combinator_region)
        self.assertIn("unpoison_if_msan(*combinator);", combinator_region)
        self.assertIn("unpoison_if_msan(parsed_arg_name);", args_region)
        self.assertIn("unpoison_if_msan(cur_arg.name);", args_region)
        self.assertIn("unpoison_if_msan(args[i]);", args_region)
        self.assertIn("unpoison_if_msan(args);", args_region)
        self.assertIn("std::string parsed_arg_name = try_parse_string();", args_region)
        self.assertIn("unpoison_if_msan(parsed_arg_name);", args_region)
        self.assertIn(
            "arg cur_arg(std::move(parsed_arg_name));",
            args_region,
        )
        self.assertNotIn("arg cur_arg{};", args_region)
        self.assertNotIn(
            "std::string(parsed_arg_name.data(), parsed_arg_name.size())", args_region
        )
        self.assertNotIn(
            "cur_arg.name.assign(parsed_arg_name.data(), parsed_arg_name.size());",
            args_region,
        )
        self.assertNotIn("cur_arg.name = try_parse_string();", args_region)

    def test_add_type_rebuilds_safe_key_without_changing_overwrite_semantics(
        self,
    ) -> None:
        source = TL_CONFIG_CPP.read_text(encoding="utf-8")
        region = extract_region(
            source,
            "void tl_config::add_type(tl_type *type) {",
            "tl_type *tl_config::get_type(std::int32_t type_id) const {",
        )

        self.assertIn(
            "name_to_type.insert_or_assign(std::string(type->name.data(), type->name.size()), type);",
            region,
        )
        self.assertNotIn("name_to_type.try_emplace(", region)
        self.assertNotIn("name_to_type.try_emplace(type->name.c_str(), type);", region)
        self.assertNotIn("name_to_type[type->name] = type;", region)

    def test_add_function_rebuilds_safe_key_without_changing_overwrite_semantics(
        self,
    ) -> None:
        source = TL_CONFIG_CPP.read_text(encoding="utf-8")
        region = extract_region(
            source,
            "void tl_config::add_function(tl_combinator *function) {",
            "tl_combinator *tl_config::get_function(std::int32_t function_id) {",
        )

        self.assertIn(
            "name_to_function.insert_or_assign(std::string(function->name.data(), function->name.size()), function);",
            region,
        )
        self.assertNotIn("name_to_function.try_emplace(", region)
        self.assertNotIn(
            "name_to_function.try_emplace(function->name.c_str(), function);", region
        )
        self.assertNotIn("name_to_function[function->name] = function;", region)


if __name__ == "__main__":
    unittest.main()
