/*
MAP_DOWNLOAD.C

Custom Edition maps a game needs, downloaded from the community map
repositories (map_download.h): the public repositories Halo PC's players
have downloaded missing maps from for years, as Chimera uses them
(maps.halonet.net, then its mirrors maps2 and maps1), asked by the map's
name for its zip archive (locator.php?format=zip&map=<name>&type=halom),
over HTTPS.

On a thread of its own: the archive is received into maps_ce (no more than
MAP_ARCHIVE_MAXIMUM_SIZE), the next repository asked when one fails; the
map alone is unpacked from it (map_archive.c), into a file of its own; its
header is checked (a Custom Edition multiplayer map named for the map asked
for, and the host's version of it, when the host says which); and only then
is it renamed <name>.map, never over a file that is there. The archive and
any partial file are deleted, whatever happens. The map is then marked as
downloaded (map_families.c, for good: maps_ce/downloaded_maps.txt), so its
scripts are held to a downloaded map's rules, and the game's own checks read all of it before it is played
(ce_map_checks.c).

The log (debug.txt) has each request's address (the map's name alone, no
player's), the sizes, which checks passed or failed, and the result.
*/

#include "platform.h"
#include "port_config.h"
#include "map_archive.h"
#include "map_download.h"
#include "halo_map_families.h"
#ifdef HALO_GAME_BROWSER
#include "browser_http.h"
#endif

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------- constants */

enum
{
	/* what is received or copied at a time */
	MAP_DOWNLOAD_CHUNK = 64 * 1024,
	MAP_DOWNLOAD_PATH_SIZE = 256,
};

/* the repositories, in the order they are asked (Chimera's: its default,
then the mirrors its map downloader names) */
static const char *const map_download_repositories[] =
{
	"https://maps.halonet.net",
	"https://maps2.halonet.net",
	"https://maps1.halonet.net",
};

/* ---------- globals */

static struct
{
	pthread_mutex_t lock;
	struct map_download_progress progress;
	unsigned int version;
	/* set by the game's thread, read by the download's */
	volatile int cancel;
	int running;
	/* the map installed is marked as downloaded (map_download_get) */
	int remembered;
} map_download = { PTHREAD_MUTEX_INITIALIZER };

/* ---------- private code */

static void set_phase(int phase, unsigned int received, unsigned int total)
{
	pthread_mutex_lock(&map_download.lock);
	map_download.progress.phase = phase;
	map_download.progress.received = received;
	map_download.progress.total = total;
	pthread_mutex_unlock(&map_download.lock);
}

static void finish(int state, const char *message)
{
	pthread_mutex_lock(&map_download.lock);
	map_download.progress.state = state;
	snprintf(map_download.progress.message, sizeof(map_download.progress.message), "%s", message ? message : "");
	map_download.running = 0;
	pthread_mutex_unlock(&map_download.lock);
}

static void paths(const char *name, char *archive, char *partial, char *final)
{
	snprintf(archive, MAP_DOWNLOAD_PATH_SIZE, "d:\\maps_ce\\%s.zip.download", name);
	snprintf(partial, MAP_DOWNLOAD_PATH_SIZE, "d:\\maps_ce\\%s.map.download", name);
	snprintf(final, MAP_DOWNLOAD_PATH_SIZE, "d:\\maps_ce\\%s.map", name);
}

#ifdef HALO_GAME_BROWSER

/* map_archive_extract's reads (the archive's file) and writes (the map's) */
struct map_files
{
	HANDLE archive;
	HANDLE map;
	unsigned int written;
	unsigned int total;
};

static int archive_read(void *context, unsigned int offset, void *buffer, unsigned int size)
{
	struct map_files *files = (struct map_files *)context;
	DWORD read = 0;

	if (SetFilePointer(files->archive, (LONG)offset, NULL, FILE_BEGIN) == INVALID_SET_FILE_POINTER)
		return 0;
	return ReadFile(files->archive, buffer, size, &read, NULL) && read == size;
}

static int map_write(void *context, const void *buffer, unsigned int size)
{
	struct map_files *files = (struct map_files *)context;
	DWORD written = 0;

	if (!WriteFile(files->map, buffer, size, &written, NULL) || written != size)
		return 0;
	files->written += size;
	set_phase(MAP_DOWNLOAD_UNPACKING, files->written, files->total);
	return 1;
}

static int stop_requested(void *context)
{
	(void)context;
	return map_download.cancel;
}

/* the archive from one repository into the file at path: 1, or 0 with why
in error and the HTTP status in *status (cancelled: map_download.cancel) */
static int receive(const char *url, const char *path, int *status, char *error, int error_size)
{
	char *buffer;
	HANDLE file;
	unsigned int received = 0;
	int length = -1;
	int download;
	int ok = 1;

	set_phase(MAP_DOWNLOAD_CONNECTING, 0, 0);
	*status = 0;
	download = posix_browser_download_open(url, build_identity_user_agent(), status, &length, error, error_size);
	if (!download)
		return 0;
	if (length > MAP_ARCHIVE_MAXIMUM_SIZE)
	{
		posix_browser_download_close(download);
		snprintf(error, (size_t)error_size, "the archive is %d MB (at most %d MB)", length >> 20,
			MAP_ARCHIVE_MAXIMUM_SIZE >> 20);
		return 0;
	}
	buffer = (char *)malloc(MAP_DOWNLOAD_CHUNK);
	file = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (!buffer || file == INVALID_HANDLE_VALUE)
	{
		posix_browser_download_close(download);
		free(buffer);
		if (file != INVALID_HANDLE_VALUE)
			CloseHandle(file);
		snprintf(error, (size_t)error_size, "could not write into maps_ce");
		return 0;
	}
	set_phase(MAP_DOWNLOAD_RECEIVING, 0, length > 0 ? (unsigned int)length : 0);
	for (;;)
	{
		int count;
		DWORD written = 0;

		if (map_download.cancel)
		{
			snprintf(error, (size_t)error_size, "cancelled");
			ok = 0;
			break;
		}
		count = posix_browser_download_read(download, buffer, MAP_DOWNLOAD_CHUNK);
		if (count == 0)
			break;
		if (count < 0)
		{
			snprintf(error, (size_t)error_size, "the connection failed after %u bytes", received);
			ok = 0;
			break;
		}
		if ((unsigned int)count > (unsigned int)MAP_ARCHIVE_MAXIMUM_SIZE - received)
		{
			snprintf(error, (size_t)error_size, "the archive is over %d MB", MAP_ARCHIVE_MAXIMUM_SIZE >> 20);
			ok = 0;
			break;
		}
		if (!WriteFile(file, buffer, (DWORD)count, &written, NULL) || written != (DWORD)count)
		{
			snprintf(error, (size_t)error_size, "could not write into maps_ce (is the disk full?)");
			ok = 0;
			break;
		}
		received += (unsigned int)count;
		set_phase(MAP_DOWNLOAD_RECEIVING, received, length > 0 ? (unsigned int)length : 0);
	}
	posix_browser_download_close(download);
	free(buffer);
	CloseHandle(file);
	if (ok && length >= 0 && received != (unsigned int)length)
	{
		snprintf(error, (size_t)error_size, "the archive was cut short (%u of %d bytes)", received, length);
		ok = 0;
	}
	if (ok)
		platform_log("map download: received %u bytes", received);
	return ok;
}

/* the map unpacked from the archive at archive_path into partial_path, and
its header checked: 1, or 0 with why in error */
static int unpack_and_check(const char *name, const char *archive_path, const char *partial_path, char *error,
	int error_size)
{
	unsigned char header[MAP_ARCHIVE_HEADER_SIZE];
	struct map_archive_io io;
	struct map_files files;
	unsigned int map_size = 0;
	DWORD high = 0, read = 0;
	int ok;

	memset(&files, 0, sizeof(files));
	files.archive = CreateFileA(archive_path, GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (files.archive == INVALID_HANDLE_VALUE)
	{
		snprintf(error, (size_t)error_size, "the archive could not be read back");
		return 0;
	}
	files.map = CreateFileA(partial_path, GENERIC_READ | GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL,
		NULL);
	if (files.map == INVALID_HANDLE_VALUE)
	{
		CloseHandle(files.archive);
		snprintf(error, (size_t)error_size, "could not write into maps_ce");
		return 0;
	}
	memset(&io, 0, sizeof(io));
	io.context = &files;
	io.archive_size = GetFileSize(files.archive, &high);
	io.read = archive_read;
	io.write = map_write;
	io.stop = stop_requested;
	if (high || io.archive_size == INVALID_FILE_SIZE)
		io.archive_size = 0xffffffffu;
	set_phase(MAP_DOWNLOAD_UNPACKING, 0, 0);
	ok = map_archive_extract(&io, name, &map_size, error, error_size);
	CloseHandle(files.archive);
	if (ok)
	{
		platform_log("map download: unpacked %s.map, %u bytes (size and CRC-32 checked)", name, map_size);
		set_phase(MAP_DOWNLOAD_CHECKING, 0, 0);
		ok = SetFilePointer(files.map, 0, NULL, FILE_BEGIN) != INVALID_SET_FILE_POINTER &&
			ReadFile(files.map, header, sizeof(header), &read, NULL) && read == sizeof(header);
		if (!ok)
			snprintf(error, (size_t)error_size, "the map could not be read back");
		else
			ok = map_archive_check_header(header, map_size, name, map_download.version, error, error_size);
		if (ok)
		{
			platform_log("map download: %s.map is a Custom Edition multiplayer map named %s, checksum %08x%s", name,
				name, map_archive_header_checksum(header), map_download.version ? " (the host's)" :
				" (the host's not known)");
		}
	}
	CloseHandle(files.map);
	return ok;
}

static void *download_thread(void *argument)
{
	char name[sizeof(map_download.progress.name)];
	char asked[sizeof(name)];
	char archive_path[MAP_DOWNLOAD_PATH_SIZE], partial_path[MAP_DOWNLOAD_PATH_SIZE], final_path[MAP_DOWNLOAD_PATH_SIZE];
	char error[160] = "";
	size_t index;
	int received = 0;
	int status = 0;
	/* (whether every repository said it has no such map) */
	int missing = 1;

	(void)argument;
	pthread_mutex_lock(&map_download.lock);
	snprintf(name, sizeof(name), "%s", map_download.progress.name);
	pthread_mutex_unlock(&map_download.lock);
	/* (asked for in lowercase, as the repositories name their maps) */
	for (index = 0; index <= strlen(name); index++)
		asked[index] = (char)(name[index] >= 'A' && name[index] <= 'Z' ? name[index] + ('a' - 'A') : name[index]);
	paths(name, archive_path, partial_path, final_path);
	CreateDirectoryA("d:\\maps_ce", NULL);
	DeleteFileA(archive_path);
	DeleteFileA(partial_path);

	for (index = 0; index < sizeof(map_download_repositories) / sizeof(map_download_repositories[0]) &&
		!received && !map_download.cancel; index++)
	{
		char url[384];

		snprintf(url, sizeof(url), "%s/halonet/locator.php?format=zip&map=%s&type=halom",
			map_download_repositories[index], asked);
		platform_log("map download: GET %s", url);
		received = receive(url, archive_path, &status, error, sizeof(error));
		if (!received)
		{
			platform_log("map download: %s", error);
			DeleteFileA(archive_path);
			missing = missing && status == 404;
		}
	}
	if (!received && missing && !map_download.cancel)
		snprintf(error, sizeof(error), "the community map repositories don't have %s.map", name);
	if (received && !unpack_and_check(name, archive_path, partial_path, error, sizeof(error)))
	{
		platform_log("map download: %s.map refused: %s", name, error);
		received = 0;
	}
	DeleteFileA(archive_path);
	/* (in place, never over a file that is there: MoveFileA refuses one) */
	if (received && !MoveFileA(partial_path, final_path))
	{
		snprintf(error, sizeof(error), "maps_ce/%s.map could not be put in place (is it there already?)", name);
		platform_log("map download: %s", error);
		received = 0;
	}
	if (!received)
	{
		DeleteFileA(partial_path);
		if (map_download.cancel)
		{
			platform_log("map download: %s.map cancelled", name);
			finish(MAP_DOWNLOAD_CANCELLED, "cancelled");
		}
		else
		{
			platform_log("map download: %s.map failed", name);
			finish(MAP_DOWNLOAD_FAILED, error);
		}
		return NULL;
	}
	platform_log("map download: %s.map installed in maps_ce", name);
	finish(MAP_DOWNLOAD_DONE, NULL);
	return NULL;
}

#endif

/* ---------- public code */

int map_download_setting(void)
{
	return map_archive_setting(config_string("network.map_downloads"));
}

void map_download_set_setting(int setting)
{
	config_write("network.map_downloads", map_archive_setting_word(setting));
}

int map_download_possible(const char *name)
{
#if defined(HALO_GAME_BROWSER) && !defined(HALO_SERVER)
	return map_download_setting() != MAP_DOWNLOADS_NEVER && map_archive_name_valid(name);
#else
	(void)name;
	return 0;
#endif
}

int map_download_start(const char *name, unsigned int version)
{
#if defined(HALO_GAME_BROWSER) && !defined(HALO_SERVER)
	pthread_t thread;

	if (!map_archive_name_valid(name))
		return 0;
	pthread_mutex_lock(&map_download.lock);
	if (map_download.running)
	{
		pthread_mutex_unlock(&map_download.lock);
		return 0;
	}
	memset(&map_download.progress, 0, sizeof(map_download.progress));
	map_download.progress.state = MAP_DOWNLOAD_RUNNING;
	snprintf(map_download.progress.name, sizeof(map_download.progress.name), "%s", name);
	map_download.version = version;
	map_download.cancel = 0;
	map_download.running = 1;
	map_download.remembered = 0;
	pthread_mutex_unlock(&map_download.lock);
	platform_log("map download: %s.map from the community map repositories (host's checksum %08x)", name, version);
	if (pthread_create(&thread, NULL, download_thread, NULL) != 0)
	{
		finish(MAP_DOWNLOAD_FAILED, "could not start the download");
		return 0;
	}
	pthread_detach(thread);
	return 1;
#else
	(void)name;
	(void)version;
	return 0;
#endif
}

void map_download_cancel(void)
{
	map_download.cancel = 1;
}

void map_download_get(struct map_download_progress *progress)
{
	pthread_mutex_lock(&map_download.lock);
	*progress = map_download.progress;
	pthread_mutex_unlock(&map_download.lock);
	/* (a map just installed marked as downloaded, on the game's thread, which
	reads the marks) */
	if (progress->state == MAP_DOWNLOAD_DONE && !map_download.remembered)
	{
		char map_name[64];

		map_download.remembered = 1;
		snprintf(map_name, sizeof(map_name), "%s@ce", progress->name);
		map_downloaded_remember(map_name);
	}
}

void map_download_reset(void)
{
	pthread_mutex_lock(&map_download.lock);
	if (!map_download.running)
		memset(&map_download.progress, 0, sizeof(map_download.progress));
	pthread_mutex_unlock(&map_download.lock);
}
