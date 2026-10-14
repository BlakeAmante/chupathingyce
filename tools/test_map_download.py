"""The map downloader's checks (port/linux/src/map_archive.c, through
port/linux/tests/map_archive_test.c): map names, network.map_downloads's
words, the zip archives the community map repositories send (stored and
deflated maps, and archives that try to write elsewhere, claim more than
the limit, lie about their sizes or CRC-32, or are damaged), and the Custom
Edition header a downloaded map must have. Built with the build machine's
C compiler and the port's zlib, under AddressSanitizer and
UndefinedBehaviorSanitizer where it has them.

The last test downloads a real map from the community map repository
(maps.halonet.net) and checks it as the game does; it is skipped when the
repository can't be reached, or when HALO_MAP_DOWNLOAD_ONLINE=0."""
import io
import os
import shutil
import struct
import subprocess
import urllib.request
import zipfile
import zlib
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
SOURCE = ROOT / "port" / "linux" / "src"
ZLIB = ROOT / "port" / "third_party" / "zlib"
ZLIB_SOURCES = ["adler32.c", "crc32.c", "inffast.c", "inflate.c", "inftrees.c", "zutil.c"]
TEST = ROOT / "port" / "linux" / "tests" / "map_archive_test.c"
SANITIZERS = ["-fsanitize=address,undefined", "-fno-sanitize-recover=all"]
LIMIT = 200 * 1024 * 1024

# a small, well-known Custom Edition map on the repository, and its header's
# checksum (what a host advertises as its version)
ONLINE_MAP = "putput"
ONLINE_CHECKSUM = "af2f0b84"
ONLINE_URL = "https://maps.halonet.net/halonet/locator.php?format=zip&map={}&type=halom"


def compiler():
    for name in ("clang", "cc", "gcc"):
        path = shutil.which(name)
        if path:
            return path
    return None


@pytest.fixture(scope="module")
def harness(tmp_path_factory):
    cc = compiler()
    if not cc:
        pytest.skip("no C compiler")
    directory = tmp_path_factory.mktemp("map_archive")
    binary = directory / "map_archive_test"
    objects = []
    # (zlib as the port builds it: prefixed, its own warnings its own)
    for name in ZLIB_SOURCES:
        obj = directory / (name + ".o")
        result = subprocess.run([cc, "-c", "-O1", "-g", "-w", "-DZ_PREFIX", "-Dz_errmsg=z_port_errmsg",
                                 str(ZLIB / name), "-o", str(obj), *SANITIZERS], capture_output=True, text=True)
        if result.returncode != 0:
            result = subprocess.run([cc, "-c", "-O1", "-g", "-w", "-DZ_PREFIX", "-Dz_errmsg=z_port_errmsg",
                                     str(ZLIB / name), "-o", str(obj)], capture_output=True, text=True)
            sanitize = []
        else:
            sanitize = SANITIZERS
        assert result.returncode == 0, result.stderr
        objects.append(obj)
    result = subprocess.run(
        [cc, "-std=c99", "-Wall", "-Wextra", "-Werror", "-O1", "-g", *sanitize, "-DZ_PREFIX",
         "-Dz_errmsg=z_port_errmsg", f"-I{SOURCE}", f"-I{ZLIB}", str(TEST), str(SOURCE / "map_archive.c"),
         *map(str, objects), "-o", str(binary)], capture_output=True, text=True)
    assert result.returncode == 0, result.stdout + result.stderr
    return binary


def run(harness, *arguments):
    result = subprocess.run([str(harness), *map(str, arguments)], capture_output=True, text=True, timeout=300)
    assert result.returncode == 0, result.stdout + result.stderr
    return result.stdout.strip()


def make_map(name="testmap", size=0x3000, version=609, checksum=0x1234ABCD, kind=1, file_length=None,
             tag_offset=0x800, tag_size=0x100, head=b"daeh", foot=b"toof", raw_name=None):
    """a Custom Edition cache file's header, and bytes after it"""
    header = bytearray(0x800)
    header[0:4] = head
    struct.pack_into("<I", header, 0x4, version)
    struct.pack_into("<I", header, 0x8, size if file_length is None else file_length)
    struct.pack_into("<I", header, 0x10, tag_offset)
    struct.pack_into("<I", header, 0x14, tag_size)
    field = raw_name if raw_name is not None else name.encode("ascii")
    header[0x20:0x20 + len(field)] = field
    header[0x40:0x40 + 10] = b"01.00.00.0"
    struct.pack_into("<H", header, 0x60, kind)
    struct.pack_into("<I", header, 0x64, checksum)
    header[0x7FC:0x800] = foot
    body = bytes((index * 7 + 3) & 0xFF for index in range(size - 0x800))
    return bytes(header) + body


def make_zip(entries, method=zipfile.ZIP_DEFLATED):
    buffer = io.BytesIO()
    with zipfile.ZipFile(buffer, "w", compression=method) as archive:
        for name, data in entries:
            archive.writestr(zipfile.ZipInfo(name, (2004, 4, 17, 18, 2, 0)), data, compress_type=method)
    return bytearray(buffer.getvalue())


def central(archive, name):
    """the offset of a central directory entry by its name"""
    offset = archive.find(b"PK\x01\x02")
    while offset >= 0:
        length = struct.unpack_from("<H", archive, offset + 28)[0]
        if bytes(archive[offset + 46:offset + 46 + length]) == name.encode():
            return offset
        offset = archive.find(b"PK\x01\x02", offset + 4)
    raise KeyError(name)


def write(tmp_path, data, name="archive.zip"):
    path = tmp_path / name
    path.write_bytes(bytes(data))
    return path


# ---------- names and the setting


@pytest.mark.parametrize("name", ["putput", "bigass_v2.1", "Coldsnap", "a", "x" * 31, "ze-ro_1.2"])
def test_valid_names(harness, name):
    assert run(harness, "name", name) == "valid"


@pytest.mark.parametrize("name", [".hidden", "a..b", "../x", "a/b", "a\\b", "a b", "x" * 32, "c:x", "map%20",
                                  "bigassv2,104", "a?b", "a&type=halor"])
def test_invalid_names(harness, name):
    assert run(harness, "name", name) == "invalid"


def test_empty_name(harness):
    assert run(harness, "name", "") == "invalid"


@pytest.mark.parametrize("text,word", [("ask", "ask"), ("always", "always"), ("NEVER", "never"),
                                       ("Always", "always"), ("", "ask"), ("yes", "ask"), ("no", "ask")])
def test_setting_words(harness, text, word):
    assert run(harness, "setting", text) == word


def test_selftest(harness):
    assert run(harness, "selftest") == "ok"


def test_setting_is_documented_and_defaults_to_ask():
    config = (SOURCE / "port_config.c").read_text(encoding="utf-8")
    assert '{ "network.map_downloads", _config_string, "\\"ask\\""' in config
    assert '{ "network.map_downloads", { "ask", "always", "never", NULL }' in config
    readme = (ROOT / "port" / "linux" / "README.md").read_text(encoding="utf-8")
    assert "| `network.map_downloads` |" in readme


# ---------- archives


@pytest.mark.parametrize("method", [zipfile.ZIP_DEFLATED, zipfile.ZIP_STORED])
def test_extracts_the_map(harness, tmp_path, method):
    data = make_map()
    path = write(tmp_path, make_zip([("readme.txt", b"hello"), ("testmap.map", data)], method))
    out = tmp_path / "out.map"
    assert run(harness, "extract", path, "testmap", out) == f"ok {len(data)} {zlib.crc32(data):08x}"
    assert out.read_bytes() == data


def test_entry_name_in_any_case(harness, tmp_path):
    data = make_map()
    path = write(tmp_path, make_zip([("TestMap.MAP", data)]))
    assert run(harness, "extract", path, "testmap").startswith("ok ")


def test_no_such_map(harness, tmp_path):
    path = write(tmp_path, make_zip([("other.map", make_map("other"))]))
    assert "has no testmap.map" in run(harness, "extract", path, "testmap")


def test_map_in_a_folder_is_not_taken(harness, tmp_path):
    path = write(tmp_path, make_zip([("maps/testmap.map", make_map())]))
    assert "has no testmap.map" in run(harness, "extract", path, "testmap")


@pytest.mark.parametrize("evil", ["../testmap.map", "../../etc/passwd", "/testmap.map", "\\testmap.map",
                                  "C:/testmap.map", "maps/../../testmap.map", "maps\\..\\..\\x.map", ".."])
def test_paths_outside_the_folder_refuse_the_archive(harness, tmp_path, evil):
    path = write(tmp_path, make_zip([("testmap.map", make_map()), (evil, b"evil")]))
    output = run(harness, "extract", path, "testmap")
    assert output.startswith("refused: the archive names a path outside its folder"), output
    assert output.endswith("(0 bytes written)")


def test_twice_the_map(harness, tmp_path):
    path = write(tmp_path, make_zip([("testmap.map", make_map()), ("TESTMAP.map", make_map())]))
    assert "more than once" in run(harness, "extract", path, "testmap")


def test_stated_size_over_the_limit(harness, tmp_path):
    archive = make_zip([("testmap.map", make_map())])
    struct.pack_into("<I", archive, central(archive, "testmap.map") + 24, LIMIT + 1)
    output = run(harness, "extract", write(tmp_path, archive), "testmap")
    assert "MB unpacked (at most 200 MB)" in output and output.endswith("(0 bytes written)")


def test_unpacking_past_the_stated_size_stops(harness, tmp_path):
    # (a bomb: highly compressible data that says it is small)
    data = make_map(size=4 * 1024 * 1024)
    data = data[:0x800] + bytes(len(data) - 0x800)
    archive = make_zip([("testmap.map", data)])
    struct.pack_into("<I", archive, central(archive, "testmap.map") + 24, 0x10000)
    output = run(harness, "extract", write(tmp_path, archive), "testmap")
    assert "unpacks to more than its stated 65536 bytes" in output
    written = int(output.rsplit("(", 1)[1].split()[0])
    assert written <= 0x10000


def test_unpacking_short_of_the_stated_size(harness, tmp_path):
    archive = make_zip([("testmap.map", make_map())])
    struct.pack_into("<I", archive, central(archive, "testmap.map") + 24, 0x3000 + 100)
    assert "not its stated" in run(harness, "extract", write(tmp_path, archive), "testmap")


def test_crc_mismatch(harness, tmp_path):
    archive = make_zip([("testmap.map", make_map())])
    offset = central(archive, "testmap.map") + 16
    struct.pack_into("<I", archive, offset, struct.unpack_from("<I", archive, offset)[0] ^ 1)
    assert "CRC-32" in run(harness, "extract", write(tmp_path, archive), "testmap")


def test_damaged_deflate(harness, tmp_path):
    data = make_map()
    archive = make_zip([("testmap.map", data)])
    local = archive.find(b"PK\x03\x04")
    start = local + 30 + struct.unpack_from("<H", archive, local + 26)[0] + struct.unpack_from("<H", archive, local + 28)[0]
    for index in range(start + 10, start + 60):
        archive[index] ^= 0x5A
    assert run(harness, "extract", write(tmp_path, archive), "testmap").startswith("refused:")


def test_cut_short(harness, tmp_path):
    archive = make_zip([("testmap.map", make_map())])
    assert "not a zip archive" in run(harness, "extract", write(tmp_path, archive[:len(archive) // 2]), "testmap")


def test_data_outside_the_archive(harness, tmp_path):
    archive = make_zip([("testmap.map", make_map())])
    struct.pack_into("<I", archive, central(archive, "testmap.map") + 20, 0x7FFFFFF0)
    assert "lies outside it" in run(harness, "extract", write(tmp_path, archive), "testmap")


def test_local_header_outside_the_archive(harness, tmp_path):
    archive = make_zip([("testmap.map", make_map())])
    struct.pack_into("<I", archive, central(archive, "testmap.map") + 42, 0x7FFFFFF0)
    assert "entry is damaged" in run(harness, "extract", write(tmp_path, archive), "testmap")


def test_encrypted(harness, tmp_path):
    archive = make_zip([("testmap.map", make_map())])
    offset = central(archive, "testmap.map") + 8
    struct.pack_into("<H", archive, offset, struct.unpack_from("<H", archive, offset)[0] | 1)
    assert "encrypted" in run(harness, "extract", write(tmp_path, archive), "testmap")


def test_other_compression(harness, tmp_path):
    archive = make_zip([("testmap.map", make_map())])
    struct.pack_into("<H", archive, central(archive, "testmap.map") + 10, 12)
    assert "method 12" in run(harness, "extract", write(tmp_path, archive), "testmap")


def test_zip64_marker(harness, tmp_path):
    archive = make_zip([("testmap.map", make_map())])
    end = archive.rfind(b"PK\x05\x06")
    struct.pack_into("<HH", archive, end + 8, 0xFFFF, 0xFFFF)
    assert "ZIP64" in run(harness, "extract", write(tmp_path, archive), "testmap")


def test_too_many_entries(harness, tmp_path):
    archive = make_zip([(f"file{index}.txt", b"x") for index in range(300)] + [("testmap.map", make_map())],
                       zipfile.ZIP_STORED)
    assert "entries (at most 256)" in run(harness, "extract", write(tmp_path, archive), "testmap")


def test_cancel_stops(harness, tmp_path):
    data = make_map(size=2 * 1024 * 1024)
    path = write(tmp_path, make_zip([("testmap.map", data)]))
    output = run(harness, "extract", path, "testmap", "-", 1)
    assert output.startswith("refused: cancelled")


def test_random_damage_never_crashes(harness, tmp_path):
    import random

    generator = random.Random(205)
    pristine = make_zip([("readme.txt", b"hello" * 50), ("testmap.map", make_map())])
    for round_index in range(200):
        archive = bytearray(pristine)
        for _ in range(generator.randint(1, 12)):
            archive[generator.randrange(len(archive))] = generator.randrange(256)
        output = run(harness, "extract", write(tmp_path, archive, f"r{round_index}.zip"), "testmap")
        assert output.startswith("ok ") or output.startswith("refused:"), output


# ---------- headers


def header(harness, tmp_path, data, name="testmap", version="0"):
    return run(harness, "header", write(tmp_path, data, "test.map"), name, version)


def test_header_valid(harness, tmp_path):
    assert header(harness, tmp_path, make_map()) == "ok"
    assert header(harness, tmp_path, make_map(), "TESTMAP") == "ok"
    assert header(harness, tmp_path, make_map(), version="1234abcd") == "ok"
    # (Invader's maps may give their length as 0)
    assert header(harness, tmp_path, make_map(file_length=0)) == "ok"


@pytest.mark.parametrize("kwargs,why", [
    ({"head": b"xxxx"}, "not a cache file"),
    ({"foot": b"xxxx"}, "not a cache file"),
    ({"version": 7}, "version 7"),
    ({"version": 5}, "version 5"),
    ({"kind": 0}, "not a multiplayer map"),
    ({"name": "othermap"}, "named othermap inside"),
    ({"raw_name": b"x" * 32}, "does not end"),
    ({"file_length": 0x5000}, "header says"),
    ({"file_length": 0x100}, "header says"),
    ({"tag_offset": 0x100}, "tag data"),
    ({"tag_offset": 0x2F00, "tag_size": 0x200}, "tag data"),
    ({"tag_offset": 0x900, "tag_size": 0xFFFFFFFF}, "tag data"),
])
def test_header_refused(harness, tmp_path, kwargs, why):
    output = header(harness, tmp_path, make_map(**kwargs))
    assert output.startswith("refused:") and why in output, output


def test_header_other_version_than_the_hosts(harness, tmp_path):
    output = header(harness, tmp_path, make_map(), version="deadbeef")
    assert "another version of the map" in output


def test_header_too_small(harness, tmp_path):
    assert header(harness, tmp_path, make_map()[:0x400]).startswith("refused:")


# ---------- the real repository


def test_real_repository_map(harness, tmp_path):
    if os.environ.get("HALO_MAP_DOWNLOAD_ONLINE") == "0":
        pytest.skip("HALO_MAP_DOWNLOAD_ONLINE=0")
    request = urllib.request.Request(ONLINE_URL.format(ONLINE_MAP), headers={"User-Agent": "ChupathingyCE tests"})
    try:
        with urllib.request.urlopen(request, timeout=60) as response:
            data = response.read(LIMIT + 1)
    except OSError as failure:
        pytest.skip(f"the repository can't be reached: {failure}")
    assert len(data) <= LIMIT
    archive = write(tmp_path, data, f"{ONLINE_MAP}.zip")
    out = tmp_path / f"{ONLINE_MAP}.map"
    output = run(harness, "extract", archive, ONLINE_MAP, out)
    assert output.startswith("ok "), output
    assert run(harness, "header", out, ONLINE_MAP, ONLINE_CHECKSUM) == "ok"
    assert "another version" in run(harness, "header", out, ONLINE_MAP, "00000001")
