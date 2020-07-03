#pragma once

#include <vcpkg/vcpkgpaths.h>

namespace vcpkg::Export
{
    extern const CommandStructure COMMAND_STRUCTURE;

    void perform_and_exit(const VcpkgCmdArguments& args, const VcpkgPaths& paths, Triplet default_triplet);

    void export_integration_files(const fs::path& raw_exported_dir_path, const VcpkgPaths& paths);

    std::string generate_export_nuspec(const std::string& raw_exported_dir,
                                       const fs::path& targets_redirect_path,
                                       const fs::path& props_redirect_path,
                                       const std::string& nuget_id,
                                       const std::string& nupkg_version);
}
