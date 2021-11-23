/*
 *  COPYRIGHT (c) 2012, 2018 Chris Johns <chrisj@rtems.org>
 *
 *  The license and distribution terms for this file may be
 *  found in the file LICENSE in this distribution or at
 *  http://www.rtems.org/license/LICENSE.
 */
/**
 * @file
 *
 * @ingroup rtl
 *
 * @brief RTEMS POSIX Dynamic Module Loading Interface.
 *
 * This is the POSIX interface to run-time loading of code into RTEMS.
 */

#include <stdint.h>
#include <dlfcn.h>
#include <rtl/rtl.h>
#include <sys/exec_elf.h>

#include <FreeRTOSConfig.h>

#ifdef __CHERI_PURE_CAPABILITY__
#include <rtl/rtl-freertos-compartments.h>
#include <cheri/cheri-utility.h>
#endif

static rtems_rtl_obj*
dl_get_obj_from_handle (void* handle)
{
  rtems_rtl_obj* obj;

  /*
   * Handle the special cases provided in NetBSD and Sun documentation.
   *   http://download.oracle.com/docs/cd/E19253-01/816-5168/dlsym-3c/index.html
   * We currently do not manage the loading dependences in the module mappings
   * so we cannot handle the searching based on loading order where overriding
   * can occur.
   */

  if ((handle == RTLD_DEFAULT) || (handle == RTLD_SELF))
    obj = rtems_rtl_baseimage ();
  else
    obj = rtems_rtl_check_handle (handle);

  return obj;
}

void*
dlopen (const char* name, int mode)
{
  rtems_rtl_obj* obj = NULL;

  if (!rtems_rtl_lock ())
    return NULL;

  _rtld_debug.r_state = RT_ADD;

  if (name)
    obj = rtems_rtl_load (name, mode);
  else
    obj = rtems_rtl_baseimage ();

  _rtld_debug.r_state = RT_CONSISTENT;

  #if configLIBDL_GDB_DEBUG
    _rtld_debug_state ();
  #endif

  #if configCHERI_COMPARTMENTALIZATION || configMPU_COMPARTMENTALIZATION
    rtl_cherifreertos_debug_print_compartments();
  #endif

  rtems_rtl_unlock ();

  return obj;
}

int
dlclose (void* handle)
{
  rtems_rtl_obj* obj;
  int            r;

  if (!rtems_rtl_lock ())
    return -1;

  obj = rtems_rtl_check_handle (handle);
  if (!obj)
  {
    rtems_rtl_unlock ();
    return -1;
  }

  _rtld_debug.r_state = RT_DELETE;

  #if configLIBDL_GDB_DEBUG
    _rtld_debug_state ();
  #endif

  r = rtems_rtl_unload (obj) ? 0 : -1;

  _rtld_debug.r_state = RT_CONSISTENT;

  #if configLIBDL_GDB_DEBUG
    _rtld_debug_state ();
  #endif

  rtems_rtl_unlock ();

  return r;
}

void*
dlsym (void* handle, const char *symbol)
{
  rtems_rtl_obj*     obj;
  rtems_rtl_obj_sym* sym = NULL;
  uintptr_t          symval = 0;

  if (!rtems_rtl_lock ())
    return NULL;

  /*
   * If the handle is "default" search the global symbol table.
   */
  if (handle == RTLD_DEFAULT)
  {
    sym = rtems_rtl_symbol_global_find (symbol);
  }
  else
  {
    obj = dl_get_obj_from_handle (handle);
    if (obj)
      sym = rtems_rtl_symbol_obj_find (obj, symbol);
  }

  if (sym)
    symval = sym->value;
  else {
    return NULL;
  }

#if configCHERI_COMPARTMENTALIZATION
    void* tramp_cap;
    void** captable = rtl_cherifreertos_compartment_obj_get_captable(obj);
    symval = (uintptr_t) captable[sym->capability];
    /* Setup a compartment switch trampoline if it is a function */
    if (ELF_ST_TYPE(sym->data >> 16) == STT_FUNC) {
      tramp_cap = rtl_cherifreertos_compartments_setup_ecall((void*) symval, rtl_cherifreertos_compartment_get_compid(obj));
      if (tramp_cap == NULL)
        return NULL;
      else
        symval = (uintptr_t) tramp_cap;
    }
#endif

  rtems_rtl_unlock ();

  return (void*) symval;
}

const char*
dlerror (void)
{
  static char msg[64];
  int         eno;
  eno = rtems_rtl_get_error (msg, sizeof (msg));
  if (eno == 0)
    return NULL;
  return msg;
}

int
dlinfo (void* handle, int request, void** p)
{
  rtems_rtl_obj* obj;
  int            rc = -1;

  if (!rtems_rtl_lock () || !p)
    return -1;

  obj = dl_get_obj_from_handle (handle);
  if (obj)
  {
    switch (request)
    {
      case RTLD_DI_UNRESOLVED:
        *((int*) p) = rtems_rtl_obj_unresolved (obj) ? 1 : 0;
        rc = 0;
        break;
#if configCHERI_COMPARTMENTALIZATION
      case RTLD_DI_CHERI_CAPTABLE: {
        void** captable = rtl_cherifreertos_compartment_obj_get_captable(obj);
        *p = cheri_seal_cap(captable, rtl_cherifreertos_compartment_get_compid(obj));
        rc = 0;
     } break;
#endif
      default:
        break;
    }
  }

  rtems_rtl_unlock ();

  return rc;
}
