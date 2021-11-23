/*
 *  Copyright (C) 2020 Hesham Almatary <hesham.almatary@cl.cam.ac.uk>
 *  COPYRIGHT (c) 2018 Chris Johns <chrisj@rtems.org>
 *
 *  The license and distribution terms for this file may be
 *  found in the file LICENSE in this distribution or at
 *  http://www.rtems.org/license/LICENSE.
 */
/**
 * @file
 *
 * @ingroup rtems_rtl
 *
 * @brief RTEMS Run-Time Linker Archive
 *
 * The RTL Archive module manages dependent loading of object files from
 * archives. The archives need to have a `ranlib` generated symbol table.
 *
 * This module reads a configuration file called `rtl-libs.conf` from a default
 * directory of `/etc`. The file is a line per glob'ed path to archives to
 * search for symbols.
 *
 * The archive symbols are held in a per archive cache for searching.
 *
 * @note Errors in the reading of a config file, locating archives, reading
 *       symbol tables and loading object files are not considered RTL error
 *       reported to a user. The user error is undefined symbols.
 */

#if !defined (_RTEMS_RTL_ARCHIVE_H_)
#define _RTEMS_RTL_ARCHIVE_H_

#include <FreeRTOS.h>
#include <FreeRTOSConfig.h>
#include "list.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * Flags for archives.
 */
#define RTEMS_RTL_ARCHIVE_USER_LOAD (1 << 0) /**< User forced load. */
#define RTEMS_RTL_ARCHIVE_REMOVE    (1 << 1) /**< The achive is not found. */
#define RTEMS_RTL_ARCHIVE_LOAD      (1 << 2) /**< Load the achive. */

/**
 * Symbol search and loading results.
 */
typedef enum rtems_rtl_archive_search
{
  rtems_rtl_archive_search_not_found = 0, /**< The search failed to find the
                                               symbol. */
  rtems_rtl_archive_search_found = 1,     /**< The symbols was found. */
  rtems_rtl_archive_search_loaded = 2,    /**< The symbol was found and the
                                               object file has been loaded */
  rtems_rtl_archive_search_error = 3,     /**< There was an error searching or
                                               loading. */
  rtems_rtl_archive_search_no_config = 4 /**< There is no config or it is
                                          *   invalid. */
} rtems_rtl_archive_search;

/**
 * RTL Archive symbols.
 */
typedef struct rtems_rtl_archive_symbol
{
  size_t      entry;  /**< Index in the symbol offset table. */
  const char* label;  /**< The symbol's label. */
} rtems_rtl_archive_symbol;

/**
 * RTL Archive symbols.
 */
typedef struct rtems_rtl_archive_symbols
{
  void*                     base;     /**< Base of the symbol table. */
  size_t                    size;     /**< Size of the symbol table. */
  size_t                    entries;  /**< Entries in the symbol table. */
  const char*               names;    /**< Start of the symbol names. */
  rtems_rtl_archive_symbol* symbols;  /**< Sorted symbol table. */
} rtems_rtl_archive_symbols;

/**
 * RTL Archive data.
 */
typedef struct rtems_rtl_archive
{
  ListItem_t                node;     /**< Chain link. */
  const char*               name;     /**< Archive absolute path. */
  size_t                    size;     /**< Size of the archive. */
  UBaseType_t               mtime;    /**< Archive's last modified time. */
  UBaseType_t               enames;   /**< Extended file name offset, lazy load. */
  rtems_rtl_archive_symbols symbols;  /**< Ranlib symbol table. */
  size_t                    refs;     /**< Loaded object modules. */
  uint32_t                  flags;    /**< Some flags. */
#if configCHERI_COMPARTMENTALIZATION_MODE == 2
  void**                   captable;  /* Capability table per library */
  #if configCHERI_COMPARTMENTALIZATION_FAULT_RESTART
    void**                 captable_clone;     /* Capability table per library */
  #endif
  size_t                   captable_free_slot; /* The next free slot in cap table */
  size_t                   caps_count;         /* The number of capabilities */
  size_t                   comp_id;            /* ID of an archive compartment */
  bool                     (*faultHandler)(void*, uint32_t); /* Compartment fault handler */
  void*                    pCompResTable; /* Per Compartment FreeRTOS resource table */
#elif configMPU_COMPARTMENTALIZATION == 2
  size_t                   captable[configMPU_REGIONS_NUM][3]; /* MPU region table per archive */
  size_t                   comp_id;                            /* ID of an archive compartment */
  bool                     (*faultHandler)(void* , uint32_t);  /* Compartment fault handler */
  void*                    pCompResTable;                      /* Per Compartment FreeRTOS resource table */
#endif
} rtems_rtl_archive;

/**
 * RTL Archive data.
 */
typedef struct rtems_rtl_archives
{
  const char*         config_name;    /**< Config file name. */
  UBaseType_t         config_mtime;   /**< Config last modified time. */
  size_t              config_length;  /**< Length the config data. */
  char*               config;         /**< Config file contents. */
  List_t              archives;       /**< The located archives. */
} rtems_rtl_archives;

/*
 * Find an object file in archive that contains the symbol we are
 * searching for.
 *
 * The symbol search is performance sensitive. The archive's symbol table being
 * searched is the symbol table in the archive created by ranlib. This table is
 * not sorted so a sorted table of pointeres to the symbols is generated after
 * loading if there are enough symbols. For small symbol tables the search is
 * linear. The entire table is held in memory. At the time of writing this code
 * the symbol table for the SPARC architecture's libc is 16k.
 *
 * The ranlib table is:
 *
 *    [4]                - size of table in bytes
 *    [0..(entries x 4)] - 4 byte binary offsets into the archive
 *                         for each symbol
 *    [0..m]             - variable length table of strings, nul
 *                         separated and sorted
 *
 * Note: The loading of an object file from an archive uses an offset in the
 *       file name to speed the loading.
 */
typedef struct rtems_rtl_archive_obj_data
{
  const char*        symbol;   /**< The symbol to search for. */
  rtems_rtl_archive* archive;  /**< The archive the symbol is found
                                *   in. */
  UBaseType_t        offset;   /**< The offset in the archive if found
                                *   else 0 */
} rtems_rtl_archive_obj_data;

bool
rtems_rtl_archive_obj_finder (rtems_rtl_archive* archive, void* data);

/**
 * Error handler call when finding an archive.
 */
typedef void (*rtems_rtl_archive_error)(int num, const char* text);

/**
 * Open the RTL archives support with the specified configration file.
 *
 * @param archives The archives data to open.
 * @param config The path to the configuration file.
 * @return bool Returns @true is the archives are open.
 */
void rtems_rtl_archives_open (rtems_rtl_archives* archives, const char* config);

/**
 * Close the RTL archives support.
 *
 * @param archives The archives data to close.
 */
void rtems_rtl_archives_close (rtems_rtl_archives* archives);

/**
 * Refresh the archives data. Check if the configuration has changes and if it
 * has reload it. Check each path in the configuration and creates archive
 * instances for new archives and remove archives not present any more.
 *
 * Refreshing is a development aid so reboots can be avoided as users trial
 * configurations that work.
 *
 * @param archives The archives data to refresh.
 * @retval false The refresh failed, an error will have been set.
 */
bool rtems_rtl_archives_refresh (rtems_rtl_archives* archives);

/**
 * Load an archive.
 *
 * @param archives The archives data to search.
 * @param name     The archive to load.
 * @retval true    The archive is loaded.
 */
bool rtems_rtl_archive_load (rtems_rtl_archives* archives, const char* name);

/**
 * Search for a symbol and load the first object file that has the symbol.
 *
 * @param archives The archives data to search.
 * @param symbol   The symbol name to search for.
 * @param load     If @true load the object file the symbol is found in
 *                 else return the found not found status.
 */
rtems_rtl_archive_search rtems_rtl_archive_obj_load (rtems_rtl_archives* archives,
                                                     const char*         symbol,
                                                     bool                load);

/**
 * Find a module in an archive returning the offset in the archive and
 * the size. If the name field is pointing to a string pointer and
 * that poniter is NULL and the offset is valid the name is extracted
 * from the archive and filled in. This is used when loading a file
 * from the archive after a symbol is found. The file name is not know
 * and could be extended which requires searching the extended string
 * table in the archive.
 *
 * @param fd Open file handle for the archive.
 * @param fsize Size of the archive.
 * @paarm name Pointer to the name string.
 * @param offset The offset of the file in the archive.
 * @param size The size of the file in the archive.
 * @param extended_names The offset in the archive for the extended names.
 * @param error The error handler called on an error.
 * @retval true The file was found in the archive.
 * @retval false The file was not found.
 */
bool rtems_rtl_obj_archive_find_obj (int                     fd,
                                     size_t                  fsize,
                                     const char**            name,
                                     UBaseType_t*            offset,
                                     size_t*                 size,
                                     UBaseType_t*            extended_names,
                                     rtems_rtl_archive_error error);

rtems_rtl_archive*
rtems_rtl_archive_find (rtems_rtl_archives* archives,
                        const char*         path);

/**
 * Selectively load an object from an archive without relying on the dependency
 * symbol resolving to load an object. This is helpful to always load an object
 * that does not have other dynamically loaded objects that rely on it-- fault
 * handlers is an example.
 */
rtems_rtl_archive_search
rtems_rtl_archive_single_obj_load(rtems_rtl_archive* archive, size_t offset);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif
