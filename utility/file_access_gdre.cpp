//
// Created by Nikita on 3/26/2023.
//

#include "file_access_gdre.h"
#include "file_access_apk.h"
#include "gdre_packed_source.h"
#include "gdre_settings.h"

#include "core/os/os.h"

bool is_gdre_file(const String &p_path) {
	return p_path.begins_with("res://") && p_path.get_basename().begins_with("gdre_");
}

GDREPackedData *GDREPackedData::singleton = nullptr;

GDREPackedData::GDREPackedData() {
	singleton = this;
	root = memnew(PackedDir);
}

void GDREPackedData::add_pack_source(PackSource *p_source) {
	if (p_source != nullptr) {
		sources.push_back(p_source);
	}
}

void GDREPackedData::add_path(const String &p_pkg_path, const String &p_path, uint64_t p_ofs, uint64_t p_size, const uint8_t *p_md5, PackSource *p_src, bool p_replace_files, bool p_encrypted, bool p_gdre_root) {
	PathMD5 pmd5(p_path.md5_buffer());

	bool exists = files.has(pmd5);

	PackedData::PackedFile pf;
	pf.encrypted = p_encrypted;
	pf.pack = p_pkg_path;
	pf.offset = p_ofs;
	pf.size = p_size;
	for (int i = 0; i < 16; i++) {
		pf.md5[i] = p_md5[i];
	}
	pf.src = p_src;

	if (!exists || p_replace_files) {
		files[pmd5] = pf;
	}

	if (!exists) {
		//search for dir
		String p = p_path.replace_first("res://", "");
		PackedDir *cd = root;

		if (p.contains("/")) { //in a subdir

			Vector<String> ds = p.get_base_dir().split("/");

			for (int j = 0; j < ds.size(); j++) {
				if (!cd->subdirs.has(ds[j])) {
					PackedDir *pd = memnew(PackedDir);
					pd->name = ds[j];
					pd->parent = cd;
					cd->subdirs[pd->name] = pd;
					cd = pd;
				} else {
					cd = cd->subdirs[ds[j]];
				}
			}
		}
		String filename = p_path.get_file();
		// Don't add as a file if the path points to a directory
		if (!filename.is_empty()) {
			cd->files.insert(filename);
		}
	}
}

void GDREPackedData::set_disabled(bool p_disabled) {
	if (p_disabled) {
		// we need to re-enable the default create funcs
		reset_default_file_access();
		if (real_packed_data_has_pack_loaded()) {
			PackedData::get_singleton()->set_disabled(false);
		}
	} else {
		// we need to disable the default create funcs and put our own in
		set_default_file_access();
		if (real_packed_data_has_pack_loaded()) {
			PackedData::get_singleton()->set_disabled(true);
		}
	}
	disabled = p_disabled;
}

bool GDREPackedData::is_disabled() const {
	return disabled;
}

GDREPackedData *GDREPackedData::get_singleton() {
	return singleton;
}

Error GDREPackedData::add_pack(const String &p_path, bool p_replace_files, uint64_t p_offset) {
	if (sources.is_empty()) {
		sources.push_back(memnew(GDREPackedSource));
		sources.push_back(memnew(APKArchive));
	}
	for (int i = 0; i < sources.size(); i++) {
		if (sources[i]->try_open_pack(p_path, p_replace_files, p_offset)) {
			// need to set the default file access to use our own
			set_disabled(false);
			// set_default_file_access();
			return OK;
		}
	}
	return ERR_FILE_UNRECOGNIZED;
}

Ref<FileAccess> GDREPackedData::try_open_path(const String &p_path) {
	PathMD5 pmd5(p_path.md5_buffer());
	HashMap<PathMD5, PackedData::PackedFile, PathMD5>::Iterator E = files.find(pmd5);
	if (!E || E->value.offset == 0) {
		if (is_gdre_file(p_path)) {
			WARN_PRINT("Attempted to load a GDRE resource while another pack is loaded...");
			if (PackedData::get_singleton()) {
				// This works even when PackedData is disabled
				return PackedData::get_singleton()->try_open_path(p_path);
			}
		}
		return Ref<FileAccess>();
	}
	return E->value.src->get_file(p_path, &E->value);
}

bool GDREPackedData::has_path(const String &p_path) {
	bool ret = files.has(PathMD5(p_path.md5_buffer()));
	if (!ret) {
		// We return true if the path is a GDRE resource to ensure that this will be used to load it
		if (is_gdre_file(p_path)) {
			WARN_PRINT("Attempted to check for existence of GDRE resource while another pack is loaded...");
			if (PackedData::get_singleton()) {
				// This works even when PackedData is disabled
				return PackedData::get_singleton()->has_path(p_path);
			}
		}
	}
	return ret;
}
Ref<DirAccess> GDREPackedData::try_open_directory(const String &p_path) {
	Ref<DirAccess> da = memnew(DirAccessGDRE());
	if (da->change_dir(p_path) != OK) {
		da = Ref<DirAccess>();
	}
	return da;
}

bool GDREPackedData::has_directory(const String &p_path) {
	Ref<DirAccess> da = try_open_directory(p_path);
	if (da.is_valid()) {
		return true;
	}
	return false;
}
void GDREPackedData::_free_packed_dirs(GDREPackedData::PackedDir *p_dir) {
	for (const KeyValue<String, PackedDir *> &E : p_dir->subdirs) {
		_free_packed_dirs(E.value);
	}
	memdelete(p_dir);
}

bool GDREPackedData::has_loaded_packs() {
	return !sources.is_empty() && !files.is_empty();
}

// Test for the existence of project.godot or project.binary in the packed data
// This works even when PackedData is disabled
bool GDREPackedData::real_packed_data_has_pack_loaded() {
	return PackedData::get_singleton() && (PackedData::get_singleton()->has_path("res://project.binary") || PackedData::get_singleton()->has_path("res://project.godot"));
}

void GDREPackedData::clear() {
	_clear();
}

void GDREPackedData::_clear() {
	// TODO: refactor the sources to have the option to clear data instead of just deleting them
	for (int i = 0; i < sources.size(); i++) {
		memdelete(sources[i]);
	}
	sources.clear();
	set_disabled(true);
	_free_packed_dirs(root);
	root = memnew(PackedDir);
	files.clear();
}
GDREPackedData::~GDREPackedData() {
	_clear();
}

Error FileAccessGDRE::open_internal(const String &p_path, int p_mode_flags) {
	//try packed data first
	if (!(p_mode_flags & WRITE) && GDREPackedData::get_singleton() && !GDREPackedData::get_singleton()->is_disabled()) {
		proxy = GDREPackedData::get_singleton()->try_open_path(p_path);
		if (proxy.is_valid()) {
			return OK;
		}
	}
	String path = p_path;
	if (is_gdre_file(p_path)) {
		WARN_PRINT("Attempted to open a gdre file while we have a pack loaded...");
		if (GDREPackedData::real_packed_data_has_pack_loaded() && !PackedData::get_singleton()->is_disabled()) {
			proxy = PackedData::get_singleton()->try_open_path(p_path);
			if (proxy.is_valid()) {
				return OK;
			}
		}
	}
	// Otherwise, it's on the file system.
	path = PathFinder::_fix_path_file_access(p_path);
	Error err;
	proxy = _open_filesystem(path, p_mode_flags, &err);
	if (err != OK) {
		proxy = Ref<FileAccess>();
	}
	return err;
}

bool FileAccessGDRE::is_open() const {
	if (proxy.is_null()) {
		return false;
	}
	return proxy->is_open();
}

void FileAccessGDRE::seek(uint64_t p_position) {
	ERR_FAIL_COND_MSG(proxy.is_null(), "File must be opened before use.");
	proxy->seek(p_position);
}

void FileAccessGDRE::seek_end(int64_t p_position) {
	ERR_FAIL_COND_MSG(proxy.is_null(), "File must be opened before use.");
	proxy->seek_end(p_position);
}

uint64_t FileAccessGDRE::get_position() const {
	ERR_FAIL_COND_V_MSG(proxy.is_null(), 0, "File must be opened before use.");
	return proxy->get_position();
}

uint64_t FileAccessGDRE::get_length() const {
	ERR_FAIL_COND_V_MSG(proxy.is_null(), 0, "File must be opened before use.");
	return proxy->get_length();
}

bool FileAccessGDRE::eof_reached() const {
	ERR_FAIL_COND_V_MSG(proxy.is_null(), true, "File must be opened before use.");
	return proxy->eof_reached();
}

uint8_t FileAccessGDRE::get_8() const {
	ERR_FAIL_COND_V_MSG(proxy.is_null(), 0, "File must be opened before use.");
	return proxy->get_8();
}

uint64_t FileAccessGDRE::get_buffer(uint8_t *p_dst, uint64_t p_length) const {
	ERR_FAIL_COND_V_MSG(proxy.is_null(), -1, "File must be opened before use.");
	return proxy->get_buffer(p_dst, p_length);
}

Error FileAccessGDRE::get_error() const {
	ERR_FAIL_COND_V_MSG(proxy.is_null(), ERR_FILE_NOT_FOUND, "File must be opened before use.");
	return proxy->get_error();
}

Error FileAccessGDRE::resize(int64_t p_length) {
	ERR_FAIL_COND_V_MSG(proxy.is_null(), ERR_FILE_NOT_FOUND, "File must be opened before use.");
	return proxy->resize(p_length);
}

void FileAccessGDRE::flush() {
	ERR_FAIL_COND_MSG(proxy.is_null(), "File must be opened before use.");
	return proxy->flush();
}

void FileAccessGDRE::store_8(uint8_t p_dest) {
	ERR_FAIL_COND_MSG(proxy.is_null(), "File must be opened before use.");
	return proxy->store_8(p_dest);
}

void FileAccessGDRE::store_buffer(const uint8_t *p_src, uint64_t p_length) {
	ERR_FAIL_COND_MSG(proxy.is_null(), "File must be opened before use.");
	return proxy->store_buffer(p_src, p_length);
}

bool FileAccessGDRE::file_exists(const String &p_name) {
	if (GDREPackedData::get_singleton() && !GDREPackedData::get_singleton()->is_disabled() && GDREPackedData::get_singleton()->has_path(p_name)) {
		return true;
	}
	if (proxy.is_valid()) {
		return proxy->file_exists(p_name);
	}

	return false;
}

void FileAccessGDRE::close() {
	if (proxy.is_null()) {
		return;
	}
	proxy->close();
}

uint64_t FileAccessGDRE::_get_modified_time(const String &p_file) {
	if (proxy.is_valid()) {
		return proxy->_get_unix_permissions(p_file);
	}
	return 0;
}

BitField<FileAccess::UnixPermissionFlags> FileAccessGDRE::_get_unix_permissions(const String &p_file) {
	if (proxy.is_valid()) {
		return proxy->_get_unix_permissions(p_file);
	}
	return 0;
}

Error FileAccessGDRE::_set_unix_permissions(const String &p_file, BitField<FileAccess::UnixPermissionFlags> p_permissions) {
	if (proxy.is_valid()) {
		return proxy->_set_unix_permissions(p_file, p_permissions);
	}
	return FAILED;
}

bool FileAccessGDRE::_get_hidden_attribute(const String &p_file) {
	if (proxy.is_valid()) {
		return proxy->_get_hidden_attribute(p_file);
	}
	return false;
}

Error FileAccessGDRE::_set_hidden_attribute(const String &p_file, bool p_hidden) {
	if (proxy.is_valid()) {
		return proxy->_set_hidden_attribute(p_file, p_hidden);
	}
	return FAILED;
}

bool FileAccessGDRE::_get_read_only_attribute(const String &p_file) {
	if (proxy.is_valid()) {
		return proxy->_get_read_only_attribute(p_file);
	}
	return false;
}

Error FileAccessGDRE::_set_read_only_attribute(const String &p_file, bool p_ro) {
	if (proxy.is_valid()) {
		return proxy->_set_read_only_attribute(p_file, p_ro);
	}
	return FAILED;
}

// DirAccessGDRE
// This is a copy/paste implementation of DirAccessPack, with the exception of the proxying
// DirAccessPack accessed the PackedData singleton, which we don't want to do.
// Instead, we use the GDREPackedData singleton
// None of the built-in resource loaders use this to load resources, so we do not have to check for "res://gdre_"
Error DirAccessGDRE::list_dir_begin() {
	if (proxy.is_valid()) {
		return proxy->list_dir_begin();
	}
	if (!current) {
		return ERR_UNCONFIGURED;
	}
	list_dirs.clear();
	list_files.clear();

	for (const KeyValue<String, GDREPackedData::PackedDir *> &E : current->subdirs) {
		list_dirs.push_back(E.key);
	}

	for (const String &E : current->files) {
		list_files.push_back(E);
	}

	return OK;
}

String DirAccessGDRE::get_next() {
	if (proxy.is_valid()) {
		return proxy->get_next();
	}
	if (list_dirs.size()) {
		cdir = true;
		String d = list_dirs.front()->get();
		list_dirs.pop_front();
		return d;
	} else if (list_files.size()) {
		cdir = false;
		String f = list_files.front()->get();
		list_files.pop_front();
		return f;
	} else {
		return String();
	}
}

bool DirAccessGDRE::current_is_dir() const {
	if (proxy.is_valid()) {
		return proxy->current_is_dir();
	}
	return cdir;
}

bool DirAccessGDRE::current_is_hidden() const {
	if (proxy.is_valid()) {
		return proxy->current_is_hidden();
	}
	return false;
}

void DirAccessGDRE::list_dir_end() {
	if (proxy.is_valid()) {
		return proxy->list_dir_end();
	}

	list_dirs.clear();
	list_files.clear();
}

int DirAccessGDRE::get_drive_count() {
	if (proxy.is_valid()) {
		return proxy->get_drive_count();
	}

	return 0;
}

String DirAccessGDRE::get_drive(int p_drive) {
	if (proxy.is_valid()) {
		return proxy->get_drive(p_drive);
	}

	return "";
}

int DirAccessGDRE::get_current_drive() {
	if (proxy.is_valid()) {
		return proxy->get_current_drive();
	}
	return 0;
}

bool DirAccessGDRE::drives_are_shortcuts() {
	if (proxy.is_valid()) {
		return proxy->drives_are_shortcuts();
	}
	return false;
}

// internal method
GDREPackedData::PackedDir *DirAccessGDRE::_find_dir(String p_dir) {
	if (!current) {
		return nullptr;
	}
	String nd = p_dir.replace("\\", "/");

	// Special handling since simplify_path() will forbid it
	if (p_dir == "..") {
		return current->parent;
	}

	bool absolute = false;
	if (nd.begins_with("res://")) {
		nd = nd.replace_first("res://", "");
		absolute = true;
	}

	nd = nd.simplify_path();

	if (nd.is_empty()) {
		nd = ".";
	}

	if (nd.begins_with("/")) {
		nd = nd.replace_first("/", "");
		absolute = true;
	}

	Vector<String> paths = nd.split("/");

	GDREPackedData::PackedDir *pd;

	if (absolute) {
		pd = GDREPackedData::get_singleton()->root;
	} else {
		pd = current;
	}

	for (int i = 0; i < paths.size(); i++) {
		String p = paths[i];
		if (p == ".") {
			continue;
		} else if (p == "..") {
			if (pd->parent) {
				pd = pd->parent;
			}
		} else if (pd->subdirs.has(p)) {
			pd = pd->subdirs[p];

		} else {
			return nullptr;
		}
	}

	return pd;
}

Error DirAccessGDRE::change_dir(String p_dir) {
	if (proxy.is_valid()) {
		return proxy->change_dir(p_dir);
	}
	if (!current) {
		return ERR_UNCONFIGURED;
	}
	GDREPackedData::PackedDir *pd = _find_dir(p_dir);
	if (pd) {
		current = pd;
		return OK;
	} else {
		return ERR_INVALID_PARAMETER;
	}
}

String DirAccessGDRE::get_current_dir(bool p_include_drive) const {
	if (proxy.is_valid()) {
		return proxy->get_current_dir(p_include_drive);
	}
	if (!current) {
		return "";
	}
	GDREPackedData::PackedDir *pd = current;
	String p = current->name;

	while (pd->parent) {
		pd = pd->parent;
		p = pd->name.path_join(p);
	}

	return "res://" + p;
}

bool DirAccessGDRE::file_exists(String p_file) {
	if (proxy.is_valid()) {
		return proxy->file_exists(p_file);
	}
	p_file = fix_path(p_file);

	GDREPackedData::PackedDir *pd = _find_dir(p_file.get_base_dir());
	if (!pd) {
		return false;
	}
	return pd->files.has(p_file.get_file());
}

bool DirAccessGDRE::dir_exists(String p_dir) {
	if (proxy.is_valid()) {
		return proxy->dir_exists(p_dir);
	}
	p_dir = fix_path(p_dir);

	return _find_dir(p_dir) != nullptr;
}

bool DirAccessGDRE::is_readable(String p_dir) {
	if (proxy.is_valid()) {
		return proxy->is_writable(p_dir);
	}
	return false;
}

bool DirAccessGDRE::is_writable(String p_dir) {
	if (proxy.is_valid()) {
		return proxy->is_writable(p_dir);
	}
	return false;
}

uint64_t DirAccessGDRE::get_modified_time(String p_file) {
	if (proxy.is_valid()) {
		return proxy->make_dir(p_file);
	}
	return 0;
}

Error DirAccessGDRE::make_dir(String p_dir) {
	if (proxy.is_valid()) {
		return proxy->make_dir(p_dir);
	}
	return ERR_UNAVAILABLE;
}

Error DirAccessGDRE::rename(String p_from, String p_to) {
	if (proxy.is_valid()) {
		return proxy->rename(p_from, p_to);
	}
	return ERR_UNAVAILABLE;
}

Error DirAccessGDRE::remove(String p_name) {
	if (proxy.is_valid()) {
		return proxy->remove(p_name);
	}
	return ERR_UNAVAILABLE;
}

uint64_t DirAccessGDRE::get_space_left() {
	if (proxy.is_valid()) {
		return proxy->get_space_left();
	}
	return 0;
}

String DirAccessGDRE::get_filesystem_type() const {
	if (proxy.is_valid()) {
		return proxy->get_filesystem_type();
	}
	return "PCK";
}
/**
 * 	virtual bool is_link(String p_file) override;
	virtual String read_link(String p_file) override;
	virtual Error create_link(String p_source, String p_target) override;
*/
bool DirAccessGDRE::is_link(String p_file) {
	if (proxy.is_valid()) {
		return proxy->is_link(p_file);
	}
	return false;
}

String DirAccessGDRE::read_link(String p_file) {
	if (proxy.is_valid()) {
		return proxy->read_link(p_file);
	}
	return "";
}

Error DirAccessGDRE::create_link(String p_source, String p_target) {
	if (proxy.is_valid()) {
		String new_source;
		return proxy->create_link(p_source, p_target);
	}
	return ERR_UNAVAILABLE;
}

DirAccessGDRE::DirAccessGDRE() {
	if (GDREPackedData::get_singleton()->is_disabled() || !GDREPackedData::get_singleton()->has_loaded_packs()) {
		proxy = _open_filesystem();
		current = nullptr;
	} else {
		current = GDREPackedData::get_singleton()->root;
		proxy = Ref<DirAccess>();
	}
}

DirAccessGDRE::~DirAccessGDRE() {
}

// At the end so that the header files that they pull in don't screw up the above.
#ifdef WINDOWS_ENABLED
#include "drivers/windows/dir_access_windows.h"
#include "drivers/windows/file_access_windows.h"
#else // UNIX_ENABLED -- covers OSX, Linux, FreeBSD, Web.
#include "drivers/unix/dir_access_unix.h"
#include "drivers/unix/file_access_unix.h"
#ifdef MACOS_ENABLED
#include "platform/macos/dir_access_macos.h"
#endif
#ifdef ANDROID_ENABLED
#include "platform/android/dir_access_jandroid.h"
#endif
#endif
// static FileAccess::CreateFunc default_file_res_create_func{ nullptr };
// enum DirAccessType {
// 	UNKNOWN = -1,
// 	UNIX,
// 	PACK,
// 	WINDOWS,
// 	MACOS,
// 	ANDROID
// };
// static DirAccessType default_dir_res_create_type = UNKNOWN;

String FileAccessGDRE::fix_path(const String &p_path) const {
	return PathFinder::_fix_path_file_access(p_path.replace("\\", "/"));
}

String DirAccessGDRE::fix_path(const String &p_path) const {
	return PathFinder::_fix_path_file_access(p_path);
}

Ref<DirAccess> DirAccessGDRE::_open_filesystem() {
	// DirAccessGDRE is only made default for DirAccess::ACCESS_RESOURCES
	String path = GDRESettings::get_singleton()->get_project_path();
	if (path == "") {
		path = "res://";
	}
#if defined(WINDOWS_ENABLED)
	Ref<DirAccessProxy<DirAccessWindows>> dir_proxy = memnew(DirAccessProxy<DirAccessWindows>);
#elif defined(MACOS_ENABLED)
	Ref<DirAccessProxy<DirAccessMacOS>> dir_proxy = memnew(DirAccessProxy<DirAccessMacOS>);
#elif defined(ANDROID_ENABLED)
	// TODO: Handle apk expansion!!
	Ref<DirAccessProxy<DirAccessJAndroid>> dir_proxy = memnew(DirAccessProxy<DirAccessJAndroid>);
#else defined(UNIX_ENABLED) // -- covers OSX, Linux, FreeBSD, Web.
	Ref<DirAccessProxy<DirAccessUnix>> dir_proxy = memnew(DirAccessProxy<DirAccessUnix>);
#endif
	dir_proxy->change_dir(path);
	return dir_proxy;
}

void GDREPackedData::set_default_file_access() {
	FileAccess::make_default<FileAccessGDRE>(FileAccess::ACCESS_RESOURCES);
	FileAccess::make_default<FileAccessGDRE>(FileAccess::ACCESS_USERDATA); // for user:// files in the pack
	DirAccess::make_default<DirAccessGDRE>(DirAccess::ACCESS_RESOURCES);
}

void GDREPackedData::reset_default_file_access() {
	// we need to check to see if the real PackedData has the GDRE packed data loaded
	// if it does, we need to reset the default DirAccess to DirAccessPack
	// (FileAccessPack is never set to the default for ACCESS_RESOURCES)
	if (GDREPackedData::real_packed_data_has_pack_loaded()) {
		DirAccess::make_default<DirAccessPack>(DirAccess::ACCESS_RESOURCES);
	} else {
#if defined(WINDOWS_ENABLED)
		DirAccess::make_default<DirAccessWindows>(DirAccess::ACCESS_RESOURCES);
#elif defined(MACOS_ENABLED)
		DirAccess::make_default<DirAccessMacOS>(DirAccess::ACCESS_RESOURCES);
#elif defined(ANDROID_ENABLED)
		// TODO: Handle apk expansion!!
		DirAccess::make_default<DirAccessJAndroid>(DirAccess::ACCESS_RESOURCES);
#else defined(UNIX_ENABLED) // -- covers OSX, Linux, FreeBSD, Web.
		DirAccess::make_default<DirAccessUnix>(DirAccess::ACCESS_RESOURCES);
#endif
	}
#if defined(WINDOWS_ENABLED)
	FileAccess::make_default<FileAccessWindows>(FileAccess::ACCESS_RESOURCES);
	FileAccess::make_default<FileAccessWindows>(FileAccess::ACCESS_USERDATA);

#elif defined(ANDROID_ENABLED)
	FileAccess::make_default<FileAccessAndroid>(FileAccess::ACCESS_RESOURCES);
	FileAccess::make_default<FileAccessAndroid>(FileAccess::ACCESS_USERDATA);
#else
	FileAccess::make_default<FileAccessUnix>(FileAccess::ACCESS_RESOURCES);
	FileAccess::make_default<FileAccessUnix>(FileAccess::ACCESS_USERDATA);
#endif
}

Ref<FileAccess> FileAccessGDRE::_open_filesystem(const String &p_path, int p_mode_flags, Error *r_error) {
#ifdef WINDOWS_ENABLED
	Ref<FileAccessProxy<FileAccessWindows>> file_proxy = memnew(FileAccessProxy<FileAccessWindows>);
#elif defined(ANDROID_ENABLED)
	Ref<FileAccessProxy<FileAccessAndroid>> file_proxy = memnew(FileAccessProxy<FileAccessAndroid>);
#else // UNIX_ENABLED -- covers OSX, Linux, FreeBSD, Web.
	Ref<FileAccessProxy<FileAccessUnix>> file_proxy = memnew(FileAccessProxy<FileAccessUnix>);
#endif
	Error err = file_proxy->open_internal(p_path, p_mode_flags);
	if (r_error) {
		*r_error = err;
	}
	if (err != OK) {
		file_proxy.unref();
	}
	return file_proxy;
}

String PathFinder::_fix_path_file_access(const String &p_path) {
	if (p_path.begins_with("res://")) {
		if (p_path.get_file().begins_with("gdre_")) {
			WARN_PRINT("WARNING: Calling fix_path on a gdre file...");
			String res_path = GDRESettings::get_singleton()->get_gdre_resource_path();
			if (res_path != "") {
				return p_path.replace("res:/", res_path);
			}
		}
		String project_path = GDRESettings::get_singleton()->get_project_path();
		if (project_path != "") {
			return p_path.replace("res:/", project_path);
		}
		return p_path.replace_first("res://", "");
	} else if (p_path.begins_with("user://")) { // Some packs have user files in them, so we need to check for those
		if (p_path.get_file().begins_with("gdre_")) {
			WARN_PRINT("WARNING: Calling fix_path on a gdre file...");
			// TODO: Once we start adding user files, we'll have to handle this; for right now, it doesn't particularly matter
		}

		// check if the file is in the PackedData first
		if (GDREPackedData::get_singleton() && !GDREPackedData::get_singleton()->is_disabled() && GDREPackedData::get_singleton()->has_path(p_path)) {
			return p_path.replace_first("user://", "");
		}
		String data_dir = OS::get_singleton()->get_user_data_dir();
		if (!data_dir.is_empty()) {
			return p_path.replace("user:/", data_dir);
		}
		return p_path.replace("user://", "");

		// otherwise, fall through to call base class
	}

	// TODO: This will have to be modified if additional fix_path overrides are added
#ifdef WINDOWS_ENABLED
#ifndef MAX_PATH
#define MAX_PATH 260
#endif
	String r_path = p_path;
	if (r_path.is_absolute_path() && !r_path.is_network_share_path() && r_path.length() > MAX_PATH) {
		r_path = "\\\\?\\" + r_path.replace("/", "\\");
	}
	return r_path;
#endif

	return p_path;
}
