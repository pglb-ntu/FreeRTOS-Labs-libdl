
#ifdef HAVE_CONFIG_H
#include <waf_config.h>
#endif

#ifdef ipconfigUSE_FAT_LIBDL
#include "ff_headers.h"
#include "ff_stdio.h"
#endif

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <rtl/rtl.h>
#include <rtl/rtl-freertos-compartments.h>
#include <rtl/rtl-allocator.h>
#include <rtl/rtl-obj.h>
#include <rtl/rtl-trace.h>
#include <errno.h>
#include "rtl-error.h"

#include <FreeRTOS.h>
#include "timers.h"

#if __riscv_xlen == 32
#define ELFSIZE 32
#elif __riscv_xlen == 64
#define ELFSIZE 64
#endif

#include <sys/exec_elf.h>

#ifdef __CHERI_PURE_CAPABILITY__
#include <cheric.h>
#include <cheriintrin.h>
#include <cheri/cheri-utility.h>
extern void *pvAlmightyDataCap;
extern void *pvAlmightyCodeCap;
#endif

#if 0
extern Elf_Sym*  __symtab_start;
extern Elf_Sym*  __symtab_end;
extern char*  __strtab_start;
extern char*  __strtab_end;
#endif

Compartment_t comp_list[configCOMPARTMENTS_NUM];
static uint32_t comp_id_free = 0;

void* rtl_freertos_compartment_open(const char *name)
{
#ifdef ipconfigUSE_FAT_LIBDL
  FF_FILE *file = ff_fopen( name, "r" );

  if (file == NULL) {
    rtems_rtl_set_error (EBADF, "Failed to open the file");
    return -1;
  }

  return file;

#else
  const char* comp_name = NULL;
  char name_buff[configMAXLEN_COMPNAME];

  char *name_dot = strrchr(name, '.');
  if (name_dot == NULL) {
    return -1;
  }

  comp_name = rtems_rtl_alloc_new (RTEMS_RTL_ALLOC_OBJECT, name_dot - name + 1, true);
  if (!comp_name) {
    rtems_rtl_set_error (ENOMEM, "no memory for comp file name");
    return -1;
  }

  memcpy(comp_name, name, name_dot - name);

  for(int i = 0; i < configCOMPARTMENTS_NUM; i++) {
    if (strncmp(comp_list[i].name, comp_name, configMAXLEN_COMPNAME) == 0) {
      return i;
    }
  }

  return -1;
#endif
}

#if configCHERI_COMPARTMENTALIZATION
bool rtl_freertos_compartment_close(rtems_rtl_obj* obj)
{
  #if configCHERI_COMPARTMENTALIZATION_MODE == 1
    if (obj->captable) {
      rtems_rtl_alloc_del(RTEMS_RTL_ALLOC_CAPTAB, obj->captable);
    }
  #elif configCHERI_COMPARTMENTALIZATION_MODE == 2
    if (obj->archive->captable) {
      rtems_rtl_alloc_del(RTEMS_RTL_ALLOC_CAPTAB, obj->archive->captable);
    }
  #endif /* configCHERI_COMPARTMENTALIZATION_MODE */
return true;
}
#endif

size_t rtl_freertos_compartment_read(void* fd, void *buffer, UBaseType_t offset, size_t count)
{
#ifdef ipconfigUSE_FAT_LIBDL
  return ff_fread( buffer, 1, count, (FF_FILE *) fd );
#else
  if (fd < 0 || fd >= configCOMPARTMENTS_NUM) {
    rtems_rtl_set_error (EBADF, "Invalid compartment/fd number");
    return -1;
  }

  // If trying to read past the file size, trim down the count to read only to
  // the EoF.
  if (offset + count > comp_list[fd].size) {
    count -= (offset + count) - comp_list[fd].size;
  }

  if (memcpy(buffer, comp_list[fd].cap + offset, count)) {
    return count;
  }

  return -1;
#endif
}

size_t rtl_freertos_compartment_getsize(void *fd) {
#ifdef ipconfigUSE_FAT_LIBDL
  return ((FF_FILE *) fd)->ulFileSize;
#else
  return comp_list[fd].size;
#endif
}

void
rtems_rtl_symbol_global_insert (rtems_rtl_symbols* symbols,
                                rtems_rtl_obj_sym* symbol);

#if 0
size_t
rtl_freertos_global_symbols_add(rtems_rtl_obj* obj) {
  Elf_Sym*  symtab_start = &__symtab_start;
  Elf_Sym*  symtab_end = &__symtab_end;
  char*  strtab_start = &__strtab_start;
  char*  strtab_end = &__strtab_end;
  uint32_t syms_count =  ((size_t) &__symtab_end - (size_t) &__symtab_start) / sizeof(Elf_Sym);

#ifdef __CHERI_PURE_CAPABILITY__
  size_t strtab_size = ((size_t) &__strtab_end - (size_t) &__strtab_start);
  symtab_start = cheri_build_data_cap((ptraddr_t) symtab_start, syms_count * sizeof(Elf_Sym), 0xff);
  strtab_start = cheri_build_data_cap((ptraddr_t) strtab_start, strtab_size, 0xff);
#endif

  rtems_rtl_symbols* symbols;
  rtems_rtl_obj_sym* sym;
  uint32_t globals_count = 0;

  for(int i = 0; i < syms_count; i++) {
    if (ELF_ST_BIND(symtab_start[i].st_info) == STB_GLOBAL) {
      globals_count++;
    }
  }

  if (rtems_rtl_trace (RTEMS_RTL_TRACE_GLOBAL_SYM))
    printf ("rtl: global symbol add: %zi\n", globals_count);

  obj->global_size = globals_count * sizeof (rtems_rtl_obj_sym);
  obj->global_table = rtems_rtl_alloc_new (RTEMS_RTL_ALLOC_SYMBOL,
                                           obj->global_size, true);
  if (!obj->global_table)
  {
    obj->global_size = 0;
    rtems_rtl_set_error (ENOMEM, "no memory for global symbols");
    return false;
  }

#ifdef __CHERI_PURE_CAPABILITY__
#if configCHERI_COMPARTMENTALIZATION_MODE == 1
  obj->captable = NULL;
  if (!rtl_cherifreertos_captable_alloc(obj, globals_count))
  {
    if (rtems_rtl_trace (RTEMS_RTL_TRACE_CHERI))
      printf("rtl:cheri: Failed to alloc a global cap table for %s\n", obj->oname);

    return 0;
  }
#elif configCHERI_COMPARTMENTALIZATION_MODE == 2
  obj->archive->captable = NULL;
  if (!rtl_cherifreertos_captable_archive_alloc(obj->archive, globals_count))
  {
    if (rtems_rtl_trace (RTEMS_RTL_TRACE_CHERI))
      printf("rtl:cheri: Failed to alloc a global cap table for %s\n", obj->aname);

    return 0;
  }
#endif /* configCHERI_COMPARTMENTALIZATION_MODE */
#endif

  symbols = rtems_rtl_global_symbols ();

  sym = obj->global_table;

  for(int i = 0; i < syms_count; i++) {
    if (ELF_ST_BIND(symtab_start[i].st_info) == STB_GLOBAL) {
      sym->name =  &strtab_start[symtab_start[i].st_name];
      uint32_t str_idx = symtab_start[i].st_name;
      char *cap_str = strtab_start + str_idx;
#ifdef __CHERI_PURE_CAPABILITY__
      void *cap = NULL;
      if (ELF_ST_TYPE(symtab_start[i].st_info) == STT_OBJECT) {
        cap = cheri_build_data_cap((ptraddr_t) symtab_start[i].st_value,
        symtab_start[i].st_size,
        __CHERI_CAP_PERMISSION_GLOBAL__ | \
        __CHERI_CAP_PERMISSION_PERMIT_LOAD__ | \
        __CHERI_CAP_PERMISSION_PERMIT_STORE__ | \
        __CHERI_CAP_PERMISSION_PERMIT_LOAD_CAPABILITY__ | \
        __CHERI_CAP_PERMISSION_PERMIT_STORE_CAPABILITY__);
      } else if (ELF_ST_TYPE(symtab_start[i].st_info) == STT_FUNC) {
        cap = cheri_build_code_cap_unbounded((ptraddr_t) symtab_start[i].st_value,
        __CHERI_CAP_PERMISSION_ACCESS_SYSTEM_REGISTERS__ | \
        __CHERI_CAP_PERMISSION_GLOBAL__ | \
        __CHERI_CAP_PERMISSION_PERMIT_EXECUTE__ | \
        __CHERI_CAP_PERMISSION_PERMIT_LOAD__ | \
        __CHERI_CAP_PERMISSION_PERMIT_LOAD_CAPABILITY__ | \
        __CHERI_CAP_PERMISSION_PERMIT_STORE__ | \
        __CHERI_CAP_PERMISSION_PERMIT_STORE_CAPABILITY__);
      }

      sym->capability = rtl_cherifreertos_captable_install_new_cap(obj, cap);
      if (!sym->capability) {
        if (rtems_rtl_trace (RTEMS_RTL_TRACE_CHERI))
          printf("rtl:cheri: Failed to install a new cap in %s captable\n", obj->oname);
        return 0;
      }
#endif
      sym->value = symtab_start[i].st_value;
      sym->size = symtab_start[i].st_size;

      if (rtems_rtl_symbol_global_find (sym->name) == NULL) {
        rtems_rtl_symbol_global_insert (symbols, sym);
        ++sym;
      }
    }
  }

  obj->global_syms = globals_count;

  return globals_count;
}
#endif

uint32_t rtl_cherifreertos_compartment_get_free_compid(void) {

  if (comp_id_free >= configCOMPARTMENTS_NUM) {
    printf("Too many compartments, only %d are supported\n", configCOMPARTMENTS_NUM);
  }

  return comp_id_free++;
}

#ifdef __CHERI_PURE_CAPABILITY__

bool
rtl_cherifreertos_compartment_captable_set_perms (size_t xCompID)
{
  if (xCompID >= configCOMPARTMENTS_NUM)
    return false;

  void** captable = comp_list[xCompID].captable;

  if (captable == NULL) {
    printf("Invalid captab to set permissions on\n");
    return false;
  }

  captable = cheri_perms_and(captable,
                 __CHERI_CAP_PERMISSION_PERMIT_LOAD__ |
                 __CHERI_CAP_PERMISSION_PERMIT_LOAD_CAPABILITY__);

  comp_list[xCompID].captable = captable;
}

#if configCHERI_COMPARTMENTALIZATION_MODE == 1
bool
rtl_cherifreertos_compartment_set_captable(rtems_rtl_obj* obj) {

  if (!obj->captable) {
    rtems_rtl_set_error (EINVAL, "Cap table hasn't been set yet");
    return false;
  }

  if (obj->comp_id >= configCOMPARTMENTS_NUM)
    return false;

  comp_list[obj->comp_id].captable = obj->captable;

  return true;
}

bool
rtl_cherifreertos_compartment_set_obj(rtems_rtl_obj* obj) {

  if (!obj) {
    rtems_rtl_set_error (EINVAL, "Invalid object to set for a compartment");
    return false;
  }

  if (obj->comp_id >= configCOMPARTMENTS_NUM)
    return false;

  comp_list[obj->comp_id].obj = obj;

  return true;
}

rtems_rtl_obj *
rtl_cherifreertos_compartment_get_obj(size_t comp_id) {

  if (comp_id >= configCOMPARTMENTS_NUM)
    return NULL;

  return (rtems_rtl_obj *) comp_list[comp_id].obj;
}

bool
rtl_cherifreertos_captable_alloc(rtems_rtl_obj* obj, size_t caps_count) {
  void** cap_table = NULL;

  if (obj->captable) {
    rtems_rtl_set_error (ENOTEMPTY, "There is already a cap table for this object");
    return false;
  }

  cap_table = (void **) rtems_rtl_alloc_new (RTEMS_RTL_ALLOC_CAPTAB,
                                   caps_count * sizeof(void *), true);
  if (!cap_table) {
    rtems_rtl_set_error (ENOMEM, "no memory to create a new captable");
    return false;
  }

  obj->captable = cap_table;
  obj->caps_count = caps_count;

  if (!rtl_cherifreertos_compartment_set_captable(obj))
    return false;

  return true;
}

bool
rtl_cherifreertos_capstack_alloc(rtems_rtl_obj* obj, size_t stack_depth) {
  void* stack = NULL;
  size_t stacks_bytes = stack_depth * sizeof(void *);

  if (!obj->captable) {
    rtems_rtl_set_error (ENOTEMPTY, "There is no captable for this object");
    return false;
  }

  if (*obj->captable) {
    rtems_rtl_set_error (ENOTEMPTY, "There is already an installed stack for this object");
    return false;
  }

  stack = (void *) rtems_rtl_alloc_new (RTEMS_RTL_ALLOC_OBJECT,
                                   stacks_bytes, true);

  stack = __builtin_cheri_offset_set(stack, stacks_bytes - sizeof(void *));

  if (!stack) {
    rtems_rtl_set_error (ENOMEM, "Failed to allocate a stack for this object");
    return false;
  }

  *obj->captable = stack;

  return true;
}

#elif configCHERI_COMPARTMENTALIZATION_MODE == 2
bool
rtl_cherifreertos_archive_compartment_set_captable(rtems_rtl_archive* archive) {

  if (!archive->captable) {
    rtems_rtl_set_error (EINVAL, "Cap table hasn't been set yet");
    return false;
  }

  if (archive->comp_id >= configCOMPARTMENTS_NUM)
    return false;

  comp_list[archive->comp_id].captable = archive->captable;

  return true;
}

bool
rtl_cherifreertos_compartment_set_archive(rtems_rtl_archive* archive) {

  if (!archive) {
    rtems_rtl_set_error (EINVAL, "Invalid archive to set for a compartment");
    return false;
  }

  if (archive->comp_id >= configCOMPARTMENTS_NUM)
    return false;

  comp_list[archive->comp_id].archive = archive;

  return true;
}

rtems_rtl_archive*
rtl_cherifreertos_compartment_get_archive(size_t comp_id) {

  if (comp_id >= configCOMPARTMENTS_NUM)
    return NULL;

  return (rtems_rtl_archive *) comp_list[comp_id].archive;
}

bool
rtl_cherifreertos_captable_archive_alloc(rtems_rtl_archive* archive, size_t caps_count) {
  void** cap_table = NULL;

  if (archive->captable) {
    rtems_rtl_set_error (ENOTEMPTY, "There is already a cap table for this archive");
    return false;
  }

  cap_table = (void **) rtems_rtl_alloc_new (RTEMS_RTL_ALLOC_CAPTAB,
                                   caps_count * sizeof(void *), true);
  if (!cap_table) {
    rtems_rtl_set_error (ENOMEM, "no memory to create a new captable");
    return false;
  }

  archive->captable = cap_table;
  archive->caps_count = caps_count;

  if (!rtl_cherifreertos_archive_compartment_set_captable(archive))
    return false;

  return true;
}

#endif /* configCHERI_COMPARTMENTALIZATION_MODE */

uint32_t
rtl_cherifreertos_compartment_get_compid(rtems_rtl_obj* obj) {
#if configCHERI_COMPARTMENTALIZATION_MODE == 1
  return obj->comp_id;
#elif configCHERI_COMPARTMENTALIZATION_MODE == 2
  return obj->archive->comp_id;
#endif
}

void **
rtl_cherifreertos_compartment_obj_get_captable(rtems_rtl_obj* obj) {
#if configCHERI_COMPARTMENTALIZATION_MODE == 1
  return obj->captable;
#elif configCHERI_COMPARTMENTALIZATION_MODE == 2
  return obj->archive->captable;
#endif
}

void **
rtl_cherifreertos_compartment_get_captable(size_t comp_id) {

  if (comp_id >= configCOMPARTMENTS_NUM)
    return NULL;

  return comp_list[comp_id].captable;
}

static bool
rtl_cherifreertos_captable_copy(void **dest_captable, void **src_captable, size_t caps_count) {
  void** cap_table = NULL;

  if (!dest_captable || !src_captable) {
    rtems_rtl_set_error (EINVAL, "Invalid captables to copy");
    return false;
  }

  memcpy(dest_captable, src_captable, caps_count * sizeof(void *));

  return true;
}

static bool
rtl_cherifreertos_captable_realloc(rtems_rtl_obj* obj, size_t new_caps_count) {
  void** cap_table = NULL;

#if configCHERI_COMPARTMENTALIZATION_MODE == 1
  if (!obj->captable) {
    rtems_rtl_set_error (ENOTEMPTY, "There is no cap table for this object");
    return false;
  }

  cap_table = (void **) rtems_rtl_alloc_new (RTEMS_RTL_ALLOC_CAPTAB,
                                   new_caps_count * sizeof(void *), true);
  if (!cap_table) {
    rtems_rtl_set_error (ENOMEM, "no memory to re-create a new captable");
    return false;
  }

  if (!rtl_cherifreertos_captable_copy(cap_table, obj->captable, obj->caps_count)) {
    rtems_rtl_set_error (ENOMEM, "Failed to copy cap tables");
  }

  memset(obj->captable, 0, obj->caps_count * sizeof(void *));
  rtems_rtl_alloc_del(RTEMS_RTL_ALLOC_CAPTAB, obj->captable);

  obj->captable = cap_table;
  obj->caps_count = new_caps_count;

  if (!rtl_cherifreertos_compartment_set_captable(obj))
    return false;
#elif configCHERI_COMPARTMENTALIZATION_MODE == 2
  if (!obj->archive->captable) {
    rtems_rtl_set_error (ENOTEMPTY, "There is no cap table for this archive");
    return false;
  }

  cap_table = (void **) rtems_rtl_alloc_new (RTEMS_RTL_ALLOC_CAPTAB,
                                   new_caps_count * sizeof(void *), true);
  if (!cap_table) {
    rtems_rtl_set_error (ENOMEM, "no memory to re-create a new captable");
    return false;
  }

  if (!rtl_cherifreertos_captable_copy(cap_table, obj->archive->captable, obj->archive->caps_count)) {
    rtems_rtl_set_error (ENOMEM, "Failed to copy cap tables");
  }

  memset(obj->archive->captable, 0, obj->archive->caps_count * sizeof(void *));
  rtems_rtl_alloc_del(RTEMS_RTL_ALLOC_CAPTAB, obj->archive->captable);

  obj->archive->captable = cap_table;
  obj->archive->caps_count = new_caps_count;

  if (!rtl_cherifreertos_archive_compartment_set_captable(obj->archive))
    return false;

#endif
  return true;
}

static uint32_t
rtl_cherifreertos_captable_get_free_slot(rtems_rtl_obj* obj) {
#if configCHERI_COMPARTMENTALIZATION_MODE == 1
  if (obj->captable_free_slot >= obj->caps_count) {
    return 0;
  }

  return obj->captable_free_slot++;
#elif configCHERI_COMPARTMENTALIZATION_MODE == 2
  if (obj->archive->captable_free_slot >= obj->archive->caps_count) {
    return 0;
  }

  return obj->archive->captable_free_slot++;
#endif
}

uint32_t
rtl_cherifreertos_captable_install_new_cap(rtems_rtl_obj* obj, void* new_cap) {
  uint32_t free_slot;

#if configCHERI_COMPARTMENTALIZATION_MODE == 1
  if (!obj->captable) {
    rtems_rtl_set_error (EINVAL, "There is no cap table for this object");
    return NULL;
  }
#elif configCHERI_COMPARTMENTALIZATION_MODE == 2
  if (!obj->archive->captable) {
    rtems_rtl_set_error (EINVAL, "There is no cap table for this archive");
    return NULL;
  }
#endif

  free_slot = rtl_cherifreertos_captable_get_free_slot(obj);
  if (!free_slot) {
    // Try to realloc a new captable to install a new slot
    if (rtems_rtl_trace (RTEMS_RTL_TRACE_CHERI)) {
      printf("rtl:captable: no empty slot for a new cap, trying to realloc\n");
    }

    // Re-alloc a new captable with an extra slot for a new cap. We may want to
    // increase the number of slots to add when re-allocating if it's expected
    // to on-demand allocate many new caps for (i.e., if the object does many
    // externals accesses.
#if configCHERI_COMPARTMENTALIZATION_MODE == 1
    if (!rtl_cherifreertos_captable_realloc(obj, obj->caps_count + 1)) {
      rtems_rtl_set_error (ENOMEM, "Couldn't realloc a new captable to install a new cap in");
      return NULL;
    }
#elif configCHERI_COMPARTMENTALIZATION_MODE == 2
    if (!rtl_cherifreertos_captable_realloc(obj, obj->archive->caps_count + 1)) {
      rtems_rtl_set_error (ENOMEM, "Couldn't realloc a new captable to install a new cap in");
      return NULL;
    }
#endif

    // Try again after increasing the cap table size
    free_slot = rtl_cherifreertos_captable_get_free_slot(obj);
    if (!free_slot) {
      rtems_rtl_set_error (ENOMEM, "Still can not find a free slot after realloc");
      return NULL;
    }
  }


  if (rtems_rtl_trace (RTEMS_RTL_TRACE_CHERI)) {
    printf("rtl:captable: Installing a new cap: "); cheri_print_cap(new_cap);
  }

#if configCHERI_COMPARTMENTALIZATION_MODE == 1
  obj->captable[free_slot] = new_cap;
#elif configCHERI_COMPARTMENTALIZATION_MODE == 2
  obj->archive->captable[free_slot] = new_cap;
#endif

  return free_slot;
}

void* rtl_cherifreertos_compartments_setup_ecall(uintcap_t code, BaseType_t compid)
{

  rtems_rtl_obj* kernel_obj = rtems_rtl_baseimage();
  rtems_rtl_obj_sym* tramp_sym;
  void* tramp_cap_template;
  volatile uintcap_t* tramp_cap_instance;
  void **captable = rtl_cherifreertos_compartment_get_captable(compid);

  /* Find the xPortCompartmentEnterTrampoline template to copy from */
  tramp_sym = rtems_rtl_symbol_global_find ("xPortCompartmentEnterTrampoline");
  if (tramp_sym == NULL) {
    printf("Failed to fine xPortCompartmentEnterTrampoline needed for inter-compartment calls\n");
    return NULL;
  }

#if configCHERI_COMPARTMENTALIZATION_MODE == 1
  tramp_cap_template = kernel_obj->captable[tramp_sym->capability];
#elif configCHERI_COMPARTMENTALIZATION_MODE == 2
  tramp_cap_template = kernel_obj->archive->captable[tramp_sym->capability];
#endif /* configCHERI_COMPARTMENTALIZATION_MODE */

  /* Allocate memory for the new trampoline */
  tramp_cap_instance = rtems_rtl_alloc_new (RTEMS_RTL_ALLOC_OBJECT, tramp_sym->size, true);
  if (tramp_cap_instance == NULL) {
    printf("Failed to allocate a new trampoline to do external calls\n");
    return NULL;
  }

  /* Copy template code into the newly allocated area of memory */
  memcpy(tramp_cap_instance, tramp_cap_template, tramp_sym->size);

  /* Setup code cap in the trampoline */
  tramp_cap_instance[0] = code;

  /* Setup captable in the trampoline */
  tramp_cap_instance[1] = &comp_list[compid].captable;

  /* Setup the new compartment ID in the trampoline */
  if (compid >= configCOMPARTMENTS_NUM) {
    return NULL;
  }
  tramp_cap_instance[2] = compid;

  /* Make the trampoline cap RX only */
  tramp_cap_instance = cheri_build_code_cap((ptraddr_t) tramp_cap_instance,
      tramp_sym->size,
      __CHERI_CAP_PERMISSION_ACCESS_SYSTEM_REGISTERS__ | \
      __CHERI_CAP_PERMISSION_GLOBAL__ | \
      __CHERI_CAP_PERMISSION_PERMIT_EXECUTE__ | \
      __CHERI_CAP_PERMISSION_PERMIT_LOAD__ | \
      __CHERI_CAP_PERMISSION_PERMIT_LOAD_CAPABILITY__);

  /* Create cap and install it */
  return &tramp_cap_instance[3];
}

void rtl_cherifreertos_compartment_register_faultHandler(BaseType_t compid, void* handler)
{
#if configCHERI_COMPARTMENTALIZATION_MODE == 1
  rtems_rtl_obj* obj = rtl_cherifreertos_compartment_get_obj(compid);

  if (obj == NULL) {
    printf("Couldn't find an object for compid %d\n", compid);
    return;
  }

  obj->faultHandler = handler;
#elif configCHERI_COMPARTMENTALIZATION_MODE == 2
  rtems_rtl_archive* archive = rtl_cherifreertos_compartment_get_archive(compid);

  if (archive == NULL) {
    printf("Couldn't find an archive for compid %d\n", compid);
    return;
  }

  archive->faultHandler = handler;
#endif
}

bool
rtl_cherifreertos_compartment_faultHandler(BaseType_t compid) {
  BaseType_t pxHigherPriorityTaskWoken = pdFALSE;
  PendedFunction_t func = NULL;

#if configCHERI_COMPARTMENTALIZATION_MODE == 1
  rtems_rtl_obj* obj = rtl_cherifreertos_compartment_get_obj(compid);

  if (obj == NULL) {
    printf("Couldn't find an object for compid %d\n", compid);
    return false;
  }

  if (obj->faultHandler == NULL) {
    printf("No attached fault handler for compartment %s, returning to caller directely\n", obj->oname);
    return false;
  }

  func = (PendedFunction_t) obj->faultHandler;
#elif configCHERI_COMPARTMENTALIZATION_MODE == 2
  rtems_rtl_archive* archive = rtl_cherifreertos_compartment_get_archive(compid);

  if (archive == NULL) {
    printf("Couldn't find an archive for compid %d\n", compid);
    return false;
  }

  if (archive->faultHandler == NULL) {
    printf("No fault handler for compartment %s, returning to caller directely\n", archive->name);
    return false;
  }

  func = (PendedFunction_t) archive->faultHandler;
#endif

  // Notify the daemon task to run the per-compartment fault handler in its context
  if (func)
    xTimerPendFunctionCallFromISR(func, NULL, compid, &pxHigherPriorityTaskWoken);

  return (bool) pxHigherPriorityTaskWoken;
}

bool
rtl_cherifreertos_compartment_init_resources (BaseType_t compid)
{
  FreeRTOSCompartmentResources_t* pCompResTable = rtems_rtl_alloc_new (RTEMS_RTL_ALLOC_OBJECT,
                                                   sizeof (FreeRTOSCompartmentResources_t),
                                                   true);
  if (!pCompResTable) {
    printf ("no memory for resources table");
    return false;
  }

  pCompResTable->buckets = rtems_rtl_alloc_new (RTEMS_RTL_ALLOC_OBJECT,
                                          FREERTOS_OBJ_COUNT * sizeof (List_t),
                                          true);
  if (!pCompResTable->buckets)
  {
    printf ("no memory for resouces table buckets");
    return false;
  }

  pCompResTable->nbuckets = FREERTOS_OBJ_COUNT;

  for (int buckets = 0; buckets < pCompResTable->nbuckets; ++buckets)
    vListInitialise (&pCompResTable->buckets[buckets]);

#if configCHERI_COMPARTMENTALIZATION_MODE == 1
  rtems_rtl_obj* obj = rtl_cherifreertos_compartment_get_obj(compid);

  if (obj == NULL) {
    printf("Couldn't find an object for compid %d\n", compid);
    return false;
  }

  obj->pCompResTable = pCompResTable;
#elif configCHERI_COMPARTMENTALIZATION_MODE == 2
  rtems_rtl_archive* archive = rtl_cherifreertos_compartment_get_archive(compid);

  if (archive == NULL) {
    printf("Couldn't find an archive for compid %d\n", compid);
    return false;
  }

  archive->pCompResTable = pCompResTable;
#endif

  return true;
}

void
rtl_cherifreertos_compartment_add_resource(BaseType_t compid,
                                           FreeRTOSResource_t xResource)
{
  FreeRTOSCompartmentResources_t* pCompResTable = NULL;

  FreeRTOSResource_t* newRes = pvPortMalloc (sizeof (FreeRTOSResource_t));

  if (newRes == NULL) {
    printf("Failed to add %d resource to compartment %d\n", xResource.type, compid);
    return;
  }

  newRes->handle = xResource.handle;
  newRes->type= xResource.type;

#if configCHERI_COMPARTMENTALIZATION_MODE == 1
  rtems_rtl_obj* obj = rtl_cherifreertos_compartment_get_obj(compid);

  pCompResTable = (FreeRTOSCompartmentResources_t*) obj->pCompResTable;
#elif configCHERI_COMPARTMENTALIZATION_MODE == 2
  rtems_rtl_archive* archive = rtl_cherifreertos_compartment_get_archive(compid);

  pCompResTable = (FreeRTOSCompartmentResources_t*) archive->pCompResTable;
#endif

  if (pCompResTable == NULL) {
    printf("No resources table found for compartment %d\n", compid);
    return;
  }

  vListInsertEnd (&pCompResTable->buckets[xResource.type],
                  &newRes->node);
}

void
rtl_cherifreertos_compartment_remove_resource(BaseType_t compid,
                                              FreeRTOSResource_t xResource)
{
  FreeRTOSCompartmentResources_t* pCompResTable = NULL;

#if configCHERI_COMPARTMENTALIZATION_MODE == 1
  rtems_rtl_obj* obj = rtl_cherifreertos_compartment_get_obj(compid);

  pCompResTable = (FreeRTOSCompartmentResources_t*) obj->pCompResTable;
#elif configCHERI_COMPARTMENTALIZATION_MODE == 2
  rtems_rtl_archive* archive = rtl_cherifreertos_compartment_get_archive(compid);

  pCompResTable = (FreeRTOSCompartmentResources_t*) archive->pCompResTable;
#endif

  if (pCompResTable == NULL) {
    printf("No resources table found for compartment %d\n", compid);
    return;
  }

  List_t* resouceList = &pCompResTable->buckets[xResource.type];

  ListItem_t* node = listGET_HEAD_ENTRY (resouceList);

  while (listGET_END_MARKER (resouceList) != node)
  {
    FreeRTOSResource_t* res = (FreeRTOSResource_t *) node;

    if (res->handle == xResource.handle) {
      uxListRemove(res);
      return;
    }

    node = listGET_NEXT (node);
  }
}

void
rtl_cherifreertos_compartment_revoke_tasks(FreeRTOSCompartmentResources_t* pCompResTable) {
  if (pCompResTable == NULL) {
    printf("Invalid resource table to revoke tasks from");
    return;
  }

  List_t* resouceList = &pCompResTable->buckets[FREERTOS_TASK];
  ListItem_t* node = listGET_HEAD_ENTRY (resouceList);

  while (listGET_END_MARKER (resouceList) != node)
  {
    FreeRTOSResource_t* res = (FreeRTOSResource_t *) node;

    uxListRemove(res);
    vTaskDelete(res->handle);

    node = listGET_NEXT (node);
  }
}

void
rtl_cherifreertos_compartment_revoke_resources(BaseType_t compid) {
FreeRTOSCompartmentResources_t* pCompResTable = NULL;

#if configCHERI_COMPARTMENTALIZATION_MODE == 1
  rtems_rtl_obj* obj = rtl_cherifreertos_compartment_get_obj(compid);

  pCompResTable = (FreeRTOSCompartmentResources_t*) obj->pCompResTable;
#elif configCHERI_COMPARTMENTALIZATION_MODE == 2
  rtems_rtl_archive* archive = rtl_cherifreertos_compartment_get_archive(compid);

  pCompResTable = (FreeRTOSCompartmentResources_t*) archive->pCompResTable;
#endif

  if (pCompResTable == NULL) {
    printf("No resources table found for compartment %d\n", compid);
    return;
  }

  rtl_cherifreertos_compartment_revoke_tasks(pCompResTable);

  // TODO: Revoke other FreeRTOS resources as well
}

void rtl_cherifreertos_debug_print_compartments(void) {
  List_t* objects = rtems_rtl_objects_unprotected();
  ListItem_t* node = listGET_HEAD_ENTRY (objects);

  while (listGET_END_MARKER (objects) != node)
  {
    rtems_rtl_obj* obj = (rtems_rtl_obj* ) node;
    void** captable = rtl_cherifreertos_compartment_obj_get_captable(obj);
    size_t xCompID = rtl_cherifreertos_compartment_get_compid(obj);

    printf("rtl:debug: %s@0x%x\t\n", obj->oname, obj->text_base);
    printf("compid = #%u ", xCompID);
    printf("captab = %p\n", obj->captable);

    node = listGET_NEXT (node);
  }
}
#endif
