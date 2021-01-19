#pragma once

#include <vcpkg/base/expected.h>
#include <vcpkg/base/textrowcol.h>

#include <vcpkg/packagespec.h>

#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace vcpkg::Parse
{
    using Paragraph = std::unordered_map<std::string, std::pair<std::string, TextRowCol>>;

    struct ParagraphParser
    {
        ParagraphParser(Paragraph&& fields);

        std::string required_field(const std::string& fieldname);
        void required_field(const std::string& fieldname, std::string& out);
        void required_field(const std::string& fieldname, std::pair<std::string&, TextRowCol&> out);

        std::string optional_field(const std::string& fieldname);
        void optional_field(const std::string& fieldname, std::pair<std::string&, TextRowCol&> out);

        void add_generic_error(const std::string& fieldname, StringView message);

        void apply_valid_field_list(View<StringView> valid_fields);

        void add_additional_error_text(StringView text);

        Optional<std::string> error_info(const std::string& name) const;

    private:
        Paragraph&& fields;
        int m_first_row;
        std::vector<std::string> error_messages;
        std::string additional_error_text;
    };

    ExpectedS<std::vector<std::string>> parse_default_features_list(const std::string& str,
                                                                    StringView origin = "<unknown>",
                                                                    TextRowCol textrowcol = {});
    ExpectedS<std::vector<ParsedQualifiedSpecifier>> parse_qualified_specifier_list(const std::string& str,
                                                                                    StringView origin = "<unknown>",
                                                                                    TextRowCol textrowcol = {});
    ExpectedS<std::vector<Dependency>> parse_dependencies_list(const std::string& str,
                                                               StringView origin = "<unknown>",
                                                               TextRowCol textrowcol = {});
}
