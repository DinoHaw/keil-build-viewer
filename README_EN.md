# keil-build-viewer v1.5b

! [demo](images/main.png)

## 1 Introduction
This is a keil compilation information display enhancement tool that supports the visualization of chip memory, lightweight and no dependencies. It has the following features:
1. Analyze the RAM and flash usage of each file involved in the compilation
    - Automatically ignore files that are not included in the compilation
    - Automatic retrieval of files renamed by keil
    - **Support for double-clicking to open documents**
    - Support for turning off the display of this information
    - Support for displaying only file names

2. Analyze the RAM and flash usage of the chip and visualize it with a progress bar
    - `■` or `#` or `X` Indicates physically occupied area
    - `□` or `O` Indicates the area of zero initialize
    - `_` Indicates unused areas

3. Demonstration of the amount of data added and subtracted after secondary compilation
    - By comparing the results of the last compilation **shows the size of the amount of data added or subtracted by this compilation, in byte**
    - If the file is new, `[NEW]` will be displayed

4. Automatically searches for keil projects in this level of the directory, so it can be called without parameters
    - The last keil project searched is selected by default
    - Support for entering absolute paths to specify keil projects
    - Support for specifying keil projects by filename only (must be a directory of the same level, no file extensions)
    - **If there are spaces in the path or project name, enclose them in `""`**

5. Support for input parameter modification options
    - Specify the keil project as described in function 4
    - `-OBJ` Display RAM and flash occupancy information for each file (default)
    - `-NOOBJ` Does not display RAM and flash usage information for each file
    - `-PATH` Displays the relative path to each file (default)
    - `-NOPATH` Shows only the filename of each file
    - `The following features are new in v1.5`
    - `-STYLE0` Progress bar style following system (default)
    - `-STYLE1` Progress bar style 1: `|####OOO____|` (default style for non-Chinese environments)
    - `-STYLE2` Progress Bar Style 2: `|XXXOOOO____|`
    - **The above commands are not case-sensitive**

6. Show maximum stack usage
    - Data from keil, static can not be analyzed accurately, data for reference only

7. support placed in the public directory, you can call the tool in any directory, without following keil uvproj(x) project
    - v1.4 New Features
    - **You must set the system environment variable and place `keil-build-viewer.exe` in the directory specified by the system environment variable**. It is recommended that you use the system environment variable `Path`.
    - This saves copying `keil-build-viewer.exe` to the corresponding keil uvproj(x) project, but `after build` still needs to be filled in, see `2 Use in keil` for details.

> **Description:** All parameters of this tool can be entered out of order, and when it is empty, it means that the default value is selected, but the parameters need to be separated from each other by **space**.

> **Double-click to open the corresponding file animated presentation**
! [Double click to open file](images/open_file.gif)

## 2 Use in keil
1. The way to invoke in keil is very simple, download latest version of `keil-build-viewer.exe` from [releases](https://gitee.com/DinoHaw/keil-build-viewer/releases) and put it in the same level directory of the uvproj(x) project corresponding to keil, and configure it according to the following figure. The configuration is done as shown below. If you want to enter other commands, enter them after `keil-build-viewer.exe`. If you want to display only the filename of each file, you can fill in the following: <br>
    ```
    keil-build-viewer.exe -NOPATH
    ```

2. In cmd or powershell use the same thing, just add the prefix `. \`. For example: <br>
    ```
    . \keil-build-viewer.exe
    ```
! [keil configuration](images/user_command.png)

## 3 I want to compile this tool myself ##
**This code is only supported on windows systems**.
### 3.1 Preparatory operations
0. If you already have gcc installed, ignore this step.
1. Download the gcc compiler, for compatibility, here is a 32-bit mingw download link: [i686-13.1.0-release-posix-dwarf-ucrt-rt_v11-rev1.7z](https://github.com/niXman/mingw- builds-binaries/releases/download/13.1.0-rt_v11-rev1/i686-13.1.0-release-posix-dwarf-ucrt-rt_v11-rev1.7z)
2. Unzip the program and put it in any path, take `C:\mingw32` as an example.
3. Configure system environment variables
    ! [Configure environment variables](images/path_config.png)

4. Open `powershell` or `cmd` and type `gcc -v`, and the following image appears to indicate successful configuration
    ! [gcc](images/gcc.png)

### 3.2 Compilation
1. Open `powershell` or `cmd` and locate the code directory.
    - If you are using `powershell`, hold down the `shift` key while clicking the right mouse button on an empty space in the code directory and select Open `powershell`, which will automatically locate the code directory.

2. Execute the following gcc command
    ``
    gcc . \keil-build-viewer.c -o . \keil-build-viewer.exe
    ``
3. Compilation passes without any message
    ! [gcc compile passed](images/gcc_compile.png)

## 4 Questions answered
1. A prompt such as `[ERROR] NO keil project found` appears.
    > Confirm that `keil-build-viewer.exe` is placed in the same directory as the keil uvproj(x) project you need to view.

2. A prompt such as `[ERROR] listing path is empty` appears.
    > Select the folder in keil where you want to place the listing-related files.
    ! [select_listing_folder](images/select_listing_folder.png)

3. Prompts such as `[ERROR] generate map file is not checked` or `[ERROR] Check if a map file exists` occur
    > Make sure that keil has checked the options shown below.
    ! [create_map](images/create_map.png)

4. If compilation information is missing or deviates from reality
    > Confirm that the parsed project is the target project (when there are multiple projects in the same level of directory) <br>
    > You can check the current project parsed by the tool with the parsed predecessor information, and if you find inconsistencies, you can specify the project name after `keil-build-viewer.exe`, for example:
    ```
    keil-build-viewer.exe TIMER
    or
    keil-build-viewer.exe TIMER.uvprojx
    ```
    > ! [parsed project](images/keil_project_name.png)

5. If there are spaces in the project directory or project name, enclose them in `""`.
    > ! [space case](images/space_example.png)

6. For other questions, please raise issues or contact the authors.

## Important note
> **1. Currently only the keil MDK is supported.**
>
> **2. Does not support parsing of files added via RTE**

## Modify the record
| version | date | modifier | modification | content
|:-----:|:----------:|--------------|---------------------------------------------------|
| v1.0 | 2023-11-10 | Dino | Initial release |
| v1.1 | 2023-11-11 | Dino | 1. Adaptation of RAM and ROM parsing |
| v1.2 | 2023-11-11 | Dino | 1. Adapted map file for keil4<br>2. Added print message when LTO is detected to be on<br>3. Fixed the problem that no region is printed when LTO is enabled.
| v1.3 | 2023-11-12 | Dino | 1. Fixed the issue that only one lib is parsed when there are multiple libs in the project |
| v1.4 | 2023-11-21 | Dino | 1. Add the function of placing this tool in the directory contained in the system environment variable Path |
| v1.5 | 2023-11-30 | Dino | 1. Add more progress bar styles<br>2. Add parsing customized memory area<br>3. Fix the problem of displaying an exception when the RAM and ROM information is missing |
| v1.5a | 2023-11-30 | Dino | 1. Fix object data overflow problem<br>2. Change the display strategy of progress bar memory size, no longer round up |
| v1.5b | 2023-12-02 | Dino | 1. Fix save file path memory dynamic allocation is too small |

