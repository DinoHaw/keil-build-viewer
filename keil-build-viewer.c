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
 * Version:       v1.5
 * Change Logs:
 * Version  Date         Author     Notes
 * v1.0     2023-11-10   Dino       the first version
 * v1.1     2023-11-11   Dino       1. 适配 RAM 和 ROM 的解析
 * v1.2     2023-11-11   Dino       1. 适配 keil4 的 map 文件
 *                                  2. 增加检测到开启 LTO 后打印提示信息
 *                                  3. 修复开启 LTO 后无打印 region 的问题
 * v1.3     2023-11-12   Dino       1. 修复工程存在多个 lib 时仅解析一个的问题
 * v1.4     2023-11-21   Dino       1. 增加将本工具放置于系统环境变量 Path 所含目录的功能
 * v1.5     2023-11-30   Dino       1. 新增更多的 progress bar 样式
 *                                  2. 新增解析自定义的 memory area
 *                                  3. 修复 RAM 和 ROM 信息缺失时显示异常的问题
 */

/* Includes ------------------------------------------------------------------*/
#include "keil-build-viewer.h"


/* Private variables ---------------------------------------------------------*/
static FILE *                   _log_file;
static bool                     _is_display_object = true;
static bool                     _is_display_path   = true;
static char                     _line_text[1024];
static char *                   _current_dir;
static ENCODING_TYPE            _encoding_type  = ENCODING_TYPE_GBK;
static PROGRESS_STYLE           _progress_style = PROGRESS_STYLE_0;
static struct prj_path_list *   _keil_prj_path_list;
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
    {
        .cmd  = "-STYLE0",
        .desc = "Progress bar style: following system (default)",
    },
    {
        .cmd  = "-STYLE1",
        .desc = "Progress bar style: |###OOO____| (when non-Chinese and not specified progress bar style)",
    },
    {
        .cmd  = "-STYLE2",
        .desc = "Progress bar style: |XXXOOO____|",
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

    struct load_region *load_region_head = NULL;
    struct object_info *object_info_head = NULL;
    struct load_region *record_load_region_head = NULL;
    struct object_info *record_object_info_head = NULL;

    /* 获取编码格式 */
    UINT acp = GetACP();
    if (acp == 936) {
        _encoding_type = ENCODING_TYPE_GBK;
    } 
    else if (acp == 950) {
        _encoding_type = ENCODING_TYPE_BIG5;
    } 
    else {
        _encoding_type = ENCODING_TYPE_OTHER;
    }

    /* 1. 获取程序运行的工作目录 */
    int result = 0;
    DWORD buff_len = GetCurrentDirectory(0, NULL);
    if (buff_len == 0) 
    {
        printf("\n[ERROR] %s %s\n", APP_NAME, APP_VERSION);
        printf("[ERROR] Get current directory length failed (code: %d)\n", GetLastError());
        result = -20;
        goto __exit;
    }

    _current_dir = (char *)malloc(buff_len + 1);
    if (_current_dir == NULL) 
    {
        printf("\n[ERROR] %s %s\n", APP_NAME, APP_VERSION);
        printf("[ERROR] Failed to allocate current directory memory\n");
        result = -21;
        goto __exit;
    }

    buff_len = GetCurrentDirectory(buff_len, _current_dir);
    if (buff_len == 0) 
    {
        printf("\n[ERROR] %s %s\n", APP_NAME, APP_VERSION);
        printf("[ERROR] Get current directory failed. (code: %d)\n", GetLastError());
        result = -22;
        goto __exit;
    }

    /* 创建 log 文件 */
    char *file_path = NULL;
    size_t file_path_size = buff_len * 2;
    file_path = (char *)malloc(file_path_size);
    if (file_path == NULL) 
    {
        printf("\n[ERROR] %s %s\n", APP_NAME, APP_VERSION);
        printf("[ERROR] Failed to allocate file path memory\n");
        result = -23;
        goto __exit;
    }
    snprintf(file_path, file_path_size, "%s\\%s.log", _current_dir, APP_NAME);
    _log_file = fopen(file_path, "w+");

    log_print(_log_file, "\n=================================================== %s %s ==================================================\n ", APP_NAME, APP_VERSION);

    /* 2. 搜索同级目录或指定目录下的所有 keil 工程并打印 */
    _keil_prj_path_list = prj_path_list_init(MAX_PATH_QTY);

    search_files_by_extension(_current_dir,
                              buff_len,
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
    log_save(_log_file, "[Encoding] %d\n", acp);

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

        char *last_slash = strrchr(keil_prj_path, '\\');
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
    snprintf(file_path, file_path_size, "%s\\%s.uvopt", _current_dir, keil_prj_name);
    if (is_keil4_prj == false) {
        strncat_s(file_path, file_path_size, "x", 1);
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
    snprintf(file_path, file_path_size, "%s\\%s.uvproj", _current_dir, keil_prj_name);
    if (is_keil4_prj == false) {
        strncat_s(file_path, file_path_size, "x", 1);
    }
    
    char target_name_label[MAX_PRJ_NAME_SIZE * 2] = {0};

    if (is_has_target) {
        snprintf(target_name_label, sizeof(target_name_label), "%s%s", LABEL_TARGET_NAME, target_name);    
    } else {
        strncpy_s(target_name_label, sizeof(target_name_label), LABEL_TARGET_NAME, strlen(LABEL_TARGET_NAME));
    }

    struct uvprojx_info uvprojx_file = {0};
    int res = uvprojx_file_process(file_path, 
                                   target_name_label, 
                                   &uvprojx_file, 
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
    log_save(_log_file, "[Is has pack] %d\n", uvprojx_file.is_has_pack);
    log_save(_log_file, "[Is enbale LTO] %d\n", uvprojx_file.is_enable_lto);
    log_save(_log_file, "[Is has user library] %d\n", uvprojx_file.is_has_user_lib);
    log_save(_log_file, "[Is custom scatter file] %d\n", uvprojx_file.is_custom_scatter);

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
        log_save(_log_file, "[name] %s [base addr] 0x%.8X [size] 0x%.8X [type] %d [off-chip] %d [is pack] %d [ID] %d \n", 
                 memory->name, memory->base_addr, memory->size, memory->type, memory->is_offchip, memory->is_from_pack, memory->id);
    }

    /* 7. 从 build_log 文件中获取被改名的文件信息 */
    if (uvprojx_file.output_path[0] != '\0')
    {
        res = combine_path(file_path, file_path_size, keil_prj_path, uvprojx_file.output_path);
        snprintf(file_path, file_path_size, "%s%s.build_log.htm", file_path, uvprojx_file.output_name);
        if (res == 0)
        {
            build_log_file_process(file_path);
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
        log_print(_log_file, "\n[WARNING] %s is empty, can't read '.build_log.htm' file\n \n", LABEL_OUTPUT_DIRECTORY);
    }

    /* 8. 处理剩余的重名文件 */
    file_rename_process();

    /* 9. 打开 map 文件，获取 Load Region 和 Execution Region */
    res = combine_path(file_path, file_path_size, keil_prj_path, uvprojx_file.listing_path);
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

    snprintf(file_path, file_path_size, "%s%s.map", file_path, uvprojx_file.output_name);
    log_save(_log_file, "[map file path] %s\n", file_path);

    res = map_file_process(file_path, 
                           &load_region_head, 
                           &object_info_head, 
                           uvprojx_file.is_has_user_lib,
                           true);   /* !uvprojx_file.is_custom_scatter */
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

    log_save(_log_file, "\n[region info]\n");
    for (struct load_region *l_region = load_region_head; 
         l_region != NULL; 
         l_region = l_region->next)
    {
        log_save(_log_file, "[load region] %s\n", l_region->name);
        for (struct exec_region *e_region = l_region->exec_region; 
             e_region != NULL; 
             e_region = e_region->next)
        {
            log_save(_log_file, "\t[execution region] %s, 0x%.8X, 0x%.8X, 0x%.8X [memory type] %d [memory ID] %d\n", 
                     e_region->name, e_region->base_addr, e_region->size, 
                     e_region->used_size, e_region->memory_type, e_region->memory_id);
            
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
        for (struct object_info *object_temp = object_info_head;
             object_temp != NULL;
             object_temp = object_temp->next)
        {
            if (path_temp->file_type == OBJECT_FILE_TYPE_LIBRARY)
            {
                if (strcasecmp(object_temp->name, path_temp->old_name) == 0) {
                    object_temp->path = path_temp->path;
                }
            }
            else 
            {
                if (strcasecmp(object_temp->name, path_temp->new_object_name) == 0) {
                    object_temp->path = path_temp->path;
                }
            }
        }

        /* 计算出各个文件名称和相对路径的最长长度 */
        size_t path_len  = strnlen_s(path_temp->path, MAX_PATH);
        size_t name_len1 = strnlen_s(path_temp->old_name, MAX_PATH);
        size_t name_len2 = strnlen_s(path_temp->new_object_name, MAX_PATH);

        if (name_len1 > max_name_len) {
            max_name_len = name_len1;
        }
        if (name_len2 > max_name_len) {
            max_name_len = name_len2;
        }
        if (path_len > max_path_len) {
            max_path_len = path_len;
        }
    }
    log_save(_log_file, "\n[object name max length] %d\n", max_name_len);
    log_save(_log_file, "[object path max length] %d\n", max_path_len);

    /* 打印抓取的 object 名称和路径 */
    log_save(_log_file, "\n[object in map file]\n");
    for (struct object_info *object_temp = object_info_head;
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
                 path_list->old_name, max_name_len + 1 - strlen(path_list->old_name), " ", 
                 path_list->file_type, path_list->path);

        if (strcmp(path_list->object_name, path_list->new_object_name)) {
            log_save(_log_file, "[new name] %s\n", path_list->new_object_name);
        }
    }

    /* 10.2 打开记录文件，打开失败则新建 */
    snprintf(file_path, file_path_size, "%s\\%s-record.txt", _current_dir, APP_NAME);

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
                            &record_load_region_head, 
                            &record_object_info_head, 
                            &is_has_object,
                            &is_has_region,
                            true);  /* !uvprojx_file.is_custom_scatter */
    }

    if (is_has_record)
    {
        /* 将旧的 object 信息绑定到匹配的新的 object 信息上 */
        for (struct object_info *new_obj_info = object_info_head;
             new_obj_info != NULL;
             new_obj_info = new_obj_info->next)
        {
            for (struct object_info *old_obj_info = record_object_info_head;
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
        for (struct load_region *old_load_region = record_load_region_head; 
             old_load_region != NULL; 
             old_load_region = old_load_region->next)
        {
            log_save(_log_file, "[load region] %s\n", old_load_region->name);
            for (struct exec_region *old_exec_region = old_load_region->exec_region; 
                 old_exec_region != NULL; 
                 old_exec_region = old_exec_region->next)
            {
                for (struct load_region *new_load_region = load_region_head; 
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
                         old_exec_region->used_size, old_exec_region->memory_type, old_exec_region->memory_id);
            }
        }
    }

    /* 10.4 打印并保存本次编译信息至记录文件 */
    if (uvprojx_file.is_enable_lto == false)
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
            object_print_process(object_info_head, len, is_has_object);
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

        for (struct object_info *object_temp = object_info_head;
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
    else {
        log_print(_log_file, "[WARNING] Because LTO is enabled, information for each file cannot be displayed\n \n");
    }
    
    /* 11. 打印总 flash 和 RAM 占用情况，以进度条显示 */
    /* 11.1 算出 execution region name 的最大长度  */
    size_t max_region_name = 0;
    for (struct load_region *l_region = load_region_head; 
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

    /* 11.2 判断当前的内存打印模式 */
    MEMORY_PRINT_MODE print_mode = MEMORY_PRINT_MODE_0;
    if (uvprojx_file.is_has_pack == false)
    {
    #if defined(ENABLE_REFER_TO_KEIL_DIALOG) && (ENABLE_REFER_TO_KEIL_DIALOG != 0)
        if (_memory_info_head == NULL) {
            print_mode = MEMORY_PRINT_MODE_2;
        } else {
            print_mode = MEMORY_PRINT_MODE_1;
        }
    #else
        if (_memory_info_head && uvprojx_file.is_custom_scatter == false) {
            print_mode = MEMORY_PRINT_MODE_1;
        } else {
            print_mode = MEMORY_PRINT_MODE_2;
        }
    #endif
    }
    log_save(_log_file, "[memory print mode]: %d\n", print_mode);

    /* 11.3 开始打印 */
    bool is_print_null = true;
    for (struct load_region *l_region = load_region_head; 
         l_region != NULL; 
         l_region = l_region->next)
    {
        log_print(_log_file, "%s\n", l_region->name);
        if (print_mode == MEMORY_PRINT_MODE_1)
        {
            memory_mode1_print(l_region->exec_region, MEMORY_TYPE_RAM,     false, max_region_name, is_has_record);
            memory_mode1_print(l_region->exec_region, MEMORY_TYPE_RAM,     true,  max_region_name, is_has_record);
            memory_mode1_print(l_region->exec_region, MEMORY_TYPE_FLASH,   false, max_region_name, is_has_record);
            memory_mode1_print(l_region->exec_region, MEMORY_TYPE_FLASH,   true,  max_region_name, is_has_record);
            memory_mode1_print(l_region->exec_region, MEMORY_TYPE_UNKNOWN, false, max_region_name, is_has_record);
        }
        else if (print_mode == MEMORY_PRINT_MODE_2)
        {
            memory_mode2_print(l_region->exec_region, max_region_name, is_has_record);
        }
        else 
        {
            memory_mode0_print(l_region->exec_region, MEMORY_TYPE_RAM,     max_region_name, is_has_record, is_print_null);
            memory_mode0_print(l_region->exec_region, MEMORY_TYPE_FLASH,   max_region_name, is_has_record, is_print_null);
            memory_mode0_print(l_region->exec_region, MEMORY_TYPE_UNKNOWN, max_region_name, is_has_record, is_print_null);
        }
        is_print_null = false;
    }

    /* 12. 打印栈使用情况 */
    if (uvprojx_file.output_path[0] != '\0')
    {
        res = combine_path(file_path, file_path_size, keil_prj_path, uvprojx_file.output_path);
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
        snprintf(file_path, file_path_size, "%s%s.htm", file_path, uvprojx_file.output_name);
        log_save(_log_file, "[htm file path] %s\n", file_path);
        stack_print_process(file_path);
    }

    /* 13. 保存本次 region 信息至记录文件 */
    snprintf(file_path, file_path_size, "%s\\%s-record.txt", _current_dir, APP_NAME);

    if (uvprojx_file.is_enable_lto) {
        p_file = fopen(file_path, "w+");
    } else {
        p_file = fopen(file_path, "a");
    }
    if (p_file == NULL)
    {
        log_print(_log_file, "\n[ERROR] can't create record file\n");
        log_print(_log_file, "[ERROR] Please check: %s\n", file_path);
        result = -19;
        goto __exit;
    }

    fputs(STR_MEMORY_MAP_OF_THE_IMAGE "\n\n", p_file);

    for (struct load_region *l_region = load_region_head; 
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
                     "\t\t%s %s (%s0x%.8X, %s0x%.8X, %s0x%.8X, END)\n\n",
                     STR_EXECUTION_REGION, e_region->name, STR_EXECUTE_BASE_ADDR, e_region->base_addr,
                     STR_REGION_USED_SIZE, e_region->used_size, STR_REGION_MAX_SIZE, e_region->size);
            fputs(_line_text, p_file);
        }
    }
    fputs(STR_IMAGE_COMPONENT_SIZE, p_file);
    fclose(p_file);
    
__exit:
    if (_current_dir) {
        free(_current_dir);
    }
    if (file_path) {
        free(file_path);
    }
    object_info_free(&object_info_head);
    object_info_free(&record_object_info_head);
    load_region_free(&load_region_head);
    load_region_free(&record_load_region_head);

    file_path_free(&_file_path_list_head);
    memory_info_free(&_memory_info_head);
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
            else if (strcasecmp(param[i], _command_list[seq++].cmd) == 0) {
                _progress_style = PROGRESS_STYLE_0;
            }
            else if (strcasecmp(param[i], _command_list[seq++].cmd) == 0) {
                _progress_style = PROGRESS_STYLE_1;
            }
            else if (strcasecmp(param[i], _command_list[seq++].cmd) == 0) {
                _progress_style = PROGRESS_STYLE_2;
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
 * @param  file_path:           uvprojx 文件的绝对路径
 * @param  target_name:         指定的 target name
 * @param  out_info:            [out] 解析出的 uvprojx 信息
 * @param  is_get_target_name:  是否获取 target name
 * @retval 0: 成功 | -x: 失败
 */
int uvprojx_file_process(const char *file_path, 
                         const char *target_name,
                         struct uvprojx_info *out_info,
                         bool is_get_target_name)
{
    /* 打开同名的 .uvprojx 或 .uvproj 文件 */
    FILE *p_file = fopen(file_path, "r");
    if (p_file == NULL) {
        return -1;
    }

    char *str     = NULL;
    char *lt      = NULL;
    uint8_t state = 0;
    long mem_pos  = 0;

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
                        lt   = strrchr(_line_text, '<');
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
                    lt   = strrchr(_line_text, '<');
                    if (lt) 
                    {
                        *lt = '\0';
                        strncpy_s(out_info->chip, sizeof(out_info->chip), str, strnlen_s(str, sizeof(out_info->chip)));
                        state = 2;
                    }
                }
                break;
            case 2:
                str = strstr(_line_text, LABEL_VENDOR);
                if (str)
                {
                    str += strlen(LABEL_VENDOR);
                    if (strncasecmp(str, "ARM", 3) == 0) 
                    {
                        out_info->is_has_pack = false;
                        state = 4;
                    } 
                    else 
                    {
                        out_info->is_has_pack = true;
                        state = 3;
                    }

                    // lt   = strrchr(_line_text, '<');
                    // if (lt) 
                    // {
                    //     *lt = '\0';
                    //     if (strncasecmp(str, "ARM", 3) == 0) {
                    //         out_info->is_has_pack = false;
                    //     } else {
                    //         out_info->is_has_pack = true;
                    //     }
                    //     state = 3;
                    // }
                }
                break;
            case 3:
                bool is_get_first    = false;
                char *str_p1         = NULL;
                char *str_p2         = NULL;
                char *end_ptr        = NULL;
                char name[MAX_PRJ_NAME_SIZE] = {0};
                uint32_t base_addr   = 0;
                uint32_t size        = 0;
                size_t mem_id        = UNKNOWN_MEMORY_ID;
                MEMORY_TYPE mem_type = MEMORY_TYPE_NONE;

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
                            mem_type = MEMORY_TYPE_RAM;
                        } 
                        else if (strstr(name, "ROM")) {
                            mem_type = MEMORY_TYPE_FLASH;
                        }
                        else 
                        {
                            state = 4;
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
                            memory_info_add(&_memory_info_head, name, 1, base_addr, size, mem_type, true, true);
                        } 
                        else 
                        {
                            mem_id++;
                            memory_info_add(&_memory_info_head, name, mem_id, base_addr, size, mem_type, false, true);
                        }
                    }
                }
                break;
            case 4:
                str = strstr(_line_text, LABEL_OUTPUT_DIRECTORY);
                if (str)
                {
                    str += strlen(LABEL_OUTPUT_DIRECTORY);
                    lt   = strrchr(_line_text, '<');
                    if (lt) 
                    {
                        *lt = '\0';
                        strncpy_s(out_info->output_path, sizeof(out_info->output_path), str, strnlen_s(str, sizeof(out_info->output_path)));
                        state = 5;
                    }
                }
                break;
            case 5:
                str = strstr(_line_text, LABEL_OUTPUT_NAME);
                if (str)
                {
                    str += strlen(LABEL_OUTPUT_NAME);
                    lt   = strrchr(_line_text, '<');
                    if (lt) 
                    {
                        *lt = '\0';
                        strncpy_s(out_info->output_name, sizeof(out_info->output_name), str, strnlen_s(str, sizeof(out_info->output_name)));
                        state = 6;
                    }
                }
                break;
            case 6:
                str = strstr(_line_text, LABEL_LISTING_PATH);
                if (str)
                {
                    str += strlen(LABEL_LISTING_PATH);
                    lt   = strrchr(_line_text, '<');
                    if (lt) 
                    {
                        *lt = '\0';
                        strncpy_s(out_info->listing_path, sizeof(out_info->listing_path), str, strnlen_s(str, sizeof(out_info->listing_path)));
                        state = 7;
                    }
                }
                break;
            case 7:
                /* 检查是否生成了 map 文件 */
                str = strstr(_line_text, LABEL_IS_CREATE_MAP);
                if (str)
                {
                    str += strlen(LABEL_IS_CREATE_MAP);
                    if (*str == '0') {
                        return -3;
                    } 
                    else 
                    {
                        /* 没有 pack 就读取自定义的 memory area */
                        if (out_info->is_has_pack == false || _memory_info_head == NULL) {
                            state = 8;
                        } else {
                            state = 9;
                        }
                        mem_pos = ftell(p_file);
                    }
                }
                break;
            case 8:
                /* 读取自定义 memory area */
                if (memory_area_process(_line_text, false) == false) {
                    state = 9;
                }
                break;
            case 9:
                /* 检查是否开启了 LTO */
                if (({str = strstr(_line_text, LABEL_AC6_LTO); str;}))
                {
                    str += strlen(LABEL_AC6_LTO);
                    if (*str == '0') {
                        out_info->is_enable_lto = false;
                    } else {
                        out_info->is_enable_lto = true;
                    }
                    state = 10;
                }
                else if (({str = strstr(_line_text, LABEL_END_CADS); str;}))
                {
                    out_info->is_enable_lto = false;
                    state = 10;
                }
                break;
            case 10:
                /* 读取是否使用了 keil 生成的 scatter file */
                str = strstr(_line_text, LABEL_IS_KEIL_SCATTER);
                if (str)
                {
                    str += strlen(LABEL_IS_KEIL_SCATTER);
                    if (*str == '0') 
                    {
                        out_info->is_custom_scatter = true;
                        fseek(p_file, mem_pos, SEEK_SET);
                        state = 11;
                    }
                    else 
                    {
                        out_info->is_custom_scatter = false;
                        state = 12;
                    }
                }
                else if (strstr(_line_text, LABEL_END_LDADS)) {
                    state = 12;
                }
                break;
            case 11:
                /* 将新的 memory area 加入 memory info 中 */
                if (memory_area_process(_line_text, true) == false) {
                    state = 12;
                }
                break;
            case 12:
                /* 获取已加入编译的文件路径，并记录重复的文件名 */
                if (file_path_process(_line_text, &out_info->is_has_user_lib) == false) {
                    state = 13;
                }
                break;
            default: break;
        }
        if (state == 13) {
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
 * @retval 
 */
void build_log_file_process(const char *file_path)
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

            for (struct file_path_list *path_temp = _file_path_list_head;
                 path_temp != NULL;
                 path_temp = path_temp->next)
            {
                if (strcmp(str_p1, path_temp->path) == 0)
                {
                    char *str_p3 = strrchr(str_p2 + 1, '\'');
                    *str_p3 = '\0';
                    str_p1  = strrchr(str_p2 + 1, '\\');
                    str_p1 += 1;
                    if (path_temp->new_object_name) {
                        free(path_temp->new_object_name);
                    }
                    path_temp->new_object_name = strdup(str_p1);
                    path_temp->is_rename       = false;
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
 * @retval None
 */
void file_rename_process(void)
{
    char str[MAX_PRJ_NAME_SIZE] = {0};

    for (struct file_path_list *path_temp1 = _file_path_list_head;
         path_temp1 != NULL;
         path_temp1 = path_temp1->next)
    {
        size_t repeat = 0;

        for (struct file_path_list *path_temp2 = path_temp1->next;
             path_temp2 != NULL;
             path_temp2 = path_temp2->next)
        {
            if (path_temp2->is_rename
            &&  strcmp(path_temp1->object_name, path_temp2->object_name) == 0)
            {
                repeat++;

                strncpy_s(str, sizeof(str), path_temp2->old_name, strnlen_s(path_temp2->old_name, sizeof(str)));
                char *dot = strrchr(str, '.');
                if (dot) {
                    *dot = '\0';
                }
                snprintf(str, sizeof(str), "%s_%d.o", str, repeat);
                if (path_temp2->new_object_name) {
                    free(path_temp2->new_object_name);
                }
                path_temp2->new_object_name = strdup(str);
                path_temp2->is_rename       = false;
                log_save(_log_file, "object '%s' rename to '%s'\n", path_temp2->old_name, str);
            }
        }
    }
}


/**
 * @brief  自定义 memory area 读取
 * @note   
 * @param  str:     读取到的 uvprojx 文件的每一行文本
 * @param  is_new:  是否为新增的 memory area
 * @retval true: 继续 | false: 结束
 */
bool memory_area_process(const char *str, bool is_new)
{
    static uint8_t id    = 0;
    static uint8_t state = 0;
    static uint32_t addr = 0;
    static uint32_t size = 0;
    static size_t mem_id = UNKNOWN_MEMORY_ID;
    static MEMORY_TYPE mem_type = MEMORY_TYPE_NONE;

    if (str == NULL || strstr(str, LABEL_END_ONCHIP_MEMORY))
    {
        id       = 0;
        state    = 0;
        addr     = 0;
        size     = 0;
        mem_id   = UNKNOWN_MEMORY_ID;
        mem_type = MEMORY_TYPE_NONE;
        return false;
    }

    char *str_p1  = NULL;
    char *str_p2  = NULL;
    char *end_ptr = NULL;

    switch (state)
    {
        case 0:
            if (strstr(str, LABEL_ONCHIP_MEMORY)) {
                state = 1;
            }
            break;
        case 1:
            if (strstr(str, LABEL_MEMORY_AREA)) {
                state = 2;
            }
            break;
        case 2:
            str_p1 = strstr(str, LABEL_MEMORY_TYPE);
            if (str_p1)
            {
                str_p1 += strlen(LABEL_MEMORY_TYPE);
                str_p2  = strrchr(str_p1, '<');
                *str_p2 = '\0';

                if (strtoul(str_p1, &end_ptr, 16) == 0) {
                    mem_type = MEMORY_TYPE_RAM;
                } else {
                    mem_type = MEMORY_TYPE_FLASH;
                }
                id++;
                state = 3;
            }
            break;
        case 3:
            str_p1 = strstr(str, LABEL_MEMORY_ADDRESS);
            if (str_p1)
            {
                str_p1 += strlen(LABEL_MEMORY_ADDRESS);
                str_p2  = strrchr(str_p1, '<');
                *str_p2 = '\0';
                addr    = strtoul(str_p1, &end_ptr, 16);
                state   = 4;
            }
            break;
        case 4:
            str_p1 = strstr(str, LABEL_MEMORY_SIZE);
            if (str_p1)
            {
                str_p1 += strlen(LABEL_MEMORY_SIZE);
                str_p2  = strrchr(str_p1, '<');
                *str_p2 = '\0';
                size    = strtoul(str_p1, &end_ptr, 16);

                if (size == 0) {
                    state = 2;
                } else {
                    state = 5;
                }

                if (is_new == false) {
                    break;
                }

                for (struct memory_info *memory = _memory_info_head;
                     memory != NULL;
                     memory = memory->next)
                {
                    if (addr >= memory->base_addr
                    &&  addr <= (memory->base_addr + memory->size))
                    {
                        state = 2;
                        break;
                    }
                }
            }
            break;
        case 5:
            if (strstr(str, LABLE_END_MEMORY_AREA))
            {
                bool is_offchip = true;
                if (id == 4 || id == 5 || id == 9 || id == 10) {
                    is_offchip = false;
                }
                mem_id++;
                memory_info_add(&_memory_info_head, NULL, mem_id, addr, size, mem_type, is_offchip, false);
                state = 2;
            }
            break;
        default: break;
    }
    return true;
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
 * @param  is_match_memory: 是否要匹配存储器信息
 * @retval 0: 正常 | -x: 错误
 */
int map_file_process(const char *file_path, 
                     struct load_region **region_head,
                     struct object_info **object_head,
                     bool is_get_user_lib,
                     bool is_match_memory)
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
    region_info_process(p_file, memory_map_pos, region_head, is_match_memory);

    /* 获取每个 .o 文件的 flash 和 RAM 占用情况 */
    return object_info_process(object_head, p_file, NULL, is_get_user_lib, 0);
}


/**
 * @brief  获取 load region 和 execution region 信息
 * @note   
 * @param  p_file:          文件对象
 * @param  read_start_pos:  开始读取的位置
 * @param  region_head:     region 链表头
 * @param  is_match_memory: 是否要将 region 与 memory 绑定
 * @retval 0: 正常 | -5: 获取失败
 */
int region_info_process(FILE *p_file, 
                        long read_start_pos, 
                        struct load_region **region_head,
                        bool is_match_memory)
{
    /* 先从记录的位置开始正序读取 */
    fseek(p_file, read_start_pos, SEEK_SET);

    bool is_has_load_region = false;
    uint8_t size_pos = 2;
    struct load_region *l_region = NULL;
    struct exec_region *e_region = NULL;
    
    while (fgets(_line_text, sizeof(_line_text), p_file))
    {
        if (strstr(_line_text, STR_IMAGE_COMPONENT_SIZE)) {
            return 0;
        }

        bool is_offchip = false;
        char *str_p1  = NULL;
        char *str_p2  = NULL;
        char *end_ptr = NULL;
        char name[MAX_PRJ_NAME_SIZE] = {0};
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
                if (strstr(_line_text, STR_LOAD_BASE)) {
                    size_pos = 3;
                }

                str_p1 += strlen(STR_EXECUTION_REGION) + 1;
                str_p2  = strstr(str_p1, " ");
                *str_p2 = '\0';
                strncpy_s(name, sizeof(name), str_p1, strnlen_s(str_p1, sizeof(name)));

                str_p1 = strstr(str_p2 + 1, STR_EXECUTE_BASE_ADDR);
                if (str_p1 == NULL)
                {
                    str_p1 = strstr(str_p2 + 1, STR_EXECUTE_BASE);
                    if (str_p1 == NULL) {
                        return -5;
                    }
                    str_p1 += strlen(STR_EXECUTE_BASE);
                }
                else {
                    str_p1 += strlen(STR_EXECUTE_BASE_ADDR);
                }
                
                str_p2    = strstr(str_p1, ",");
                *str_p2   = '\0';
                base_addr = strtoul(str_p1, &end_ptr, 16);

                str_p1    = strstr(str_p2 + 1, STR_REGION_USED_SIZE);
                str_p1   += strlen(STR_REGION_USED_SIZE);
                str_p2    = strstr(str_p1, ",");
                *str_p2   = '\0';
                used_size = strtoul(str_p1, &end_ptr, 16);

                str_p1    = strstr(str_p2 + 1, STR_REGION_MAX_SIZE);
                str_p1   += strlen(STR_REGION_MAX_SIZE);
                str_p2    = strstr(str_p1, ",");
                *str_p2   = '\0';
                size      = strtoul(str_p1, &end_ptr, 16);

                is_offchip  = false;
                memory_id   = UNKNOWN_MEMORY_ID;
                memory_type = MEMORY_TYPE_UNKNOWN;

                if (is_match_memory)
                {
                    /* 将 execution region 与 对应的 memory 绑定  */
                    for (struct memory_info *memory_temp = _memory_info_head;
                         memory_temp != NULL;
                         memory_temp = memory_temp->next)
                    {
                        if (base_addr >= memory_temp->base_addr
                        &&  base_addr <= (memory_temp->base_addr + memory_temp->size))
                        {
                            is_offchip  = memory_temp->is_offchip;
                            memory_id   = memory_temp->id;
                            memory_type = memory_temp->type;
                            break;
                        }
                    }
                }

                region_zi_process(NULL, NULL, 0);
                e_region = load_region_add_exec_region(&l_region, name, memory_id, base_addr, size, used_size, memory_type, is_offchip);
            }
            else if (e_region 
            &&       e_region->memory_type != MEMORY_TYPE_FLASH
            &&       strstr(_line_text, "0x"))
            {
                region_zi_process(&e_region, _line_text, size_pos);
            }
        }
    }

    return -5;
}


/**
 * @brief  获取 region 中的 zero init 区域块分布
 * @note   e_region 参数传值为 NULL 时将复位本函数。
 *         切换至新的 execution region 前，必须复位本函数
 * @param  e_region:    execution region
 * @param  text:        一行文本内容
 * @param  size_pos:    Size 栏目所在的位置，从 1 算起
 * @retval None
 */
void region_zi_process(struct exec_region **e_region,
                       char *text,
                       size_t size_pos)
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

    char *addr_token = strtok(text, " ");
    for (size_t i = 2; i < size_pos; i++) {
        strtok(NULL, " ");
    }
    char *size_token = strtok(NULL, " ");

    char *end_ptr = NULL;
    uint32_t addr = strtoul(addr_token, &end_ptr, 16);
    uint32_t size = strtoul(size_token, &end_ptr, 16);

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
 * @param  is_get_user_lib: 是否获取用户 lib 信息
 * @param  parse_mode:      解析模式 0: 按 map 文件解析 | 1: 按 record 文件解析
 * @retval 0: 正常 | -x: 错误
 */
int object_info_process(struct object_info **object_head,
                        FILE *p_file,
                        long *end_pos,
                        bool is_get_user_lib,
                        uint8_t parse_mode)
{
    uint8_t state  = 0;
    int  result    = 0;
    int  value[16] = {0};
    char name[MAX_PRJ_NAME_SIZE] = {0};
    char *token    = NULL;
    char *end_ptr  = NULL;
    char *new_line = NULL;
    size_t index   = 0;

    /* 获取用户文件的 object info */
    while (fgets(_line_text, sizeof(_line_text), p_file))
    {
        switch (state)
        {
            case 0:
                if (parse_mode == 0)
                {
                    /* Object Name 全部添加 */
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
                        if (index == OBJECT_INFO_STR_QTY) {
                            object_info_add(object_head, name, value[0], value[2], value[3], value[4]);
                        } 
                        else 
                        {
                            result = -3;
                            break;
                        }
                    }
                    else if (strstr(_line_text, STR_LIBRARY_MEMBER_NAME)) 
                    {
                        if (is_get_user_lib) {
                            state = 1;
                        } else {
                            state = 3;
                        }
                    }
                }
                else if (parse_mode == 1)
                {
                    if (strstr(_line_text, STR_OBJECT_NAME)) {
                        state = 2;
                    }
                }
                break;
            case 1:
                /* Library Member Name 仅添加匹配的 object */
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
                    if (index == OBJECT_INFO_STR_QTY) 
                    {
                        for (struct file_path_list *path_temp = _file_path_list_head; 
                             path_temp != NULL; 
                             path_temp = path_temp->next)
                        {
                            if (path_temp->file_type == OBJECT_FILE_TYPE_LIBRARY)
                            {
                                if (strcasecmp(name, path_temp->new_object_name) == 0) 
                                {
                                    object_info_add(object_head, name, value[0], value[2], value[3], value[4]);
                                    break;
                                }
                            }
                        }
                    }
                }
                else if (strstr(_line_text, STR_LIBRARY_NAME)) {
                    state = 2;
                }
                break;
            case 2:
                /* Library Member Name 仅添加匹配的 object */
                if (strstr(_line_text, STR_OBJECT_TOTALS)) 
                {
                    state = 3;
                    break;
                }
                else
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
                    if (index == OBJECT_INFO_STR_QTY) 
                    {
                        if (parse_mode == 1)
                        {
                            object_info_add(object_head, name, value[0], value[2], value[3], value[4]);
                            break;
                        }

                        for (struct file_path_list *path_temp = _file_path_list_head; 
                             path_temp != NULL; 
                             path_temp = path_temp->next)
                        {
                            if (path_temp->file_type == OBJECT_FILE_TYPE_LIBRARY)
                            {
                                if (strcasecmp(name, path_temp->old_name) == 0) 
                                {
                                    object_info_add(object_head, name, value[0], value[2], value[3], value[4]);
                                    break;
                                }
                            }
                        }
                    }
                }
                break;
            default: break;
        }

        if (state == 3 || result != 0) {
            break;
        }
    }

    if (state == 3) 
    {
        if (end_pos) {
            *end_pos = ftell(p_file);
        }
    }
    fclose(p_file);
    return result;
}


/**
 * @brief  记录文件处理
 * @note   
 * @param  file_path:       文件的绝对路径
 * @param  region_head:     region 链表头
 * @param  object_head:     object 文件链表头
 * @param  is_has_object:   [out] 是否存在 object 信息
 * @param  is_has_region:   [out] 是否存在 region 信息
 * @param  is_match_memory: 是否要将 region 与 memory 绑定
 * @retval 0: 正常 | -x: 错误
 */
int record_file_process(const char *file_path, 
                        struct load_region **region_head,
                        struct object_info **object_head,
                        bool *is_has_object,
                        bool *is_has_region,
                        bool is_match_memory)
{
    *is_has_object = false;
    *is_has_region = false;

    FILE *p_file = fopen(file_path, "r");
    if (p_file == NULL) {
        return -1;
    }

    long end_pos = 0;
    int result = object_info_process(object_head, p_file, &end_pos, false, 1);
    if (result == 0) {
        *is_has_object = true;
    }

    p_file = fopen(file_path, "r");
    if (p_file == NULL) {
        return -1;
    }
    result = region_info_process(p_file, end_pos, region_head, is_match_memory);
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
        if (obj_info->path == NULL) {
            continue;
        }

        char *path        = obj_info->path;
        char ram_text[MAX_PRJ_NAME_SIZE]   = {0};
        char flash_text[MAX_PRJ_NAME_SIZE] = {0};
        size_t path_len   = strnlen_s(obj_info->path, MAX_PATH);
        size_t path_space = max_path_len - path_len + 1;
        uint32_t ram      = obj_info->rw_data + obj_info->zi_data;
        uint32_t flash    = obj_info->code + obj_info->ro_data + obj_info->rw_data;

//        if (obj_info->path == NULL || _is_display_path == false) 
        if (_is_display_path == false) 
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
 * @brief  模式零打印内存占用情况
 * @note   
 * @param  e_region:        execution region
 * @param  mem_type:        指定打印的 execution region 内存类型
 * @param  max_region_name: 最大的 execution region 名称长度
 * @param  is_has_record:   是否有记录文件
 * @param  is_print_null:   是否打印未使用的存储器
 * @retval None
 */
void memory_mode0_print(struct exec_region *e_region,
                        MEMORY_TYPE mem_type,
                        size_t max_region_name, 
                        bool is_has_record,
                        bool is_print_null)
{
    char str[MAX_PRJ_NAME_SIZE] = {0};

    if (mem_type == MEMORY_TYPE_UNKNOWN) 
    {
        bool is_has_unknown = false;
        
        for (struct exec_region *region_temp = e_region;
             region_temp != NULL;
             region_temp = region_temp->next)
        {
            if (region_temp->memory_type == MEMORY_TYPE_UNKNOWN) 
            {
                is_has_unknown = true;
                break;
            }
        }
        if (is_has_unknown)
        {
            strncpy_s(str, sizeof(str), "        UNKNOWN\n", strlen("        UNKNOWN\n"));
            log_print(_log_file, str);
            memory_mode2_print(e_region, max_region_name, is_has_record);
        }
        return;
    }

    size_t id = 0;
    bool is_no_region  = true;
    bool is_print_head = false;

    for (struct memory_info *memory = _memory_info_head;
         memory != NULL;
         memory = memory->next)
    {
        if (memory->type != mem_type) {
            continue;
        }

        id++;
        is_no_region  = true;
        is_print_head = false;

        if (mem_type == MEMORY_TYPE_RAM) {
            snprintf(str, sizeof(str), "        RAM %d    ", id);
        }
        else if (mem_type == MEMORY_TYPE_FLASH) {
            snprintf(str, sizeof(str), "        FLASH %d  ", id);
        }

        for (struct exec_region *region = e_region;
             region != NULL;
             region = region->next)
        {
            if (region->is_printed == false
            &&  memory->id   == region->memory_id
            &&  memory->type == region->memory_type)
            {
                if (is_print_head == false)
                {
                    log_print(_log_file, "%s%*s [0x%.8X | 0x%.8X (%d)]\n",
                              str, max_region_name, " ", memory->base_addr, memory->size, memory->size);
                    is_print_head = true;
                }

                progress_print(region, max_region_name, is_has_record);
                is_no_region = false;
            }
        }

        if (is_no_region 
        &&  is_print_null 
        &&  memory->is_from_pack) 
        {
            log_print(_log_file, "%s%*s [0x%.8X | 0x%.8X (%d)]\n",
                      str, max_region_name, " ", memory->base_addr, memory->size, memory->size);
            log_print(_log_file, "                NULL\n \n");
        }
        else {
            log_print(_log_file, " \n");
        }
    }
}


/**
 * @brief  模式一打印内存占用情况
 * @note   
 * @param  e_region:        execution region
 * @param  mem_type:        指定打印的 execution region 内存类型
 * @param  is_offchip:      是否为片外 memory
 * @param  max_region_name: 最大的 execution region 名称长度
 * @param  is_has_record:   是否有记录文件
 * @retval None
 */
void memory_mode1_print(struct exec_region *e_region,
                        MEMORY_TYPE mem_type,
                        bool is_offchip,
                        size_t max_region_name, 
                        bool is_has_record)
{
    char str[MAX_PRJ_NAME_SIZE] = {0};

    if (mem_type == MEMORY_TYPE_UNKNOWN) 
    {
        bool is_has_unknown = false;
        
        for (struct exec_region *region = e_region;
             region != NULL;
             region = region->next)
        {
            if (region->memory_type == MEMORY_TYPE_UNKNOWN) 
            {
                is_has_unknown = true;
                break;
            }
        }
        if (is_has_unknown)
        {
            strncpy_s(str, sizeof(str), "        UNKNOWN\n", strlen("        UNKNOWN\n"));
            log_print(_log_file, str);
            memory_mode2_print(e_region, max_region_name, is_has_record);
        }
        return;
    }

    if (mem_type == MEMORY_TYPE_RAM) {
        strncpy_s(str, sizeof(str), "        RAM", strlen("        RAM"));
    }
    else if (mem_type == MEMORY_TYPE_FLASH) {
        strncpy_s(str, sizeof(str), "        FLASH", strlen("        FLASH"));
    }

    if (is_offchip == false) {
        strncat_s(str, sizeof(str), " (on-chip)\n", strlen(" (on-chip)\n"));
    } else {
        strncat_s(str, sizeof(str), " (off-chip)\n", strlen(" (off-chip)\n"));
    }
    
    bool is_print_head = false;
    for (struct exec_region *region = e_region;
         region != NULL;
         region = region->next)
    {
        if (region->is_printed  == false
        &&  region->is_offchip  == is_offchip
        &&  region->memory_type == mem_type)
        {
            if (is_print_head == false)
            {
                log_print(_log_file, str);
                is_print_head = true;
            }
            progress_print(region, max_region_name, is_has_record);
            region->is_printed = true;
        }
    }

    if (is_print_head) {
        log_print(_log_file, " \n");
    }
}


/**
 * @brief  模式二打印内存占用情况
 * @note   
 * @param  e_region:        execution region
 * @param  max_region_name: 最大的 execution region 名称长度
 * @param  is_has_record:   是否有记录文件
 * @retval None
 */
void memory_mode2_print(struct exec_region *e_region,
                        size_t max_region_name, 
                        bool is_has_record)
{
    for (struct exec_region *region_temp = e_region;
         region_temp != NULL;
         region_temp = region_temp->next)
    {
        if (region_temp->is_printed == false)
        {
            progress_print(region_temp, max_region_name, is_has_record);
            region_temp->is_printed = true;
        }
    }
    log_print(_log_file, " \n");
}


/**
 * @brief  内存占用进度条化打印
 * @note   
 * @param  region:          execution region
 * @param  max_region_name: 最大的 execution region 名称长度
 * @param  is_has_record:   是否有记录文件
 * @retval None
 */
void progress_print(struct exec_region *region,
                    size_t max_region_name, 
                    bool is_has_record)
{
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
        snprintf(used_size_str, sizeof(used_size_str), "%5.1f MB", used_size);
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
        snprintf(size_str, sizeof(size_str), "%5.1f MB", size);
    }

    double percent = (double)region->used_size * 100 / region->size;
    if (percent > 100) {
        percent = 100;
    }

    size_t used            = 0;
    char progress[256]     = {0};
    uint8_t zi_symbol[2]   = {0};
    uint8_t used_symbol[2] = {0};
    uint8_t symbol_size    = 0;

    symbol_size    = 1;
    zi_symbol[0]   = ZI_SYMBOL_0;
    used_symbol[0] = USED_SYMBOL_0;

    if (_progress_style == PROGRESS_STYLE_0)
    {
        if (_encoding_type == ENCODING_TYPE_GBK)
        {
            symbol_size    = 2;
            zi_symbol[0]   = ZI_SYMBOL_GBK_H;
            zi_symbol[1]   = ZI_SYMBOL_GBK_L;
            used_symbol[0] = USED_SYMBOL_GBK_H;
            used_symbol[1] = USED_SYMBOL_GBK_L;
        }
        else if (_encoding_type == ENCODING_TYPE_BIG5)
        {
            symbol_size    = 2;
            zi_symbol[0]   = ZI_SYMBOL_BIG5_H;
            zi_symbol[1]   = ZI_SYMBOL_BIG5_L;
            used_symbol[0] = USED_SYMBOL_BIG5_H;
            used_symbol[1] = USED_SYMBOL_BIG5_L;
        }
    }
    else if (_progress_style == PROGRESS_STYLE_2)
    {
        symbol_size    = 1;
        zi_symbol[0]   = ZI_SYMBOL_1;
        used_symbol[0] = USED_SYMBOL_1;
    }

    /* 先填充实际占用部分 */
    for (; used < ((uint32_t)percent / 2); used++)
    {
        memcpy(&progress[symbol_size * used], used_symbol, symbol_size);
    }
    /* 小于显示比例，避免是空的 */
    if (used == 0 && region->used_size != 0)
    {
        memcpy(&progress[0], used_symbol, symbol_size);
        used = 1;
    }
    /* 将占用部分的 ZI 部分替换 */
    for (struct region_block *block = region->zi_block;
         block != NULL;
         block = block->next)
    {
        size_t zi_start = ((double)block->start_addr - region->base_addr) * 100 / region->size / 2;
        size_t zi_end   = ((double)block->start_addr + block->size - region->base_addr) * 100 / region->size / 2;

        if (zi_start == 0 && block->start_addr > region->base_addr) {
            zi_start = 1;
        }
        log_save(_log_file, "                [zi start] %d   [zi end] %d\n", zi_start, zi_end);

        for (; zi_start < zi_end && zi_start < used; zi_start++) {
            memcpy(&progress[symbol_size * zi_start], zi_symbol, symbol_size);
        }

        if ((block->start_addr + block->size) >= (region->base_addr + region->size)) {
            break;
        }
    }
    /* 剩下未使用部分 */
    for (size_t unused = 0; unused < (50 - used); unused++){
        strncat_s(progress, sizeof(progress), UNUSE_SYMBOL, strlen(UNUSE_SYMBOL));
    }

    size_t space_len = max_region_name - strnlen_s(region->name, max_region_name) + 1;
    snprintf(_line_text, sizeof(_line_text),
             "                %s%*s [0x%.8X]|%s| ( %s / %s ) %5.1f%%  ",
             region->name, space_len, " ", region->base_addr, progress, used_size_str, size_str, percent);

    if (is_has_record)
    {
        if (region->old_exec_region == NULL) {
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
 * @param  dir:             要搜索的目录
 * @param  dir_len:         要搜索的目录长度
 * @param  extension[]:     扩展名，支持多个
 * @param  extension_qty:   扩展名数量
 * @param  list:            [out] 保存搜索到的文件路径
 * @retval None
 */
void search_files_by_extension(const char *dir, 
                               size_t dir_len,
                               const char *extension[], 
                               size_t extension_qty, 
                               struct prj_path_list *list)
{
    HANDLE h_find;
    WIN32_FIND_DATA find_data;

    /* 加上 '*' 以搜索所有文件和文件夹 */
    char *path = (char *)malloc(dir_len + 3);
    snprintf(path, dir_len + 3, "%s\\*", dir);

    /* 开始搜索 */
    h_find = FindFirstFile(path, &find_data);
    if (h_find == INVALID_HANDLE_VALUE) 
    {
        free(path);
        return;
    }

    do {
        /* 如果找到的是文件，判断其后缀是否为 keil 工程 */
        if ((find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
        {
            char *str = strrchr(find_data.cFileName, '.');
            if (str && is_same_string(str, extension, extension_qty))
            {
                char *file_path = malloc(dir_len * 2);
                snprintf(file_path, dir_len * 2, "%s\\%s", dir, find_data.cFileName);
                prj_path_list_add(list, file_path);
            }
        }
    } while (FindNextFile(h_find, &find_data));

    FindClose(h_find);
    free(path);
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
 * @param  out_path_size:   输出的路径的大小
 * @param  absolute_path:   输入的绝对路径
 * @param  relative_path:   输入的相对路径
 * @retval 0: 正常 | -x: 错误
 */
int combine_path(char *out_path, 
                 size_t out_path_size,
                 const char *absolute_path, 
                 const char *relative_path)
{
    /* 1. 将绝对路径 absolute_path 的文件名和扩展名去除 */
    strncpy_s(out_path, out_path_size, absolute_path, strnlen_s(absolute_path, MAX_PATH));

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
    for (size_t i = 0; i < strnlen_s(out_path, out_path_size); i++)
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
    strncat_s(out_path, out_path_size, "\\", 1);
    strncat_s(out_path, out_path_size, &relative_path[valid_path_offset], strnlen_s(&relative_path[valid_path_offset], MAX_PATH));

    for (size_t i = 0; i < strnlen_s(out_path, out_path_size); i++)
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
    char old_name[MAX_PRJ_NAME_SIZE] = {0};
    struct file_path_list **path_list = path_head;

    memcpy_s(old_name, sizeof(old_name), name, strnlen_s(name, sizeof(old_name)));

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
            &&  strcmp(str, list->object_name) == 0) 
            {
                is_rename = true;
            }
            last_list = list;
            list = list->next;
        } while (list);

        path_list = &last_list->next;
    }

    *path_list = (struct file_path_list *)malloc(sizeof(struct file_path_list));

    (*path_list)->old_name        = strdup(old_name);
    (*path_list)->object_name     = strdup(str);
    (*path_list)->new_object_name = strdup(str);
    (*path_list)->path            = strdup(path);
    (*path_list)->file_type       = file_type;
    (*path_list)->is_rename       = is_rename;
    (*path_list)->next            = NULL;

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
        free(temp->object_name);
        free(temp->new_object_name);
        free(temp->path);
        free(temp);
    }
    *path_head = NULL;
}


/**
 * @brief  创建新的 memory 并添加进链表
 * @note   
 * @param  memory_head:     memory 链表头
 * @param  name:            memory 名称
 * @param  id:              memory ID
 * @param  base_addr:       memory 基地址
 * @param  size:            memory 大小，单位 byte
 * @param  mem_type:        memory 类型
 * @param  is_offchip:      是否为片外 memory
 * @param  is_from_pack:    信息是否来自 keil 的 pack
 * @retval true: 成功 | false: 失败
 */
bool memory_info_add(struct memory_info **memory_head,
                     const char  *name,
                     size_t      id,
                     uint32_t    base_addr,
                     uint32_t    size,
                     MEMORY_TYPE mem_type,
                     bool        is_offchip,
                     bool        is_from_pack)
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
    if (name) {
        (*memory)->name = strdup(name);
    } else {
        (*memory)->name = NULL;
    }
    (*memory)->id           = id;
    (*memory)->base_addr    = base_addr;
    (*memory)->size         = size;
    (*memory)->type         = mem_type;
    (*memory)->is_offchip   = is_offchip;
    (*memory)->is_from_pack = is_from_pack;
    (*memory)->next         = NULL;

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
        if (temp->name) {
            free(temp->name);
        }
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
 * @param  mem_type:    execution region 内存类型
 * @param  is_offchip:  是否为片外 memory
 * @retval NULL | struct exec_region *
 */
struct exec_region * load_region_add_exec_region(struct load_region **l_region, 
                                                 const char  *name,
                                                 size_t      memory_id,
                                                 uint32_t    base_addr,
                                                 uint32_t    size,
                                                 uint32_t    used_size,
                                                 MEMORY_TYPE mem_type,
                                                 bool        is_offchip)
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
    (*e_region)->memory_type     = mem_type;
    (*e_region)->is_offchip      = is_offchip;
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
