#if !defined (_FREERTOS_RTL_COMPARTMENTS_H_)
#define _FREERTOS_RTL_COMPARTMENTS_H_

#include <FreeRTOS.h>
#include <FreeRTOSConfig.h>
#include <stdbool.h>
#include <rtl/rtl-obj.h>

typedef struct compartment {
  void**      captable;
#if ((configCHERI_COMPARTMENTALIZATION_MODE == 1) || (configMPU_COMPARTMENTALIZATION_MODE == 1))
  void*       obj;
#elif ((configCHERI_COMPARTMENTALIZATION_MODE == 2) || (configMPU_COMPARTMENTALIZATION_MODE == 2))
  void*       archive;
#endif /* configCHERI_COMPARTMENTALIZATION_MODE */
  uint64_t    id;
  
  uint64_t counter;
} Compartment_t;

typedef enum resource {
  FREERTOS_TASK,
  FREERTOS_QUEUE,
  FREERTOS_TIMER,
  FREERTOS_EVENT,
  FREERTOS_STREAMBUFF,
  FREERTOS_MSGBUFF,
  FREERTOS_MEM,

  FREERTOS_OBJ_COUNT
} FreeRTOSResourceType_t;

typedef struct ResourceObj {
  ListItem_t                node;
  void*                     handle;
  FreeRTOSResourceType_t    type;
} FreeRTOSResource_t;

typedef struct resouceTable {
  List_t      *buckets;
  size_t      nbuckets;
} FreeRTOSCompartmentResources_t;

extern Compartment_t comp_list[configCOMPARTMENTS_NUM];
extern char comp_strtab[configCOMPARTMENTS_NUM][configMAXLEN_COMPNAME];

int rtl_freertos_compartment_open(const char *name);
bool rtl_freertos_compartment_close(rtems_rtl_obj* obj);
size_t rtl_freertos_compartment_read(int fd, void *buffer, UBaseType_t offset, size_t count);
size_t rtl_freertos_compartment_getsize(int fd);
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

/**
 * Set permissions on the captable for a given compartment
 */
bool
rtl_cherifreertos_compartment_captable_set_perms (size_t xCompID);

/**
 * Print all loaded object compartments with extra debugging info
 */
void
rtl_cherifreertos_debug_print_compartments(void);

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
size_t
rtl_cherifreertos_compartment_get_free_compid(void);

/**
 * Get the compartment ID value for a compartment from an object.
 */
size_t
rtl_cherifreertos_compartment_get_compid(rtems_rtl_obj* obj);

/**
 * Get the captable value for a compartment from an object.
 */
void **
rtl_cherifreertos_compartment_obj_get_captable(rtems_rtl_obj* obj);

/**
 * Get the number of regions/symbols each compartment protects.
 */
size_t
rtl_cherifreertos_compartment_get_regions_count(size_t compid);

#if __CHERI_PURE_CAPABILITY__
/**
 * Create a new inter-compartment trampoline for external domain-crossing calls
 */
void*
rtl_cherifreertos_compartments_setup_ecall(void* code, size_t compid);

/**
 * Register a fault handler for a compartment passed in handler.
 */
void
rtl_cherifreertos_compartment_register_faultHandler(size_t compid, void* handler);

/**
 * Call a fault handler of a compartment.
 * Return true if a reschedule is required
 */
bool
rtl_cherifreertos_compartment_faultHandler(size_t compid);

/**
 * Initialize data structures needed to bookkeep run-time allocated FreeRTOS
 * resourced per compartment.
 */
bool
rtl_cherifreertos_compartment_init_resources (size_t compid);

/**
 * Add/Delete FreeRTOS allocated resoucres to a compartment.
 */
void
rtl_cherifreertos_compartment_add_resource(size_t compid,
                                           FreeRTOSResource_t xResource);
void
rtl_cherifreertos_compartment_remove_resource(size_t compid,
                                              FreeRTOSResource_t xResource);

/**
 * Iterate over all compartment-owned resources and revoke them
 */
void
rtl_cherifreertos_compartment_revoke_resources(size_t compid);

/**
 * Trace the stack and function calls from a given @pc across CHERI compartments
 * and print a backtrace of that, including compartment switches.
 *
 * @param pc: The start pc to backtrace from
 * @param sp: The leaf stack pointer for @pc
 * @param ra: Return address of the current function (in case it is a leaf one)
 * @param xCompID: Current compartment ID for @pc when called
 */
void*
rtl_cherifreertos_compartment_backtrace(void* pc, void* sp, void* ra, size_t xCompID);

/**
 * Check if the symbol exists in the same object or library compartment. If it does not,
 * this is an inter-compartment call that should trigger a compartment switch.
 */
bool
rtl_cherifreertos_is_inter_compartment(rtems_rtl_obj* obj, const char* symaname);

int rtl_cherifreertos_compartment_snapshot(size_t compid);
int rtl_cherifreertos_compartment_rollback(size_t compid);
int rtl_cherifreertos_compartments_snapshot(void);

#endif /* __CHERI_PURE_CAPABILITY__ */
#endif
