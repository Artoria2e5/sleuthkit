/*
 ** The Sleuth Kit
 **
 ** Brian Carrier [carrier <at> sleuthkit [dot] org]
 ** Copyright (c) 2010-2011 Brian Carrier.  All Rights reserved
 **
 ** This software is distributed under the Common Public License 1.0
 **
 */

/**
 * \file db_sqlite.cpp
 * Contains code to perform operations against SQLite database.
 */

#include "tsk_db_sqlite.h"
#include "sqlite3.h"
#include <string.h>


#define TSK_SCHEMA_VER 2

/**
 * Set the locations and logging object.  Must call
 * initialize() before the object can be used.
 */
TskDbSqlite::TskDbSqlite(const char *a_dbFilePathUtf8, bool a_blkMapFlag)
{
    strncpy(m_dbFilePathUtf8, a_dbFilePathUtf8, 1024);
    m_utf8 = true;
    m_blkMapFlag = a_blkMapFlag;
    m_db = NULL;
    m_selectFileIdByMetaAddr = NULL;
}

#ifdef TSK_WIN32
//@@@@
TskDbSqlite::TskDbSqlite(const TSK_TCHAR * a_dbFilePath, bool a_blkMapFlag)
{
    wcsncpy(m_dbFilePath, a_dbFilePath, 1024);
    m_utf8 = false;
    m_blkMapFlag = a_blkMapFlag;
    m_db = NULL;
    m_selectFileIdByMetaAddr = NULL;
}
#endif

TskDbSqlite::~TskDbSqlite()
{
    (void) close();
}

/*
 * Close the Sqlite database.
 * Return 0 on success, 1 on failure
 */
int
 TskDbSqlite::close()
{

    if (m_db) {
        sqlite3_finalize(m_selectFileIdByMetaAddr);     // calling on NULL is okay
        sqlite3_close(m_db);
        m_db = NULL;
    }
    return 0;
}


int
 TskDbSqlite::attempt(int resultCode, int expectedResultCode,
    const char *errfmt)
{
    if (resultCode != expectedResultCode) {
        tsk_error_reset();
        tsk_error_set_errno(TSK_ERR_AUTO_DB);
        tsk_error_set_errstr(errfmt, sqlite3_errmsg(m_db), resultCode);
        return 1;
    }
    return 0;
}


int
 TskDbSqlite::attempt(int resultCode, const char *errfmt)
{
    return attempt(resultCode, SQLITE_OK, errfmt);
}



int
 TskDbSqlite::attempt_exec(const char *sql, int (*callback) (void *, int,
        char **, char **), void *callback_arg, const char *errfmt)
{
    char *
        errmsg;

    if (!m_db)
        return 1;

    if (sqlite3_exec(m_db, sql, callback, callback_arg,
            &errmsg) != SQLITE_OK) {
        tsk_error_reset();
        tsk_error_set_errno(TSK_ERR_AUTO_DB);
        tsk_error_set_errstr(errfmt, errmsg);
        sqlite3_free(errmsg);
        tsk_error_print(stderr);
        return 1;
    }
    return 0;
}

int
 TskDbSqlite::attempt_exec(const char *sql, const char *errfmt)
{
    return attempt_exec(sql, NULL, NULL, errfmt);
}


int
 TskDbSqlite::prepare_stmt(const char *sql, sqlite3_stmt ** ppStmt)
{
    if (sqlite3_prepare_v2(m_db, sql, -1, ppStmt, NULL) != SQLITE_OK) {
        tsk_error_reset();
        tsk_error_set_errno(TSK_ERR_AUTO_DB);
        tsk_error_set_errstr("Error preparing SQL statement: %s\n", sql);
        tsk_error_print(stderr);
        return 1;
    }
    return 0;
}



int
 TskDbSqlite::addObject(DB_OBJECT_TYPES type, int64_t parObjId,
    int64_t & objId)
{
    char
        stmt[1024];

    snprintf(stmt, 1024,
        "INSERT INTO tsk_objects (obj_id, par_obj_id, type) VALUES (NULL, %lld, %d);",
        parObjId, type);
    if (attempt_exec(stmt, "Error adding data to tsk_objects table: %s\n")) {
        return 1;
    }

    objId = sqlite3_last_insert_rowid(m_db);

    return 0;
}





/** 
 * Initialize the open DB: set PRAGMAs, create tables and indexes
 * @returns 1 on error
 */
int
 TskDbSqlite::initialize()
{
    char
        foo[1024];

    // disable synchronous for loading the DB since we have no crash recovery anyway...
    if (attempt_exec("PRAGMA synchronous =  OFF;",
            "Error setting PRAGMA synchronous: %s\n")) {
        return 1;
    }

    if (attempt_exec
        ("CREATE TABLE tsk_db_info (schema_ver INTEGER, tsk_ver INTEGER);",
            "Error creating tsk_db_info table: %s\n")) {
        return 1;
    }

    snprintf(foo, 1024,
        "INSERT INTO tsk_db_info (schema_ver, tsk_ver) VALUES (%d, %d);",
        TSK_SCHEMA_VER, TSK_VERSION_NUM);
    if (attempt_exec(foo, "Error adding data to tsk_db_info table: %s\n")) {
        return 1;
    }

    if (attempt_exec
        ("CREATE TABLE tsk_objects (obj_id INTEGER PRIMARY KEY, par_obj_id INTEGER, type INTEGER);",
            "Error creating tsk_objects table: %s\n")
        ||
        attempt_exec
        ("CREATE TABLE tsk_image_info (obj_id INTEGER, type INTEGER, ssize INTEGER);",
            "Error creating tsk_image_info table: %s\n")
        ||
        attempt_exec
        ("CREATE TABLE tsk_image_names (obj_id INTEGER, name TEXT, sequence INTEGER);",
            "Error creating tsk_image_names table: %s\n")
        ||
        attempt_exec
        ("CREATE TABLE tsk_vs_info (obj_id INTEGER, vs_type INTEGER, img_offset INTEGER NOT NULL, block_size INTEGER NOT NULL);",
            "Error creating tsk_vs_info table: %s\n")
        ||
        attempt_exec
        ("CREATE TABLE tsk_vs_parts (obj_id INTEGER PRIMARY KEY, addr INTEGER, start INTEGER NOT NULL, length INTEGER NOT NULL, desc TEXT, flags INTEGER);",
            "Error creating tsk_vol_info table: %s\n")
        ||
        attempt_exec
        ("CREATE TABLE tsk_fs_info (obj_id INTEGER PRIMARY KEY, img_offset INTEGER, fs_type INTEGER, block_size INTEGER, block_count INTEGER, root_inum INTEGER, first_inum INTEGER, last_inum INTEGER);",
            "Error creating tsk_fs_info table: %s\n")
        ||
        attempt_exec
        ("CREATE TABLE tsk_files (fs_obj_id INTEGER NOT NULL, obj_id INTEGER NOT NULL UNIQUE, attr_type INTEGER, attr_id INTEGER, name TEXT NOT NULL, meta_addr INTEGER, type INTEGER, has_layout INTEGER, has_path INTEGER, dir_type INTEGER, meta_type INTEGER, dir_flags INTEGER, meta_flags INTEGER, size INTEGER, ctime INTEGER, crtime INTEGER, atime INTEGER, mtime INTEGER, mode INTEGER, uid INTEGER, gid INTEGER);",
            "Error creating tsk_fs_files table: %s\n")
        ||
        attempt_exec
        ("CREATE TABLE tsk_files_path (obj_id INTEGER, path TEXT)",
            "Error creating tsk_files_path table: %s\n")
        ||
        attempt_exec
        ("CREATE TABLE tsk_files_derived (obj_id INTEGER UNIQUE, derived_id INTEGER, rederive TEXT)",
            "Error creating tsk_files_derived table: %s\n")
        ||
        attempt_exec
        ("CREATE TABLE tsk_files_derived_method (derived_id INTEGER PRIMARY KEY, tool_name TEXT, tool_version TEXT, other TEXT)",
            "Error creating tsk_files_derived_method table: %s\n")) {
        return 1;
    }

    if (m_blkMapFlag) {
        if (attempt_exec
            ("CREATE TABLE tsk_file_layout (fs_id INTEGER NOT NULL, byte_start INTEGER NOT NULL, byte_len INTEGER NOT NULL, obj_id);",
                "Error creating tsk_fs_blocks table: %s\n")) {
            return 1;
        }
    }

    if (createIndexes())
        return 1;


    return 0;
}

int
 TskDbSqlite::createIndexes()
{
    return
        attempt_exec("CREATE INDEX parObjId ON tsk_objects(par_obj_id);",
        "Error creating tsk_objects index on par_obj_id: %s\n");
}


/*
 * If the database file exists this method will open it otherwise
 * it will create a new database. 
 */
int
 TskDbSqlite::open()
{

    if (m_utf8) {
        if (attempt(sqlite3_open(m_dbFilePathUtf8, &m_db),
                "Can't open database: %s\n")) {
            sqlite3_close(m_db);
            return 1;
        }
    }
    else {
        if (attempt(sqlite3_open16(m_dbFilePath, &m_db),
                "Can't open database: %s\n")) {
            sqlite3_close(m_db);
            return 1;
        }
    }

    return 0;
}

/**
 * Must be called on an intialized database, before adding any content to it.
 */
int
 TskDbSqlite::setup()
{
    if (prepare_stmt
        ("SELECT obj_id FROM tsk_files WHERE meta_addr IS ? AND fs_obj_id IS ?",
            &m_selectFileIdByMetaAddr)) {
        return 1;
    }

    return 0;
}


/**
 * Must be called after adding content to the database.
 */
int
 TskDbSqlite::cleanup()
{
    if (m_selectFileIdByMetaAddr != NULL) {
        sqlite3_finalize(m_selectFileIdByMetaAddr);
        m_selectFileIdByMetaAddr = NULL;
    }
    return 1;
}

int
 TskDbSqlite::addImageInfo(int type, int size, int64_t & objId)
{
    char
        stmt[1024];

    snprintf(stmt, 1024,
        "INSERT INTO tsk_objects (obj_id, par_obj_id, type) VALUES (NULL, NULL, %d);",
        DB_OBJECT_TYPE_IMG);
    if (attempt_exec(stmt, "Error adding data to tsk_objects table: %s\n"))
        return 1;

    objId = sqlite3_last_insert_rowid(m_db);

    snprintf(stmt, 1024,
        "INSERT INTO tsk_image_info (obj_id, type, ssize) VALUES (%lld, %d, %d);",
        objId, type, size);
    return attempt_exec(stmt,
        "Error adding data to tsk_image_info table: %s\n");
}

int
 TskDbSqlite::addImageName(int64_t objId, char const *imgName,
    int sequence)
{
    char
        stmt[1024];

    snprintf(stmt, 1024,
        "INSERT INTO tsk_image_names (obj_id, name, sequence) VALUES (%lld, '%s', %d)",
        objId, imgName, sequence);

    return attempt_exec(stmt,
        "Error adding data to tsk_image_names table: %s\n");
}


int
 TskDbSqlite::addVsInfo(const TSK_VS_INFO * vs_info, int64_t parObjId,
    int64_t & objId)
{
    char
        stmt[1024];

    if (addObject(DB_OBJECT_TYPE_VS, parObjId, objId))
        return 1;

    snprintf(stmt, 1024,
        "INSERT INTO tsk_vs_info (obj_id, vs_type, img_offset, block_size) VALUES (%lld, %d,%"
        PRIuOFF ",%d)", objId, vs_info->vstype, vs_info->offset,
        vs_info->block_size);

    return attempt_exec(stmt,
        "Error adding data to tsk_vs_info table: %s\n");
}





/**
 * Adds the sector addresses of the volumes into the db.
 */
int
 TskDbSqlite::addVolumeInfo(const TSK_VS_PART_INFO * vs_part,
    int64_t parObjId, int64_t & objId)
{
    char
        stmt[1024];

    if (addObject(DB_OBJECT_TYPE_VOL, parObjId, objId))
        return 1;

    snprintf(stmt, 1024,
        "INSERT INTO tsk_vs_parts (obj_id, addr, start, length, desc, flags)"
        "VALUES (%lld, %" PRIuPNUM ",%" PRIuOFF ",%" PRIuOFF ",'%s',%d)",
        objId, (int) vs_part->addr, vs_part->start, vs_part->len,
        vs_part->desc, vs_part->flags);

    return attempt_exec(stmt,
        "Error adding data to tsk_vs_parts table: %s\n");
}

int
 TskDbSqlite::addFsInfo(const TSK_FS_INFO * fs_info, int64_t parObjId,
    int64_t & objId)
{
    char
        stmt[1024];

    if (addObject(DB_OBJECT_TYPE_FS, parObjId, objId))
        return 1;

    snprintf(stmt, 1024,
        "INSERT INTO tsk_fs_info (obj_id, img_offset, fs_type, block_size, block_count, "
        "root_inum, first_inum, last_inum) "
        "VALUES ("
        "%lld,%" PRIuOFF ",%d,%u,%" PRIuDADDR ","
        "%" PRIuINUM ",%" PRIuINUM ",%" PRIuINUM ")",
        objId, fs_info->offset, (int) fs_info->ftype, fs_info->block_size,
        fs_info->block_count, fs_info->root_inum, fs_info->first_inum,
        fs_info->last_inum);

    return attempt_exec(stmt,
        "Error adding data to tsk_fs_info table: %s\n");
}

// ?????
//int TskDbSqlite::addCarvedFile(TSK_FS_FILE * fs_file,
//    const TSK_FS_ATTR * fs_attr, const char *path, int64_t fsObjId, int64_t parObjId, int64_t & objId)
//{
//
//    return addFile(fs_file, fs_attr, path, fsObjId, parObjId, objId);
//}


int
 TskDbSqlite::addFsFile(TSK_FS_FILE * fs_file,
    const TSK_FS_ATTR * fs_attr, const char *path, int64_t fsObjId,
    int64_t & objId)
{
    int64_t
        parObjId;

    if (fs_file->name == NULL)
        return 0;

    if (fs_file->fs_info->root_inum == fs_file->name->meta_addr) {
        // this entry is for root directory
        parObjId = fsObjId;
    }
    else {

        // Find the parent file id in the database using the parent metadata address
        if (attempt(sqlite3_reset(m_selectFileIdByMetaAddr),
                "Error reseting 'select file id by meta_addr' statement: %s\n")
            || attempt(sqlite3_bind_int64(m_selectFileIdByMetaAddr, 1,
                    fs_file->name->par_addr),
                "Error binding meta_addr to statment: %s (result code %d)\n")
            || attempt(sqlite3_bind_int64(m_selectFileIdByMetaAddr, 2,
                    fsObjId),
                "Error binding fs_obj_id to statment: %s (result code %d)\n")
            || attempt(sqlite3_step(m_selectFileIdByMetaAddr), SQLITE_ROW,
                "Error selecting file id by meta_addr: %s (result code %d)\n"))
        {
            return 1;
        }

        parObjId = sqlite3_column_int64(m_selectFileIdByMetaAddr, 0);
    }


    return addFile(fs_file, fs_attr, path, fsObjId, parObjId, objId);
}


/*
 * Add file data to the file table
 * Return 0 on success, 1 on error.
 */
int
 TskDbSqlite::addFile(TSK_FS_FILE * fs_file,
    const TSK_FS_ATTR * fs_attr, const char *path, int64_t fsObjId,
    int64_t parObjId, int64_t & objId)
{


    char
     foo[1024];
    int
     mtime = 0;
    int
     crtime = 0;
    int
     ctime = 0;
    int
     atime = 0;
    TSK_OFF_T
        size = 0;
    int
     meta_type = 0;
    int
     meta_flags = 0;
    int
     meta_mode = 0;
    int
     gid = 0;
    int
     uid = 0;
    int
        type = 0;
    int
        idx = 0;

    if (fs_file->name == NULL)
        return 0;

    if (fs_file->meta) {
        mtime = fs_file->meta->mtime;
        atime = fs_file->meta->atime;
        ctime = fs_file->meta->ctime;
        crtime = fs_file->meta->crtime;
        size = fs_file->meta->size;
        meta_type = fs_file->meta->type;
        meta_flags = fs_file->meta->flags;
        meta_mode = fs_file->meta->mode;
        gid = fs_file->meta->gid;
        uid = fs_file->meta->uid;
    }

    size_t
        attr_nlen = 0;
    if (fs_attr) {
        type = fs_attr->type;
        idx = fs_attr->id;
        if (fs_attr->name) {
            if ((fs_attr->type != TSK_FS_ATTR_TYPE_NTFS_IDXROOT) ||
                (strcmp(fs_attr->name, "$I30") != 0)) {
                attr_nlen = strlen(fs_attr->name);
            }
        }
    }

    // clean up special characters in name before we insert
    size_t
        len = strlen(fs_file->name->name);
    char *
        name;
    size_t
        nlen = 2 * (len + attr_nlen);
    if ((name = (char *) tsk_malloc(nlen + 1)) == NULL) {
        return 1;
    }

    size_t
        j = 0;
    for (size_t i = 0; i < len && j < nlen; i++) {
        // ' is special in SQLite
        if (fs_file->name->name[i] == '\'') {
            name[j++] = '\'';
            name[j++] = '\'';
        }
        else {
            name[j++] = fs_file->name->name[i];
        }
    }

    // Add the attribute name
    if (attr_nlen > 0) {
        name[j++] = ':';

        for (unsigned i = 0; i < attr_nlen && j < nlen; i++) {
            // ' is special in SQLite
            if (fs_attr->name[i] == '\'') {
                name[j++] = '\'';
                name[j++] = '\'';
            }
            else {
                name[j++] = fs_attr->name[i];
            }
        }
    }

    if (addObject(DB_OBJECT_TYPE_FILE, parObjId, objId))
        return 1;

    snprintf(foo, 1024,
        "INSERT INTO tsk_files (fs_obj_id, obj_id, type, attr_type, attr_id, name, meta_addr, dir_type, meta_type, dir_flags, meta_flags, size, crtime, ctime, atime, mtime, mode, gid, uid) "
        "VALUES ("
        "%lld,%lld,"
        "%d,"
        "%d,%d,'%s',"
        "%" PRIuINUM ","
        "%d,%d,%d,%d,"
        "%" PRIuOFF ","
        "%d,%d,%d,%d,%d,%d,%d)",
        fsObjId, objId,
        DB_FILES_TYPE_FS,
        type, idx, name,
        fs_file->name->meta_addr,
        fs_file->name->type, meta_type, fs_file->name->flags, meta_flags,
        size, crtime, ctime, atime, mtime, meta_mode, gid, uid);

    if (attempt_exec(foo, "Error adding data to tsk_fs_files table: %s\n")) {
        free(name);
        return 1;
    }

    free(name);
    return 0;
}

int
 TskDbSqlite::begin()
{
    return attempt_exec("BEGIN",
        "Error using BEGIN for insert transaction: %s\n");
}

int
 TskDbSqlite::commit()
{
    return attempt_exec("COMMIT",
        "Error using COMMIT for insert transaction: %s\n");
}


int
 TskDbSqlite::savepoint(const char *name)
{
    char
        buff[1024];

    snprintf(buff, 1024, "SAVEPOINT %s", name);

    return attempt_exec(buff, "Error setting savepoint: %s\n");
}

int
 TskDbSqlite::rollbackSavepoint(const char *name)
{
    char
        buff[1024];

    snprintf(buff, 1024, "ROLLBACK TO SAVEPOINT %s", name);

    return attempt_exec(buff, "Error rolling back savepoint: %s\n");
}

int
 TskDbSqlite::releaseSavepoint(const char *name)
{
    char
        buff[1024];

    snprintf(buff, 1024, "RELEASE SAVEPOINT %s", name);

    return attempt_exec(buff, "Error releasing savepoint: %s\n");
}




/**
 * Add block info to the database.  This table stores the run information for each file so that we
 * can map which blocks are used by what files.
 * @param a_fsObjId Id that the file is located in
 * @param a_fileObjId ID of the file
 * @param a_byteStart Byte address relative to the start of the image file
 * @param a_byteLen Length of the run in bytes
 * @returns 1 on error
 */
int
 TskDbSqlite::addFsBlockInfo(int64_t a_fsObjId, int64_t a_fileObjId,
    uint64_t a_byteStart, uint64_t a_byteLen)
{
    char
        foo[1024];

    snprintf(foo, 1024,
        "INSERT INTO tsk_file_layout (fs_id, byte_start, byte_len, obj_id) VALUES (%lld, %lld, %llu, %llu)",
        a_fsObjId, a_byteStart, a_byteLen, a_fileObjId);

    return attempt_exec(foo,
        "Error adding data to tsk_fs_info table: %s\n");
}


/**
 * Adds information about a carved file into the database.
 * @param size Number of bytes in file
 * @param runStarts Array with starting sector (relative to start of image) for each run in file.
 * @param runLengths Array with number of sectors in each run 
 * @param numRuns Number of entries in previous arrays
 * @param fileId Carved file Id (output)
 * @returns 0 on success or -1 on error.
 */
int
 TskDbSqlite::addCarvedFileInfo(int fsObjId, const char *fileName,
    uint64_t size, int64_t & objId)
{
    char
        foo[1024];

    // clean up special characters in name before we insert
    size_t
        len = strlen(fileName);
    char *
        name;
    size_t
        nlen = 2 * (len);
    if ((name = (char *) tsk_malloc(nlen + 1)) == NULL) {
        return 1;
    }

    size_t
        j = 0;
    for (size_t i = 0; i < len && j < nlen; i++) {
        // ' is special in SQLite
        if (fileName[i] == '\'') {
            name[j++] = '\'';
            name[j++] = '\'';
        }
        else {
            name[j++] = fileName[i];
        }
    }

    if (addObject(DB_OBJECT_TYPE_FILE, fsObjId, objId))
        return 1;

    snprintf(foo, 1024,
        "INSERT INTO tsk_files (fs_obj_id, obj_id, type, attr_type, attr_id, name, meta_addr, dir_type, meta_type, dir_flags, meta_flags, size, crtime, ctime, atime, mtime, mode, gid, uid) "
        "VALUES ("
        "%d,%lld,"
        "%d,"
        "NULL,NULL,'%s',"
        "NULL,"
        "%d,%d,%d,%d,"
        "%" PRIuOFF ","
        "NULL,NULL,NULL,NULL,NULL,NULL,NULL)",
        fsObjId, objId,
        DB_FILES_TYPE_CARVED,
        name,
        TSK_FS_NAME_TYPE_REG, TSK_FS_META_TYPE_REG,
        TSK_FS_NAME_FLAG_UNALLOC, TSK_FS_NAME_FLAG_UNALLOC, size);

    if (attempt_exec(foo, "Error adding data to tsk_fs_files table: %s\n")) {
        free(name);
        return 1;
    }

    free(name);
    return 0;
}



bool TskDbSqlite::dbExist() const
{
    if (m_db)
        return true;
    else
        return false;
}
