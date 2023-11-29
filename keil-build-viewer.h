#ifndef __KEIL_BUILD_VIEWER_H__
#define __KEIL_BUILD_VIEWER_H__

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include <windows.h>

#define APP_NAME                        "keil-build-viewer"
#define APP_VERSION                     "v1.5"

#define MAX_DIR_HIERARCHY               32      /* 最大目录层级 */
#define MAX_PATH_QTY                    32      /* 最大目录数量 */
#define MAX_FILE_QTY                    512     /* 最大文件数量 */
#define MAX_PRJ_NAME_SIZE               128     /* 最大工程名称长度 */
#define OBJECT_INFO_STR_QTY             7       /* Code + (inc. data) + RO Data + RW Data + ZI Data + Debug + Object Name */

#define ENABLE_REFER_TO_KEIL_DIALOG     0       /* 当 chip 没有对应的 keil pack 且使用自定义的 scatter file 时，是否参考 keil 的 memory dialog */

#define UNKNOWN_MEMORY_ID               1
#define ZI_SYMBOL_0                     0x4F    /* O */
#define USED_SYMBOL_0                   0x23    /* # */
#define ZI_SYMBOL_1                     0x4F    /* O */
#define USED_SYMBOL_1                   0x58    /* X */
#define ZI_SYMBOL_GBK_H                 0xA1    /* □ */
#define ZI_SYMBOL_GBK_L                 0xF5
#define USED_SYMBOL_GBK_H               0xA1    /* ■ */
#define USED_SYMBOL_GBK_L               0xF6
#define ZI_SYMBOL_BIG5_H                0xA1    /* □ */
#define ZI_SYMBOL_BIG5_L                0xBC
#define USED_SYMBOL_BIG5_H              0xA1    /* ■ */
#define USED_SYMBOL_BIG5_L              0xBD
#define UNUSE_SYMBOL                    "_"

#define STR_ZERO_INIT                   " Zero "
#define STR_PADDING                     " PAD"
#define STR_RENAME_MARK                 " - object file renamed from "
#define STR_COMPILING                   "compiling "
#define STR_MAX_STACK_USAGE             "Maximum Stack Usage "
#define STR_FILE                        "FILE(s)"
#define STR_LTO_LLVW                    "lto-llvm-"
#define STR_MEMORY_MAP_OF_THE_IMAGE     "Memory Map of the image"
#define STR_LOAD_REGION                 "Load Region"
#define STR_EXECUTION_REGION            "Execution Region"
#define STR_LOAD_BASE                   "Load base: "
#define STR_REGION_USED_SIZE            "Size: "
#define STR_REGION_MAX_SIZE             "Max: "
#define STR_EXECUTE_BASE                "Base: "
#define STR_EXECUTE_BASE_ADDR           "Exec base: "
#define STR_IMAGE_COMPONENT_SIZE        "Image component sizes"
#define STR_OBJECT_NAME                 "Object Name"
#define STR_LIBRARY_MEMBER_NAME         "Library Member Name"
#define STR_LIBRARY_NAME                "Library Name"
#define STR_OBJECT_TOTALS               "Object Totals"
#define STR_LIBRARY_TOTALS              "Library Totals"
#define LABEL_TARGET_NAME               "<TargetName>"
#define LABEL_IS_CURRENT_TARGET         "<IsCurrentTarget>"
#define LABEL_DEVICE                    "<Device>"
#define LABEL_VENDOR                    "<Vendor>"
#define LABEL_CPU                       "<Cpu>"
#define LABEL_OUTPUT_DIRECTORY          "<OutputDirectory>"
#define LABEL_OUTPUT_NAME               "<OutputName>"
#define LABEL_LISTING_PATH              "<ListingPath>"
#define LABEL_IS_CREATE_MAP             "<AdsLLst>"
#define LABEL_AC6_LTO                   "<v6Lto>"
#define LABEL_IS_KEIL_SCATTER           "<umfTarg>"
#define LABEL_END_GROUPS                "</Groups>"
#define LABEL_END_FILE                  "</File>"
#define LABEL_END_FILES                 "</Files>"
#define LABEL_END_CADS                  "</Cads>"
#define LABEL_END_LDADS                 "</LDads>"                      
#define LABEL_GROUP_NAME                "<GroupName>"
#define LABEL_FILE_NAME                 "<FileName>"
#define LABEL_FILE_TYPE                 "<FileType>"
#define LABEL_FILE_PATH                 "<FilePath>"
#define LABEL_INCLUDE_IN_BUILD          "<IncludeInBuild>"
#define LABEL_ONCHIP_MEMORY             "<OnChipMemories>"
#define LABEL_END_ONCHIP_MEMORY         "</OnChipMemories>"
#define LABEL_MEMORY_AREA               "<OCR_RVCT"
#define LABLE_END_MEMORY_AREA           "</OCR_RVCT"
#define LABEL_MEMORY_TYPE               "<Type>"
#define LABEL_MEMORY_ADDRESS            "<StartAddress>"
#define LABEL_MEMORY_SIZE               "<Size>"

#define log_save(log, fmt, ...)         log_write(log, false, fmt, ##__VA_ARGS__)
#define log_print(log, fmt, ...)        log_write(log, true, fmt, ##__VA_ARGS__)


typedef enum
{
    ENCODING_TYPE_GBK = 0x00,
    ENCODING_TYPE_BIG5,
    ENCODING_TYPE_OTHER,

} ENCODING_TYPE;

typedef enum
{
    PROGRESS_STYLE_0 = 0x00,
    PROGRESS_STYLE_1,
    PROGRESS_STYLE_2,
    
} PROGRESS_STYLE;

typedef enum
{
    
    MEMORY_PRINT_MODE_0 = 0x00, /* keil pack 有 RAM 和 ROM 信息（多数情况） */
    MEMORY_PRINT_MODE_1,        /* keil pack 没有 RAM 和 ROM 信息，但 memory 使用了 keil dialog 配置 */
    MEMORY_PRINT_MODE_2,        /* keil pack 没有 RAM 和 ROM 信息，并且使用自定义的 scatter file */

} MEMORY_PRINT_MODE;

typedef enum 
{
    MEMORY_TYPE_NONE = 0x00,
    MEMORY_TYPE_RAM,
    MEMORY_TYPE_FLASH,
    MEMORY_TYPE_UNKNOWN,

} MEMORY_TYPE;

typedef enum 
{
    OBJECT_FILE_TYPE_UNKNOWN = 0x00,
    OBJECT_FILE_TYPE_USER,
    OBJECT_FILE_TYPE_OBJECT,
    OBJECT_FILE_TYPE_LIBRARY,

} OBJECT_FILE_TYPE;


/* keil 工程路径存储链表 */
struct prj_path_list
{
    char **items;
    size_t capacity;
    size_t size;
};

struct object_info
{
    char *name;
    char *path;
    uint16_t code;
    uint16_t ro_data;
    uint16_t rw_data;
    uint16_t zi_data;
    struct object_info *old_object;
    struct object_info *next;
};

struct region_block
{
    uint32_t start_addr;
    uint32_t size;
    struct region_block *next;
};

struct exec_region
{
    char *name;
    size_t memory_id;       /* 从 1 开始， 1 固定为 unknown */
    uint32_t base_addr;
    uint32_t size;
    uint32_t used_size;
    MEMORY_TYPE memory_type;
    bool is_offchip;
    bool is_printed;

    struct region_block *zi_block;
    struct exec_region *old_exec_region;
    struct exec_region *next;
};

struct load_region
{
    char *name;
    struct exec_region *exec_region;
    struct load_region *next;
};

struct memory_info
{
    char *name;
    size_t id;
    uint32_t base_addr;
    uint32_t size;
    MEMORY_TYPE type;
    bool is_from_pack;
    bool is_offchip;
    struct memory_info *next;
};

struct file_path_list
{
    char *old_name;         /* 原名 */
    char *object_name;      /* 更改为 .o 后缀名的名称 */
    char *new_object_name;  /* 因重名而改名后的名称，为 .o 后缀 */
    char *path;
    bool is_rename;
    OBJECT_FILE_TYPE file_type;
    struct file_path_list *next;
};

struct command_list
{
    const char *cmd;
    const char *desc;
};

struct uvprojx_info
{
    bool is_has_pack;
    bool is_enable_lto;
    bool is_has_user_lib;
    bool is_custom_scatter;
    char chip[MAX_PRJ_NAME_SIZE];
    char target_name[MAX_PRJ_NAME_SIZE];
    char output_name[MAX_PRJ_NAME_SIZE];
    char output_path[MAX_PATH];
    char listing_path[MAX_PATH];
};


bool                    is_keil_project             (const char *path);
bool                    is_same_string              (const char *str1, 
                                                     const char *str2[], 
                                                     size_t      str2_qty);
int                     combine_path                (char       *out_path,
                                                     size_t      out_path_size,
                                                     const char *absolute_path, 
                                                     const char *relative_path);
bool                    file_path_add               (struct file_path_list **path_head,
                                                     const char *name,
                                                     const char *path,
                                                     OBJECT_FILE_TYPE file_type);
void                    file_path_free              (struct file_path_list **path_head);
bool                    memory_info_add             (struct memory_info **memory_head,
                                                     const char *name,
                                                     size_t      id,
                                                     uint32_t    base_addr,
                                                     uint32_t    size,
                                                     MEMORY_TYPE mem_type,
                                                     bool        is_offchip,
                                                     bool        is_from_pack);
void                    memory_info_free            (struct memory_info **memory_head);
bool                    object_info_add             (struct object_info **object_head,
                                                     const char *name,
                                                     uint16_t    code,
                                                     uint16_t    ro_data,
                                                     uint16_t    rw_data,
                                                     uint16_t    zi_data);
void                    object_info_free            (struct object_info **object_head);
struct load_region *    load_region_create          (struct load_region **region_head, const char *name);
struct exec_region *    load_region_add_exec_region (struct load_region **region_head, 
                                                     const char *name,
                                                     size_t      memory_id,
                                                     uint32_t    base_addr,
                                                     uint32_t    size,
                                                     uint32_t    used_size,
                                                     MEMORY_TYPE mem_type,
                                                     bool        is_offchip);
void                    load_region_free            (struct load_region **region_head);
void                    search_files_by_extension   (const char *dir,
                                                     size_t dir_len,
                                                     const char *extension[], 
                                                     size_t extension_qty, 
                                                     struct prj_path_list *list);
struct prj_path_list *  prj_path_list_init          (size_t capacity);
void                    prj_path_list_add           (struct prj_path_list *list, char *path);
void                    prj_path_list_free          (struct prj_path_list *list);
int                     parameter_process           (int    param_qty,
                                                     char   *param[], 
                                                     char   *prj_name, 
                                                     size_t  name_size,
                                                     char   *prj_path,
                                                     size_t  path_size,
                                                     int    *err_param);
bool                    uvoptx_file_process         (const char *file_path, 
                                                     char *target_name,
                                                     size_t max_size);
int                     uvprojx_file_process        (const char *file_path, 
                                                     const char *target_name,
                                                     struct uvprojx_info *out_info,
                                                     bool is_get_target_name);
bool                    memory_area_process         (const char *str, bool is_new);
bool                    file_path_process           (const char *str, bool *is_has_user_lib);
void                    build_log_file_process      (const char *file_path);
void                    file_rename_process         (void);
int                     map_file_process            (const char *file_path, 
                                                     struct load_region **region_head,
                                                     struct object_info **object_head,
                                                     bool is_get_user_lib,
                                                     bool is_match_memory);
int                     region_info_process         (FILE *p_file, 
                                                     long read_start_pos,
                                                     struct load_region **region_head,
                                                     bool is_match_memory);
void                    region_zi_process           (struct exec_region **e_region,
                                                     char *text,
                                                     size_t size_pos);
int                     object_info_process         (struct object_info **object_head,
                                                     FILE *p_file,
                                                     long *end_pos,
                                                     bool is_get_user_lib,
                                                     uint8_t parse_mode);
int                     record_file_process         (const char *file_path, 
                                                     struct load_region **region_head,
                                                     struct object_info **object_head,
                                                     bool *is_has_object,
                                                     bool *is_has_region,
                                                     bool is_match_memory);
void                    object_print_process        (struct object_info *object_head,
                                                     size_t max_path_len, 
                                                     bool is_has_record);
void                    memory_mode0_print          (struct exec_region *e_region,
                                                     MEMORY_TYPE mem_type,
                                                     size_t max_region_name, 
                                                     bool is_has_record,
                                                     bool is_print_null);
void                    memory_mode1_print          (struct exec_region *e_region,
                                                     MEMORY_TYPE mem_type,
                                                     bool is_offchip,
                                                     size_t max_region_name, 
                                                     bool is_has_record);
void                    memory_mode2_print          (struct exec_region *e_region,
                                                     size_t max_region_name, 
                                                     bool is_has_record);
void                    progress_print              (struct exec_region *region,
                                                     size_t max_region_name, 
                                                     bool is_has_record);
void                    stack_print_process         (const char *file_path);
void                    log_write                   (FILE *p_log, 
                                                     bool is_print, 
                                                     const char *fmt, 
                                                     ...);


#endif
