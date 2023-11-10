/**
 * \file            keil-build-viewer.c
 * \brief           main application
 */

/*
 * Copyright (c) 2023 Dino Haw
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without restriction,
 * including without limitation the rights to use, copy, modify, merge,
 * publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE
 * AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * This file is keil-build-viewer.
 *
 * Author:        Dino Haw <347341799@qq.com>
 * Version:       v1.1
 * Change Logs:
 * Version  Date         Author     Notes
 * v1.0     2023-11-10   Dino       the first version
 * v1.1     2023-11-11   Dino       1. 适配 RAM 和 ROM 的解析
 */

/* Includes ------------------------------------------------------------------*/
#include "keil-build-viewer.h"


/* Private variables ---------------------------------------------------------*/
static FILE *                   _log_file;
static bool                     _is_display_object = true;
static bool                     _is_display_path   = true;
static char                     _line_text[1024];
static char                     _current_dir[MAX_PATH];
static struct prj_path_list *   _keil_prj_path_list;
static struct load_region *     _load_region_head;
static struct load_region *     _record_load_region_head;
static struct object_info *     _object_info_head;
static struct object_info *     _record_object_info_head;
static struct memory_info *     _memory_info_head;
static struct file_path_list *  _file_path_list_head;
static const char *             _keil_prj_extension[] = 
{
    ".uvprojx",
    ".uvproj"
};
static struct command_list      _command_list[] = 
{
    {
        .cmd  = "-OBJ",
        .desc = "Display the ram and flash occupancy of each object file (default)",
    },
    {
        .cmd  = "-NOOBJ",
        .desc = "NOT display the ram and flash occupancy of each object file",
    },
    {
        .cmd  = "-PATH",
        .desc = "Display each object file path (default)",
    },
    {
        .cmd  = "-NOPATH",
        .desc = "NOT display each object file path",
    },
};


/**
 * @brief  主程序
 * @note   
 * @param  argc:    参数数量
 * @param  argv[]:  参数列表
 * @retval 0: 正常 | -x: 错误
 */
int main(int argc, char *argv[])
{
    clock_t run_time = clock();

    /* 1. 获取本程序所在目录 */
    GetModuleFileName(NULL, _current_dir, MAX_PATH);

    char *last_slash = strrchr(_current_dir, '\\');
    if (last_slash) {
        *last_slash = '\0';
    }

    /* 创建 log 文件 */
    char file_path[MAX_PATH];
    snprintf(file_path, sizeof(file_path), "%s\\%s.log", _current_dir, APP_NAME);
    _log_file = fopen(file_path, "w+");

    log_print(_log_file, "\n=================================================== %s %s ==================================================\n", APP_NAME, APP_VERSION);

    /* 2. 搜索同级目录或指定目录下的所有 keil 工程并打印 */
    _keil_prj_path_list = prj_path_list_init(MAX_PATH_QTY);

    search_files_by_extension(_current_dir, 
                              _keil_prj_extension, 
                              sizeof(_keil_prj_extension) / sizeof(char *),
                              _keil_prj_path_list);

    if (_keil_prj_path_list->size > 0) {
        log_save(_log_file, "\n[Search keil project] %d item(s)\n", _keil_prj_path_list->size);
    }

    for (size_t i = 0; i < _keil_prj_path_list->size; i++) {
        log_save(_log_file, "\t%s\n", _keil_prj_path_list->items[i]);
    }

    /* 3. 参数处理 */
    int  result = 0;
    char input_param[MAX_PATH] = {0};
    char keil_prj_name[MAX_PRJ_NAME_SIZE] = {0};
    if (argc > 1)
    {
        int err_param = 0;
        int res = parameter_process(argc, 
                                    argv, 
                                    keil_prj_name, 
                                    sizeof(keil_prj_name), 
                                    input_param, 
                                    sizeof(input_param),
                                    &err_param);
        if (res == -1)
        {
            log_print(_log_file, "\n[ERROR] INVALID INPUT (code: %d): %s\n", GetLastError(), argv[1]);
            result = -1;
            goto __exit;
        }
        else if (res == -2)
        {
            log_print(_log_file, "\n[ERROR] INVALID INPUT: %s\n", argv[1]);
            log_print(_log_file, "[ERROR] Please enter the absolute path or keil project name with extension\n");
            result = -2;
            goto __exit;
        }
        else if (res == -3)
        {
            log_print(_log_file, "\n[ERROR] INVALID INPUT: %s\n", argv[err_param]);
            log_print(_log_file, "[ERROR] Only the following commands are supported\n");
            for (size_t i = 0; i < sizeof(_command_list) / sizeof(struct command_list); i++) {
                log_print(_log_file, "\t%s\t %s\n", _command_list[i].cmd, _command_list[i].desc);
            }
            result = -3;
            goto __exit;
        }
        else if (res == -4)
        {
            log_print(_log_file, "\nYou can control the displayed information by entering the following commands\n \n");
            for (size_t i = 0; i < sizeof(_command_list) / sizeof(struct command_list); i++) {
                log_print(_log_file, "\t%s\t %s\n", _command_list[i].cmd, _command_list[i].desc);
            }
            result = 0;
            goto __exit;
        }
    }

    log_save(_log_file, "\n[User input] %s\n", input_param);
    log_save(_log_file, "[Current folder] %s\n", _current_dir);

    /* 4. 确定 keil 工程 */
    char *keil_prj_path;
    if (input_param[0] != '\0')
    {
        log_print(_log_file, "\n[Hint] You specify the keil project!\n");
        keil_prj_path = input_param;
    }
    else if (_keil_prj_path_list->size > 0)
    {
        keil_prj_path = _keil_prj_path_list->items[_keil_prj_path_list->size - 1];

        last_slash = strrchr(keil_prj_path, '\\');
        if (last_slash) 
        {
            last_slash += 1;
            strncpy_s(keil_prj_name, sizeof(keil_prj_name), last_slash, strnlen(last_slash, sizeof(keil_prj_name)));
        }
    }
    else
    {
        log_print(_log_file, "\n[ERROR] NO keil project found\n");
        log_print(_log_file, "[ERROR] Please check: %s\n", input_param);
        result = -4;
        goto __exit;
    }

    log_save(_log_file, "[Keil project path] %s\n", keil_prj_path);
    log_save(_log_file, "[Keil project name] %s\n", keil_prj_name);

    bool is_keil4_prj = false;
    if (keil_prj_name[strlen(keil_prj_name) - 1] == 'j') {
        is_keil4_prj = true;
    }
    log_save(_log_file, "[Is keil v4] %d\n", is_keil4_prj);

    char keil_prj_full_name[MAX_PRJ_NAME_SIZE] = {0};
    memcpy_s(keil_prj_full_name, sizeof(keil_prj_full_name), keil_prj_name, strnlen_s(keil_prj_name, sizeof(keil_prj_full_name)));
    char *dot = strrchr(keil_prj_name, '.');
    if (dot) {
        *dot = '\0';
    }

    /* 5. 获取启用的 project target */
    /* 打开同名的 .uvoptx 或 .uvopt 文件 */
    char target_name[MAX_PRJ_NAME_SIZE] = {0};
    snprintf(file_path, sizeof(file_path), "%s\\%s.uvopt", _current_dir, keil_prj_name);
    if (is_keil4_prj == false) {
        strncat_s(file_path, sizeof(file_path), "x", 1);
    }

    /* 不存在 uvoptx 文件时，默认选择第一个 target name */
    bool is_has_target = true;
    if (uvoptx_file_process(file_path, target_name, sizeof(target_name)) == false) 
    {
        is_has_target = false;
        log_print(_log_file, "\n[WARNING] can't open '%s'\n", file_path);
        log_print(_log_file, "[WARNING] The first project target is selected by default.\n");
    }

    /* 6. 获取 map 和 htm 文件所在的目录及 device 和 output_name 信息 */
    /* 打开同名的 .uvprojx 或 .uvproj 文件 */
    snprintf(file_path, sizeof(file_path), "%s\\%s.uvproj", _current_dir, keil_prj_name);
    if (is_keil4_prj == false) {
        strncat_s(file_path, sizeof(file_path), "x", 1);
    }
    
    char target_name_label[MAX_PRJ_NAME_SIZE] = {0};

    if (is_has_target) {
        snprintf(target_name_label, sizeof(target_name_label), "%s%s", LABEL_TARGET_NAME, target_name);    
    } else {
        strncpy_s(target_name_label, sizeof(target_name_label), LABEL_TARGET_NAME, strlen(LABEL_TARGET_NAME));
    }

    bool is_has_user_lib = false;
    struct uvprojx_info uvprojx_file = {0};
    int res = uvprojx_file_process(&_memory_info_head, 
                                   file_path, 
                                   target_name_label, 
                                   &uvprojx_file, 
                                   &is_has_user_lib, 
                                   !is_has_target);
    if (res == -1)
    {
        log_print(_log_file, "\n[ERROR] can't open .uvproj(x) file\n");
        log_print(_log_file, "[ERROR] Please check: %s\n", file_path);
        result = -5;
        goto __exit;
    }
    else if (res == -2)
    {
        log_print(_log_file, "\n[ERROR] <Cpu> contains unsupported types\n");
        log_print(_log_file, "[ERROR] Please check: %s\n", file_path);
        result = -6;
        goto __exit;
    }
    else if (res == -3)
    {
        log_print(_log_file, "\n[ERROR] generate map file is not checked (Options for Target -> Listing -> Linker Listing)\n");
        result = -7;
        goto __exit;
    }

    log_save(_log_file, "\n[Device] %s\n", uvprojx_file.chip);
    log_save(_log_file, "[Target name] %s\n", uvprojx_file.target_name);
    log_save(_log_file, "[Output name] %s\n", uvprojx_file.output_name);
    log_save(_log_file, "[Output path] %s\n", uvprojx_file.output_path);
    log_save(_log_file, "[Listing path] %s\n", uvprojx_file.listing_path);

    if (uvprojx_file.output_name[0] == '\0') 
    {
        log_print(_log_file, "\n[ERROR] output name is empty\n");
        log_print(_log_file, "[ERROR] Please check: %s\n", file_path);
        result = -8;
        goto __exit;
    }
    if (uvprojx_file.listing_path[0] == '\0') 
    {
        log_print(_log_file, "\n[ERROR] listing path is empty\n");
        log_print(_log_file, "[ERROR] Please check: %s\n", file_path);
        result = -9;
        goto __exit;
    }

    char *p_target_name = target_name;
    if (is_has_target == false) {
        p_target_name = uvprojx_file.target_name;
    }
    log_print(_log_file, "\n[%s]  [%s]  [%s]\n \n", keil_prj_full_name, p_target_name, uvprojx_file.chip);

    log_save(_log_file, "[memory info]\n");
    for (struct memory_info *memory = _memory_info_head; 
         memory != NULL; 
         memory = memory->next)
    {
        log_save(_log_file, "[name] %s [base addr] 0x%.8X [size] 0x%.8X [type] %d [ID] %d\n", 
                 memory->name, memory->base_addr, memory->size, memory->type, memory->id);
    }

    /* 7. 从 build_log 文件中获取被改名的文件信息 */
    if (uvprojx_file.output_path[0] != '\0')
    {
        res = combine_path(file_path, keil_prj_path, uvprojx_file.output_path);
        snprintf(file_path, sizeof(file_path), "%s%s.build_log.htm", file_path, uvprojx_file.output_name);
        if (res == 0)
        {
            build_log_file_process(file_path, &_file_path_list_head);
        }
        if (res == -1)
        {
            log_print(_log_file, "\n[WARNING] %s not a absolute path\n", keil_prj_path);
            log_print(_log_file, "[WARNING] path: %s\n \n", file_path);
        }
        else if (res == -2)
        {
            log_print(_log_file, "\n[WARNING] relative paths go up more levels than absolute paths\n");
            log_print(_log_file, "[WARNING] path: %s\n \n", file_path);
        }
    }
    else {
        log_print(_log_file, "\n[WARNING] is empty, can't read '.build_log.htm' file\n \n");
    }

    /* 8. 处理剩余的重名文件 */
    file_rename_process(&_file_path_list_head);

    /* 9. 打开 map 文件，获取 Load Region 和 Execution Region */
    res = combine_path(file_path, keil_prj_path, uvprojx_file.listing_path);
    if (res == -1)
    {
        log_print(_log_file, "\n[ERROR] %s not a absolute path\n \n", keil_prj_path);
        result = -10;
        goto __exit;
    }
    else if (res == -2)
    {
        log_print(_log_file, "\n[ERROR] relative paths go up more levels than absolute paths\n \n");
        result = -11;
        goto __exit;
    }

    snprintf(file_path, sizeof(file_path), "%s%s.map", file_path, uvprojx_file.output_name);
    log_save(_log_file, "[map file path] %s\n", file_path);

    bool is_enable_lto = false;
    res = map_file_process(file_path, &_load_region_head, &_object_info_head, is_has_user_lib, &is_enable_lto);
    if (res == -1)
    {
        log_print(_log_file, "\n[ERROR] Check if a map file exists (Options for Target -> Listing -> Linker Listing)\n");
        log_print(_log_file, "[ERROR] map file path: %s\n", file_path);
        result = -12;
        goto __exit;
    }
    else if (res == -2)
    {
        log_print(_log_file, "\n[ERROR] map file does not contain \"%s\"\n", STR_MEMORY_MAP_OF_THE_IMAGE);
        log_print(_log_file, "[ERROR] Please check: %s\n", file_path);
        result = -13;
        goto __exit;
    }
    else if (res == -3)
    {
        log_print(_log_file, "\n[ERROR] map file does not find object's information\n");
        log_print(_log_file, "[ERROR] Please check: %s\n", file_path);
        result = -14;
        goto __exit;
    }
    log_save(_log_file, "[Is enbale LTO] %d\n", is_enable_lto);

    log_save(_log_file, "\n[region info]\n");
    for (struct load_region *l_region = _load_region_head; 
         l_region != NULL; 
         l_region = l_region->next)
    {
        log_save(_log_file, "[load region] %s\n", l_region->name);
        for (struct exec_region *e_region = l_region->exec_region; 
             e_region != NULL; 
             e_region = e_region->next)
        {
            log_save(_log_file, "\t[execution region] %s, 0x%.8X, 0x%.8X, 0x%.8X [type] %d [ID] %d\n", 
                     e_region->name, e_region->base_addr, e_region->size, e_region->used_size, e_region->type, e_region->memory_id);
            
            for (struct region_block *block = e_region->zi_block;
                 block != NULL;
                 block = block->next)
            {
                log_save(_log_file, "\t\t[ZI block] addr: 0x%.8X, size: 0x%.8X (%d)\n", 
                         block->start_addr, block->size, block->size);
            }
            log_save(_log_file, "\n");
        }
    }

    /* 10. 打印用户 object 和用户 library 文件的 flash 和 RAM 占用情况 */
    /* 10.1 将路径绑定到 object info 对应的 path 成员 */
    size_t max_name_len = 0;
    size_t max_path_len = 0;
    for (struct file_path_list *path_temp = _file_path_list_head;
         path_temp != NULL;
         path_temp = path_temp->next)
    {
        for (struct object_info *object_temp = _object_info_head;
             object_temp != NULL;
             object_temp = object_temp->next)
        {
            if (strcasecmp(object_temp->name, path_temp->new_name) == 0) {
                object_temp->path = path_temp->path;
            }
        }
        /* 计算出各个文件名称和相对路径的最长长度 */
        size_t path_len = strnlen_s(path_temp->path, MAX_PATH);
        size_t name_len = strnlen_s(path_temp->new_name, MAX_PATH);

        if (name_len > max_name_len) {
            max_name_len = name_len;
        }
        if (path_len > max_path_len) {
            max_path_len = path_len;
        }
    }
    log_save(_log_file, "\n[object name max length] %d\n", max_name_len);
    log_save(_log_file, "[object path max length] %d\n", max_path_len);

    /* 打印抓取的 object 名称和路径 */
    log_save(_log_file, "\n[object in map file]\n");
    for (struct object_info *object_temp = _object_info_head;
         object_temp != NULL;
         object_temp = object_temp->next)
    {
        log_save(_log_file, "[object name] %s%*s [path] %s\n", 
                 object_temp->name, max_name_len + 1 - strlen(object_temp->name), " ", object_temp->path);
    }

    /* 打印抓取的 keil 工程中的文件名和路径 */
    log_save(_log_file, "\n[file path in keil project]\n");
    for (struct file_path_list *path_list = _file_path_list_head; 
         path_list != NULL; 
         path_list = path_list->next)
    {
        log_save(_log_file, "[old name] %s%*s [type] %d   [path] %s\n", 
                 path_list->old_name, max_name_len + 1 - strlen(path_list->old_name), " ", path_list->file_type, path_list->path);

        if (strcmp(path_list->new_name, path_list->old_name)) {
            log_save(_log_file, "[new name] %s\n", path_list->new_name);
        }
    }

    /* 10.2 打开记录文件，打开失败则新建 */
    snprintf(file_path, sizeof(file_path), "%s\\%s-record.txt", _current_dir, APP_NAME);

    bool is_has_record = true;
    FILE *p_file = fopen(file_path, "r");
    if (p_file == NULL)
    {
        p_file = fopen(file_path, "w+");
        if (p_file == NULL)
        {
            log_print(_log_file, "\n[ERROR] can't create log file\n");
            log_print(_log_file, "[ERROR] Please check: %s\n", file_path);
            result = -15;
            goto __exit;
        }
        is_has_record = false;
    }
    fclose(p_file);

    /* 10.3 若存在记录文件，则读取各个文件 flash 和 RAM 占用情况 */
    bool is_has_object = false;
    bool is_has_region = false;
    if (is_has_record)
    {
        record_file_process(file_path, 
                            &_record_load_region_head, 
                            &_record_object_info_head, 
                            is_has_user_lib,
                            &is_has_object,
                            &is_has_region);
    }

    if (is_has_record)
    {
        /* 将旧的 object 信息绑定到匹配的新的 object 信息上 */
        for (struct object_info *new_obj_info = _object_info_head;
             new_obj_info != NULL;
             new_obj_info = new_obj_info->next)
        {
            for (struct object_info *old_obj_info = _record_object_info_head;
                 old_obj_info != NULL;
                 old_obj_info = old_obj_info->next)
            {
                if (strcasecmp(new_obj_info->name, old_obj_info->name) == 0) {
                    new_obj_info->old_object = old_obj_info;
                }
            }
        }

        log_save(_log_file, "\n[record region info]\n");
        /* 将旧的 execution region 绑定到匹配的新的 execution region 上 */
        for (struct load_region *old_load_region = _record_load_region_head; 
             old_load_region != NULL; 
             old_load_region = old_load_region->next)
        {
            log_save(_log_file, "[load region] %s\n", old_load_region->name);
            for (struct exec_region *old_exec_region = old_load_region->exec_region; 
                 old_exec_region != NULL; 
                 old_exec_region = old_exec_region->next)
            {
                for (struct load_region *new_load_region = _load_region_head; 
                     new_load_region != NULL; 
                     new_load_region = new_load_region->next)
                {
                    for (struct exec_region *new_exec_region = new_load_region->exec_region; 
                         new_exec_region != NULL; 
                         new_exec_region = new_exec_region->next)
                    {
                        if (strcmp(new_exec_region->name, old_exec_region->name) == 0) {
                            new_exec_region->old_exec_region = old_exec_region;
                        }
                    }
                }
                log_save(_log_file, "\t[execution region] %s, 0x%.8X, 0x%.8X, 0x%.8X [type] %d [ID] %d\n", 
                         old_exec_region->name, old_exec_region->base_addr, old_exec_region->size, 
                         old_exec_region->used_size, old_exec_region->type, old_exec_region->memory_id);
            }
        }
    }

    /* 10.4 打印并保存本次编译信息至记录文件 */
    if (is_enable_lto == false)
    {
        if (_is_display_object) 
        {
            size_t len = 0;
            if (_is_display_path) 
            {
                if (max_name_len > max_path_len) {
                    len = max_name_len;
                } else {
                    len = max_path_len;
                }
            } 
            else {
                len = max_name_len;
            }
            object_print_process(_object_info_head, len, is_has_object);
        }

        /* 保存本次编译信息至记录文件 */
        p_file = fopen(file_path, "w+");
        if (p_file == NULL)
        {
            log_print(_log_file, "\n[ERROR] can't create record file\n");
            log_print(_log_file, "[ERROR] Please check: %s\n", file_path);
            result = -16;
            goto __exit;
        }

        fputs("      Code (inc. data)   RO Data    RW Data    ZI Data      Debug   Object Name\n", p_file);

        for (struct object_info *object_temp = _object_info_head;
             object_temp != NULL;
             object_temp = object_temp->next)
        {
            snprintf(_line_text, sizeof(_line_text), 
                     "%10d %10d %10d %10d %10d %10d   %s\n",
                     object_temp->code, 0, object_temp->ro_data, object_temp->rw_data, object_temp->zi_data, 0, object_temp->name);
            fputs(_line_text, p_file);
        }
        fputs(STR_OBJECT_TOTALS "\n\n", p_file);
        fclose(p_file);
    }
    
    /* 11. 打印总 flash 和 RAM 占用情况，以进度条显示 */
    /* 11.1 算出 execution region name 的最大长度  */
    size_t max_region_name = 0;
    for (struct load_region *l_region = _load_region_head; 
         l_region != NULL; 
         l_region = l_region->next)
    {
        for (struct exec_region *e_region = l_region->exec_region; 
             e_region != NULL; 
             e_region = e_region->next)
        {
            size_t len = strnlen_s(e_region->name, 32);
            if (len > max_region_name) {
                max_region_name = len;
            }
        }
    }

    /* 11.2 开始打印 */
    for (struct load_region *l_region = _load_region_head; 
         l_region != NULL; 
         l_region = l_region->next)
    {
        log_print(_log_file, "%s\n", l_region->name);
        memory_print_process(_memory_info_head, l_region->exec_region, MEMORY_TYPE_SRAM,    max_region_name, is_has_region);
        memory_print_process(_memory_info_head, l_region->exec_region, MEMORY_TYPE_FLASH,   max_region_name, is_has_region);
        memory_print_process(_memory_info_head, l_region->exec_region, MEMORY_TYPE_UNKNOWN, max_region_name, is_has_region);
    }

    /* 12. 打印栈使用情况 */
    if (uvprojx_file.output_path)
    res = combine_path(file_path, keil_prj_path, uvprojx_file.output_path);
    if (res == -1)
    {
        log_print(_log_file, "\n[ERROR] %s not a absolute path\n \n", keil_prj_path);
        result = -17;
        goto __exit;
    }
    else if (res == -2)
    {
        log_print(_log_file, "\n[ERROR] relative paths go up more levels than absolute paths\n \n");
        result = -18;
        goto __exit;
    }
    snprintf(file_path, sizeof(file_path), "%s%s.htm", file_path, uvprojx_file.output_name);
    log_save(_log_file, "[htm file path] %s\n", file_path);
    stack_print_process(file_path);

    /* 13. 保存本次 region 信息至记录文件 */
    snprintf(file_path, sizeof(file_path), "%s\\%s-record.txt", _current_dir, APP_NAME);

    p_file = fopen(file_path, "a");
    if (p_file == NULL)
    {
        log_print(_log_file, "\n[ERROR] can't create record file\n");
        log_print(_log_file, "[ERROR] Please check: %s\n", file_path);
        result = -19;
        goto __exit;
    }

    fputs(STR_MEMORY_MAP_OF_THE_IMAGE "\n\n", p_file);

    for (struct load_region *l_region = _load_region_head; 
         l_region != NULL; 
         l_region = l_region->next)
    {
        snprintf(_line_text, sizeof(_line_text),
                 "\t%s %s \n\n",
                 STR_LOAD_REGION, l_region->name);
        fputs(_line_text, p_file);

        for (struct exec_region *e_region = l_region->exec_region; 
             e_region != NULL; 
             e_region = e_region->next)
        {
            snprintf(_line_text, sizeof(_line_text),
                     "\t\t%s %s (%s: 0x%.8X, %s: 0x%.8X, %s: 0x%.8X, END)\n\n",
                     STR_EXECUTION_REGION, e_region->name, STR_EXECUTE_BASE_ADDR, e_region->base_addr,
                     STR_REGION_USED_SIZE, e_region->used_size, STR_REGION_MAX_SIZE, e_region->size);
            fputs(_line_text, p_file);
        }
    }
    fputs(STR_IMAGE_COMPONENT_SIZE, p_file);
    fclose(p_file);
    
__exit:
    object_info_free(&_object_info_head);
    object_info_free(&_record_object_info_head);
    load_region_free(&_load_region_head);
    load_region_free(&_record_load_region_head);

    file_path_free(&_file_path_list_head);
    prj_path_list_free(_keil_prj_path_list);
    log_print(_log_file, "=============================================================================================================================\n\n");
    log_save(_log_file, "run time: %.3f s\n", (double)(clock() - run_time) / CLOCKS_PER_SEC);
    fclose(_log_file);
    return result;
}


/**
 * @brief  入口参数处理
 * @note   
 * @param  param_qty:   参数数量
 * @param  param[]:     参数列表
 * @param  prj_name:    [out] keil 工程名
 * @param  name_size:   prj_name 的最大 size
 * @param  prj_path:    [out] keil 工程绝对路径
 * @param  path_size:   prj_path 的最大 size
 * @param  err_param:   [out] 发生错误的参数位
 * @retval 0: 正常 | -x: 错误
 */
int parameter_process(int    param_qty,
                      char   *param[], 
                      char   *prj_name,
                      size_t name_size,
                      char   *prj_path,
                      size_t path_size,
                      int    *err_param)
{
    for (size_t i = 1; i < param_qty; i++)
    {
        log_save(_log_file, "[param %d] %s\n", i, param[i]);

        if (param[i][0] == '-') 
        {
            int seq = 0;
            if (strcasecmp(param[i], _command_list[seq++].cmd) == 0) {
                _is_display_object = true;
            }
            else if (strcasecmp(param[i], _command_list[seq++].cmd) == 0) {
                _is_display_object = false;
            }
            else if (strcasecmp(param[i], _command_list[seq++].cmd) == 0) {
                _is_display_path = true;
            }
            else if (strcasecmp(param[i], _command_list[seq++].cmd) == 0) {
                _is_display_path = false;
            }
            else if (strcasecmp(param[i], "-H")    == 0
            ||       strcasecmp(param[i], "-HELP") == 0) {
                return -4;
            }
            else
            {
                *err_param = i;
                return -3;
            }
        }
        else 
        {
            char *last_slash = NULL;
            size_t param_len = strnlen_s(param[i], MAX_PATH);

            /* 绝对路径 */
            if (param[i][1] == ':')
            {
                DWORD attributes = GetFileAttributes(param[i]);
                if (attributes == INVALID_FILE_ATTRIBUTES) {
                    return -1;
                }

                /* 目录 */
                if (attributes & FILE_ATTRIBUTE_DIRECTORY)
                {
                    strncpy_s(_current_dir, sizeof(_current_dir), param[i], param_len);

                    if (param[i][param_len - 1] == '\\') {
                        _current_dir[param_len - 1] = '\0';
                    }
                }
                /* 文件 */
                else
                {
                    /* 不是 keil 工程则报错退出 */
                    if (is_keil_project(param[i]) == false) {
                        return -2;
                    }
                    strncpy_s(prj_path, path_size, param[i], param_len);

                    last_slash = strrchr(prj_path, '\\');
                    if (last_slash)
                    {
                        last_slash += 1;
                        strncpy_s(prj_name, name_size, last_slash, strnlen(last_slash, name_size));
                    }
                }
            }
            /* 不支持相对路径 */
            else if (param[i][0] == '\\' || param[i][0] == '.') {
                return -2;
            }
            /* 文件名 */
            else
            {
                snprintf(prj_path, path_size, "%s\\%s", _current_dir, param[i]);

                /* 非 keil 工程则检查是否有扩展名 */
                if (is_keil_project(param[i]) == false)
                {
                    /* 有扩展名报错退出，无扩展名则从搜索到的列表里进行匹配 */
                    char *dot = strrchr(param[i], '.');
                    if (dot) {
                        return -2;
                    }

                    for (size_t index = 0; index < _keil_prj_path_list->size; index++) 
                    {
                        if (strstr(_keil_prj_path_list->items[index], param[i])) 
                        {
                            strncpy_s(prj_path, path_size, _keil_prj_path_list->items[index], strnlen_s(_keil_prj_path_list->items[index], MAX_PATH));
                            break;
                        }
                    }
                }

                last_slash = strrchr(prj_path, '\\');
                if (last_slash)
                {
                    last_slash += 1;
                    strncpy_s(prj_name, name_size, last_slash, strnlen(last_slash, name_size));
                }
            }
        }
    }

    return 0;
}


/**
 * @brief  uvoptx 文件处理
 * @note   获取指定的 target name
 * @param  file_path:   uvoptx 文件路径
 * @param  target_name: [out] keil target name
 * @param  max_size:    target_name 的最大 size
 * @retval true: 成功 | false: 失败
 */
bool uvoptx_file_process(const char *file_path, 
                         char *target_name,
                         size_t max_size)
{
    FILE *p_file = fopen(file_path, "r");
    if (p_file == NULL) {
        return false;
    }

    uint8_t state = 0;
    while (fgets(_line_text, sizeof(_line_text), p_file))     
    { 
        char *str;
        switch (state)
        {
            case 0:
                str = strstr(_line_text, LABEL_TARGET_NAME);
                if (str)
                {
                    str += strlen(LABEL_TARGET_NAME);
                    char *lt = strrchr(_line_text, '<');
                    if (lt) 
                    {
                        *lt = '\0';
                        strncpy_s(target_name, max_size, str, strnlen_s(str, max_size));
                        log_save(_log_file, "[target name] %s\n", target_name);
                        state = 1;
                    }
                }
                break;
            case 1:
                str = strstr(_line_text, LABEL_IS_CURRENT_TARGET);
                if (str)
                {
                    str += strlen(LABEL_IS_CURRENT_TARGET);
                    if (*str == '0') {
                        state = 0;
                    } else {
                        state = 2;
                    }
                }
                break;
            default: break;
        }
        if (state == 2) 
        {
            log_save(_log_file, "[final target name] %s\n", target_name);
            break;
        }
    }
    fclose(p_file);

    return true;
}


/**
 * @brief  uvprojx 文件处理
 * @note   获取 uvprojx 文件中的信息
 * @param  memory_head:         memory 链表头
 * @param  file_path:           uvprojx 文件的绝对路径
 * @param  target_name:         指定的 target name
 * @param  out_info:            [out] 解析出的 uvprojx 信息
 * @param  is_has_user_lib:     [out] 是否有 user lib
 * @param  is_get_target_name:  是否获取 target name
 * @retval 0: 成功 | -x: 失败
 */
int uvprojx_file_process(struct memory_info **memory_head,
                         const char *file_path, 
                         const char *target_name,
                         struct uvprojx_info *out_info,
                         bool *is_has_user_lib,
                         bool is_get_target_name)
{
    /* 打开同名的 .uvprojx 或 .uvproj 文件 */
    FILE *p_file = fopen(file_path, "r");
    if (p_file == NULL) {
        return -1;
    }

    uint8_t state = 0;
    char *str     = NULL;
    char *lt      = NULL;
    char *str_p1  = NULL;
    char *str_p2  = NULL;
    char *end_ptr = NULL;
    char name[MAX_PRJ_NAME_SIZE] = {0};
    uint32_t base_addr   = 0;
    uint32_t size        = 0;
    size_t memory_id     = 2;   /* 1 预留给 unknown 的 memory */
    MEMORY_TYPE mem_type = MEMORY_TYPE_NONE;

    /* 逐行读取 */
    while (fgets(_line_text, sizeof(_line_text), p_file))     
    { 
        switch (state)
        {
            case 0:
                str = strstr(_line_text, target_name);
                if (str) 
                {
                    if (is_get_target_name)
                    {
                        str += strlen(LABEL_TARGET_NAME);
                        lt = strrchr(_line_text, '<');
                        if (lt) 
                        {
                            *lt = '\0';
                            strncpy_s(out_info->target_name, sizeof(out_info->target_name), str, strnlen_s(str, sizeof(out_info->target_name)));
                        }
                    }
                    state = 1;
                }
                break;
            case 1:
                str = strstr(_line_text, LABEL_DEVICE);
                if (str)
                {
                    str += strlen(LABEL_DEVICE);
                    lt = strrchr(_line_text, '<');
                    if (lt) 
                    {
                        *lt = '\0';
                        strncpy_s(out_info->chip, sizeof(out_info->chip), str, strnlen_s(str, sizeof(out_info->chip)));
                        state = 2;
                    }
                }
                break;
            case 2:
                bool is_get_first = false;

                /* 获取 RAM 和 ROM  */
                str = strstr(_line_text, LABEL_CPU);
                if (str)
                {
                    strtok(_line_text, " ");
                    while (1)
                    {
                        if (is_get_first == false) 
                        {
                            str_p1  = strstr(_line_text, LABEL_CPU);
                            str_p1 += strlen(LABEL_CPU);
                            is_get_first = true;
                        }
                        else 
                        {
                            str_p1 = strtok(NULL, " ");
                            if (str_p1 == NULL)
                            {
                                state = 3;
                                break;
                            }
                        }

                        str_p2  = strstr(str_p1, "(");
                        *str_p2 = '\0';
                        strncpy_s(name, sizeof(name), str_p1, strnlen_s(str_p1, sizeof(name)));

                        mem_type = MEMORY_TYPE_UNKNOWN;
                        if (strstr(name, "RAM")) {
                            mem_type = MEMORY_TYPE_SRAM;
                        } 
                        else if (strstr(name, "ROM")) {
                            mem_type = MEMORY_TYPE_FLASH;
                        }
                        else 
                        {
                            state = 3;
                            break;
                        }

                        str_p1  = str_p2 + 1;
                        str_p2 += 3;
                        while ((*str_p2 >= '0') && (*str_p2 <= 'F')) {
                            str_p2 += 1;
                        } 

                        int parse_mode = 0;
                        if (*str_p2 == ',') {
                            parse_mode = 0;
                        } 
                        else if (*str_p2 == '-') {
                            parse_mode = 1;
                        } 
                        else {
                            return -2;
                        }

                        *str_p2   = '\0';
                        base_addr = strtoul(str_p1, &end_ptr, 16); 

                        str_p1    = str_p2 + 1;
                        str_p2    = strstr(str_p1, ")");
                        *str_p2   = '\0';
                        size      = strtoul(str_p1, &end_ptr, 16);

                        if (parse_mode == 1) 
                        {
                            size -= base_addr;
                            size += 1;
                        }

                        if (mem_type == MEMORY_TYPE_UNKNOWN) {
                            memory_info_add(memory_head, name, 1, base_addr, size, mem_type);
                        } else {
                            memory_info_add(memory_head, name, memory_id, base_addr, size, mem_type);
                        }
                        memory_id++;
                    }
                }
                break;
            case 3:
                str = strstr(_line_text, LABEL_OUTPUT_DIRECTORY);
                if (str)
                {
                    str += strlen(LABEL_OUTPUT_DIRECTORY);
                    lt   = strrchr(_line_text, '<');
                    if (lt) 
                    {
                        *lt = '\0';
                        strncpy_s(out_info->output_path, sizeof(out_info->output_path), str, strnlen_s(str, sizeof(out_info->output_path)));
                        state = 4;
                    }
                }
                break;
            case 4:
                str = strstr(_line_text, LABEL_OUTPUT_NAME);
                if (str)
                {
                    str += strlen(LABEL_OUTPUT_NAME);
                    lt   = strrchr(_line_text, '<');
                    if (lt) 
                    {
                        *lt = '\0';
                        strncpy_s(out_info->output_name, sizeof(out_info->output_name), str, strnlen_s(str, sizeof(out_info->output_name)));
                        state = 5;
                    }
                }
                break;
            case 5:
                str = strstr(_line_text, LABEL_LISTING_PATH);
                if (str)
                {
                    str += strlen(LABEL_LISTING_PATH);
                    lt = strrchr(_line_text, '<');
                    if (lt) 
                    {
                        *lt = '\0';
                        strncpy_s(out_info->listing_path, sizeof(out_info->listing_path), str, strnlen_s(str, sizeof(out_info->listing_path)));
                        state = 6;
                    }
                }
                break;
            case 6:
                /* 检查是否生成了 map 文件 */
                str = strstr(_line_text, LABEL_IS_CREATE_MAP);
                if (str)
                {
                    str += strlen(LABEL_IS_CREATE_MAP);
                    if (*str == '0') {
                        return -3;
                    } else {
                        state = 7;
                    }
                }
                break;
            case 7:
                /* 获取已加入编译的文件路径，并记录重复的文件名 */
                if (file_path_process(_line_text, is_has_user_lib) == false) {
                    state = 8;
                }
                break;
            default: break;
        }
        if (state == 8) {
            break;
        }
    }
    fclose(p_file);

    return true;
}


/**
 * @brief  读取 build_log 文件，获取文件的改名信息
 * @note   
 * @param  file_path: build_log 文件所在的路径
 * @param  path_head: 文件路径链表头
 * @retval 
 */
void build_log_file_process(const char *file_path, struct file_path_list **path_head)
{
    FILE *p_file = fopen(file_path, "r");
    if (p_file == NULL) {
        return;
    }

    char *ptr = NULL;
    log_save(_log_file, "\n");

    while (fgets(_line_text, sizeof(_line_text), p_file))
    {
        if (({ptr = strstr(_line_text, STR_RENAME_MARK); ptr;}))
        {
            log_save(_log_file, "%s", _line_text);

            char *str_p1 = strstr(_line_text, "'");
            str_p1 += 1;
            char *str_p2 = strstr(str_p1, "'");
            *str_p2 = '\0';

            for (struct file_path_list *path_temp = *path_head;
                 path_temp != NULL;
                 path_temp = path_temp->next)
            {
                if (strcmp(str_p1, path_temp->path) == 0)
                {
                    char *str_p3 = strrchr(str_p2 + 1, '\'');
                    *str_p3 = '\0';
                    str_p1  = strrchr(str_p2 + 1, '\\');
                    str_p1 += 1;
                    if (path_temp->new_name) {
                        free(path_temp->new_name);
                    }
                    path_temp->new_name  = strdup(str_p1);
                    path_temp->is_rename = false;
                    log_save(_log_file, "'%s' rename to '%s'\n", path_temp->old_name, str_p1);
                }
            }
        }
        else if (({ptr = strstr(_line_text, STR_COMPILING); ptr;})) {
            break;
        }
    }
    log_save(_log_file, "\n");
    fclose(p_file);
    return;
}


/**
 * @brief  文件名重名修改处理
 * @note   
 * @param  path_head: 文件路径链表头
 * @retval None
 */
void file_rename_process(struct file_path_list **path_head)
{
    char str[MAX_PRJ_NAME_SIZE] = {0};

    for (struct file_path_list *path_temp1 = *path_head;
         path_temp1 != NULL;
         path_temp1 = path_temp1->next)
    {
        size_t repeat = 0;

        for (struct file_path_list *path_temp2 = path_temp1->next;
             path_temp2 != NULL;
             path_temp2 = path_temp2->next)
        {
            if (path_temp2->is_rename
            &&  strcmp(path_temp1->old_name, path_temp2->old_name) == 0)
            {
                repeat++;

                strncpy_s(str, sizeof(str), path_temp2->old_name, strnlen_s(path_temp2->old_name, sizeof(str)));
                char *dot = strrchr(str, '.');
                if (dot) {
                    *dot = '\0';
                }
                snprintf(str, sizeof(str), "%s_%d.o", str, repeat);
                if (path_temp2->new_name) {
                    free(path_temp2->new_name);
                }
                path_temp2->new_name  = strdup(str);
                path_temp2->is_rename = false;
                log_save(_log_file, "object '%s' rename to '%s'\n", path_temp2->old_name, str);
            }
        }
    }
}


/**
 * @brief  文件路径处理
 * @note   获取 uvprojx 文件中被添加进 keil 工程的文件及其相对路径
 * @param  str:             读取到的 uvprojx 文件的每一行文本
 * @param  is_has_user_lib: [out] 是否有 user lib
 * @retval true: 继续 | false: 结束
 */
bool file_path_process(const char *str, bool *is_has_user_lib)
{
    static uint8_t state = 0;
    static char path[MAX_PATH] = {0};
    static char name[MAX_PRJ_NAME_SIZE] = {0};
    static OBJECT_FILE_TYPE type = OBJECT_FILE_TYPE_USER;

    char *str_p1 = NULL;
    char *str_p2 = NULL;

    if (strstr(str, LABEL_END_GROUPS)) 
    {
        state = 0;
        return false;
    }

    switch (state)
    {
        case 0:
            if (strstr(str, LABEL_GROUP_NAME)) {
                state = 1;
            }
            break;
        case 1:
            if (({str_p1 = strstr(str, LABEL_FILE_NAME); str_p1;})) 
            {
                str_p1 += strlen(LABEL_FILE_NAME);
                str_p2  = strrchr(str_p1, '<');
                *str_p2 = '\0';
                strncpy_s(name, sizeof(name), str_p1, strnlen_s(str_p1, sizeof(name)));
                type  = OBJECT_FILE_TYPE_USER;
                state = 2;
            }
            else if (({str_p1 = strstr(str, LABEL_INCLUDE_IN_BUILD); str_p1;}))
            {
                str_p1 += strlen(LABEL_INCLUDE_IN_BUILD);
                if (*str_p1 == '0') {
                    state = 0;
                }
            }
            else if (strstr(str, LABEL_END_FILES)) {
                state = 0;
            }
            break;
        case 2:
            str_p1 = strstr(str, LABEL_FILE_TYPE);
            if (str_p1) 
            {
                str_p1 += strlen(LABEL_FILE_TYPE);
                /* text document file or custom file */
                if (*str_p1 == '5' || *str_p1 == '6') {
                    state = 1;
                } 
                else if (*str_p1 == '3')    /* object file */
                {
                    type  = OBJECT_FILE_TYPE_OBJECT;
                    state = 3;
                }
                else if (*str_p1 == '4')    /* library file */
                {
                    *is_has_user_lib = true;
                    type  = OBJECT_FILE_TYPE_LIBRARY;
                    state = 3;
                }
                else {
                    state = 3;
                }
            }
            break;
        case 3:
            str_p1 = strstr(str, LABEL_FILE_PATH);
            if (str_p1) 
            {
                str_p1 += strlen(LABEL_FILE_PATH);
                str_p2  = strrchr(str_p1, '<');
                *str_p2 = '\0';
                strncpy_s(path, sizeof(path), str_p1, strnlen_s(str_p1, sizeof(path)));
                state = 4;
            }
            break;
        case 4:
            if (strstr(str, LABEL_END_FILE)) 
            {
                file_path_add(&_file_path_list_head, name, path, type);
                state = 1;
            }
            else if (({str_p1 = strstr(str, LABEL_INCLUDE_IN_BUILD); str_p1;}))
            {
                str_p1 += strlen(LABEL_INCLUDE_IN_BUILD);
                if (*str_p1 != '0') {
                    file_path_add(&_file_path_list_head, name, path, type);
                }
                state = 1;
            }
            break;
        default: break;
    }

    return true;
}


/**
 * @brief  map 文件处理
 * @note   获取 map 文件中每个编译文件的信息
 * @param  file_path:       map 文件的绝对路径
 * @param  region_head:     region 链表头
 * @param  object_head:     object 文件链表头
 * @param  is_has_user_lib: 是否获取 user lib 信息
 * @param  is_enable_lto:   [out] 是否使能了 LTO
 * @retval 0: 正常 | -x: 错误
 */
int map_file_process(const char *file_path, 
                     struct load_region **region_head,
                     struct object_info **object_head,
                     bool is_get_user_lib,
                     bool *is_enable_lto)
{
    FILE *p_file = fopen(file_path, "r");
    if (p_file == NULL) {
        return -1;
    }

    /* 读取 map 文件 */
    fseek(p_file, 0, SEEK_END);
    long pos_head = ftell(p_file);
    long pos_end  = pos_head;
    long memory_map_pos = 0;

    /* 从文件末尾开始逆序读取 */
    while (pos_head)
    {
        fseek(p_file, pos_head, SEEK_SET);
        if (fgetc(p_file) == '\n' && (pos_end - pos_head) > 1)
        {
            fseek(p_file, pos_head + 1, SEEK_SET);
            fgets(_line_text, sizeof(_line_text), p_file);
            pos_end = pos_head;

            if (strstr(_line_text, STR_MEMORY_MAP_OF_THE_IMAGE))
            {
                /* 记录位置并退出循环 */
                memory_map_pos = ftell(p_file);
                break;
            }
        }
        pos_head--;
    }

    if (pos_head == 0)
    {
        fclose(p_file);
        return -2;
    }

    /* 获取 map 文件中的 load region 和 execution region 信息 */
    region_info_process(p_file, memory_map_pos, region_head);

    /* 获取每个 .o 文件的 flash 和 RAM 占用情况 */
    return object_info_process(object_head, p_file, NULL, is_enable_lto, is_get_user_lib);
}


/**
 * @brief  获取 load region 和 execution region 信息
 * @note   
 * @param  p_file:          文件对象
 * @param  read_start_pos:  开始读取的位置
 * @param  region_head:     region 链表头
 * @retval 0: 正常 | -5: 获取失败
 */
int region_info_process(FILE *p_file, 
                        long read_start_pos, 
                        struct load_region **region_head)
{
    /* 先从记录的位置开始正序读取 */
    fseek(p_file, read_start_pos, SEEK_SET);

    bool is_has_load_region = false;
    struct load_region *l_region = NULL;
    struct exec_region *e_region = NULL;
    
    while (fgets(_line_text, sizeof(_line_text), p_file))
    {
        if (strstr(_line_text, STR_IMAGE_COMPONENT_SIZE)) {
            return 0;
        }

        char *str_p1  = NULL;
        char *str_p2  = NULL;
        char *end_ptr = NULL;
        char name[MAX_PRJ_NAME_SIZE]     = {0};
        char str_temp[MAX_PRJ_NAME_SIZE] = {0};
        uint32_t base_addr = 0;
        uint32_t size      = 0;
        uint32_t used_size = 0;
        size_t memory_id   = 0;
        MEMORY_TYPE memory_type = MEMORY_TYPE_NONE;
        
        str_p1 = strstr(_line_text, STR_LOAD_REGION);
        if (str_p1)
        {
            str_p1 += strlen(STR_LOAD_REGION) + 1;
            str_p2  = strstr(str_p1, " ");
            *str_p2 = '\0';
            strncpy_s(name, sizeof(name), str_p1, strnlen_s(str_p1, sizeof(name)));

            l_region = load_region_create(region_head, name);
            is_has_load_region = true;
        }
        else if (is_has_load_region)
        {
            str_p1 = strstr(_line_text, STR_EXECUTION_REGION);
            if (str_p1)
            {
                str_p1 += strlen(STR_EXECUTION_REGION) + 1;
                str_p2  = strstr(str_p1, " ");
                *str_p2 = '\0';
                strncpy_s(name, sizeof(name), str_p1, strnlen_s(str_p1, sizeof(name)));

                str_p1  = strstr(str_p2 + 1, STR_EXECUTE_BASE_ADDR);
                str_p1 += strlen(STR_EXECUTE_BASE_ADDR) + 4;
                str_p2  = strstr(str_p1, ",");
                *str_p2 = '\0';
                base_addr = strtoul(str_p1, &end_ptr, 16);

                str_p1  = strstr(str_p2 + 1, STR_REGION_USED_SIZE);
                str_p1 += strlen(STR_REGION_USED_SIZE) + 4;
                str_p2  = strstr(str_p1, ",");
                *str_p2 = '\0';
                used_size = strtoul(str_p1, &end_ptr, 16);

                str_p1  = strstr(str_p2 + 1, STR_REGION_MAX_SIZE);
                str_p1 += strlen(STR_REGION_MAX_SIZE) + 4;
                str_p2  = strstr(str_p1, ",");
                *str_p2 = '\0';
                size = strtoul(str_p1, &end_ptr, 16);

                memory_id   = 1;    /* ID 1 表示 unknown */
                memory_type = MEMORY_TYPE_UNKNOWN;
                for (struct memory_info *memory_temp = _memory_info_head;
                     memory_temp != NULL;
                     memory_temp = memory_temp->next)
                {
                    if (base_addr >= memory_temp->base_addr
                    &&  base_addr <= (memory_temp->base_addr + memory_temp->size))
                    {
                        memory_id   = memory_temp->id;
                        memory_type = memory_temp->type;
                        break;
                    }
                }

                region_zi_process(NULL, NULL);
                e_region = load_region_add_exec_region(&l_region, name, memory_id, base_addr, size, used_size, memory_type);
            }
            else if (e_region
            &&       e_region->type != MEMORY_TYPE_FLASH
            &&       strstr(_line_text, "0x"))
            {
                region_zi_process(&e_region, _line_text);
            }
        }
    }

    return -5;
}


/**
 * @brief  获取 region 中的 zero init 区域块分布
 * @note   e_region 参数传值为 NULL 时将复位本函数。
 *         切换至新的 execution region 前，必须复位本函数
 * @param  e_region: execution region
 * @param  text:     一行文本内容
 * @retval None
 */
void region_zi_process(struct exec_region **e_region, char *text)
{
    static bool is_zi_start = false;
    static uint32_t last_end_addr = 0;
    static struct region_block **zi_block = NULL;

    if (e_region == NULL) 
    {
        zi_block      = NULL;
        is_zi_start   = false;
        last_end_addr = 0;
        return;
    }

    if (strstr(text, STR_ZERO_INIT)) {
        is_zi_start = true;
    }
    else if (strstr(text, STR_PADDING)) 
    { 
        if (is_zi_start == false) {
            return;
        }
    }
    else 
    {
        zi_block      = NULL;
        is_zi_start   = false;
        last_end_addr = 0;
        return;
    }

    char *token1 = strtok(text, " ");
    strtok(NULL, " ");
    char *token3 = strtok(NULL, " ");

    char *end_ptr = NULL;
    uint32_t addr = strtoul(token1, &end_ptr, 16);
    uint32_t size = strtoul(token3, &end_ptr, 16);

    if (addr > last_end_addr) 
    {
        zi_block = &(*e_region)->zi_block;

        if (*zi_block)
        {
            struct region_block *block = *zi_block;

            while (block->next) {
                block = block->next;
            }
            zi_block = &block->next;
        }

        *zi_block = (struct region_block *)malloc(sizeof(struct region_block));

        (*zi_block)->start_addr = addr;
        (*zi_block)->size       = size;
        (*zi_block)->next       = NULL;
    }
    else if (*zi_block) {
        (*zi_block)->size += size;
    }

    last_end_addr = addr + size;
}


/**
 * @brief  获取 object info
 * @note   
 * @param  object_head:     object 文件链表头
 * @param  p_file:          文件对象
 * @param  end_pos:         [out] 最后读取到的文件位置
 * @param  is_enable_lto:   [out] 是否使能了 LTO
 * @param  is_get_user_lib: 是否获取用户 lib 信息
 * @retval 0: 正常 | -x: 错误
 */
int object_info_process(struct object_info **object_head,
                        FILE *p_file,
                        long *end_pos,
                        bool *is_enable_lto,
                        bool is_get_user_lib)
{
    bool is_lto    = false;
    int  value[16] = {0};
    char name[MAX_PRJ_NAME_SIZE] = {0};
    char *token    = NULL;
    char *end_ptr  = NULL;
    char *new_line = NULL;
    size_t index   = 0;

    /* 获取用户文件的 object info */
    while (fgets(_line_text, sizeof(_line_text), p_file))
    {
        if (is_lto == false)
        {
            if (strstr(_line_text, STR_LTO_LLVW)) 
            {
                is_lto = true;
                if (is_enable_lto) 
                {
                    *is_enable_lto = true;
                    *object_head   = NULL;
                    break;
                }
            }
        }

        if (strstr(_line_text, ".o")) 
        {
            index = 0;
            /* 切割后转换 */
            token = strtok(_line_text, " ");
            while (token != NULL)
            {
                if (index < OBJECT_INFO_STR_QTY - 1) {
                    value[index] = strtoul(token, &end_ptr, 10);
                } 
                else    /* 最后一个是名称 */
                {
                    new_line = strrchr(token, '\n');
                    if (new_line) {
                        *new_line = '\0';
                    }
                    strncpy_s(name, sizeof(name), token, strnlen_s(token, sizeof(name)));
                }
                if (++index == OBJECT_INFO_STR_QTY) {
                    break;
                }
                token = strtok(NULL, " ");
            }

            /* 保存 */
            object_info_add(object_head, name, value[0], value[2], value[3], value[4]);
        }
        else if (strstr(_line_text, STR_OBJECT_TOTALS)) 
        {
            if (is_lto == false) 
            {
                if (is_enable_lto) {
                    *is_enable_lto = false;
                }
            }
            break;
        }
    }

    if (index == 0) {
        return -3;
    }

    if (is_get_user_lib == false) 
    {
        if (end_pos) {
            *end_pos = ftell(p_file);
        }
        fclose(p_file);
        return 0;
    }

    if (is_lto) {
        return -4;
    }

    /* 获取 library 的 object info */
    bool is_get_lib_info = true;

    while (fgets(_line_text, sizeof(_line_text), p_file))
    {
        if (is_get_lib_info
        &&  strstr(_line_text, ".o"))
        {
            size_t i = 0;

            /* 切割后转换 */
            token = strtok(_line_text, " ");
            while (token != NULL)
            {
                if (i < OBJECT_INFO_STR_QTY - 1) {
                    value[i] = strtoul(token, &end_ptr, 10);
                } 
                else    /* 最后一个是名称 */
                {
                    new_line = strrchr(token, '\n');
                    if (new_line) {
                        *new_line = '\0';
                    }
                    strncpy_s(name, sizeof(name), token, strnlen_s(token, sizeof(name)));
                }
                if (++i == OBJECT_INFO_STR_QTY) {
                    break;
                }
                token = strtok(NULL, " ");
            }

            struct file_path_list *temp = _file_path_list_head;
            for (; temp != NULL; temp = temp->next)
            {
                if (temp->file_type == OBJECT_FILE_TYPE_LIBRARY)
                {
                    if (strcasecmp(name, temp->new_name) == 0) 
                    {
                        object_info_add(object_head, name, value[0], value[2], value[3], value[4]);
                        break;
                    }
                }
            }

            if (temp == NULL) {
                is_get_lib_info = false;
            }
        }
        else if (strstr(_line_text, STR_LIBRARY_TOTALS))
        {
            if (end_pos) {
                *end_pos = ftell(p_file);
            }
            break;
        }
    }
    fclose(p_file);

    return 0;
}


/**
 * @brief  记录文件处理
 * @note   
 * @param  file_path:       文件的绝对路径
 * @param  region_head:     region 链表头
 * @param  object_head:     object 文件链表头
 * @param  is_get_user_lib: 是否获取 user lib 信息
 * @param  is_has_object:   [out] 是否存在 object 信息
 * @param  is_has_region:   [out] 是否存在 region 信息
 * @retval 0: 正常 | -x: 错误
 */
int record_file_process(const char *file_path, 
                        struct load_region **region_head,
                        struct object_info **object_head,
                        bool is_get_user_lib,
                        bool *is_has_object,
                        bool *is_has_region)
{
    *is_has_object = false;
    *is_has_region = false;

    FILE *p_file = fopen(file_path, "r");
    if (p_file == NULL) {
        return -1;
    }

    long end_pos = 0;
    int result = object_info_process(object_head, p_file, &end_pos, NULL, false);
    if (result == 0) {
        *is_has_object = true;
    }

    p_file = fopen(file_path, "r");
    if (p_file == NULL) {
        return -1;
    }
    result = region_info_process(p_file, end_pos, region_head);
    if (result == 0) {
        *is_has_region = true;
    }

    return result;
}


/**
 * @brief  object 信息打印处理
 * @note   
 * @param  object_head:     object 文件链表头
 * @param  max_path_len:    最大路径长度
 * @param  is_has_record:   是否有记录文件
 * @retval None
 */
void object_print_process(struct object_info *object_head,
                          size_t max_path_len, 
                          bool is_has_record)
{
    if ((max_path_len + 2) < strlen(STR_FILE)) {
        max_path_len = strlen(STR_FILE);
    }

    size_t len = max_path_len + 2 - strlen(STR_FILE);
    if (_is_display_path) {
        len += strlen("():");
    }

    size_t left_space  = len / 2;
    size_t right_space = left_space;
    if (len % 2) {
        left_space += 1;
    } 

    snprintf(_line_text, sizeof(_line_text), 
             "%*s%s%*s|         RAM (byte)       |       FLASH (byte)       |\n", 
             left_space, " ", STR_FILE, right_space, " ");

    len = strnlen_s(_line_text, sizeof(_line_text));
    char *line = (char *)malloc(len);
    size_t i = 0;
    for (; i < len - 1; i++) {
        line[i] = '-';
    }
    line[i] = '\0';
    log_print(_log_file, "%s\n", line);
    log_print(_log_file, "%s", _line_text);
    log_print(_log_file, "%s\n", line);
    
    for (struct object_info *obj_info = object_head; 
         obj_info != NULL; 
         obj_info = obj_info->next)
    {
        char *path          = obj_info->path;
        char ram_text[MAX_PRJ_NAME_SIZE]   = {0};
        char flash_text[MAX_PRJ_NAME_SIZE] = {0};
        size_t path_len     = strnlen_s(obj_info->path, MAX_PATH);
        size_t path_space   = max_path_len - path_len + 1;
        uint32_t ram        = obj_info->rw_data + obj_info->zi_data;
        uint32_t flash      = obj_info->code + obj_info->ro_data + obj_info->rw_data;

        if (obj_info->path == NULL || _is_display_path == false) 
        {
            path = obj_info->name;
            if (path == NULL) {
                path = "UNKNOWN";
            }
            path_len   = strnlen_s(path, MAX_PATH);
            path_space = max_path_len - path_len + 1;
        }

        if (is_has_record)
        {
            if (obj_info->old_object == NULL)
            {
                strncpy_s(ram_text,   sizeof(ram_text),   "[NEW]     ", 10);
                strncpy_s(flash_text, sizeof(flash_text), "[NEW]     ", 10);
            }
            else
            {
                char ram_sign;
                char flash_sign;
                uint32_t old_ram      = obj_info->old_object->rw_data + obj_info->old_object->zi_data;
                uint32_t old_flash    = obj_info->old_object->code + obj_info->old_object->ro_data + obj_info->old_object->rw_data;
                uint32_t ram_increm   = 0;
                uint32_t flash_increm = 0;

                if (ram < old_ram)
                {
                    ram_increm = old_ram - ram;
                    ram_sign   = '-';
                }
                else
                {
                    ram_increm = ram - old_ram;
                    ram_sign   = '+';
                }

                if (flash < old_flash) 
                {
                    flash_increm = old_flash - flash;
                    flash_sign   = '-';
                }
                else
                {
                    flash_increm = flash - old_flash;
                    flash_sign   = '+';
                }

                char str[MAX_PRJ_NAME_SIZE] = {0};
                size_t str_len   = 0;
                size_t space_len = 0;

                if (ram_increm)
                {
                    snprintf(ram_text, sizeof(ram_text), "[%c%d]", ram_sign, ram_increm);
                    str_len   = strnlen_s(ram_text, sizeof(ram_text));
                    space_len = 10 - str_len;
                    if (space_len)
                    {
                        for (size_t i = 0; i < space_len; i++) {
                            str[i] = ' ';
                        }
                        strncat_s(ram_text, sizeof(ram_text), str, space_len);
                    }
                }
                else {
                    strncpy_s(ram_text, sizeof(ram_text), "          ", 10);
                }

                if (flash_increm)
                {
                    snprintf(flash_text, sizeof(flash_text), "[%c%d]", flash_sign, flash_increm);
                    str_len   = strnlen_s(flash_text, sizeof(flash_text));
                    space_len = 10 - str_len;
                    if (space_len)
                    {
                        for (size_t i = 0; i < space_len; i++) {
                            str[i] = ' ';
                        }
                        strncat_s(flash_text, sizeof(flash_text), str, space_len);
                    }
                }
                else {
                    strncpy_s(flash_text, sizeof(flash_text), "          ", 10);
                }
            }
        }
        else 
        {
            strncpy_s(ram_text,   sizeof(ram_text),   "          ", 10);
            strncpy_s(flash_text, sizeof(flash_text), "          ", 10);
        }

        if (_is_display_path) 
        {
            snprintf(_line_text, sizeof(_line_text), 
                     "%s():%*s |  %10d  %s  |  %10d  %s  |", 
                     path, path_space, " ", ram, ram_text, flash, flash_text);
        }
        else 
        {
            snprintf(_line_text, sizeof(_line_text), 
                     "%s%*s |  %10d  %s  |  %10d  %s  |", 
                     obj_info->name, path_space, " ", ram, ram_text, flash, flash_text);
        }
        log_print(_log_file, "%s\n", _line_text);
    }
    log_print(_log_file, "%s\n", line);
    free(line);
}


/**
 * @brief  打印内存占用情况
 * @note   
 * @param  memory_head:     memory 链表头
 * @param  e_region:        execution region
 * @param  type:            指定打印的 execution region 内存类型
 * @param  max_region_name: 最大的 execution region 名称长度
 * @param  is_has_record:   是否有记录文件
 * @retval None
 */
void memory_print_process(struct memory_info *memory_head,
                          struct exec_region *e_region, 
                          MEMORY_TYPE type,
                          size_t max_region_name,
                          bool is_has_record)
{
    size_t sram_id    = 0;
    size_t flash_id   = 0;
    size_t unknown_id = 0;
    bool is_print_unknown = false;

    for (struct memory_info *memory_temp = memory_head;
         memory_temp != NULL;
         memory_temp = memory_temp->next)
    {
        size_t id = 0;
        char str[MAX_PRJ_NAME_SIZE] = {0};

        if (type == MEMORY_TYPE_SRAM
        &&  memory_temp->type == MEMORY_TYPE_SRAM)
        {
            sram_id++;
            id = sram_id;
            snprintf(str, sizeof(str), "SRAM %d   ", id);
        }
        else if (type == MEMORY_TYPE_FLASH
        &&       memory_temp->type == MEMORY_TYPE_FLASH)
        {
            flash_id++;
            id = flash_id;
            snprintf(str, sizeof(str), "FLASH %d  ", id);
        }
        else if (type == MEMORY_TYPE_UNKNOWN) {
            strncpy_s(str, sizeof(str), "UNKNOWN", strlen("UNKNOWN"));
        }
        else {
            continue;
        }

        bool is_print_region = false;
        for (struct exec_region *region = e_region;
             region != NULL;
             region = region->next)
        {
            if (region->memory_id == memory_temp->id)
            {
                if (memory_temp->is_printed == false)
                {
                    log_print(_log_file, "        %s%*s [0x%.8X | 0x%.8X (%d)]\n",
                              str, max_region_name, " ", memory_temp->base_addr, memory_temp->size, memory_temp->size);
                    memory_temp->is_printed = true;
                }
            }
            else if (type == MEMORY_TYPE_UNKNOWN
            &&       region->memory_id == 1)
            {
                if (is_print_unknown == false) 
                {
                    log_print(_log_file, "        %s\n", str);
                    is_print_unknown = true;
                }
            }
            else {
                continue;
            }

            if (region->is_printed) {
                continue;
            }

            double size = 0;
            double used_size = 0;
            char size_str[MAX_PRJ_NAME_SIZE] = {0};
            char used_size_str[MAX_PRJ_NAME_SIZE] = {0};

            if (region->used_size < 1024)
            {
                used_size = region->used_size;
                snprintf(used_size_str, sizeof(used_size_str), "%8d", (uint32_t)used_size);
            }
            else if (region->used_size < (1024 * 1024))
            {
                used_size = (double)region->used_size / 1024;
                snprintf(used_size_str, sizeof(used_size_str), "%5.1f KB", used_size);
            }
            else
            {
                used_size = (double)region->used_size / (1024 * 1024);
                snprintf(used_size_str, sizeof(used_size_str), "%5.2f MB", used_size);
            }

            if (region->size < 1024)
            {
                size = region->size;
                snprintf(size_str, sizeof(size_str), "%8d", (uint32_t)size);
            }
            else if (region->size < (1024 * 1024))
            {
                size = (double)region->size / 1024;
                snprintf(size_str, sizeof(size_str), "%5.1f KB", size);
            }
            else
            {
                size = (double)region->size / (1024 * 1024);
                snprintf(size_str, sizeof(size_str), "%5.2f MB", size);
            }

            double percent = (double)region->used_size * 100 / region->size;
            if (percent > 100) {
                percent = 100;
            }

            size_t used = 0;
            char progress[256] = {0};

            /* 先填充实际占用部分 */
            for (; used < ((uint32_t)percent / 2); used++) {
                strncat_s(progress, sizeof(progress), USED_SYMBOL, strlen(USED_SYMBOL));
            }
            /* 小于显示比例，避免是空的 */
            if (used == 0 && region->used_size != 0) {
                strncpy_s(progress, sizeof(progress), USED_SYMBOL, strlen(USED_SYMBOL));
            }
            /* 将占用部分的 ZI 部分替换 */
            for (struct region_block *block = region->zi_block;
                 block != NULL;
                 block = block->next)
            {
                size_t zi_start = ((double)block->start_addr - region->base_addr) * 100 / region->size / 2;
                size_t zi_end   = ((double)block->start_addr + block->size - region->base_addr) * 100 / region->size / 2;

                log_save(_log_file, "                [zi start] %d   [zi end] %d\n", zi_start, zi_end);
                for (; zi_start < zi_end && zi_start < used; zi_start++) {
                    strncpy_s(&progress[strlen(USED_SYMBOL) * zi_start], sizeof(progress), ZI_SYMBOL, strlen(ZI_SYMBOL));
                }

                if ((block->start_addr + block->size) >= (region->base_addr + region->size)) {
                    break;
                }
            }
            /* 剩下未使用部分 */
            for (size_t unused = 0; unused < (50 - used); unused++) {
                strncat_s(progress, sizeof(progress), UNUSE_SYMBOL, strlen(UNUSE_SYMBOL));
            }

            size_t space_len = max_region_name - strnlen_s(region->name, max_region_name) + 1;
            snprintf(_line_text, sizeof(_line_text),
                     "                %s%*s [0x%.8X]|%s| ( %s / %s ) %5.1f%%  ",
                     region->name, space_len, " ", region->base_addr, progress, used_size_str, size_str, percent);

            if (is_has_record)
            {
                if (region->old_exec_region == NULL)
                {
                    strncat_s(_line_text, sizeof(_line_text), "[NEW]", 5);
                }
                else
                {
                    char sign;
                    uint32_t data_increm = 0;
                    char str_increm[MAX_PRJ_NAME_SIZE] = {0};

                    if (region->used_size < region->old_exec_region->used_size)
                    {
                        sign = '-';
                        data_increm = region->old_exec_region->used_size - region->used_size;
                    }
                    else
                    {
                        sign = '+';
                        data_increm = region->used_size - region->old_exec_region->used_size;
                    }

                    if (data_increm)
                    {
                        snprintf(str_increm, sizeof(str_increm), "[%c%d]", sign, data_increm);
                        strncat_s(_line_text, sizeof(_line_text), str_increm, strnlen_s(str_increm, sizeof(str_increm)));
                    }
                }
            }
            log_print(_log_file, "%s\n", _line_text);

            is_print_region    = true;
            region->is_printed = true;
        }

        if (is_print_region == false) 
        {
            if (type != MEMORY_TYPE_UNKNOWN)
            {
                log_print(_log_file, "        %s%*s [0x%.8X | 0x%.8X (%d)]\n",
                          str, max_region_name, " ", memory_temp->base_addr, memory_temp->size, memory_temp->size);
                log_print(_log_file, "                NULL\n \n");
            }
        }
        else {
            log_print(_log_file, " \n");
        }
    }
}


/**
 * @brief  打印栈使用情况
 * @note   
 * @param  file_path: htm 文件路径
 * @retval None
 */
void stack_print_process(const char *file_path)
{
    FILE *p_file = fopen(file_path, "r");
    if (p_file == NULL) {
        return;
    }

    char *str_p1 = NULL;
    char *str_p2 = NULL;
    while (fgets(_line_text, sizeof(_line_text), p_file))
    {
        str_p1 = strstr(_line_text, STR_MAX_STACK_USAGE);
        if (str_p1)
        {
            str_p2  = strrchr(_line_text, ')');
            str_p2 += 1;
            *str_p2 = '\0';
            log_print(_log_file, "%s\n \n", str_p1);
            break;
        }
    }
    fclose(p_file);
    return;
}


/**
 * @brief  按扩展名搜索文件
 * @note   本函数不会递归搜索
 * @param  dir: 要搜索的目录
 * @param  extension[]:     扩展名，支持多个
 * @param  extension_qty:   扩展名数量
 * @param  list:            [out] 保存搜索到的文件路径
 * @retval None
 */
void search_files_by_extension(const char *dir, 
                               const char *extension[], 
                               size_t extension_qty, 
                               struct prj_path_list *list)
{
    HANDLE h_find;
    WIN32_FIND_DATA find_data;

    /* 加上 '*' 以搜索所有文件和文件夹 */
    char path[MAX_PATH];
    snprintf(path, sizeof(path), "%s\\*", dir);

    /* 开始搜索 */
    h_find = FindFirstFile(path, &find_data);
    if (h_find == INVALID_HANDLE_VALUE) {
        return;
    }

    do {
        /* 如果找到的是文件，判断其后缀是否为 keil 工程 */
        if ((find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
        {
            char *str = strrchr(find_data.cFileName, '.');
            if (str && is_same_string(str, extension, extension_qty))
            {
                char *file_path = malloc(MAX_PATH * sizeof(char));
                snprintf(file_path, MAX_PATH * sizeof(char), "%s\\%s", dir, find_data.cFileName);
                prj_path_list_add(list, file_path);
            }
        }
    } while (FindNextFile(h_find, &find_data));

    FindClose(h_find);
}


/**
 * @brief  log 记录
 * @note   
 * @param  p_log:    log 文件
 * @param  is_print: 是否打印
 * @param  fmt:      格式化字符串
 * @param  ...:      不定长参数 
 * @retval None
 */
void log_write(FILE *p_log, 
               bool is_print, 
               const char *fmt, 
               ...)
{
    if (p_log == NULL) {
        return;
    }

    va_list args;
    uint16_t len;
    static char buff[1024];
    
    va_start(args, fmt);

    memset(buff, 0, sizeof(buff));
    len = vsnprintf(buff, sizeof(buff) - 1, fmt, args);
    if (len > sizeof(buff) - 1) {
        len = sizeof(buff) - 1;
    }
    
    fputs(buff, p_log);

    if (is_print) {
        printf("%s", buff);
    }
    
    va_end(args);
}


/**
 * @brief  拼接路径
 * @note   
 * @param  out_path:        [out] 拼接后输出的路径
 * @param  absolute_path:   输入的绝对路径
 * @param  relative_path:   输入的相对路径
 * @retval 0: 正常 | -x: 错误
 */
int combine_path(char *out_path, 
                 const char *absolute_path, 
                 const char *relative_path)
{
    /* 1. 将绝对路径 absolute_path 的文件名和扩展名去除 */
    strncpy_s(out_path, MAX_PATH, absolute_path, strnlen_s(absolute_path, MAX_PATH));

    char *last_slash = strrchr(out_path, '\\');
    if (last_slash == NULL) {
        last_slash = strrchr(out_path, '/');
    }
    if (last_slash != NULL)
    {
        /* 说明不是是盘符根目录 */
        if (*(last_slash - 1) != ':') {
            *last_slash = '\0';
        }
    }
    else {
        return -1;
    }

    /* 2. 读取绝对路径的目录层级 并 记录每一层级的相对偏移值 */
    size_t hierarchy_count = 0;
    size_t dir_hierarchy[MAX_DIR_HIERARCHY];

    /* 逐字符遍历路径 */
    for (size_t i = 0; i < strnlen_s(out_path, MAX_PATH); i++)
    {
        if (out_path[i] == '\\' || out_path[i] == '/')
        {
            dir_hierarchy[hierarchy_count++] = i;

            if (hierarchy_count >= MAX_DIR_HIERARCHY) {
                break;
            }
        }
    }

    /* 3. 读取相对路径 relative_path 的向上层级数 */
    size_t dir_up_count = 0;
    size_t valid_path_offset = 0;

    for (size_t i = 0; i < strnlen_s(relative_path, MAX_PATH); )
    {
        if (relative_path[i]   == '.' 
        &&  relative_path[i+1] == '.' 
        &&  (relative_path[i+2] == '\\' || relative_path[i+2] == '/'))
        {
            i += 3;
            dir_up_count++;
            valid_path_offset += 3;
        }
        else if (relative_path[i]   == '.' 
        &&       (relative_path[i+1] == '\\' || relative_path[i+1] == '/')) 
        {
            valid_path_offset = 2;
            break;
        }
        else {
            break;
        }
    }

    /* 4. 根据 3 获得的级数，缩减绝对路径的目录层级 */
    if (dir_up_count > 0)
    {
        if ((hierarchy_count - dir_up_count) > 0)
        {
            hierarchy_count -= (dir_up_count - 1);
            size_t offset = dir_hierarchy[hierarchy_count - 1];
            out_path[offset] = '\0';
        }
        else {
            return -2;
        }
    }

    /* 5. 根据 2 记录的偏移值和 3 获得的级数，将 absolute_path 和 relative_path 拼接 */
    strncat_s(out_path, MAX_PATH, "\\", 1);
    strncat_s(out_path, MAX_PATH, &relative_path[valid_path_offset], strnlen_s(&relative_path[valid_path_offset], MAX_PATH));

    for (size_t i = 0; i < strnlen_s(out_path, MAX_PATH); i++)
    {
        if (out_path[i] == '/') {
            out_path[i] = '\\';
        }
    }

    return 0;
}


/**
 * @brief  创建新的文件信息并添加进链表
 * @note   
 * @param  path_head:   文件路径链表头
 * @param  name:        文件名
 * @param  path:        文件所在路径
 * @param  file_type:   文件类型
 * @retval true: 成功 | false: 失败
 */
bool file_path_add(struct file_path_list **path_head,
                   const char *name,
                   const char *path,
                   OBJECT_FILE_TYPE file_type)
{
    bool is_rename = false;
    char str[MAX_PRJ_NAME_SIZE] = {0};
    struct file_path_list **path_list = path_head;

    /* 可编译的文件和 lib 文件均会被编译为 .o 文件，此处提前进行文件扩展名的替换，便于后续的字符比对和查找 */
    if (file_type == OBJECT_FILE_TYPE_USER || file_type == OBJECT_FILE_TYPE_LIBRARY)
    {
        char *dot = strrchr(name, '.');
        if (dot) {
            *dot = '\0';
        }
        strncpy_s(str, sizeof(str), name, strnlen_s(name, sizeof(str)));
        strncat_s(str, sizeof(str), ".o", strlen(".o"));
    }
    else {
        strncpy_s(str, sizeof(str), name, strnlen_s(name, sizeof(str)));
    }

    if (*path_head)
    {
        struct file_path_list *list = *path_head;
        struct file_path_list *last_list = list;

        /* 文件名相同的可编译文件会被 keil 改名，此处提前处理，便于后续的字符比对和查找 */
        do {
            if (is_rename == false
            &&  (file_type == OBJECT_FILE_TYPE_USER || file_type == OBJECT_FILE_TYPE_LIBRARY)
            &&  strcmp(str, list->old_name) == 0) 
            {
                is_rename = true;
            }
            last_list = list;
            list = list->next;
        } while (list);

        path_list = &last_list->next;
    }

    *path_list = (struct file_path_list *)malloc(sizeof(struct file_path_list));

    (*path_list)->old_name  = strdup(str);
    (*path_list)->new_name  = strdup(str);
    (*path_list)->path      = strdup(path);
    (*path_list)->file_type = file_type;
    (*path_list)->is_rename = is_rename;
    (*path_list)->next      = NULL;

    return true;
}


/**
 * @brief  释放文件信息链表占用的内存
 * @note   
 * @param  path_head: 链表头
 * @retval None
 */
void file_path_free(struct file_path_list **path_head)
{
    struct file_path_list *list = *path_head;
    while (list != NULL)
    {
        struct file_path_list *temp = list;
        list = list->next;
        free(temp->old_name);
        free(temp->new_name);
        free(temp->path);
        free(temp);
    }
    *path_head = NULL;
}


/**
 * @brief  创建新的 memory 并添加进链表
 * @note   
 * @param  memory_head: memory 链表头
 * @param  name:        memory 名称
 * @param  id:          memory ID
 * @param  base_addr:   memory 基地址
 * @param  size:        memory 大小，单位 byte
 * @param  type:        memory 类型
 * @retval true: 成功 | false: 失败
 */
bool memory_info_add(struct memory_info **memory_head,
                     const char  *name,
                     size_t      id,
                     uint32_t    base_addr,
                     uint32_t    size,
                     MEMORY_TYPE type)
{
    struct memory_info **memory = memory_head;

    if (*memory_head)
    {
        struct memory_info *memory_temp = *memory_head;

        while (memory_temp->next) {
            memory_temp = memory_temp->next;
        }
        memory = &memory_temp->next;
    }

    *memory = (struct memory_info *)malloc(sizeof(struct memory_info));
    (*memory)->name = strdup(name);
    if ((*memory)->name == NULL) {
        return false;
    }
    (*memory)->id         = id;
    (*memory)->base_addr  = base_addr;
    (*memory)->size       = size;
    (*memory)->type       = type;
    (*memory)->is_printed = false;
    (*memory)->next       = NULL;

    return true;
}


/**
 * @brief  释放 memory 链表占用的内存
 * @note   
 * @param  memory_head: memory 链表头
 * @retval None
 */
void memory_info_free(struct memory_info **memory_head)
{
    struct memory_info *memory = *memory_head;
    while (memory != NULL)
    {
        struct memory_info *temp = memory;
        memory = memory->next;
        free(temp->name);
        free(temp);
    }
    *memory_head = NULL;
}


/**
 * @brief  创建新的 load region
 * @note   
 * @param  region_head: region 链表头
 * @param  name:        region 名称
 * @retval NULL | struct load_region *
 */
struct load_region * load_region_create(struct load_region **region_head, const char *name)
{
    struct load_region **region = region_head;

    if (*region_head)
    {
        struct load_region *region_temp = *region_head;

        while (region_temp->next) {
            region_temp = region_temp->next;
        }
        region = &region_temp->next;
    }

    *region = (struct load_region *)malloc(sizeof(struct load_region));

    (*region)->name = strdup(name);
    if ((*region)->name == NULL) {
        return NULL;
    }
    (*region)->exec_region = NULL;
    (*region)->next        = NULL;

    return (*region);
}


/**
 * @brief  创建新的 execution region 并添加进 load region 链表
 * @note   
 * @param  l_region:    load region 链表头
 * @param  name:        execution region 名
 * @param  memory_id:   所在的内存 ID
 * @param  base_addr:   execution region 基地址
 * @param  size:        execution region 大小，单位 byte
 * @param  used_size:   execution region 已使用大小，单位 byte
 * @param  type:        execution region 内存类型
 * @retval NULL | struct exec_region *
 */
struct exec_region * load_region_add_exec_region(struct load_region **l_region, 
                                                 const char  *name,
                                                 size_t      memory_id,
                                                 uint32_t    base_addr,
                                                 uint32_t    size,
                                                 uint32_t    used_size,
                                                 MEMORY_TYPE type)
{
    if (*l_region == NULL) {
        return NULL;
    }

    struct exec_region **e_region = &((*l_region)->exec_region);

    if ((*l_region)->exec_region)
    {
        struct exec_region *region_temp = (*l_region)->exec_region;

        while (region_temp->next) {
            region_temp = region_temp->next;
        }

        e_region = &region_temp->next;
    }

    *e_region = (struct exec_region *)malloc(sizeof(struct exec_region));

    (*e_region)->name            = strdup(name);
    (*e_region)->memory_id       = memory_id;
    (*e_region)->base_addr       = base_addr;
    (*e_region)->size            = size;
    (*e_region)->used_size       = used_size;
    (*e_region)->type            = type;
    (*e_region)->is_printed      = false;
    (*e_region)->zi_block        = NULL;
    (*e_region)->old_exec_region = NULL;
    (*e_region)->next            = NULL;

    return (*e_region);
}


/**
 * @brief  释放 load region 链表占用的内存
 * @note   
 * @param  region_head: 链表头
 * @retval None
 */
void load_region_free(struct load_region **region_head)
{
    struct load_region *l_region = *region_head;
    while (l_region != NULL)
    {
        struct load_region *l_region_temp = l_region;

        struct exec_region *e_region = l_region_temp->exec_region;
        while (e_region != NULL)
        {
            struct exec_region *e_region_temp = e_region;
            
            e_region = e_region->next;
            free(e_region_temp->name);

            struct region_block *block = e_region_temp->zi_block;
            while (block != NULL)
            {
                struct region_block *block_temp = block;

                block = block->next;
                free(block_temp);
            }

            free(e_region_temp);
        }

        l_region = l_region->next;
        free(l_region_temp->name);
        free(l_region_temp);
    }
    *region_head = NULL;
}


/**
 * @brief  创建新的 object 文件信息并添加进链表
 * @note   
 * @param  object_head: 链表头
 * @param  name:        object 文件名
 * @param  code:        code 大小，单位 byte
 * @param  ro_data:     read-only data 大小，单位 byte
 * @param  rw_data:     read-write data 大小，单位 byte
 * @param  zi_data:     zero-initialize data 大小，单位 byte
 * @retval true: 成功 | false: 失败
 */
bool object_info_add(struct object_info **object_head,
                     const char *name,
                     uint16_t   code,
                     uint16_t   ro_data,
                     uint16_t   rw_data,
                     uint16_t   zi_data)
{
    struct object_info **object = object_head;

    if (*object_head)
    {
        struct object_info *object_temp = *object_head;

        while (object_temp->next) {
            object_temp = object_temp->next;
        }
        object = &object_temp->next;
    }

    *object = (struct object_info *)malloc(sizeof(struct object_info));
    (*object)->name = strdup(name);
    if ((*object)->name == NULL) {
        return false;
    }
    (*object)->code       = code;
    (*object)->ro_data    = ro_data;
    (*object)->rw_data    = rw_data;
    (*object)->zi_data    = zi_data;
    (*object)->path       = NULL;
    (*object)->old_object = NULL;
    (*object)->next       = NULL;

    return true;
}


/**
 * @brief  释放 object 文件信息占用的内存
 * @note   
 * @param  object_head: 链表头
 * @retval None
 */
void object_info_free(struct object_info **object_head)
{
    struct object_info *object = *object_head;
    while (object != NULL)
    {
        struct object_info *temp = object;
        object = object->next;
        free(temp->name);
        free(temp);
    }
    *object_head = NULL;
}


/**
 * @brief  初始化动态列表
 * @note   
 * @param  capacity: 列表容量
 * @retval NULL | struct prj_path_list *
 */
struct prj_path_list * prj_path_list_init(size_t capacity)
{
    struct prj_path_list *list = malloc(sizeof(struct prj_path_list));

    list->items    = malloc(capacity * sizeof(char *));
    list->capacity = capacity;
    list->size     = 0;

    return list;
}


/**
 * @brief  向动态列表添加元素
 * @note   
 * @param  list: 列表对象
 * @param  item: 要新增的项
 * @retval None
 */
void prj_path_list_add(struct prj_path_list *list, char *item)
{
    /* 如果数组已满，将其最大容量翻倍 */
    if (list->size == list->capacity)
    {
        list->capacity *= 2;
        list->items = realloc(list->items, list->capacity * sizeof(char *));
    }
    list->items[list->size++] = item;
}


/**
 * @brief  释放动态列表
 * @note   
 * @param  list: 列表对象
 * @retval None
 */
void prj_path_list_free(struct prj_path_list *list)
{
    for (size_t i = 0; i < list->size; i++) {
        free(list->items[i]);
    }
    free(list->items);
    free(list);
}


/**
 * @brief  某路径是否为 keil 工程
 * @note   
 * @param  path: 路径
 * @retval true: 是 | false: 否
 */
bool is_keil_project(const char *path)
{
    for (size_t i = 0; i < sizeof(_keil_prj_extension) / sizeof(_keil_prj_extension[0]); i++)
    {
        char *dot = strrchr(path, '.');
        if (dot == NULL) {
            return false;
        }

        if (strncmp(dot, _keil_prj_extension[i], strlen(_keil_prj_extension[i])) == 0) {
            return true;
        }
    }

    return false;
}


/**
 * @brief  字符串比对
 * @note   
 * @param  str1:     字符串 1
 * @param  str2[]:   字符串组 2
 * @param  str2_qty: 字符串组 2 的数量
 * @retval true: 一致 | false: 不一致
 */
bool is_same_string(const char *str1, 
                    const char *str2[], 
                    size_t str2_qty)
{
    for (size_t i = 0; i < str2_qty; i++)
    {
        if (strcmp(str1, str2[i]) == 0) {
            return true;
        }
    }

    return false;
}
