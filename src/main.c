#include <windows.h>
#include "gui.h"
#include "bc7enc_wrapper.h"
#include "nvtt_c_wrapper.h"
#include "log.h"

FILE *g_log_file = NULL;

LONG WINAPI CrashHandler(EXCEPTION_POINTERS *ExceptionInfo) {
    if (g_log_file) {
        fprintf(g_log_file, "\n========================================\n");
        fprintf(g_log_file, "CRITICAL ERROR: Application Crashed!\n");
        fprintf(g_log_file, "Exception Code: 0x%08X\n", ExceptionInfo->ExceptionRecord->ExceptionCode);
        fprintf(g_log_file, "Exception Address: 0x%p\n", ExceptionInfo->ExceptionRecord->ExceptionAddress);
        fprintf(g_log_file, "========================================\n");
        fflush(g_log_file);
    }
    MessageBoxA(NULL, "O aplicativo encontrou um erro crítico e será fechado.\nUm log foi salvo em EasyOptimizer.log.", "Crash", MB_ICONERROR | MB_OK);
    return EXCEPTION_EXECUTE_HANDLER;
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nCmdShow) {
    (void)hPrevInstance;
    (void)lpCmdLine;
    (void)nCmdShow;
    
    // Force current directory to be the executable's directory
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(NULL, exePath, MAX_PATH);
    wchar_t *lastSlash = wcsrchr(exePath, L'\\');
    if (lastSlash) {
        *lastSlash = L'\0';
        SetCurrentDirectoryW(exePath);
    }
    
    g_log_file = fopen("EasyOptimizer.log", "w");
    if (g_log_file) {
        setvbuf(g_log_file, NULL, _IONBF, 0); // Disable buffering completely
    }
    SetUnhandledExceptionFilter(CrashHandler);

    log_init_console();
    LOG("Initializing bc7enc (ISPC BC7 + rgbcx)...");
    bc7enc_init();
    LOG("bc7enc ready");
    LOG("Default encoder: CPU (bc7enc ISPC). Toggle 'Encoder' in the sidebar to use GPU (NVTT/CUDA).");
    nvtt_wrapper_probe();

    gui_init(hInstance);
    LOG("GUI initialized, entering message loop");
    gui_run();

    LOG("Shutting down");
    FreeConsole();
    if (g_log_file) fclose(g_log_file);
    return 0;
}
