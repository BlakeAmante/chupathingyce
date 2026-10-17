"""Check config.toml's handling of defaults (port/linux/src/port_config.c).

Builds port_config.c on its own with tools/tests/port_config_test.c (SDL's
few functions stood in for with the C library), once for each kind of build
whose defaults differ, and runs it on config.toml files of each history a
player's can have:

- a new file: every setting commented out at its default;
- a file from before config_version, every setting written as a value:
  values at this version's default are commented out, every other kept
  (an earlier default may be the player's choice);
- a file of this layout that an older version then ran on, which added each
  commented-out setting again as a value at its default: those values are
  taken out, so that a default changed since reaches the player;
- the per-platform defaults of each build (DEFAULTS below).

Needs only a C compiler. Usage: test_port_config.py [--cc clang]
"""

import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

# the defaults this version gives each kind of build, for the settings whose
# default is not the same everywhere or changed in this version
DEFAULTS = {
	"desktop": {
		"display.anti_aliasing": "msaa2x",
		"display.shadow_resolution": "256",
	},
	"desktop32": {
		"display.anti_aliasing": "off",
		"display.shadow_resolution": "256",
		"audio.buffer_frames": "512",
	},
	"android": {
		"display.anti_aliasing": "off",
		"display.shadow_resolution": "256",
		"audio.buffer_frames": "1024",
	},
	"server": {
		"display.anti_aliasing": "off",
		"display.shadow_resolution": "128",
	},
}

# the macros each kind of build is compiled with; the others are not macOS
# (whose own defaults the native build checks on a Mac)
DEFINES = {
	"desktop": ["-DHALO_64BIT"],
	"desktop32": ["-U__APPLE__"],
	"android": ["-U__APPLE__", "-DHALO_ANDROID"],
	"server": ["-U__APPLE__", "-DHALO_SERVER", "-DHALO_64BIT"],
}

STUB_PLATFORM = """#include <stddef.h>
void platform_log(const char *format, ...);
"""

STUB_SDL = """#include <stdbool.h>
#include <stddef.h>
const char *SDL_GetBasePath(void);
bool SDL_CreateDirectory(const char *path);
void *SDL_LoadFile(const char *path, size_t *size);
bool SDL_SaveFile(const char *path, const void *data, size_t size);
void SDL_free(void *memory);
typedef struct { long long size; long long modify_time; } SDL_PathInfo;
bool SDL_GetPathInfo(const char *path, SDL_PathInfo *info);
int SDL_strncasecmp(const char *first, const char *second, size_t length);
"""


def build(cc, kind, directory):
	"""The test program for a kind of build, in directory."""
	source = directory / "source"
	(source / "SDL3").mkdir(parents=True, exist_ok=True)
	shutil.copy(ROOT / "port/linux/src/port_config.c", source)
	shutil.copy(ROOT / "port/linux/src/port_config.h", source)
	(source / "platform.h").write_text(STUB_PLATFORM)
	(source / "SDL3/SDL.h").write_text(STUB_SDL)
	program = directory / f"port_config_test_{kind}"
	subprocess.run([cc, "-std=c11", "-D_DEFAULT_SOURCE", "-D_DARWIN_C_SOURCE", "-Wall", "-Werror", "-g",
		*DEFINES[kind], "-I", str(source), "-I", str(ROOT / "port/third_party/tomlc17"),
		str(source / "port_config.c"), str(ROOT / "tools/tests/port_config_test.c"),
		str(ROOT / "port/third_party/tomlc17/tomlc17.c"), "-lpthread", "-o", str(program)],
		check=True)
	return program


class Case:
	def __init__(self, program, directory):
		self.program = program
		self.folder = directory
		self.folder.mkdir(parents=True, exist_ok=True)
		self.path = self.folder / "config.toml"

	def run(self, *arguments):
		environment = {key: value for key, value in os.environ.items() if not key.startswith("HALO_")}
		environment["PORT_CONFIG_TEST_FOLDER"] = str(self.folder)
		environment["HALO_DATA_ROOT"] = str(self.folder)
		result = subprocess.run([str(self.program), *arguments], capture_output=True, text=True,
			env=environment, check=True)
		values = dict(re.findall(r"^([a-z_]+\.[a-z_0-9]+)=(.*)$", result.stdout, re.M))
		return values, result.stdout

	def get(self, *names):
		return self.run("get", *names)

	def text(self):
		return self.path.read_text()


def value_lines(text, key):
	"""The file's lines "key = ..." and "# key = ..." for a key."""
	values = re.findall(rf"^\s*{key}\s*=.*$", text, re.M)
	commented = re.findall(rf"^\s*#\s*{key}\s*=.*$", text, re.M)
	return values, commented


def check(condition, message):
	if not condition:
		raise AssertionError(message)


def test_new_file(program, directory, defaults):
	case = Case(program, directory / "new")
	values, _ = case.get(*defaults)
	for name, expected in defaults.items():
		check(values[name] == expected, f"new file: {name} is {values[name]}, not {expected}")
	text = case.text()
	check("config_version = " in text, "new file: no config_version")
	for name, expected in defaults.items():
		key = name.split(".")[1]
		lines, commented = value_lines(text, key)
		check(not lines and len(commented) == 1, f"new file: {key} is not commented out once: {lines} {commented}")


def test_readded_by_older_version(program, directory, defaults):
	"""The owner's case: a config_version 2 file that a build from before
	config_version then ran on, adding every setting again as a value."""
	case = Case(program, directory / "readded")
	case.get("display.vsync")
	text = case.text()
	# what that older build appends to [display]: its default (the old one
	# for each setting this version changed) and one the player chose
	old = {"shadow_resolution": "128", "anti_aliasing": "\"off\"", "interpolation": "true", "vsync": "true",
		"high_res_hud": "false"}
	block = "".join(f"\n# An older version's comment for {key}.\n# (for one run: HALO_X=<value>)\n{key} = {value}\n"
		for key, value in old.items())
	audio = text.index("\n[audio]")
	text = text[:audio].rstrip("\n") + "\n" + block + text[audio:]
	case.path.write_text(text)
	values, _ = case.get("display.shadow_resolution", "display.anti_aliasing", "display.interpolation",
		"display.vsync", "display.high_res_hud")
	check(values["display.shadow_resolution"] == defaults["display.shadow_resolution"],
		f"readded: shadow_resolution stayed {values['display.shadow_resolution']}")
	check(values["display.anti_aliasing"] == defaults["display.anti_aliasing"],
		f"readded: anti_aliasing stayed {values['display.anti_aliasing']}")
	check(values["display.high_res_hud"] == "false", "readded: the player's high_res_hud = false was lost")
	text = case.text()
	for key in ("shadow_resolution", "anti_aliasing", "interpolation", "vsync"):
		lines, commented = value_lines(text, key)
		check(not lines and len(commented) == 1, f"readded: {key}: {lines} {commented}")
		check(f"An older version's comment for {key}." not in text, f"readded: {key}'s added comment stayed")
	lines, _ = value_lines(text, "high_res_hud")
	check(lines == ["high_res_hud = false"], f"readded: high_res_hud: {lines}")
	# (nothing more to do the second time)
	before = case.text()
	case.get("display.vsync")
	check(case.text() == before, "readded: the file changed again on the next start")


def test_before_config_version(program, directory, defaults):
	"""A file from before config_version: every setting a value. Those at
	this version's default are commented out; an earlier default is kept,
	being possibly the player's choice (existing players keep their
	settings: the new defaults are for new files)."""
	case = Case(program, directory / "version1")
	case.get("display.vsync")
	text = case.text()
	# every commented-out setting as a value, the way version 1 wrote them,
	# at the old defaults for those this version changed
	old = {"shadow_resolution": "128", "anti_aliasing": "\"off\""}
	text = re.sub(r"^config_version = \d+\n", "", text, flags=re.M)
	text = text.replace("# The layout of this file, for the game: leave it.\n", "")

	def value(match):
		key = match.group(1)
		return f"{key} = {old.get(key, match.group(2))}"
	text = re.sub(r"^# ([a-z_0-9]+) = (.*)$", value, text, flags=re.M)
	text = text.replace("volume = 1.0", "volume = 0.5", 1)
	case.path.write_text(text)
	values, _ = case.get("display.shadow_resolution", "display.anti_aliasing", "audio.volume")
	check(values["display.shadow_resolution"] == "128", f"version 1: shadow_resolution {values['display.shadow_resolution']}")
	check(values["display.anti_aliasing"] == "off", f"version 1: anti_aliasing {values['display.anti_aliasing']}")
	check(values["audio.volume"] == "0.5", "version 1: the player's audio.volume was lost")
	text = case.text()
	check("config_version = 2" in text, "version 1: no config_version written")
	lines, commented = value_lines(text, "vsync")
	check(not lines and len(commented) == 1, f"version 1: vsync, at its default, is not commented out: {lines} {commented}")


def main():
	parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
	parser.add_argument("--cc", default=os.environ.get("CC", "cc"))
	arguments = parser.parse_args()
	failures = 0
	with tempfile.TemporaryDirectory() as directory:
		directory = Path(directory)
		for kind in DEFAULTS:
			program = build(arguments.cc, kind, directory / kind)
			for test in (test_new_file, test_readded_by_older_version, test_before_config_version):
				try:
					test(program, directory / kind / "cases", DEFAULTS[kind])
					print(f"ok   {kind}: {test.__name__}")
				except AssertionError as error:
					print(f"FAIL {kind}: {test.__name__}: {error}")
					failures += 1
	sys.exit(1 if failures else 0)


if __name__ == "__main__":
	main()
