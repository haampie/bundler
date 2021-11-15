#if defined(LIBTREE_HAS_AUXV_HEADER)
#include <sys/auxv.h>
#endif

#include <cxxopts.hpp>
#include <termcolor/termcolor.hpp>

#include <libtree/excludelist.hpp>
#include <libtree/elf.hpp>
#include <libtree/ld.hpp>
#include <libtree/deploy.hpp>
#include <libtree/deps.hpp>

#include "libtree_version.hpp"

namespace fs = std::filesystem;

int main(int argc, char ** argv) {
    cxxopts::Options options("libtree", "Show the dependency tree of binaries and optionally bundle them into a single folder.");

#if defined(LIBTREE_HAS_AUXV_HEADER)
    auto default_platform = reinterpret_cast<char const *>(getauxval(AT_PLATFORM));
#else
    // Default to x86_64 substitution for PLATFORM if getauxval is not available.
    auto default_platform = "x86_64";
#endif

    // Use the strip and chrpath that we ship if we can detect them
    std::string strip = "strip";
    std::string chrpath = "chrpath";

    options.positional_help("binary [more binaries...]");

    options.add_options("A. Locating libs")
      ("p,path", "Show the path of libraries instead of their SONAME", cxxopts::value<bool>()->default_value("false"))
      ("v,verbose", "Show the skipped libraries without their children", cxxopts::value<bool>()->default_value("false"))
      ("a,all", "Show the skipped libraries and their children", cxxopts::value<bool>()->default_value("false"))
      ("l,ldconf", "Path to custom ld.conf to test settings", cxxopts::value<std::string>()->default_value("/etc/ld.so.conf"))
      ("s,skip", "Skip library and its dependencies from being deployed or inspected", cxxopts::value<std::vector<std::string>>())
      ("platform", "Platform used for interpolation in rpaths", cxxopts::value<std::string>()->default_value(default_platform))
      ("r,root", "Use this ROOT filesystem tree", cxxopts::value<std::string>()->default_value("/"))
      ("b,binary", "Binary to inspect", cxxopts::value<std::vector<std::string>>());

    options.add_options("B. Copying libs")
      ("d,destination", "OPTIONAL: When a destination is set to a folder, all binaries and their dependencies are copied over", cxxopts::value<std::string>())
      ("strip", "Call strip on binaries when deploying", cxxopts::value<bool>()->default_value("false"))
      ("chrpath", "Call chrpath on binaries when deploying", cxxopts::value<bool>()->default_value("false"));
    
    options.add_options()
        ("h,help", "Print usage")
        ("version", "Print version info");

    options.parse_positional("binary");

    auto result = options.parse(argc, argv);

    if (result.count("version") != 0) {
        std::cout << s_libtree_version << '\n';
        return 0;
    }

    auto platform = result["platform"].as<std::string>();
    auto root = fs::canonical(result["root"].as<std::string>());

    std::vector<Elf> pool;

    if (result["binary"].count()) {
        for (auto const &binary : result["binary"].as<std::vector<std::string>>()) {
            auto binary_path = fs::path(binary);

            // If we have a relative path and custom root, what should we do?
            // For now error.
            if (binary_path.is_relative() && root != fs::path{"/"})
                throw std::runtime_error("When using a custom root, provide absolute paths to binaries.");
            
            // Otherwise make absolute.
            auto abs_path = fs::absolute(binary_path);

            // Notice: is_lib doesn't use ELF file type checks, cause PIE executables
            // are often shown as shared objects -- so we make an educated guess based
            // on the filename: libxyz.so is a lib... 
            auto type = is_lib(abs_path) ? deploy_t::LIBRARY : deploy_t::EXECUTABLE;
            auto val = from_path(type, found_t::NONE, root, abs_path, platform);
            if (val != std::nullopt)
                pool.push_back(*val);
        }
    }

    if (result.count("help") || pool.size() == 0) {
      std::cout << options.help() << std::endl;
      return 0;
    }

    if (result["skip"].count()) {
        auto const &list = result["skip"].as<std::vector<std::string>>();
        for (auto const &lib : list)
            generatedExcludelist.insert(lib);
    }

    // Fill ld library path from the env variable
    std::vector<fs::path> ld_library_paths;
    auto env = std::getenv("LD_LIBRARY_PATH");
    if (env != nullptr)
        for (auto const &path : split_paths(std::string(env)))
            ld_library_paths.push_back(path);

    // Default search paths is ldconfig + /lib + /usr/lib
    auto ld_conf = parse_ld_conf(root, result["ldconf"].as<std::string>());

    // Walk the dependency tree
    bool print_paths = result.count("path") > 0;
    deps::verbosity_t verbosity = result.count("all") ? deps::verbosity_t::VERY_VERBOSE
                                                      : result.count("v") ? deps::verbosity_t::VERBOSE
                                                                         : deps::verbosity_t::NONE;

    deps tree{
        root,
        std::move(pool),
        std::move(ld_conf),
        std::move(ld_library_paths),
        std::move(generatedExcludelist),
        platform,
        verbosity,
        print_paths
    };

    std::cout << '\n';

    // And deploy the binaries if requested.
    if (result.count("destination") == 1) {
        fs::path usr_dir = fs::path(result["destination"].as<std::string>()) / "usr";
        fs::path bin_dir = usr_dir / "bin";
        fs::path lib_dir = usr_dir / "lib";

        std::cout << termcolor::bold << "Deploying to " << usr_dir << termcolor::reset << '\n';

        fs::create_directories(bin_dir);
        fs::create_directories(lib_dir);

        deploy(root, tree.get_deps(), bin_dir, lib_dir, chrpath, strip, result["chrpath"].as<bool>(), result["strip"].as<bool>());
    }

    return !tree.success();
}
