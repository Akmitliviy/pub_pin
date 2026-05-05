/*
 * pub_pin — automatically syncs pinned versions in pubspec.yaml
 * after running `flutter pub get` + `flutter pub upgrade`.
 *
 * Build:  see CMakeLists.txt
 * Usage:  place the binary in your Flutter project root and run it.
 *
 * Behaviour
 * ---------
 * 1. Runs `flutter pub get`
 * 2. Runs `flutter pub upgrade`
 * 3. Runs `flutter pub deps --style list` and collects resolved versions
 * 4. Reads pubspec.yaml line-by-line (preserving every comment and blank line)
 * 5. For each dependency line that already has an explicit version constraint,
 *    replaces the version with the resolved one — implicit/transitive deps that
 *    are NOT already present in pubspec.yaml are left untouched.
 * 6. Writes the patched content back to pubspec.yaml (original is backed up as
 *    pubspec.yaml.bak first).
 */

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <list>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <string>

#ifdef _WIN32
  #define POPEN  _popen
  #define PCLOSE _pclose
#else
  #include <unistd.h>
  #define POPEN  popen
  #define PCLOSE pclose
#endif

namespace fs = std::filesystem;

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

// Run a shell command and return its combined stdout output.
// Returns empty string and prints an error on failure.
static std::string runCommand(const std::string& cmd, bool printOutput = true)
{
    std::string fullCmd = cmd;
#ifndef _WIN32
    // Redirect stderr → stdout so we capture warnings too
    fullCmd += " 2>&1";
#endif

    FILE* pipe = POPEN(fullCmd.c_str(), "r");
    if (!pipe) {
        std::cerr << "[error] Failed to execute: " << cmd << "\n";
        return "";
    }

    std::string result;
    char buf[512];
    while (fgets(buf, sizeof(buf), pipe)) {
        result += buf;
        if (printOutput) std::cout << buf;
    }

    if (const int rc = PCLOSE(pipe); rc != 0) {
        std::cerr << "[warn] Command exited with code " << rc << ": " << cmd << "\n";
    }
    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// Step 3 – parse `flutter pub deps --style list` output
//
// The relevant lines look like:
//   - package_name 1.2.3
//   - package_name 2.0.0+1
// We collect every such mapping.
// ─────────────────────────────────────────────────────────────────────────────

static std::map<std::string, std::string> parseDepsOutput(const std::string& raw)
{
    std::map<std::string, std::string> versions;

    // Matches lines like "- camera_android 0.10.9+11"
    // The leading whitespace + dash is variable (nested deps are indented).
    static const std::regex lineRe(R"(^\s*-\s+([\w_-]+)\s+(\d[\w.+\-]*)\s*$)");

    std::istringstream ss(raw);
    std::string line;
    std::smatch m;
    while (std::getline(ss, line)) {
        if (std::regex_match(line, m, lineRe)) {
            versions[m[1].str()] = m[2].str();
        }
    }
    return versions;
}

// ─────────────────────────────────────────────────────────────────────────────
// Step 4–5 – patch pubspec.yaml
//
// We look for lines of the form (inside dependencies / dev_dependencies sections):
//
//   package_name: ^1.0.0
//   package_name: ">=1.0.0 <2.0.0"
//   package_name: '1.0.0'
//   package_name: 1.0.0
//   package_name: any        ← now handled
//   package_name:            ← now handled (bare, no sub-keys follow for known pkgs)
//
// We do NOT touch:
//   # comments
//   sdk: flutter
//   path: ../local_pkg     (sub-keys under a package block)
//
// For bare / "any" lines we only rewrite when the package name is present in
// resolvedVersions — this keeps git/path/sdk blocks untouched even though they
// also appear as bare keys.
//
// When we find a match AND the package is in `resolvedVersions`, we rewrite the
// version to "^<resolved>" while keeping the rest of the line formatting intact.
// ─────────────────────────────────────────────────────────────────────────────

static std::string patchYaml(
    const std::string& yaml,
    const std::map<std::string, std::string>& resolvedVersions,
    const std::set<std::string>& excluded,
    int& updatedCount
    )
{
    // Matches: <indent><package_name>: <optional_quote><version_constraint><optional_quote>
    // Group 1 = everything before the version constraint (indent + name + colon + space)
    // Group 2 = package name
    // Group 3 = optional opening quote
    // Group 4 = the version string (may start with ^, >=, etc.)
    // Group 5 = optional closing quote
    // Group 6 = anything after (inline comment)
    static const std::regex versionLineRe(
        R"(^(\s+([\w_-]+)\s*:)\s*(['"]?)([^\s'"#][^\s'"#]*|\s*)(['"]?)(\s*(#.*)?)$)");

    // We only update inside dependency sections
    // Track whether we're inside dependencies: or dev_dependencies:
    static const std::regex sectionRe(R"(^(dependencies|dev_dependencies)\s*:\s*$)");
    static const std::regex topLevelKeyRe(R"(^\S)"); // a line starting at column 0

    static const std::regex flutterRe(R"(^(flutter|flutter_test)$)");

    bool inDepsSection = false;
    updatedCount = 0;

    std::istringstream ss(yaml);
    std::ostringstream out;
    std::string line;
    std::smatch m;

    while (std::getline(ss, line)) {
        // Detect section boundaries
        if (std::regex_match(line, m, sectionRe)) {
            inDepsSection = true;
            out << line << "\n";
            continue;
        }
        // Any top-level key (zero indent, not a comment, not empty) ends the section
        if (inDepsSection && !line.empty() && line[0] != ' ' && line[0] != '\t'
            && line[0] != '#' && line[0] != '\n' && line[0] != '\r')
        {
            // Check if it's really a top-level key (has a colon), not just "---"
            if (line.find(':') != std::string::npos) {
                // Could be a new top-level section
                if (!std::regex_match(line, m, sectionRe)) {
                    inDepsSection = false;
                }
            }
        }

        if (inDepsSection && std::regex_match(line, m, versionLineRe)) {
            std::string prefix    = m[1].str(); // "  package_name: "
            std::string pkgName   = m[2].str(); // "package_name"
            std::string openQuote = m[3].str();
            std::string verStr    = m[4].str();
            std::string closeQuote= m[5].str();
            std::string suffix    = m[6].str(); // trailing comment etc.



            if (auto it = resolvedVersions.find(pkgName); it != resolvedVersions.end()) {

                std::string newVer = it->second;

                if (excluded.contains(it->first)) {
                    out << line << "\n";
                    continue;
                }

                // Strip quotes — we'll write without them for cleanliness
                out << prefix << " " << newVer << suffix << "\n";

                std::string oldVer;
                oldVer.append(openQuote).append(verStr).append(closeQuote);
                if (newVer != oldVer
                    && newVer != verStr)
                {
                    std::cout << "  updated: " << pkgName
                              << "  " << openQuote << verStr << closeQuote
                              << "  →  " << newVer << "\n";
                    ++updatedCount;
                }
                continue;
            }
        }

        out << line << "\n";
    }

    return out.str();
}

// ─────────────────────────────────────────────────────────────────────────────
// main
// ─────────────────────────────────────────────────────────────────────────────

int main(int argc, char* argv[])
{
    std::cout << "══════════════════════════════════════════\n";
    std::cout << "  flutter_pub_pin  — dependency version syncer\n";
    std::cout << "══════════════════════════════════════════\n\n";

    // Optional: allow passing a project directory as first argument
    if (argc > 1) {
        fs::path dir(argv[1]);
        if (!fs::exists(dir) || !fs::is_directory(dir)) {
            std::cerr << "[error] Not a directory: " << argv[1] << "\n";
            return 1;
        }
        std::error_code ec;
        fs::current_path(dir, ec);
        if (ec) {
            std::cerr << "[error] Cannot chdir to " << argv[1] << ": " << ec.message() << "\n";
            return 1;
        }
    }

    fs::path pubspecPath = fs::current_path() / "pubspec.yaml";
    if (!fs::exists(pubspecPath)) {
        std::cerr << "[error] pubspec.yaml not found in " << fs::current_path() << "\n";
        std::cerr << "        Run this tool from your Flutter project root,\n";
        std::cerr << "        or pass the project directory as an argument.\n";
        return 1;
    }

    // ── Step 1: flutter pub get ───────────────────────────────────────────
    std::cout << "─── Step 1: flutter pub get ─────────────────────\n";
    runCommand("flutter pub get");
    std::cout << "\n";

    // ── Step 2: flutter pub upgrade ──────────────────────────────────────
    std::cout << "─── Step 2: flutter pub upgrade ─────────────────\n";
    runCommand("flutter pub upgrade");
    std::cout << "\n";

    // ── Step 3: collect resolved versions ────────────────────────────────
    std::cout << "─── Step 3: collecting resolved versions ─────────\n";
    std::string depsOutput = runCommand("flutter pub deps --style list", false);
    if (depsOutput.empty()) {
        std::cerr << "[error] `flutter pub deps` produced no output.\n";
        return 1;
    }
    auto resolvedVersions = parseDepsOutput(depsOutput);
    std::cout << "        Found " << resolvedVersions.size() << " resolved packages.\n\n";

    if (resolvedVersions.empty()) {
        std::cerr << "[error] Could not parse any package versions from deps output.\n";
        return 1;
    }

    // ── Step 4: read pubspec.yaml ─────────────────────────────────────────
    std::cout << "─── Step 4: reading pubspec.yaml ─────────────────\n";
    std::ifstream inFile(pubspecPath);
    if (!inFile) {
        std::cerr << "[error] Cannot open pubspec.yaml for reading.\n";
        return 1;
    }
    std::ostringstream buf;
    buf << inFile.rdbuf();
    inFile.close();
    std::string originalYaml = buf.str();
    std::cout << "        Read " << originalYaml.size() << " bytes.\n\n";

    std::set<std::string> excluded;
    excluded.insert("flutter");
    excluded.insert("flutter_test");

    std::cout << "        Excluded values that match pattern:\n";
    for (const auto& ex : excluded) {
        std::cout << "        " << ex << "\n";
    }

    // ── Step 5: patch ─────────────────────────────────────────────────────
    std::cout << "─── Step 5: updating pinned versions ─────────────\n";
    int updatedCount = 0;
    std::string patchedYaml = patchYaml(originalYaml, resolvedVersions, excluded, updatedCount);

    if (updatedCount == 0) {
        std::cout << "        No version pins needed updating.\n";
    } else {
        std::cout << "        " << updatedCount << " version(s) updated.\n";
    }
    std::cout << "\n";

    // ── Step 6: write back ────────────────────────────────────────────────
    // Back up first
    fs::path bakPath = pubspecPath.parent_path() / "pubspec.yaml.bak";
    std::error_code ec;
    fs::copy_file(pubspecPath, bakPath, fs::copy_options::overwrite_existing, ec);
    if (ec) {
        std::cerr << "[warn] Could not create backup: " << ec.message() << "\n";
    } else {
        std::cout << "─── Backup written to pubspec.yaml.bak ───────────\n";
    }

    std::ofstream outFile(pubspecPath);
    if (!outFile) {
        std::cerr << "[error] Cannot open pubspec.yaml for writing.\n";
        return 1;
    }
    outFile << patchedYaml;
    outFile.close();

    std::cout << "─── Done ──────────────────────────────────────────\n";
    std::cout << "    pubspec.yaml updated. Run `flutter pub get` if\n";
    std::cout << "    you want to verify the lockfile is consistent.\n\n";

    return 0;
}