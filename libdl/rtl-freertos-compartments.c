#ifdef HAVE_CONFIG_H
#include <waf_config.h>
#endif

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <rtl/rtl.h>
#include <rtl/rtl-freertos-compartments.h>
#include <rtl/rtl-allocator.h>
#include <rtl/rtl-obj.h>
#include <rtl/rtl-archive.h>
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

#if configCHERI_COMPARTMENTALIZATION
#include <cheriintrin.h>
#include <cheri/cheri-utility.h>
extern void *pvAlmightyDataCap;
extern void *pvAlmightyCodeCap;

#if 0
extern Elf_Sym*  __symtab_start;
extern Elf_Sym*  __symtab_end;
extern char*  __strtab_start;
extern char*  __strtab_end;
#endif

Compartment_t comp_list[configCOMPARTMENTS_NUM];
static size_t comp_id_free = 0;
#endif /* configCHERI_COMPARTMENTALIZATION */

#if configCHERI_COMPARTMENTALIZATION
void
rtems_rtl_symbol_global_insert (rtems_rtl_symbols* symbols,
                                rtems_rtl_obj_sym* symbol);

#if 0
intptr_t rtl_freertos_compartment_open(const char *name)
{
  int file = open( name, O_RDONLY );

  if (file == NULL) {
    rtems_rtl_set_error (EBADF, "Failed to open the file");
    return -1;
  }

  return file;
}

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

size_t rtl_freertos_compartment_read(intptr_t fd, void *buffer, UBaseType_t offset, size_t count)
{
  if (lseek (fd, offset, SEEK_SET) < 0)
    return 0;

  return read(fd, buffer, count);
}

size_t rtl_freertos_compartment_getsize(intptr_t fd) {
  struct stat sb;

  if (stat ("", &sb) == 0) {
    return sb.st_size;
  } else {
    return 0;
  }
}

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

size_t rtl_cherifreertos_compartment_get_free_compid(void) {

  if (comp_id_free >= configCOMPARTMENTS_NUM) {
    printf("Too many compartments, only %d are supported\n", configCOMPARTMENTS_NUM);
  }

  return comp_id_free++;
}

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

  return true;
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

size_t
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
    return 0;
  }
#elif configCHERI_COMPARTMENTALIZATION_MODE == 2
  if (!obj->archive->captable) {
    rtems_rtl_set_error (EINVAL, "There is no cap table for this archive");
    return 0;
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
      return 0;
    }
#elif configCHERI_COMPARTMENTALIZATION_MODE == 2
    if (!rtl_cherifreertos_captable_realloc(obj, obj->archive->caps_count + 1)) {
      rtems_rtl_set_error (ENOMEM, "Couldn't realloc a new captable to install a new cap in");
      return 0;
    }
#endif

    // Try again after increasing the cap table size
    free_slot = rtl_cherifreertos_captable_get_free_slot(obj);
    if (!free_slot) {
      rtems_rtl_set_error (ENOMEM, "Still can not find a free slot after realloc");
      return 0;
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

bool
rtl_cherifreertos_is_inter_compartment(rtems_rtl_obj* obj, const char* symname) {
  bool isInterCompartment = true;
  // If it is in the same object or in the kernel's globals it is intra-compartment
  if (rtems_rtl_gsymbol_obj_find(obj, symname) ||
      rtems_rtl_lsymbol_obj_find(obj, symname) ||
      rtems_rtl_symbol_global_find(symname))
    isInterCompartment = false;
#if configCHERI_COMPARTMENTALIZATION_MODE == 2
  /* Search for a per-archive fault handler */
  rtems_rtl_archive_obj_data search = {
    .symbol  = symname,
    .archive = obj->archive,
    .offset  = 0
  };

  rtems_rtl_archive_obj_finder(obj->archive, &search);

  // Found an object in the library compartment that has the symbol
  if (search.offset)
    isInterCompartment = false;
#endif

  return isInterCompartment;
}

void* rtl_cherifreertos_compartments_setup_ecall(void* code, size_t compid)
{
  rtems_rtl_obj* kernel_obj = rtems_rtl_baseimage();
  rtems_rtl_obj_sym* tramp_sym;
  rtems_rtl_obj_sym* comp_switch_sym;
  void* tramp_cap_template;
  volatile void** tramp_cap_instance;
  volatile void* global_comp_switch;
  void **captable = rtl_cherifreertos_compartment_obj_get_captable(kernel_obj);

  /* Find the xPortCompartmentTrampSetup template to copy from. This contains metadata such as
   * function, captable, trampoline func, etc required for further compartment switch */
  tramp_sym = rtems_rtl_symbol_global_find ("xPortCompartmentTrampSetup");
  if (tramp_sym == NULL) {
    printf("Failed to find xPortCompartmentTrampSetup needed for inter-compartment calls\n");
    return NULL;
  }

  /* Install the default compartment switch. TODO This might be cusom for compartment-matrices with
   * refined compartment policies that differ between different inter-compartment calls
   */
  comp_switch_sym = rtems_rtl_symbol_global_find ("xPortCompartmentEnterTrampoline");
  if (comp_switch_sym == NULL) {
    printf("Failed to find xPortCompartmentEnterTrampoline needed for inter-compartment calls\n");
    return NULL;
  }

  // Get a capability to the setup trampoline function and metadata
  tramp_cap_template = captable[tramp_sym->capability];
  // Get a capability to the global default compartment switch function
  global_comp_switch = captable[comp_switch_sym->capability];

  /* Allocate memory for the new setup trampoline */
  tramp_cap_instance = rtems_rtl_alloc_new (RTEMS_RTL_ALLOC_READ_EXEC, tramp_sym->size, true);
  if (tramp_cap_instance == NULL) {
    printf("Failed to allocate a new trampoline to do external calls\n");
    return NULL;
  }

  /* Copy template trampoline into the newly allocated area of memory */
  memcpy((void *) tramp_cap_instance, (void *) tramp_cap_template, tramp_sym->size);

  /* Setup code cap in the trampoline */
  tramp_cap_instance[0] = code;

  /* Setup captable in the trampoline */
  tramp_cap_instance[1] = &comp_list[compid].captable;

  /* Setup default compartment switch function */
  tramp_cap_instance[2] = global_comp_switch;

  /* Setup the new compartment ID in the trampoline */
  if (compid >= configCOMPARTMENTS_NUM) {
    return NULL;
  }

  /* Setup compartment ID by fixing up ADDI immediate */
  uint32_t* addi_inst = (uint32_t*) &tramp_cap_instance[3];
  *addi_inst = ((*addi_inst) & 0x000fffff) | (compid << 20);

  /* Make the trampoline cap RX only */
  tramp_cap_instance = cheri_build_code_cap((ptraddr_t) tramp_cap_instance,
      tramp_sym->size,
      __CHERI_CAP_PERMISSION_ACCESS_SYSTEM_REGISTERS__ | \
      __CHERI_CAP_PERMISSION_GLOBAL__ | \
      __CHERI_CAP_PERMISSION_PERMIT_EXECUTE__ | \
      __CHERI_CAP_PERMISSION_PERMIT_LOAD__ | \
      __CHERI_CAP_PERMISSION_PERMIT_LOAD_CAPABILITY__);

  /* return a sentry trampoline cap with an address of the first instruction */
  return cheri_sentry_create(&tramp_cap_instance[3]);
}

void rtl_cherifreertos_compartment_register_faultHandler(size_t compid, void* handler)
{
#if configCHERI_COMPARTMENTALIZATION_MODE == 1
  rtems_rtl_obj* obj = rtl_cherifreertos_compartment_get_obj(compid);

  if (obj == NULL) {
    printf("Couldn't find an object for compid %zu\n", compid);
    return;
  }

  obj->faultHandler = handler;
#elif configCHERI_COMPARTMENTALIZATION_MODE == 2
  rtems_rtl_archive* archive = rtl_cherifreertos_compartment_get_archive(compid);

  if (archive == NULL) {
    printf("Couldn't find an archive for compid %zu\n", compid);
    return;
  }

  archive->faultHandler = handler;
#endif
}

__attribute__((section(".text.fast"))) bool
rtl_cherifreertos_compartment_faultHandler(size_t compid) {
  BaseType_t pxHigherPriorityTaskWoken = pdFALSE;
  PendedFunction_t func = NULL;

#if configCHERI_COMPARTMENTALIZATION_MODE == 1
  rtems_rtl_obj* obj = rtl_cherifreertos_compartment_get_obj(compid);

  if (obj == NULL) {
    printf("Couldn't find an object for compid %zu\n", compid);
    return false;
  }

  if (obj->faultHandler == NULL) {
    #if DEBUG
      printf("No attached fault handler for compartment %s, returning to caller directly\n", obj->oname);
    #endif
    return false;
  }

  func = (PendedFunction_t) obj->faultHandler;
#elif configCHERI_COMPARTMENTALIZATION_MODE == 2
  rtems_rtl_archive* archive = rtl_cherifreertos_compartment_get_archive(compid);

  if (archive == NULL) {
    printf("Couldn't find an archive for compid %zu\n", compid);
    return false;
  }

  if (archive->faultHandler == NULL) {
    #if DEBUG
      printf("No fault handler for compartment %s, returning to caller directly\n", archive->name);
    #endif
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
rtl_cherifreertos_compartment_init_resources (size_t compid)
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
    printf("Couldn't find an object for compid %zu\n", compid);
    return false;
  }

  obj->pCompResTable = pCompResTable;
#elif configCHERI_COMPARTMENTALIZATION_MODE == 2
  rtems_rtl_archive* archive = rtl_cherifreertos_compartment_get_archive(compid);

  if (archive == NULL) {
    printf("Couldn't find an archive for compid %zu\n", compid);
    return false;
  }

  archive->pCompResTable = pCompResTable;
#endif

  return true;
}

void
rtl_cherifreertos_compartment_add_resource(size_t compid,
                                           FreeRTOSResource_t xResource)
{
  FreeRTOSCompartmentResources_t* pCompResTable = NULL;

  FreeRTOSResource_t* newRes = pvPortMalloc (sizeof (FreeRTOSResource_t));

  if (newRes == NULL) {
    printf("Failed to add %d resource to compartment %zu\n", (int) xResource.type, compid);
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
    printf("No resources table found for compartment %zu\n", compid);
    return;
  }

  vListInsertEnd (&pCompResTable->buckets[xResource.type],
                  &newRes->node);
}

void
rtl_cherifreertos_compartment_remove_resource(size_t compid,
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
    printf("No resources table found for compartment %zu\n", compid);
    return;
  }

  List_t* resouceList = &pCompResTable->buckets[xResource.type];

  ListItem_t* node = listGET_HEAD_ENTRY (resouceList);

  while (listGET_END_MARKER (resouceList) != node)
  {
    FreeRTOSResource_t* res = (FreeRTOSResource_t *) node;

    if (res->handle == xResource.handle) {
      uxListRemove(node);
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

    uxListRemove(node);
    vTaskDelete(res->handle);

    node = listGET_NEXT (node);
  }
}

void
rtl_cherifreertos_compartment_revoke_resources(size_t compid) {
FreeRTOSCompartmentResources_t* pCompResTable = NULL;

#if configCHERI_COMPARTMENTALIZATION_MODE == 1
  rtems_rtl_obj* obj = rtl_cherifreertos_compartment_get_obj(compid);

  pCompResTable = (FreeRTOSCompartmentResources_t*) obj->pCompResTable;
#elif configCHERI_COMPARTMENTALIZATION_MODE == 2
  rtems_rtl_archive* archive = rtl_cherifreertos_compartment_get_archive(compid);

  pCompResTable = (FreeRTOSCompartmentResources_t*) archive->pCompResTable;
#endif

  if (pCompResTable == NULL) {
    printf("No resources table found for compartment %zu\n", compid);
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

    printf("rtl:debug: %s@0x%x\t", obj->oname, (unsigned int)(uintptr_t) obj->text_base);
    printf("compid = #%3zu ", xCompID);
    printf("captab = %16p\n", captable);

    node = listGET_NEXT (node);
  }
}
#endif
