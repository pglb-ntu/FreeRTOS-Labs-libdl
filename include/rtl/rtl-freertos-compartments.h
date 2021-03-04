#if !defined (_FREERTOS_RTL_COMPARTMENTS_H_)
#define _FREERTOS_RTL_COMPARTMENTS_H_

#include <FreeRTOS.h>
#include <FreeRTOSConfig.h>
#include <stdbool.h>
#include <rtl/rtl-obj.h>

typedef struct compartment {
  void**      captable;
#if configCHERI_COMPARTMENTALIZATION_MODE == 1
  void*       obj;
#elif configCHERI_COMPARTMENTALIZATION_MODE == 2
  void*       archive;
#endif /* configCHERI_COMPARTMENTALIZATION_MODE */
  uint64_t    id;
} Compartment_t;

extern Compartment_t comp_list[configCOMPARTMENTS_NUM];
extern char comp_strtab[configCOMPARTMENTS_NUM][configMAXLEN_COMPNAME];

void *rtl_freertos_compartment_open(const char *name);
bool rtl_freertos_compartment_close(rtems_rtl_obj* obj);
size_t rtl_freertos_compartment_read(void* fd, void *buffer, UBaseType_t offset, size_t count);
size_t rtl_freertos_compartment_getsize(void* fd);
size_t rtl_freertos_global_symbols_add(rtems_rtl_obj* obj);

/**
 * Set the obj cap this compartment points to.
 *
 * @param obj The object compartment to set.
 * @retval true If set successfully
 */
bool
rtl_cherifreertos_compartment_set_obj(rtems_rtl_obj* obj);

/**
 * Set the archive cap this compartment points to.
 *
 * @param archive The archive compartment to set.
 * @retval true If set successfully
 */
bool
rtl_cherifreertos_compartment_set_archive(rtems_rtl_archive* archive);

/**
 * Get the object pointer this comp_id/otype refers to.
 *
 * @param comp_id The compartment ID (held in the otype of the cap)
 * @retval NULL if failed, or a pointer to the obj if found.
 */
rtems_rtl_obj *
rtl_cherifreertos_compartment_get_obj(size_t comp_id);


/**
 * Get the archive pointer this comp_id/otype refers to.
 *
 * @param comp_id The compartment ID (held in the otype of the cap)
 * @retval NULL if failed, or a pointer to the archive if found.
 */
rtems_rtl_archive *
rtl_cherifreertos_compartment_get_archive(size_t comp_id);

/**
 * Set the obj captable for a compartment.
 *
 * @param obj The object compartment to set the captable for.
 * @retval true If set successfully
 */
bool
rtl_cherifreertos_compartment_set_captable(rtems_rtl_obj* obj);

/**
 * Set the archive captable for a compartment.
 *
 * @param archive The archive compartment to set the captable for.
 * @retval true If set successfully
 */
bool
rtl_cherifreertos_archive_compartment_set_captable(rtems_rtl_archive* archive);

/**
 * Get the captable this comp_id/otype refers to.
 *
 * @param comp_id The compartment ID (held in the otype of the cap)
 * @retval NULL if failed, or a pointer to the captable if found.
 */
void **
rtl_cherifreertos_compartment_get_captable(size_t comp_id);

/**
 * Allocate a new array-based captable for an object.
 *
 * @param obj The object compartment to allocate a captable for.
 * @param caps_count The number of capabilities to allocate a table for.
 * @retval true If allocated successfully
 * @retval false The table could not be created. The RTL error has the error.
 */
bool
rtl_cherifreertos_captable_alloc(rtems_rtl_obj* obj, size_t caps_count);

/**
 * Allocate a new array-based captable for an archive.
 *
 * @param archive The archive compartment to allocate a captable for.
 * @param caps_count The number of capabilities to allocate a table for.
 * @retval true If allocated successfully
 * @retval false The table could not be created. The RTL error has the error.
 */
bool
rtl_cherifreertos_captable_archive_alloc(rtems_rtl_archive* archive, size_t caps_count);

/**
 * Allocate a separate stack for each compartment object
 *
 * @param obj The object compartment to allocate a stack for.
 * @param stack_depths The depth (multiple of cap size) of the stack.
 * @retval true If allocated successfully
 * @retval false If couldn't be allocated. The RTL error has the error.
 */
bool
rtl_cherifreertos_capstack_alloc(rtems_rtl_obj* obj, size_t stack_depth);

/**
 * Install a new capability in the first free slot in the captable of an object.
 * If the table is full, it will try to re-alloc a new captable to increase the
 * size and fit in the new cap.
 *
 * @param obj The object compartment to allocate a captable for.
 * @param new_cap The content of the capability to install in the table.
 * @retval A pointer to the slot in the captable the new_cap got installed into
 * returns 0 if failed and The RTL error has the error.
 */
uint32_t
rtl_cherifreertos_captable_install_new_cap(rtems_rtl_obj* obj, void* new_cap);

/**
 * Get a new compartment ID value to set a newly loaded compartment with
 */
uint32_t
rtl_cherifreertos_compartment_get_free_compid(void);

/**
 * Get the compartment ID value for a compartment from an object.
 */
uint32_t
rtl_cherifreertos_compartment_get_compid(rtems_rtl_obj* obj);

/**
 * Get the captable value for a compartment from an object.
 */
void **
rtl_cherifreertos_compartment_obj_get_captable(rtems_rtl_obj* obj);

#if __CHERI_PURE_CAPABILITY__
/**
 * Create a new inter-compartment trampoline for external domain-crossing calls
 */
void*
rtl_cherifreertos_compartments_setup_ecall(uintcap_t code, BaseType_t compid);
#endif /* __CHERI_PURE_CAPABILITY__ */
#endif
