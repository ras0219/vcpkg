#include "pch.h"

#include <vcpkg/logicexpression.h>
#include <vcpkg/packagespec.h>
#include <vcpkg/sourceparagraph.h>
#include <vcpkg/triplet.h>

#include <vcpkg/base/checks.h>
#include <vcpkg/base/expected.h>
#include <vcpkg/base/span.h>
#include <vcpkg/base/system.print.h>
#include <vcpkg/base/util.h>

namespace vcpkg
{
    using namespace vcpkg::Parse;

    namespace SourceParagraphFields
    {
        static const std::string BUILD_DEPENDS = "Build-Depends";
        static const std::string DEFAULTFEATURES = "Default-Features";
        static const std::string DESCRIPTION = "Description";
        static const std::string FEATURE = "Feature";
        static const std::string MAINTAINERS = "Maintainer";
        static const std::string SOURCE = "Source";
        static const std::string VERSION = "Version";
        static const std::string HOMEPAGE = "Homepage";
        static const std::string TYPE = "Type";
        static const std::string SUPPORTS = "Supports";
    }

    namespace ManifestFields
    {
        static const std::string BUILD_DEPENDS = "dependencies";
        static const std::string DEFAULTFEATURES = "default_features";
        static const std::string DESCRIPTION = "description";
        static const std::string FEATURE = "features";
        static const std::string MAINTAINERS = "maintainers";
        static const std::string SOURCE = "name";
        static const std::string VERSION = "version";
        static const std::string HOMEPAGE = "homepage";
        static const std::string SUPPORTS = "supports";
    }

    static Span<const std::string> get_list_of_valid_fields()
    {
        static const std::string valid_fields[] = {
            SourceParagraphFields::SOURCE,
            SourceParagraphFields::VERSION,
            SourceParagraphFields::DESCRIPTION,
            SourceParagraphFields::MAINTAINERS,
            SourceParagraphFields::BUILD_DEPENDS,
            SourceParagraphFields::HOMEPAGE,
            SourceParagraphFields::TYPE,
            SourceParagraphFields::SUPPORTS,
        };

        return valid_fields;
    }

    static Span<const StringView> get_list_of_manifest_fields()
    {
        static const StringView valid_fields[] = {
            ManifestFields::SOURCE,
            ManifestFields::VERSION,
            ManifestFields::DESCRIPTION,
            ManifestFields::MAINTAINERS,
            ManifestFields::BUILD_DEPENDS,
            ManifestFields::HOMEPAGE,
            ManifestFields::SUPPORTS,
        };

        return valid_fields;
    }

    void print_error_message(Span<const std::unique_ptr<Parse::ParseControlErrorInfo>> error_info_list)
    {
        Checks::check_exit(VCPKG_LINE_INFO, error_info_list.size() > 0);

        for (auto&& error_info : error_info_list)
        {
            Checks::check_exit(VCPKG_LINE_INFO, error_info != nullptr);
            if (!error_info->error.empty())
            {
                System::print2(
                    System::Color::error, "Error: while loading ", error_info->name, ":\n", error_info->error, '\n');
            }
        }

        bool have_remaining_fields = false;
        for (auto&& error_info : error_info_list)
        {
            if (!error_info->extra_fields.empty())
            {
                System::print2(System::Color::error,
                               "Error: There are invalid fields in the control file of ",
                               error_info->name,
                               '\n');
                System::print2("The following fields were not expected:\n\n    ",
                               Strings::join("\n    ", error_info->extra_fields),
                               "\n\n");
                have_remaining_fields = true;
            }
        }

        if (have_remaining_fields)
        {
            System::print2("This is the list of valid fields (case-sensitive): \n\n    ",
                           Strings::join("\n    ", get_list_of_valid_fields()),
                           "\n\n");
            System::print2("Different source may be available for vcpkg. Use .\\bootstrap-vcpkg.bat to update.\n\n");
        }

        for (auto&& error_info : error_info_list)
        {
            if (!error_info->missing_fields.empty())
            {
                System::print2(System::Color::error,
                               "Error: There are missing fields in the control file of ",
                               error_info->name,
                               '\n');
                System::print2("The following fields were missing:\n\n    ",
                               Strings::join("\n    ", error_info->missing_fields),
                               "\n\n");
            }
        }
    }

    std::string Type::to_string(const Type& t)
    {
        switch (t.type)
        {
            case Type::ALIAS: return "Alias";
            case Type::PORT: return "Port";
            default: return "Unknown";
        }
    }

    Type Type::from_string(const std::string& t)
    {
        if (t == "Alias") return Type{Type::ALIAS};
        if (t == "Port" || t == "") return Type{Type::PORT};
        return Type{Type::UNKNOWN};
    }

    static ParseExpected<SourceParagraph> parse_source_paragraph(const fs::path& path_to_control, Paragraph&& fields)
    {
        auto origin = path_to_control.u8string();

        ParagraphParser parser(std::move(fields));

        auto spgh = std::make_unique<SourceParagraph>();

        parser.required_field(SourceParagraphFields::SOURCE, spgh->name);
        parser.required_field(SourceParagraphFields::VERSION, spgh->version);

        spgh->description = parser.optional_field(SourceParagraphFields::DESCRIPTION);

        spgh->maintainers = Strings::split(parser.optional_field(SourceParagraphFields::MAINTAINERS), "\n");
        for (auto& maintainer : spgh->maintainers) {
            maintainer = Strings::trim(std::move(maintainer));
        }

        spgh->homepage = parser.optional_field(SourceParagraphFields::HOMEPAGE);
        TextRowCol textrowcol;
        std::string buf;
        parser.optional_field(SourceParagraphFields::BUILD_DEPENDS, {buf, textrowcol});
        spgh->depends = parse_dependencies_list(buf, origin, textrowcol).value_or_exit(VCPKG_LINE_INFO);
        buf.clear();
        parser.optional_field(SourceParagraphFields::DEFAULTFEATURES, {buf, textrowcol});
        spgh->default_features = parse_default_features_list(buf, origin, textrowcol).value_or_exit(VCPKG_LINE_INFO);
        spgh->supports_expression = parser.optional_field(SourceParagraphFields::SUPPORTS);
        spgh->type = Type::from_string(parser.optional_field(SourceParagraphFields::TYPE));
        auto err = parser.error_info(spgh->name.empty() ? origin : spgh->name);
        if (err)
            return err;
        else
            return spgh;
    }

    static ParseExpected<FeatureParagraph> parse_feature_paragraph(const fs::path& path_to_control, Paragraph&& fields)
    {
        auto origin = path_to_control.u8string();
        ParagraphParser parser(std::move(fields));

        auto fpgh = std::make_unique<FeatureParagraph>();

        parser.required_field(SourceParagraphFields::FEATURE, fpgh->name);
        parser.required_field(SourceParagraphFields::DESCRIPTION, fpgh->description);

        fpgh->depends = parse_dependencies_list(parser.optional_field(SourceParagraphFields::BUILD_DEPENDS), origin)
                            .value_or_exit(VCPKG_LINE_INFO);

        auto err = parser.error_info(fpgh->name.empty() ? origin : fpgh->name);
        if (err)
            return err;
        else
            return fpgh;
    }

    ParseExpected<SourceControlFile> SourceControlFile::parse_control_file(
        const fs::path& path_to_control, std::vector<Parse::Paragraph>&& control_paragraphs)
    {
        if (control_paragraphs.size() == 0)
        {
            auto ret = std::make_unique<Parse::ParseControlErrorInfo>();
            ret->name = path_to_control.u8string();
            return ret;
        }

        auto control_file = std::make_unique<SourceControlFile>();

        auto maybe_source = parse_source_paragraph(path_to_control, std::move(control_paragraphs.front()));
        if (const auto source = maybe_source.get())
            control_file->core_paragraph = std::move(*source);
        else
            return std::move(maybe_source).error();

        control_paragraphs.erase(control_paragraphs.begin());

        for (auto&& feature_pgh : control_paragraphs)
        {
            auto maybe_feature = parse_feature_paragraph(path_to_control, std::move(feature_pgh));
            if (const auto feature = maybe_feature.get())
                control_file->feature_paragraphs.emplace_back(std::move(*feature));
            else
                return std::move(maybe_feature).error();
        }

        return control_file;
    }


    static std::vector<std::string> invalid_json_fields(
        const Json::Object& obj, Span<const StringView> known_fields) noexcept
    {
        const auto field_is_unknown = [known_fields](StringView sv) {
            // allow directives
            if (sv.size() != 0 && *sv.begin() == '$') {
                return false;
            }
            return std::find(known_fields.begin(), known_fields.end(), sv) == known_fields.end();
        };

        std::vector<std::string> res;
        for (const auto& kv : obj) {
            if (field_is_unknown(kv.first)) {
                res.push_back(kv.first.to_string());
            }
        }

        return res;
    }


    Parse::ParseExpected<SourceControlFile> SourceControlFile::parse_manifest_file(
        const fs::path& /*path_to_manifest*/, const Json::Object& manifest)
    {
        const auto invalid_fields = invalid_json_fields(manifest, get_list_of_manifest_fields());
        if (!invalid_fields.empty()) {
            vcpkg::Checks::exit_fail(VCPKG_LINE_INFO);
        }

        auto control_file = std::make_unique<SourceControlFile>();

        auto source_paragraph = std::make_unique<SourceParagraph>();
        auto name = manifest.get(ManifestFields::SOURCE);
        if (!name || !name->is_string()) {
            vcpkg::Checks::exit_fail(VCPKG_LINE_INFO);
        } else {
            source_paragraph->name = name->string().to_string();
        }

        auto version = manifest.get(ManifestFields::VERSION);
        if (!version || !version->is_string()) {
            vcpkg::Checks::exit_fail(VCPKG_LINE_INFO);
        } else {
            source_paragraph->version = version->string().to_string();
        }

        if (auto description = manifest.get(ManifestFields::DESCRIPTION)) {
            if (description->is_string()) {
                source_paragraph->description = description->string().to_string();
            } else if (description->is_array()) {
                for (auto&& el : description->array()) {
                    if (el.is_string()) {
                        const auto str = el.string();
                        source_paragraph->description.append(str.begin(), str.end());
                        source_paragraph->description.append("  \n");
                    } else {
                        vcpkg::Checks::exit_fail(VCPKG_LINE_INFO);
                    }
                }
            } else {
                vcpkg::Checks::exit_fail(VCPKG_LINE_INFO);
            }
        }

        if (auto maintainers = manifest.get(ManifestFields::MAINTAINERS)) {
            if (maintainers->is_array()) {
                for (auto&& el : maintainers->array()) {
                    if (el.is_string()) {
                        source_paragraph->maintainers.push_back(el.string().to_string());
                    } else {
                        vcpkg::Checks::exit_fail(VCPKG_LINE_INFO);
                    }
                }
            } else {
                vcpkg::Checks::exit_fail(VCPKG_LINE_INFO);
            }
        }

        if (auto homepage = manifest.get(ManifestFields::HOMEPAGE)) {
            if (homepage->is_string()) {
                source_paragraph->homepage = homepage->string().to_string();
            } else {
                vcpkg::Checks::exit_fail(VCPKG_LINE_INFO);
            }
        }

        if (auto deps = manifest.get(ManifestFields::BUILD_DEPENDS)) {
            if (!deps->is_array()) {
                vcpkg::Checks::exit_fail(VCPKG_LINE_INFO);
            }

            for (auto const& dep: deps->array()) {
                if (!dep.is_string()) {
                    vcpkg::Checks::exit_fail(VCPKG_LINE_INFO);
                }
                vcpkg::Dependency real_dep;
                real_dep.depend.name = dep.string().to_string();
                source_paragraph->depends.push_back(real_dep);
            }
        }

        control_file->core_paragraph = std::move(source_paragraph);
        return control_file;
    }



    Optional<const FeatureParagraph&> SourceControlFile::find_feature(const std::string& featurename) const
    {
        auto it = Util::find_if(feature_paragraphs,
                                [&](const std::unique_ptr<FeatureParagraph>& p) { return p->name == featurename; });
        if (it != feature_paragraphs.end())
            return **it;
        else
            return nullopt;
    }
    Optional<const std::vector<Dependency>&> SourceControlFile::find_dependencies_for_feature(
        const std::string& featurename) const
    {
        if (featurename == "core")
        {
            return core_paragraph->depends;
        }
        else if (auto p_feature = find_feature(featurename).get())
            return p_feature->depends;
        else
            return nullopt;
    }

    std::vector<FullPackageSpec> filter_dependencies(const std::vector<vcpkg::Dependency>& deps,
                                                     Triplet t,
                                                     const std::unordered_map<std::string, std::string>& cmake_vars)
    {
        std::vector<FullPackageSpec> ret;
        for (auto&& dep : deps)
        {
            const auto& qualifier = dep.qualifier;
            if (qualifier.empty() ||
                evaluate_expression(qualifier, {cmake_vars, t.canonical_name()}).value_or_exit(VCPKG_LINE_INFO))
            {
                ret.emplace_back(FullPackageSpec({dep.depend.name, t}, dep.depend.features));
            }
        }
        return ret;
    }
}
