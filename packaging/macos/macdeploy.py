#!/usr/bin/env python3
"""Produce a self-contained, launchable GitTide.app on macOS.

Driven by the ``deploy_macos`` CMake target. Three passes on the built bundle:

1. **macdeployqt** — copy the Qt frameworks and QML plugins into the bundle.
   ``-qmldir`` points at the QML *sources* because GitTide's own QML is compiled
   into Qt resources (``ui/qml/qml.qrc``) and so is not scannable on disk; the
   source tree is what tells macdeployqt which Qt QML modules to pull in.
2. **De-Homebrew** — macdeployqt (at least with a Homebrew Qt) leaves the
   transitively pulled-in *qtdeclarative* frameworks with absolute install names
   and references (``/opt/homebrew/opt/qtdeclarative/lib/...``). Left alone the
   bundle loads the *system* Qt and crashes on any Mac without that exact
   Homebrew install — defeating the point of bundling. We rewrite every absolute
   Qt reference to ``@rpath/...`` and make sure the executable has an
   ``@executable_path/../Frameworks`` rpath (and no rpath pointing outside the
   bundle).
3. **Ad-hoc codesign** — rewriting Mach-O load commands invalidates the
   signatures Homebrew shipped; Apple Silicon SIGKILLs a process the moment it
   maps a page whose signature does not match ("Code Signature Invalid"). Every
   Mach-O in the bundle is re-signed ad-hoc, deepest path first, the frameworks
   and the app bundle last.

Usage: macdeploy.py --app GitTide.app --macdeployqt <path> --qmldir <ui/qml>
"""
import argparse
import os
import subprocess
import sys

# Prefixes that mean "outside the bundle" — a dependency here must be rewritten.
EXTERNAL_PREFIXES = ("/opt/homebrew/", "/usr/local/", "/opt/local/")


def run(cmd, **kw):
    return subprocess.run(cmd, check=True, capture_output=True, text=True, **kw)


def is_macho(path):
    if os.path.islink(path) or not os.path.isfile(path):
        return False
    try:
        return "Mach-O" in run(["file", "-b", path]).stdout
    except subprocess.CalledProcessError:
        return False


def rpath_target(ref):
    """Map an absolute external reference to its in-bundle @rpath form.

    ``/opt/homebrew/opt/qtdeclarative/lib/QtLabs.framework/Versions/A/QtLabs``
        -> ``@rpath/QtLabs.framework/Versions/A/QtLabs``
    ``/opt/homebrew/opt/qtbase/lib/libfoo.1.dylib`` -> ``@rpath/libfoo.1.dylib``
    """
    idx = ref.find(".framework/")
    if idx != -1:
        start = ref.rfind("/", 0, idx) + 1
        return "@rpath/" + ref[start:]
    return "@rpath/" + os.path.basename(ref)


def macho_files(app):
    for root, _dirs, files in os.walk(app):
        for name in files:
            p = os.path.join(root, name)
            if is_macho(p):
                yield p


def de_homebrew(app):
    """Rewrite absolute external Qt install-names and references to @rpath."""
    for f in macho_files(app):
        # Own install-name (id).
        own = run(["otool", "-D", f]).stdout.splitlines()
        if len(own) > 1 and own[1].startswith(EXTERNAL_PREFIXES):
            run(["install_name_tool", "-id", rpath_target(own[1]), f])
        # Dependent references.
        for line in run(["otool", "-L", f]).stdout.splitlines()[1:]:
            dep = line.strip().split(" (")[0]
            if dep.startswith(EXTERNAL_PREFIXES):
                run(["install_name_tool", "-change", dep, rpath_target(dep), f])


def fix_rpaths(exe):
    """Ensure the executable resolves @rpath inside the bundle, nowhere else."""
    load = run(["otool", "-l", exe]).stdout.splitlines()
    rpaths = []
    for i, line in enumerate(load):
        if "cmd LC_RPATH" in line:
            # path is two lines down: "         path <P> (offset N)"
            rpaths.append(load[i + 2].split("path ", 1)[1].split(" (offset")[0])
    for rp in rpaths:
        if rp.startswith(EXTERNAL_PREFIXES):
            run(["install_name_tool", "-delete_rpath", rp, exe])
    if "@executable_path/../Frameworks" not in rpaths:
        run(["install_name_tool", "-add_rpath", "@executable_path/../Frameworks", exe])


def codesign(app):
    """Ad-hoc sign the whole bundle.

    ``codesign --deep`` recurses into the code locations it knows about
    (Frameworks, PlugIns, the main executable) and signs them bottom-up in the
    right order — but it does *not* descend into ``Contents/Resources``, where
    macdeployqt drops the Qt QML plugin dylibs. So we sign the Resources-tree
    Mach-O files individually first (deepest path first), then ``--deep``-sign
    the app, which seals everything into a consistent whole.
    """
    resources = os.path.join(app, "Contents", "Resources")
    res_machos = sorted((p for p in macho_files(resources) if os.path.isdir(resources)),
                        key=lambda p: p.count("/"), reverse=True)
    for f in res_machos:
        run(["codesign", "--force", "--timestamp=none", "-s", "-", f])
    run(["codesign", "--force", "--deep", "--timestamp=none", "-s", "-", app])
    run(["codesign", "--verify", "--deep", "--strict", app])


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--app", required=True)
    ap.add_argument("--macdeployqt", required=True)
    ap.add_argument("--qmldir", required=True)
    args = ap.parse_args()

    exe = os.path.join(args.app, "Contents", "MacOS",
                       os.path.splitext(os.path.basename(args.app))[0])

    print("[1/3] macdeployqt: bundling Qt frameworks + QML plugins")
    run([args.macdeployqt, args.app, "-qmldir=" + args.qmldir, "-always-overwrite"])
    print("[2/3] rewriting absolute Qt references to @rpath")
    de_homebrew(args.app)
    fix_rpaths(exe)
    print("[3/3] ad-hoc codesigning the bundle")
    codesign(args.app)
    print("Done: " + args.app)


if __name__ == "__main__":
    try:
        main()
    except subprocess.CalledProcessError as e:
        sys.stderr.write("command failed: %s\n%s\n" % (" ".join(e.cmd), e.stderr))
        sys.exit(1)
