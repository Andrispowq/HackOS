#include "support.hpp"
#include "hackos/abi.h"
#include "hackos/hardware.h"
#include "hackos/syscall_numbers.h"
static_assert(HACKOS_SYS_exit == 0 && HACKOS_SYS_user_write == 14 && HACKOS_SYS_input == 15 && HACKOS_SYS_power == 26 &&
                  HACKOS_SYS_yield == 27 && HACKOS_SYS_spawn_args == 28 && HACKOS_SYS_wait == 29 &&
                  HACKOS_SYS_spawn_foreground == 30 && HACKOS_SYS_window_create == 31 &&
                  HACKOS_SYS_window_close == 34 && HACKOS_SYS_window_state == 35 && HACKOS_SYS_window_event == 36 &&
                  HACKOS_SYS_window_mode == 37 && HACKOS_SYS_getcwd == 38 && HACKOS_SYS_chdir == 39 &&
                  HACKOS_SYS_identity == 40 && HACKOS_SYS_user_info == 41 && HACKOS_SYS_group_info == 42 &&
                  HACKOS_SYS_user_create == 43 && HACKOS_SYS_group_create == 44 && HACKOS_SYS_user_add_group == 45 &&
                  HACKOS_SYS_spawn_as == 46 && HACKOS_SYS_auth_state == 47 && HACKOS_SYS_auth_setup == 48 &&
                  HACKOS_SYS_auth_login == 49 && HACKOS_SYS_password_set == 50 && HACKOS_SYS_file_save == 51 &&
                  HACKOS_SYS_mkdir == 52 && HACKOS_SYS_unlink == 53 && HACKOS_SYS_rename == 54 &&
                  HACKOS_SYS_open_mode == 55 && HACKOS_SYS_chown == 60 && HACKOS_SYS_terminal_read == 61 &&
                  HACKOS_SYS_job_wait == 62 && HACKOS_SYS_job_control == 63 && HACKOS_SYS_heap_resize == 64 &&
                  HACKOS_SYS_spawn_job == 65 && HACKOS_SYS_fd_open == 66 && HACKOS_SYS_spawn_io == 76 &&
                  HACKOS_SYS_fd_flush == 78 && HACKOS_SYS_hardware == 79 && HACKOS_SYS_terminal_info == 80 &&
                  HACKOS_SYS_terminal_get == 81 && HACKOS_SYS_terminal_set == 82 && HACKOS_SYS_terminal_cursor == 83 &&
                  HACKOS_SYS_readdir_batch == 84 && HACKOS_SYS_window_frame == 85 && HACKOS_SYS_window_upload == 86 &&
                  HACKOS_SYS_window_input == 87 && HACKOS_SYS_window_event2 == 88 && HACKOS_SYS_clipboard == 89 &&
                  HACKOS_SYS_window_close_reply == 90 && HACKOS_SYS_auth_desktop == 91 && HACKOS_SYS_desktop == 92 &&
                  HACKOS_SYS_terminal_session == 93 && HACKOS_SYS_session_logout == 94 &&
                  HACKOS_SYS_account_edit == 95 && HACKOS_SYS_password_change == 96 && HACKOS_SYS_echo == 97 &&
                  HACKOS_SYS_resolve == 98 && HACKOS_SYSCALL_COUNT == 99,
              "User syscall ABI changed");

TEST(syscall_record_layout)
{
    CHECK(sizeof(HackosTerminalState) == 40 && sizeof(HackosDesktopEntry) == 64);
    CHECK(sizeof(HackosPollFd) == 16 && sizeof(HackosSpawnIo) == 32);
    CHECK(sizeof(HackosTerminalInfo) == 16 && sizeof(HackosTerminalSettings) == 24);
    CHECK(sizeof(HackosClock) == 16 && __builtin_offsetof(HackosClock, year) == 14);
    CHECK(sizeof(HackosHardwareInfo) == 128);
    CHECK(sizeof(HackosWindowSpec) == 48);
    CHECK(sizeof(HackosWindowRect) == 16);
    CHECK(sizeof(HackosWindowFrame) == 16);
    CHECK(sizeof(HackosWindowEvent2) == 48);
    CHECK(sizeof(HackosWindowInputState) == 56);
    CHECK(sizeof(HackosWindowUpload) == 24);
    CHECK(sizeof(HackosWindowState) == 32);
    CHECK(sizeof(HackosWindowEvent) == 32);
    CHECK(__builtin_offsetof(HackosWindowSpec, title) == 16);
    CHECK(sizeof(HackosFileInfo) == 296 && sizeof(HackosProcessInfo) == 296);
    CHECK(__builtin_offsetof(HackosFileInfo, name) == 16);
    CHECK(__builtin_offsetof(HackosFileInfo, created) == 288);
    CHECK(__builtin_offsetof(HackosFileInfo, modified) == 292);
    CHECK(__builtin_offsetof(HackosProcessInfo, name) == 16);
    CHECK(__builtin_offsetof(HackosProcessInfo, uid) == 272);
    CHECK(__builtin_offsetof(HackosProcessInfo, parentPid) == 280);
    CHECK(__builtin_offsetof(HackosProcessInfo, processGroup) == 288);
    CHECK(sizeof(HackosCredentials) == 48 && sizeof(HackosUserInfo) == 80 && sizeof(HackosGroupInfo) == 40);
    return 0;
}
