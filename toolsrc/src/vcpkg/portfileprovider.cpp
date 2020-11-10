#include <vcpkg/base/system.debug.h>
#include <vcpkg/base/system.process.h>

#include <vcpkg/configuration.h>
#include <vcpkg/paragraphs.h>
#include <vcpkg/portfileprovider.h>
#include <vcpkg/registries.h>
#include <vcpkg/sourceparagraph.h>
#include <vcpkg/tools.h>
#include <vcpkg/vcpkgcmdarguments.h>
#include <vcpkg/vcpkgpaths.h>

using namespace vcpkg;

namespace
{
    const System::ExitCodeAndOutput run_git_command(const VcpkgPaths& paths,
                                                    const fs::path& dot_git_directory,
                                                    const fs::path& working_directory,
                                                    const std::string& cmd)
    {
        const fs::path& git_exe = paths.get_tool_exe(Tools::GIT);

        System::CmdLineBuilder builder;
        builder.path_arg(git_exe)
            .string_arg(Strings::concat("--git-dir=", fs::u8string(dot_git_directory)))
            .string_arg(Strings::concat("--work-tree=", fs::u8string(working_directory)));
        const std::string full_cmd = Strings::concat(builder.extract(), " ", cmd);

        const auto output = System::cmd_execute_and_capture_output(full_cmd);
        return output;
    }

    fs::path fetch_port(const VcpkgPaths& paths, const std::string& port_name, const std::string& git_tree)
    {
        (void)port_name;
        const auto local_repo = paths.root;
        const auto dot_git_dir = paths.buildtrees / "versioning_tmp" / ".git";
        const auto working_dir = paths.buildtrees / "versioning_tmp" / "versions" / git_tree;

        auto& fs = paths.get_filesystem();

        if (!fs.exists(working_dir / "CONTROL") && !fs.exists(working_dir / "vcpkg.json"))
        {
            if (fs.exists(working_dir))
            {
                fs.remove_all(working_dir, VCPKG_LINE_INFO);
            }

            if (fs.exists(dot_git_dir))
            {
                fs.remove_all(dot_git_dir, VCPKG_LINE_INFO);
            }

            // git --git-dir={dot_git_dir} --work-tree={working_dir} clone --no-checkout --local {vcpkg_root}
            // {dot_git_dir} {working_dir}
            System::CmdLineBuilder clone_cmd_builder;
            clone_cmd_builder.string_arg("clone")
                .string_arg("--no-checkout")
                .string_arg("--local")
                .path_arg(local_repo)
                .path_arg(dot_git_dir);
            const auto output = run_git_command(paths, dot_git_dir, working_dir, clone_cmd_builder.extract());
            Checks::check_exit(VCPKG_LINE_INFO, output.exit_code == 0, "Failed to clone temporary vcpkg instance");

            // git checkout {tree_object} .
            System::CmdLineBuilder checkout_cmd_builder;
            checkout_cmd_builder.string_arg("checkout").string_arg(git_tree).string_arg(".");
            const auto git_cmd = checkout_cmd_builder.extract();
            const auto checkout_output = run_git_command(paths, dot_git_dir, working_dir, git_cmd);
            Checks::check_exit(
                VCPKG_LINE_INFO, checkout_output.exit_code == 0, "Failed to checkout %s:%s", port_name, git_tree);
        }
        return working_dir;
    }

    vcpkg::Versions::VersionSpec extract_version_spec(const std::string& package_spec, const Json::Object& version_obj)
    {
        static const std::map<vcpkg::Versions::Scheme, std::string> version_schemes{
            {Versions::Scheme::Relaxed, "version"},
            {Versions::Scheme::Semver, "version-semver"},
            {Versions::Scheme::Date, "version-date"},
            {Versions::Scheme::String, "version-string"}};

        const auto port_version = static_cast<int>(version_obj.get("port-version")->integer());

        for (auto&& kv_pair : version_schemes)
        {
            if (const auto version_string = version_obj.get(kv_pair.second))
            {
                return Versions::VersionSpec(
                    package_spec, version_string->string().to_string(), port_version, kv_pair.first);
            }
        }

        Checks::unreachable(VCPKG_LINE_INFO);
    }

    Optional<fs::path> get_versions_json_path(const VcpkgPaths& paths, const std::string& port_name)
    {
        // TODO: Get correct `port_versions` path for the registry the port belongs to, pseudocode below:
        // auto registry = paths.get_registry_for_port(port_name);
        // auto port_versions_dir_path = registry.get_port_versions_path();
        const auto port_versions_dir_path = paths.root / "port_versions";
        const auto subpath = Strings::concat(port_name[0], "-/", port_name, ".json");
        const auto json_path = port_versions_dir_path / subpath;
        if (paths.get_filesystem().exists(json_path))
        {
            return json_path;
        }
        return nullopt;
    }
}

namespace vcpkg::PortFileProvider
{
    MapPortFileProvider::MapPortFileProvider(const std::unordered_map<std::string, SourceControlFileLocation>& map)
        : ports(map)
    {
    }

    ExpectedS<const SourceControlFileLocation&> MapPortFileProvider::get_control_file(const std::string& spec) const
    {
        auto scf = ports.find(spec);
        if (scf == ports.end()) return std::string("does not exist in map");
        return scf->second;
    }

    std::vector<const SourceControlFileLocation*> MapPortFileProvider::load_all_control_files() const
    {
        return Util::fmap(ports, [](auto&& kvpair) -> const SourceControlFileLocation* { return &kvpair.second; });
    }

    PathsPortFileProvider::PathsPortFileProvider(const vcpkg::VcpkgPaths& paths_,
                                                 const std::vector<std::string>& overlay_ports_)
        : paths(paths_)
    {
        auto& fs = paths.get_filesystem();
        for (auto&& overlay_path : overlay_ports_)
        {
            if (!overlay_path.empty())
            {
                auto overlay = fs::u8path(overlay_path);
                if (overlay.is_absolute())
                {
                    overlay = fs.canonical(VCPKG_LINE_INFO, overlay);
                }
                else
                {
                    overlay = fs.canonical(VCPKG_LINE_INFO, paths.original_cwd / overlay);
                }

                Debug::print("Using overlay: ", fs::u8string(overlay), "\n");

                Checks::check_exit(
                    VCPKG_LINE_INFO, fs.exists(overlay), "Error: Path \"%s\" does not exist", fs::u8string(overlay));

                Checks::check_exit(VCPKG_LINE_INFO,
                                   fs::is_directory(fs.status(VCPKG_LINE_INFO, overlay)),
                                   "Error: Path \"%s\" must be a directory",
                                   overlay.string());

                overlay_ports.emplace_back(overlay);
            }
        }
    }

    static Optional<SourceControlFileLocation> try_load_overlay_port(const Files::Filesystem& fs,
                                                                     View<fs::path> overlay_ports,
                                                                     const std::string& spec)
    {
        for (auto&& ports_dir : overlay_ports)
        {
            // Try loading individual port
            if (Paragraphs::is_port_directory(fs, ports_dir))
            {
                auto maybe_scf = Paragraphs::try_load_port(fs, ports_dir);
                if (auto scf = maybe_scf.get())
                {
                    if (scf->get()->core_paragraph->name == spec)
                    {
                        return SourceControlFileLocation{std::move(*scf), ports_dir};
                    }
                }
                else
                {
                    vcpkg::print_error_message(maybe_scf.error());
                    Checks::exit_with_message(
                        VCPKG_LINE_INFO, "Error: Failed to load port %s from %s", spec, fs::u8string(ports_dir));
                }

                continue;
            }

            auto ports_spec = ports_dir / fs::u8path(spec);
            if (Paragraphs::is_port_directory(fs, ports_spec))
            {
                auto found_scf = Paragraphs::try_load_port(fs, ports_spec);
                if (auto scf = found_scf.get())
                {
                    if (scf->get()->core_paragraph->name == spec)
                    {
                        return SourceControlFileLocation{std::move(*scf), std::move(ports_spec)};
                    }
                    Checks::exit_with_message(VCPKG_LINE_INFO,
                                              "Error: Failed to load port from %s: names did not match: '%s' != '%s'",
                                              fs::u8string(ports_spec),
                                              spec,
                                              scf->get()->core_paragraph->name);
                }
                else
                {
                    vcpkg::print_error_message(found_scf.error());
                    Checks::exit_with_message(
                        VCPKG_LINE_INFO, "Error: Failed to load port %s from %s", spec, fs::u8string(ports_dir));
                }
            }
        }
        return nullopt;
    }

    static Optional<SourceControlFileLocation> try_load_registry_port(const VcpkgPaths& paths, const std::string& spec)
    {
        const auto& fs = paths.get_filesystem();
        if (auto registry = paths.get_configuration().registry_set.registry_for_port(spec))
        {
            auto registry_root = registry->get_registry_root(paths);
            auto port_directory = registry_root / fs::u8path(spec);

            if (fs.exists(port_directory))
            {
                auto found_scf = Paragraphs::try_load_port(fs, port_directory);
                if (auto scf = found_scf.get())
                {
                    if (scf->get()->core_paragraph->name == spec)
                    {
                        return SourceControlFileLocation{std::move(*scf), std::move(port_directory)};
                    }
                    Checks::exit_with_message(VCPKG_LINE_INFO,
                                              "Error: Failed to load port from %s: names did not match: '%s' != '%s'",
                                              fs::u8string(port_directory),
                                              spec,
                                              scf->get()->core_paragraph->name);
                }
                else
                {
                    vcpkg::print_error_message(found_scf.error());
                    Checks::exit_with_message(
                        VCPKG_LINE_INFO, "Error: Failed to load port %s from %s", spec, fs::u8string(port_directory));
                }
            }
        }
        return nullopt;
    }

    ExpectedS<const SourceControlFileLocation&> PathsPortFileProvider::get_control_file(const std::string& spec) const
    {
        auto cache_it = cache.find(spec);
        if (cache_it == cache.end())
        {
            const auto& fs = paths.get_filesystem();
            auto maybe_port = try_load_overlay_port(fs, overlay_ports, spec);
            if (!maybe_port)
            {
                maybe_port = try_load_registry_port(paths, spec);
            }
            if (auto p = maybe_port.get())
            {
                cache_it = cache.emplace(spec, std::move(*p)).first;
            }
        }

        if (cache_it == cache.end())
        {
            return std::string("Port definition not found");
        }
        else
        {
            if (!paths.get_feature_flags().versions)
            {
                if (cache_it->second.source_control_file->core_paragraph->version_scheme != Versions::Scheme::String)
                {
                    return Strings::concat(
                        "Port definition rejected because the `",
                        VcpkgCmdArguments::VERSIONS_FEATURE,
                        "` feature flag is disabled.\nThis can be fixed by using the \"version-string\" field.");
                }
            }
            return cache_it->second;
        }
    }

    std::vector<const SourceControlFileLocation*> PathsPortFileProvider::load_all_control_files() const
    {
        // Reload cache with ports contained in all ports_dirs
        cache.clear();
        std::vector<const SourceControlFileLocation*> ret;

        for (const fs::path& ports_dir : overlay_ports)
        {
            // Try loading individual port
            if (Paragraphs::is_port_directory(paths.get_filesystem(), ports_dir))
            {
                auto maybe_scf = Paragraphs::try_load_port(paths.get_filesystem(), ports_dir);
                if (auto scf = maybe_scf.get())
                {
                    auto port_name = scf->get()->core_paragraph->name;
                    if (cache.find(port_name) == cache.end())
                    {
                        auto scfl = SourceControlFileLocation{std::move(*scf), ports_dir};
                        auto it = cache.emplace(std::move(port_name), std::move(scfl));
                        ret.emplace_back(&it.first->second);
                    }
                }
                else
                {
                    vcpkg::print_error_message(maybe_scf.error());
                    Checks::exit_with_message(
                        VCPKG_LINE_INFO, "Error: Failed to load port from %s", fs::u8string(ports_dir));
                }
                continue;
            }

            // Try loading all ports inside ports_dir
            auto found_scfls = Paragraphs::load_overlay_ports(paths, ports_dir);
            for (auto&& scfl : found_scfls)
            {
                auto port_name = scfl.source_control_file->core_paragraph->name;
                if (cache.find(port_name) == cache.end())
                {
                    auto it = cache.emplace(std::move(port_name), std::move(scfl));
                    ret.emplace_back(&it.first->second);
                }
            }
        }

        auto all_ports = Paragraphs::load_all_registry_ports(paths);
        for (auto&& scfl : all_ports)
        {
            auto port_name = scfl.source_control_file->core_paragraph->name;
            if (cache.find(port_name) == cache.end())
            {
                auto it = cache.emplace(port_name, std::move(scfl));
                ret.emplace_back(&it.first->second);
            }
        }

        return ret;
    }

    VersionedPortfileProvider::VersionedPortfileProvider(const vcpkg::VcpkgPaths& paths) : paths(paths) { }

    const std::vector<vcpkg::Versions::VersionSpec>& VersionedPortfileProvider::get_port_versions(
        const std::string& package_spec) const
    {
        auto cache_it = versions_cache.find(package_spec);
        if (cache_it != versions_cache.end())
        {
            return cache_it->second;
        }

        auto maybe_versions_json_path = get_versions_json_path(paths, package_spec);
        if (!maybe_versions_json_path.has_value())
        {
            // TODO: Handle db version not existing
            Checks::exit_fail(VCPKG_LINE_INFO);
        }
        auto versions_json_path = maybe_versions_json_path.value_or_exit(VCPKG_LINE_INFO);

        auto versions_json = Json::parse_file(VCPKG_LINE_INFO, paths.get_filesystem(), versions_json_path);

        // NOTE: A dictionary would be the best way to store this, for now we use a vector
        if (versions_json.first.is_object())
        {
            auto versions_object = std::move(versions_json.first.object());
            auto maybe_versions_array = versions_object.get("versions");

            if (maybe_versions_array && maybe_versions_array->is_array())
            {
                auto versions_array = std::move(maybe_versions_array->array());
                for (auto&& version : versions_array)
                {
                    auto version_obj = std::move(version.object());
                    Versions::VersionSpec spec = extract_version_spec(package_spec, version_obj);

                    auto& package_versions = versions_cache[spec.package_spec];
                    package_versions.push_back(spec);

                    auto&& git_tree = version_obj.get("git-tree")->string().to_string();
                    git_tree_cache.emplace(std::move(spec), git_tree);
                }
            }
        }

        return versions_cache.at(package_spec);
    }

    ExpectedS<const SourceControlFileLocation&> VersionedPortfileProvider::get_control_file(
        const vcpkg::Versions::VersionSpec& version_spec) const
    {
        auto cache_it = control_cache.find(version_spec);
        if (cache_it != control_cache.end())
        {
            return cache_it->second;
        }

        // 1. Load database for port
        auto git_tree_cache_it = git_tree_cache.find(version_spec);
        if (git_tree_cache_it == git_tree_cache.end())
        {
            // TODO: Try to load port from database
            Checks::exit_fail(VCPKG_LINE_INFO);
        }

        const std::string git_tree = git_tree_cache_it->second;
        const auto port_directory = fetch_port(paths, version_spec.package_spec, git_tree);

        auto maybe_control_file = Paragraphs::try_load_port(paths.get_filesystem(), port_directory);
        if (auto scf = maybe_control_file.get())
        {
            if (scf->get()->core_paragraph->name == version_spec.package_spec)
            {
                return SourceControlFileLocation{std::move(*scf), std::move(port_directory)};
            }
            Checks::exit_with_message(VCPKG_LINE_INFO,
                                      "Error: Failed to load port from %s: names did not match: '%s' != '%s'",
                                      fs::u8string(port_directory),
                                      version_spec.package_spec,
                                      scf->get()->core_paragraph->name);
        }

        vcpkg::print_error_message(maybe_control_file.error());
        Checks::exit_with_message(VCPKG_LINE_INFO,
                                  "Error: Failed to load port %s from %s",
                                  version_spec.package_spec,
                                  fs::u8string(port_directory));
    }

    // BaselineProvider::BaselineProvider(const VcpkgPaths& paths) : paths(paths) { }

    // Versions::VersionSpec BaselineProvider::get_baseline_version(const std::string& baseline,
    //                                                              const std::string& port_name) const
    // {
    //     const auto key = Strings::concat(port_name, "@", baseline);
    //     auto it = baseline_versions.find(key);
    //     if (it != baseline_versions.end())
    //     {
    //         return it->second;
    //     }

    //     // Search in DB
    //     auto fetch_baseline_file = [](const std::string& baseline) -> fs::path { paths.checkout_file() };
    // }
}
