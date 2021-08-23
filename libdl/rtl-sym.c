/*
 *  COPYRIGHT (c) 2012-2014, 2018 Chris Johns <chrisj@rtems.org>
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
 * @brief RTEMS Run-Time Linker Object File Symbol Table.
 */

#if HAVE_CONFIG_H
#include "waf_config.h"
#endif

#include <errno.h>
#include <inttypes.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <rtl/rtl.h>
#include "rtl-error.h"
#include "rtl-elf.h"
#include <rtl/rtl-sym.h>
#include <rtl/rtl-obj.h>
#include <rtl/rtl-trace.h>
#include <rtl/rtl-freertos-compartments.h>

#ifdef __CHERI_PURE_CAPABILITY__
#include <cheri/cheri-utility.h>
#endif

#define KYEL  "\x1B[33m"
#define KNRM  "\x1B[0m"
#define KBLU  "\x1B[34m"
#define KGRN  "\x1B[32m"
#define KCYN  "\x1B[36m"

/**
 * The single symbol forced into the global symbol table that is used to load a
 * symbol table from an object file.
 */
static rtems_rtl_obj_sym global_sym_add =
{
  .name  = "rtems_rtl_base_sym_global_add",
  .value = (void*) rtems_rtl_base_sym_global_add
};

static uint_fast32_t
rtems_rtl_symbol_hash (const char *s)
{
  uint_fast32_t h = 5381;
  unsigned char c;
  for (c = *s; c != '\0'; c = *++s)
    h = h * 33 + c;
  return h & 0xffffffff;
}

void
rtems_rtl_symbol_global_insert (rtems_rtl_symbols* symbols,
                                rtems_rtl_obj_sym* symbol)
{
  uint_fast32_t hash = rtems_rtl_symbol_hash (symbol->name);
  vListInsertEnd (&symbols->buckets[hash % symbols->nbuckets],
                      &symbol->node);
}

bool
rtems_rtl_symbol_table_open (rtems_rtl_symbols* symbols,
                             size_t             buckets)
{
  symbols->buckets = rtems_rtl_alloc_new (RTEMS_RTL_ALLOC_SYMBOL,
                                          buckets * sizeof (List_t),
                                          true);
  if (!symbols->buckets)
  {
    rtems_rtl_set_error (ENOMEM, "no memory for global symbol table");
    return false;
  }
  symbols->nbuckets = buckets;
  for (buckets = 0; buckets < symbols->nbuckets; ++buckets)
    vListInitialise (&symbols->buckets[buckets]);
  rtems_rtl_symbol_global_insert (symbols, &global_sym_add);
  return true;
}

void
rtems_rtl_symbol_table_close (rtems_rtl_symbols* symbols)
{
  rtems_rtl_alloc_del (RTEMS_RTL_ALLOC_SYMBOL, symbols->buckets);
}

bool
rtems_rtl_symbol_global_add (rtems_rtl_obj*       obj,
                             const unsigned char* esyms,
                             unsigned int         size)
{
  rtems_rtl_symbols* symbols;
  rtems_rtl_obj_sym* sym;
  size_t             count;
  size_t             s;
  uint32_t           marker;

  count = 0;
  s = 0;
  while ((s < size) && (esyms[s] != 0))
  {
    int l = strlen ((char*) &esyms[s]);
    if ((esyms[s + l] != '\0') || ((s + l) > size))
    {
      rtems_rtl_set_error (EINVAL, "invalid exported symbol table");
      return false;
    }
    ++count;
    // 3 long words after the symbol name: value, size and type
    s += l + sizeof (unsigned long) * 3 + 1;
  }

  /*
   * Check this is the correct end of the table.
   */
  marker = esyms[s + 1];
  marker <<= 8;
  marker |= esyms[s + 2];
  marker <<= 8;
  marker |= esyms[s + 3];
  marker <<= 8;
  marker |= esyms[s + 4];

  if (marker != 0xdeadbeefUL)
  {
    rtems_rtl_set_error (ENOMEM, "invalid export symbol table");
    return false;
  }

  if (rtems_rtl_trace (RTEMS_RTL_TRACE_GLOBAL_SYM))
    printf ("rtl: global symbol add: %zi\n", count);

  obj->global_size = count * sizeof (rtems_rtl_obj_sym);
  obj->global_table = rtems_rtl_alloc_new (RTEMS_RTL_ALLOC_SYMBOL,
                                           obj->global_size, true);
  if (!obj->global_table)
  {
    obj->global_size = 0;
    rtems_rtl_set_error (ENOMEM, "no memory for global symbols");
    return false;
  }

#if configCHERI_COMPARTMENTALIZATION
#if configCHERI_COMPARTMENTALIZATION_MODE == 1
  obj->captable = NULL;
  if (!rtl_cherifreertos_captable_alloc(obj, count))
  {
    if (rtems_rtl_trace (RTEMS_RTL_TRACE_CHERI))
      printf("rtl:cheri: Failed to alloc a global cap table for %s\n", obj->oname);

    return 0;
  }
#elif configCHERI_COMPARTMENTALIZATION_MODE == 2
  obj->archive->captable = NULL;
  if (!rtl_cherifreertos_captable_archive_alloc(obj->archive, count))
  {
    if (rtems_rtl_trace (RTEMS_RTL_TRACE_CHERI))
      printf("rtl:cheri: Failed to alloc a global cap table for %s\n", obj->aname);

    return 0;
  }
#endif /* configCHERI_COMPARTMENTALIZATION_MODE */
#endif

  symbols = rtems_rtl_global_symbols ();

  s = 0;
  sym = obj->global_table;

  while ((s < size) && (esyms[s] != 0))
  {
    /*
     * Copy the long using a union and memcpy to avoid any strict aliasing or
     * alignment issues. The variable length of the label and the packed nature
     * of the table means casting is not suitable.
     */
    union {
      uint8_t data[sizeof (unsigned long) * 3];
      unsigned long    value;
    } copy_long;
    int b;

    typedef struct {
      unsigned long value;
      unsigned long size;
      unsigned long type;
    } sym_t;

    sym->name = (const char*) &esyms[s];
    s += strlen (sym->name) + 1;
    for (b = 0; b < sizeof (long) * 3; ++b, ++s)
      copy_long.data[b] = esyms[s];
    sym->value = copy_long.value;

    sym_t *sym_details = (sym_t *) &copy_long.data;

    sym->size = sym_details->size;

#if configCHERI_COMPARTMENTALIZATION
      void *cap = NULL;
      if (ELF_ST_TYPE(sym_details->type) == STT_OBJECT) {
        cap = cheri_build_data_cap((ptraddr_t) sym_details->value,
        sym_details->size,
        __CHERI_CAP_PERMISSION_GLOBAL__ | \
        __CHERI_CAP_PERMISSION_PERMIT_LOAD__ | \
        __CHERI_CAP_PERMISSION_PERMIT_STORE__ | \
        __CHERI_CAP_PERMISSION_PERMIT_LOAD_CAPABILITY__ | \
        __CHERI_CAP_PERMISSION_PERMIT_STORE_CAPABILITY__);
      } else if (ELF_ST_TYPE(sym_details->type) == STT_FUNC) {
        cap = cheri_build_code_cap_unbounded((ptraddr_t) sym_details->value,
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

    if (rtems_rtl_trace (RTEMS_RTL_TRACE_GLOBAL_SYM))
      printf ("rtl: esyms: %s -> 0x%x\n", sym->name, sym->value);
    if (rtems_rtl_symbol_global_find (sym->name) == NULL)
      rtems_rtl_symbol_global_insert (symbols, sym);
    ++sym;
  }

  obj->global_syms = count;

  return true;
}

rtems_rtl_obj_sym*
rtems_rtl_symbol_global_find (const char* name)
{
  rtems_rtl_symbols*   symbols;
  uint_fast32_t        hash;
  List_t* bucket;
  ListItem_t*    node;

  symbols = rtems_rtl_global_symbols ();

  hash = rtems_rtl_symbol_hash (name);
  bucket = &symbols->buckets[hash % symbols->nbuckets];
  node = listGET_HEAD_ENTRY (bucket);

  while (listGET_END_MARKER (bucket) != node)
  {
    rtems_rtl_obj_sym* sym = (rtems_rtl_obj_sym*) node;
    /*
     * Use the hash. I could add this to the symbol but it uses more memory.
     */
    if (strcmp (name, sym->name) == 0)
      return sym;
    node = listGET_NEXT (node);
  }

  return NULL;
}

static int
rtems_rtl_symbol_obj_compare (const void* a, const void* b)
{
  const rtems_rtl_obj_sym* sa;
  const rtems_rtl_obj_sym* sb;
  sa = (const rtems_rtl_obj_sym*) a;
  sb = (const rtems_rtl_obj_sym*) b;
  return strcmp (sa->name, sb->name);
}

void
rtems_rtl_symbol_obj_sort (rtems_rtl_obj* obj)
{
  qsort (obj->local_table,
         obj->local_syms,
         sizeof (rtems_rtl_obj_sym),
         rtems_rtl_symbol_obj_compare);
  qsort (obj->global_table,
         obj->global_syms,
         sizeof (rtems_rtl_obj_sym),
         rtems_rtl_symbol_obj_compare);
  qsort (obj->interface_table,
         obj->interface_syms,
         sizeof (rtems_rtl_obj_sym),
         rtems_rtl_symbol_obj_compare);
}

static rtems_rtl_obj_sym*
rtems_rtl_symbol_list_find (List_t* list, const char* name)
{
  ListItem_t *node = listGET_HEAD_ENTRY (list);

  while (listGET_END_MARKER (list) != node)
  {
    rtems_rtl_obj_sym* sym = (rtems_rtl_obj_sym*) node;

    if (strcmp(sym->name, name) == 0) {
      return sym;
    }

    node = listGET_NEXT (node);
  }

  return NULL;
}

rtems_rtl_obj_sym*
rtems_rtl_lsymbol_obj_find (rtems_rtl_obj* obj, const char* name)
{
#if 0
  return rtems_rtl_symbol_list_find(&obj->locals_list, name);
#else
  rtems_rtl_obj_sym* match = NULL;
  rtems_rtl_obj_sym  key = { 0 };

  if (!rtems_rtl_symbol_name_valid(name)) {
    return NULL;
  }

  key.name = name;
  match = bsearch (&key, obj->local_table,
                   obj->local_syms,
                   sizeof (rtems_rtl_obj_sym),
                   rtems_rtl_symbol_obj_compare);

  return match;
#endif
}

rtems_rtl_obj_sym*
rtems_rtl_gsymbol_obj_find (rtems_rtl_obj* obj, const char* name)
{
#if 0
  return rtems_rtl_symbol_list_find(&obj->globals_list, name);
#else

  if (obj == rtems_rtl_baseimage())
    return NULL;

  rtems_rtl_obj_sym* match = NULL;
  rtems_rtl_obj_sym  key = { 0 };

  if (!rtems_rtl_symbol_name_valid(name)) {
    return NULL;
  }

  key.name = name;
  match = bsearch (&key, obj->global_table,
                   obj->global_syms,
                   sizeof (rtems_rtl_obj_sym),
                   rtems_rtl_symbol_obj_compare);

  return match;
#endif
}

rtems_rtl_obj_sym*
rtems_rtl_isymbol_obj_find (rtems_rtl_obj* obj, const char* name)
{
#if 0
  return rtems_rtl_symbol_list_find(&obj->interface_list, name);
#else
  rtems_rtl_obj_sym* match = NULL;
  rtems_rtl_obj_sym  key = { 0 };

  if (!rtems_rtl_symbol_name_valid(name)) {
    return NULL;
  }

  key.name = name;
  match = bsearch (&key, obj->interface_table,
                   obj->interface_syms,
                   sizeof (rtems_rtl_obj_sym),
                   rtems_rtl_symbol_obj_compare);

  return match;
#endif
}

rtems_rtl_obj_sym*
rtems_rtl_esymbol_obj_find (rtems_rtl_obj* obj, const char* name)
{
  if (!rtems_rtl_symbol_name_valid(name)) {
    return NULL;
  }

  return rtems_rtl_symbol_list_find(&obj->externals_list, name);
}

rtems_rtl_obj_sym*
rtems_rtl_symbol_obj_find (rtems_rtl_obj* obj, const char* name)
{
  rtems_rtl_obj_sym* match = NULL;
  rtems_rtl_obj_sym  key = { 0 };

  if (!rtems_rtl_symbol_name_valid(name)) {
    return NULL;
  }

  key.name = name;
  /*
   * Check the object file's symbols first. If not found search the
   * global symbol table.
   */

#if 0
  if (obj->local_syms)
  {
    match = rtems_rtl_lsymbol_obj_find (obj, name);
    if (match != NULL)
      return match;
  }

  if (obj->global_syms)
  {
    match = rtems_rtl_gsymbol_obj_find (obj, name);
    if (match != NULL)
      return match;
  }

#else
  match = rtems_rtl_lsymbol_obj_find(obj, name);
  if (match != NULL)
    return match;

  match = rtems_rtl_gsymbol_obj_find(obj, name);
  if (match != NULL)
    return match;
#endif

  if (obj->externals_syms)
  {
    match = rtems_rtl_esymbol_obj_find (obj, name);
    if (match != NULL)
      return match;
  }

  /*
   * If the symbol is found in the public global list (FreeRTOS/libc) mint it to
   * the obj cap table.
   */
  match = rtems_rtl_symbol_global_find (name);
  if (match) {
    return rtems_rtl_isymbol_obj_mint(NULL, obj, match->name);
  }

  return NULL;
}

rtems_rtl_obj_sym*
rtems_rtl_symbol_obj_find_namevalue (rtems_rtl_obj* obj, const char* name, UBaseType_t value)
{
#if 0
  ListItem_t *node = listGET_HEAD_ENTRY (&obj->locals_list);

  while (listGET_END_MARKER (&obj->locals_list) != node)
  {
    rtems_rtl_obj_sym* sym = (rtems_rtl_obj_sym*) node;

    if (strcmp(sym->name, name) == 0 && (UBaseType_t) sym->value == value) {
      return sym;
    }

    node = listGET_NEXT (node);
  }

  return NULL;
#else
  return rtems_rtl_lsymbol_obj_find(obj, name);
#endif
}

rtems_rtl_obj_sym*
rtems_rtl_symbol_obj_extract (rtems_rtl_obj* obj, const char* name)
{
#if 0
  ListItem_t *node = listGET_HEAD_ENTRY (&obj->locals_list);

  while (listGET_END_MARKER (&obj->locals_list) != node)
  {
    rtems_rtl_obj_sym* sym = (rtems_rtl_obj_sym*) node;

    if (strcmp(sym->name, name) == 0) {
      uxListRemove(node);
      return sym;
    }

    node = listGET_NEXT (node);
  }

  return NULL;
#else
  rtems_rtl_obj_sym* match = NULL;
  match = rtems_rtl_lsymbol_obj_find(obj, name);

  if (match && listGET_LIST_ITEM_OWNER( &match->node))
  {
    uxListRemove((ListItem_t *) &match->node);
  }

  return match;
#endif
}

bool
rtems_rtl_isymbol_create (rtems_rtl_obj* obj, isymbol_table_mode_t mode)
{
  char *istring = NULL;
  char *name = NULL;
  size_t slen = 0;

  // Copy all the globals to the interface list. Ang global symbol in this
  // compartment can then be accessed from other compartments. That is the
  // simplest and most straightforward policy, following standard C static
  // linking behavior.
  if (mode == RTL_INTERFACE_SYMBOL_ALL_GLOBALS) {

    // Return if that object does not have globals
    if (obj->global_size == 0)
      return true;

    obj->interface_table = rtems_rtl_alloc_new (RTEMS_RTL_ALLOC_SYMBOL,
                                            obj->global_size, true);
    if (!obj->interface_table) {
      obj->interface_syms = 0;
      rtems_rtl_set_error (ENOMEM, "no memory for obj interface syms");
      return false;
    }

    memcpy(obj->interface_table, obj->global_table, obj->global_size);

    istring = (char*) obj->interface_table + (obj->global_syms * sizeof(rtems_rtl_obj_sym));

    for (int i = 0; i < obj->global_syms; i++) {

       vListInitialiseItem(&obj->interface_table[i].node);
       vListInsertEnd(&obj->interface_list, &obj->interface_table[i].node);

       name = obj->global_table[i].name;
       slen = strlen (name) + 1;
       memcpy(istring, name, slen);

       obj->interface_table[i].name = istring;
       istring += slen;
    }

    obj->interface_syms = obj->global_syms;

    return true;
  } else {
    rtems_rtl_set_error (EINVAL, "Invalid mode for creating a new interface list");
    return false;
  }

  return false;
}

rtems_rtl_obj_sym*
rtems_rtl_isymbol_obj_mint (rtems_rtl_obj* src_obj, rtems_rtl_obj* dest_obj, const char* name)
{
  char *estring = NULL;
  size_t slen = 0;
  rtems_rtl_obj_sym *esym = NULL;
  rtems_rtl_obj_sym *sym = NULL;
  bool is_func = false;

  if (!rtems_rtl_symbol_name_valid(name)) {
    return NULL;
  }

  // If src_obj is NULL, search the global symbol table (FreeRTOS/libc) as they
  // do not have an allocated object.
  if (src_obj == NULL) {
    sym = rtems_rtl_symbol_global_find (name);
    src_obj = rtems_rtl_baseimage();
    if (!sym) {
      rtems_rtl_set_error (ENOMEM, "Could not find %s in the global symbol table", name);
      return NULL;
    }
  } else {
    // Seach the interface list of the src_obj to check if it does own that symbol
    // TODO: check of dest_obj is allowed to call src_obj:name
    sym = rtems_rtl_isymbol_obj_find(src_obj, name);
    if (!sym) {
      return NULL;
    }
  }

  if (ELF_ST_TYPE(sym->data >> 16) == STT_FUNC)
    is_func = true;

  slen = strlen(name) + 1;

  // Allocate a new buffer for the symbol and its name
  esym = rtems_rtl_alloc_new (RTEMS_RTL_ALLOC_SYMBOL, sizeof(rtems_rtl_obj_sym) + slen, true);
  if (!esym) {
    rtems_rtl_set_error (ENOMEM, "no memory for an external symbol");
    return NULL;
  }

  estring = (char*) esym + (sizeof(rtems_rtl_obj_sym));

  // Copy the symbol from interface table to externals table
  memcpy(esym, sym, sizeof(rtems_rtl_obj_sym));
  memcpy(estring, name, slen);

#if configCHERI_COMPARTMENTALIZATION
  // Allocate a new cap slot in the interface captable and install it
#if configCHERI_COMPARTMENTALIZATION_MODE == 1
  esym->capability = rtl_cherifreertos_captable_install_new_cap(dest_obj, *(src_obj->captable + sym->capability));

#elif configCHERI_COMPARTMENTALIZATION_MODE == 2
  if (src_obj->archive->comp_id != dest_obj->archive->comp_id)
  {
    esym->capability = rtl_cherifreertos_captable_install_new_cap(dest_obj, src_obj->archive->captable[sym->capability]);
  }
#endif /* configCHERI_COMPARTMENTALIZATION_MODE */
  if (!esym->capability) {
    rtems_rtl_set_error (ENOMEM, "Could not mint a new cap to the dest obj");
    return NULL;
  }
#endif

  // Add the symbol to the dest_obj extenals list
  vListInitialiseItem(&esym->node);
  vListInsertEnd(&dest_obj->externals_list, &esym->node);
  dest_obj->externals_syms++;

  return esym;
}

#if configCHERI_STACK_TRACE
static void*
rtems_rtl_symbol_global_find_by_address (size_t target_pc)
{
  rtems_rtl_symbols*   symbols;
  List_t* bucket;
  ListItem_t*    node;
  size_t sym_addr = 0;

  symbols = rtems_rtl_global_symbols ();
  rtems_rtl_obj* obj = rtems_rtl_baseimage();

  for (int i = 0; i < symbols->nbuckets; i++) {
    bucket = &symbols->buckets[i];
    node = listGET_HEAD_ENTRY (bucket);
    while (listGET_END_MARKER (bucket) != node)
    {
      rtems_rtl_obj_sym* sym = (rtems_rtl_obj_sym*) node;

      sym_addr = (size_t) sym->value;

      if ( (target_pc >= sym_addr) && (target_pc < (sym_addr + sym->size)))
      {
          printf("%s0x%x: %s%s%s<%s+%d>\n", KBLU, (size_t) target_pc, KGRN, obj->oname, KYEL, sym->name, target_pc - sym_addr);
          void** captab = rtl_cherifreertos_compartment_obj_get_captable(obj);
          return (void *) captab[sym->capability];
      }

      node = listGET_NEXT (node);
    }
  }

  return NULL;
}

void*
rtl_cherifreertos_compartment_backtrace(void* pc, void* sp, void* ret_reg, size_t xCompID) {

  rtems_rtl_obj_sym* sym;
  size_t             s;
  size_t target_pc = (size_t) pc;
  size_t sym_addr = 0;
  void* func_addr = NULL;

#if configCHERI_COMPARTMENTALIZATION_MODE == 1
  rtems_rtl_obj* obj = rtl_cherifreertos_compartment_get_obj(xCompID);
#else
  rtems_rtl_obj* obj = rtems_rtl_baseimage();
#endif
  if (obj == NULL) {
    printf("%s", KNRM);
    return NULL;
  }

  printf("%s", KYEL);

  // Search FreeRTOS kernel's symtab
  func_addr = rtems_rtl_symbol_global_find_by_address((size_t) pc);

  // Search object compartment's global symols
  for (s = 0, sym = obj->global_table; func_addr == NULL && s < obj->global_syms; ++s, ++sym) {
    sym_addr = sym->value;
    if ( target_pc >= sym_addr && target_pc < sym_addr + sym->size) {
      printf("%s0x%x: %s%s%s<%s+%d>\n", KBLU, (size_t) pc, KGRN, obj->oname, KYEL, sym->name, pc - sym->value);
      func_addr = sym->value;
      break;
    }
  }

  // Search object compartment's local symols
  for (s = 0, sym = obj->local_table; func_addr == NULL && s < obj->local_syms; ++s, ++sym) {
    sym_addr = sym->value;
    if ( target_pc >= sym_addr && target_pc < sym_addr + sym->size) {
      printf("%s0x%x: %s%s%s<%s+%d>\n", KBLU, (size_t) pc, KGRN, obj->oname, KYEL, sym->name, pc - sym->value);
      func_addr = sym->value;
      break;
    }
  }

  // Found a function symbol for the target PC
  if (func_addr) {

    // Build a new cap for a function symbol, just need to read the first instruction
    func_addr = cheri_build_data_cap((size_t) func_addr, 16, __CHERI_CAP_PERMISSION_PERMIT_LOAD__);

    // Read the first instruction of the function to help walk/unwind the stack
    int32_t func_instruction;
    memcpy(&func_instruction, func_addr, 4);

    size_t frame_size = 0;
    uint32_t func_opcode = func_instruction & ((0x1 << 12) | 0x7f);
    uint32_t cincoffimm_opcode = (0x1 << 12) | 0x5b;
    void* prev_csp;
    void* prev_cra;
    uint32_t prev_cra_instruction;
    size_t prevCompID = xCompID;

    #if __riscv_xlen == 64
        uint32_t tramp_switch_instructions[] = {0x03012d8f,  // inter-compartment return instruction (load_x cs0, 13 * portWORD_SIZE(csp))
                                                0x0d01240f}; // intra-compartment return instruction (load_x cs11, 3 * portWORD_SIZE(csp))
    #else
        uint32_t tramp_switch_instructions[] = {0x01813d83,  // inter-compartment return instruction (load_x cs0, 13 * portWORD_SIZE(csp))
                                                0x06813403}; // intra-compartment return instruction (load_x cs11, 3 * portWORD_SIZE(csp))
    #endif

    // For non-leaf function, the very first instruction is a cincoffset of csp
    // to setup the stack frame
    if (func_opcode == cincoffimm_opcode) {
       // decode frame size
       frame_size = (func_instruction >> 20) * -1;

       prev_csp = sp + frame_size;
       prev_cra = (void *) *((uintcap_t *) (prev_csp - sizeof(void *)));
    } else { // Leaf functions not setting up a stack frame (> -O1)
       // previous stack pointer is the same as the current one didn't setup or
       // have a stack frame being a leaf function.
       prev_csp = sp;

       // cra of the leaf function that got an exception
       prev_cra = ret_reg;
    }

    // Build a cap for prev_cra (which is likely  a sentry) and read it
    prev_cra = cheri_build_data_cap((size_t) prev_cra, 16, __CHERI_CAP_PERMISSION_PERMIT_LOAD__);
    memcpy(&prev_cra_instruction, prev_cra, 4);

    // Check if the previous stack frame is a compartment trampoline, then skip/unwind it
    if ((prev_cra_instruction == tramp_switch_instructions[0] ||
         prev_cra_instruction == tramp_switch_instructions[1])) {

      // If it was a compartment_switch, get the previous compartment ID
      if (tramp_switch_instructions[0] == prev_cra_instruction) {
        prevCompID = (size_t) *((size_t *) (prev_csp - 0 * sizeof(void *)));
        printf("%s0x%x: %s<compartment_switch>\n", KBLU, (size_t) prev_cra, KCYN);
      }

      // If the previous compartment ID is valid, unwind the trampoline and recursively
      // walk the stack.
      if (prevCompID < configCOMPARTMENTS_NUM) {
        // Get the pre-trampoline stack frame
        prev_csp += 15 * sizeof(void*);
        // Get the pre-trampoline cra
        prev_cra = (void *) *((uintcap_t *) (prev_csp - sizeof(void *)));

        // Recursively stack trace the pre-trampoline function/cra
        func_addr = rtl_cherifreertos_compartment_backtrace(prev_cra, prev_csp, prev_cra, prevCompID);
      } else { // The previous compartment is invalid or not a compartment
        printf("%s", KNRM);
        return NULL;
      }
    } else { // Not a trampoline, just normal c-function call. Recursively unwind
      func_addr = rtl_cherifreertos_compartment_backtrace(prev_cra, prev_csp, prev_cra, prevCompID);
    }
  } else { // Function not found, return and end stack trace
    printf("%s", KNRM);
    return NULL;
  }
}
#endif

void
rtems_rtl_symbol_obj_add (rtems_rtl_obj* obj)
{
  rtems_rtl_symbols* symbols;
  rtems_rtl_obj_sym* sym;
  size_t             s;

  symbols = rtems_rtl_global_symbols ();

  for (s = 0, sym = obj->global_table; s < obj->global_syms; ++s, ++sym)
    rtems_rtl_symbol_global_insert (symbols, sym);
}

void
rtems_rtl_symbol_obj_erase_local (rtems_rtl_obj* obj)
{
  if (obj->local_table)
  {
    rtems_rtl_alloc_del (RTEMS_RTL_ALLOC_SYMBOL, obj->local_table);
    obj->local_table = NULL;
    obj->local_size = 0;
    obj->local_syms = 0;
  }
}

void
rtems_rtl_symbol_obj_erase (rtems_rtl_obj* obj)
{
  rtems_rtl_symbol_obj_erase_local (obj);
  if (obj->global_table)
  {
    rtems_rtl_obj_sym* sym;
    size_t             s;
    for (s = 0, sym = obj->global_table; s < obj->global_syms; ++s, ++sym)
        if (listLIST_ITEM_CONTAINER (&sym->node))
          uxListRemove (&sym->node);
    rtems_rtl_alloc_del (RTEMS_RTL_ALLOC_SYMBOL, obj->global_table);
    obj->global_table = NULL;
    obj->global_size = 0;
    obj->global_syms = 0;
  }
}
