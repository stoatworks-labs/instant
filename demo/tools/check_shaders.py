"""The demo's shaders must be the plugin's shaders, character for character.

    python3 demo/tools/check_shaders.py [--dump DIR]

Called from `tools/verify.sh`, with `--dump` pointing at `intest --dump-shaders`
output. Exit code 1 means the two copies have drifted.

------------------------------------------------------------------- why

`demo/plugin.js` holds the seven GLSL strings of `source/Shaders.cpp` -- the
vertex body, the `kModel` library and the capture, meter, develop, resample and
print bodies -- plus the constants block `constants()` writes from Model.h at
run time, and the numbers the CPU half is made of. Two copies drift quietly: a
demo that renders a *plausible* developing print looks exactly like one that
renders the right one. The page's claim is that the develop and print passes
running in your browser are the plugin's, so the claim needs something
enforcing it. `intest` drives the real plugin class and has never heard of this
page.

------------------------------------------------------------------- what it does

1. Pulls each `R"( ... )"` body out of Shaders.cpp and each matching backtick
   literal out of plugin.js, and compares them exactly -- no whitespace
   normalisation, no comment stripping. A comment updated on one side only is
   drift worth catching; the comments carry the reasoning.

   The one transformation is a decode, not a normalisation. A template literal
   cannot hold a raw backtick, backslash or `${`, so `sync_shaders.py` escapes
   those three; this undoes exactly those three and REJECTS any other backslash
   on the JS side, which could only be somebody hiding a difference in the
   decoder.

2. Regenerates the rest of the generated block -- the constants text, every
   Model.h constant, the stocks, the Controls counts and option lists -- from
   the C++ and compares it with plugin.js, line for line.

3. With `--dump DIR`: assembles every stage from plugin.js's strings exactly as
   the page does (`assemble`: the version line, then for the model stages the
   constants block and the model, then the body) and compares the whole text
   with the file the PLUGIN compiled, as `intest --dump-shaders DIR` wrote it.
   That closes the one gap step 2 leaves: `constants()` is C++ that formats at
   run time, and this proves the page's copy of its output is its output.

------------------------------------------------------------------- what it cannot

Nothing here checks the PORT. Every control's law, PrintLayout,
ArrheniusFactor, and Instant::ProcessOpenGL's clock, take, buffers, resize
resample, stock arithmetic and uniforms in plugin.js are a hand translation of
Controls.cpp and Instant.cpp, and only a reader can tell whether they still
agree. Change one of those and change the page by hand to match.

If this fails because a shader changed in the plugin: `python3 demo/tools/sync_shaders.py`,
never an edit of plugin.js by hand.
"""
import os
import re
import sys

# No __pycache__ beside the page's sources.
sys.dont_write_bytecode = True
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import sync_shaders  # noqa: E402

REPO = sync_shaders.REPO


def from_js(source, name):
    match = re.search(r"^const " + name + r" = `(.*?)`;$", source, re.S | re.M)
    if match is None:
        return None, None
    body = match.group(1)
    stray = re.search(r"\\(?![`\\$])", body)
    if stray is not None:
        upto = body[: stray.start()]
        return None, f"backslash that is not an escaped backtick, backslash or dollar, at line {upto.count(chr(10)) + 1}"
    decoded = re.sub(r"\\([`\\$])", r"\1", body)
    return decoded, None


def first_difference(a_text, b_text, a_label, b_label):
    a_lines, b_lines = a_text.splitlines(), b_text.splitlines()
    for i in range(max(len(a_lines), len(b_lines))):
        a = a_lines[i] if i < len(a_lines) else "<missing>"
        b = b_lines[i] if i < len(b_lines) else "<missing>"
        if a != b:
            print(f"        first difference at line {i + 1}")
            print(f"          {a_label}: {a[:120]}")
            print(f"          {b_label}: {b[:120]}")
            return
    print("        (they differ only in a trailing newline)")


def main():
    dump = None
    if "--dump" in sys.argv:
        dump = sys.argv[sys.argv.index("--dump") + 1]

    with open(os.path.join(REPO, "demo", "plugin.js")) as handle:
        js = handle.read()
    shaders_cpp = sync_shaders.read("source/Shaders.cpp")

    problems = 0
    pieces = 0

    # The version line assemble() prepends.
    pieces += 1
    want = sync_shaders.cpp_version(shaders_cpp)
    got = re.search(r'^const K_VERSION = "(.*?)";$', js, re.M)
    if got is None or got.group(1) != want:
        print("FAIL  K_VERSION is not Shaders.cpp's kVersion")
        problems += 1
    else:
        print(f"ok    {'K_VERSION':<14} matches kVersion")

    texts = {}
    for name, symbol in sync_shaders.SHADERS:
        pieces += 1
        cpp_text = sync_shaders.cpp_shader(shaders_cpp, symbol)
        js_text, complaint = from_js(js, name)
        if complaint is not None:
            print(f"FAIL  {name} in demo/plugin.js has a {complaint}")
            problems += 1
            continue
        if js_text is None:
            print(f"FAIL  {name} not found in demo/plugin.js")
            problems += 1
            continue
        texts[name] = js_text
        if cpp_text == js_text:
            print(f"ok    {name:<14} matches {symbol} ({len(cpp_text)} chars)")
            continue
        problems += 1
        print(f"FAIL  {name} has drifted from {symbol} in source/Shaders.cpp")
        first_difference(cpp_text, js_text, "C++", "js ")

    # The tables and constants: the whole generated block, regenerated.
    where = sync_shaders.region(js)
    if where is None:
        print("FAIL  demo/plugin.js has no generated block")
        problems += 1
    else:
        have = js[where[0]: where[1]].splitlines()
        want_lines = sync_shaders.block().splitlines()
        if have == want_lines:
            constants = sum(1 for line in want_lines if re.match(r"^MODEL_H\.k", line))
            print(f"ok    the generated block matches source/ (the constants text, {constants} Model.h constants, the stocks, the Controls lists)")
        else:
            problems += 1
            for i in range(max(len(have), len(want_lines))):
                a = want_lines[i] if i < len(want_lines) else "<missing>"
                b = have[i] if i < len(have) else "<missing>"
                if a != b:
                    print(f"FAIL  the generated block differs from source/ at its line {i + 1}")
                    print(f"          source: {a[:120]}")
                    print(f"          js    : {b[:120]}")
                    break

    # The page's assembled text against what the plugin compiles.
    stages = 0
    if dump is not None:
        version = re.search(r'^const K_VERSION = "(.*?)";$', js, re.M)
        constants, complaint = from_js(js, "K_CONSTANTS")
        model = texts.get("MODEL")
        if version is None or constants is None or model is None or complaint is not None:
            print("FAIL  cannot assemble the page's shaders (K_VERSION, K_CONSTANTS or MODEL missing)")
            problems += 1
        else:
            for filename, body_name, with_model in sync_shaders.STAGES:
                path = os.path.join(dump, filename)
                if not os.path.exists(path):
                    print(f"FAIL  {filename} is not in {dump}: did intest --dump-shaders run?")
                    problems += 1
                    continue
                with open(path) as handle:
                    compiled = handle.read()
                body = texts.get(body_name)
                if body is None:
                    problems += 1
                    continue
                page = version.group(1) + "\n" + (constants + model if with_model else "") + body
                if page == compiled:
                    stages += 1
                    print(f"ok    {filename:<14} the page assembles exactly what the plugin compiles ({len(page)} chars)")
                else:
                    problems += 1
                    print(f"FAIL  {filename}: the page's assembled shader is not the plugin's")
                    first_difference(compiled, page, "plugin", "page  ")

    print()
    if problems:
        print(f"{problems} piece(s) differ -- run python3 demo/tools/sync_shaders.py, do not edit plugin.js by hand")
        return 1
    tail = f", and all {stages} assembled stages equal intest --dump-shaders" if dump is not None else ""
    print(f"all {pieces} shader pieces and every copied constant are identical to the plugin's{tail}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
