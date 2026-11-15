# Writes SHA256SUMS.upstream: for every file taken from upstream the hash of
# the upstream file, and whether the local file still is that file.
#
#   python instruments/elements/tools/update_provenance.py <eurorack checkout> <stmlib checkout>
#
# The checkouts must be at the commits named in the list's header (stmlib at
# the commit eurorack pins as its submodule). Run it after changing a patched
# file's upstream base, not after changing a patch: the list holds upstream
# hashes only. The test elements.provenance checks the result.
import hashlib
import os
import sys

EURORACK_COMMIT = "f4cfa7697095a28d292bff758beb51deb49b5a86"
STMLIB_COMMIT = "e3bd7c9cc00e4364166f9905c0509b6ffd0535ec"

CORE_DSP = [
    "dsp.h", "exciter.h", "exciter.cc", "multistage_envelope.h", "multistage_envelope.cc",
    "part.h", "part.cc", "patch.h", "resonator.h", "resonator.cc", "tube.h", "tube.cc",
    "voice.h", "voice.cc", "fx/fx_engine.h", "fx/reverb.h", "fx/diffuser.h",
]
CORE_STMLIB = [
    "stmlib.h", "LICENSE", "dsp/dsp.h", "dsp/filter.h", "dsp/delay_line.h",
    "dsp/cosine_oscillator.h", "dsp/units.h", "dsp/units.cc",
]


def normalized_hash(path):
    with open(path, "rb") as file:
        return hashlib.sha256(file.read().replace(b"\r\n", b"\n")).hexdigest()


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__ or "usage: update_provenance.py <eurorack> <stmlib>")
    sources = {"eurorack": sys.argv[1], "stmlib": sys.argv[2]}
    plugin = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
    rows = []

    def add(local, repository, upstream):
        source = os.path.join(sources[repository], upstream)
        target = os.path.join(plugin, local)
        if not os.path.exists(source) or not os.path.exists(target):
            sys.exit("missing: %s or %s" % (source, target))
        upstream_hash = normalized_hash(source)
        relation = "verbatim" if upstream_hash == normalized_hash(target) else "patched"
        rows.append((upstream_hash, relation, local, repository + ":" + upstream))

    for name in ("resources.h", "resources.cc"):
        add("core/elements/" + name, "eurorack", "elements/" + name)
    for name in CORE_DSP:
        add("core/elements/dsp/" + name, "eurorack", "elements/dsp/" + name)
    add("core/elements/dsp/rng.h", "stmlib", "utils/random.h")
    for name in CORE_STMLIB:
        add("core/stmlib/" + name, "stmlib", name)
    add("licenses/stmlib-LICENSE.txt", "stmlib", "LICENSE")

    reference = "tests/reference/eurorack/"
    for root, _, files in os.walk(os.path.join(plugin, reference)):
        for name in sorted(files):
            local = os.path.relpath(os.path.join(root, name), plugin).replace(os.sep, "/")
            relative = local[len(reference):]
            if relative.startswith("elements/"):
                add(local, "eurorack", relative)
            else:
                add(local, "stmlib", relative[len("stmlib/"):])

    rows.sort(key=lambda row: row[2])
    lines = [
        "# Elements: files taken from",
        "#   eurorack  https://github.com/pichenettes/eurorack  " + EURORACK_COMMIT,
        "#   stmlib    https://github.com/pichenettes/stmlib    " + STMLIB_COMMIT,
        "#             (the commit eurorack pins as its submodule)",
        "# SHA-256 of the upstream file's content with CRLF replaced by LF, the relation",
        "# of the local file to it, the local path, and the upstream path.",
        "#   verbatim  the local file has that same hash",
        "#   patched   the local file was changed (rng.h: derived); see UPSTREAM.md",
        "# Written by tools/update_provenance.py; checked by the test elements.provenance.",
    ]
    lines += ["  ".join(row) for row in rows]
    with open(os.path.join(plugin, "SHA256SUMS.upstream"), "w", newline="\n") as file:
        file.write("\n".join(lines) + "\n")
    counts = {}
    for row in rows:
        counts[row[1]] = counts.get(row[1], 0) + 1
    print(counts)


if __name__ == "__main__":
    main()
