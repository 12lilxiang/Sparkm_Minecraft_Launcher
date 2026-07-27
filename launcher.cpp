/*
 * SparkmLauncher 启动引导程序 (launcher.exe)
 *
 * 编译: cl /MT /EHsc /O2 /utf-8 /Fe:launcher.exe launcher.cpp
 *
 * 特点:
 *   - 静态CRT (/MT), 不依赖 VC 运行时 DLL
 *   - 无第三方库, 纯 Win32 API + WinHTTP
 *   - 检测 VC 运行时 -> 缺失则自动下载安装 -> 启动主程序
 *
 * 工作流程:
 *   1. 检测 System32 下的 MSVCP140/VCRUNTIME140/VCRUNTIME140_1
 *   2. 缺失则从 aka.ms 下载 VC++ Redistributable
 *   3. ShellExecuteEx + runas 提权静默安装
 *   4. 安装完成后启动同目录下的 SparkmLauncher.exe
 *   5. 等待主程序退出后自身退出（保持控制台窗口不关闭）
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <winhttp.h>
#include <shlwapi.h>

#pragma comment(lib, "kernel32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "shlwapi.lib")

// ===================== 辅助函数 =====================

// 宽字符控制台输出
static void Print(const wchar_t* msg)
{
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hOut == INVALID_HANDLE_VALUE) return;
    DWORD written = 0;
    WriteConsoleW(hOut, msg, (DWORD)wcslen(msg), &written, NULL);
}

// 等待用户按键（纯Win32 API，不依赖CRT的system函数）
static void WaitKey()
{
    HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
    if (hIn == INVALID_HANDLE_VALUE) return;

    DWORD mode = 0, read = 0;
    GetConsoleMode(hIn, &mode);
    SetConsoleMode(hIn, mode & ~(ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT));
    FlushConsoleInputBuffer(hIn);

    Print(L"\r\n按任意键退出...\r\n");

    // 只响应键盘按键事件
    for (;;)
    {
        INPUT_RECORD rec;
        if (!ReadConsoleInputW(hIn, &rec, 1, &read)) break;
        if (rec.EventType == KEY_EVENT && rec.Event.KeyEvent.bKeyDown)
            break;
    }

    SetConsoleMode(hIn, mode);
}

// ===================== VC 运行时检测 =====================

static bool IsVCRuntimeInstalled()
{
    wchar_t sysDir[MAX_PATH] = { 0 };
    GetSystemDirectoryW(sysDir, MAX_PATH);

    const wchar_t* dlls[] = {
        L"MSVCP140.dll",
        L"VCRUNTIME140.dll",
        L"VCRUNTIME140_1.dll"
    };

    for (int i = 0; i < 3; i++)
    {
        wchar_t path[MAX_PATH] = { 0 };
        wsprintfW(path, L"%s\\%s", sysDir, dlls[i]);
        if (!PathFileExistsW(path))
            return false;
    }
    return true;
}

// ===================== WinHTTP 文件下载 =====================

static bool DownloadFile(const wchar_t* host, const wchar_t* path, const wchar_t* savePath)
{
    bool ok = false;
    HINTERNET hSession = NULL, hConnect = NULL, hRequest = NULL;
    HANDLE hFile = INVALID_HANDLE_VALUE;

    hSession = WinHttpOpen(L"SparkmLauncher/1.0",
        WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) goto cleanup;

    hConnect = WinHttpConnect(hSession, host, INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!hConnect) goto cleanup;

    hRequest = WinHttpOpenRequest(hConnect, L"GET", path, NULL,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
        WINHTTP_FLAG_SECURE);
    if (!hRequest) goto cleanup;

    if (!WinHttpSendRequest(hRequest,
        WINHTTP_NO_ADDITIONAL_HEADERS, 0,
        WINHTTP_NO_REQUEST_DATA, 0, 0, 0))
        goto cleanup;

    if (!WinHttpReceiveResponse(hRequest, NULL))
        goto cleanup;

    // 检查 HTTP 状态码（WinHTTP 自动跟随重定向后应该是 200）
    DWORD statusCode = 0, statusSize = sizeof(statusCode);
    if (WinHttpQueryHeaders(hRequest,
        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &statusSize,
        WINHTTP_NO_HEADER_INDEX))
    {
        if (statusCode != 200)
        {
            wchar_t msg[128];
            wsprintfW(msg, L"[引导] HTTP %u\r\n", statusCode);
            Print(msg);
            goto cleanup;
        }
    }

    // 创建输出文件
    hFile = CreateFileW(savePath, GENERIC_WRITE, 0, NULL,
        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE)
    {
        Print(L"[引导] 无法创建临时文件\r\n");
        goto cleanup;
    }

    // 循环读取数据并写入文件
    DWORD totalDownloaded = 0;
    for (;;)
    {
        DWORD bytesAvailable = 0;
        if (!WinHttpQueryDataAvailable(hRequest, &bytesAvailable))
            break;
        if (bytesAvailable == 0)
            break;

        DWORD bufferSize = bytesAvailable;
        if (bufferSize > 65536) bufferSize = 65536;

        char* buffer = (char*)HeapAlloc(GetProcessHeap(), 0, bufferSize);
        if (!buffer) break;

        DWORD bytesRead = 0;
        if (WinHttpReadData(hRequest, buffer, bufferSize, &bytesRead))
        {
            if (bytesRead > 0)
            {
                DWORD bytesWritten = 0;
                WriteFile(hFile, buffer, bytesRead, &bytesWritten, NULL);
                totalDownloaded += bytesRead;
            }
        }
        HeapFree(GetProcessHeap(), 0, buffer);

        if (bytesRead == 0) break;
    }

    ok = (totalDownloaded > 0);
    if (ok)
    {
        wchar_t msg[128];
        wsprintfW(msg, L"[引导] 下载完成 (%lu KB)\r\n", totalDownloaded / 1024);
        Print(msg);
    }

cleanup:
    if (hFile != INVALID_HANDLE_VALUE) CloseHandle(hFile);
    if (hRequest) WinHttpCloseHandle(hRequest);
    if (hConnect) WinHttpCloseHandle(hConnect);
    if (hSession) WinHttpCloseHandle(hSession);
    if (!ok) DeleteFileW(savePath);
    return ok;
}

// ===================== 下载并安装 VC++ Redistributable =====================

static bool InstallVCRuntime()
{
    wchar_t tempDir[MAX_PATH] = { 0 };
    GetTempPathW(MAX_PATH, tempDir);

#if defined(_WIN64)
    const wchar_t* redistName = L"vc_redist.x64.exe";
    const wchar_t* redistUrlPath = L"/vs/17/release/vc_redist.x64.exe";
#else
    const wchar_t* redistName = L"vc_redist.x86.exe";
    const wchar_t* redistUrlPath = L"/vs/17/release/vc_redist.x86.exe";
#endif

    wchar_t tempPath[MAX_PATH] = { 0 };
    wsprintfW(tempPath, L"%s%s", tempDir, redistName);

    Print(L"[引导] 正在从微软服务器下载 VC++ Redistributable...\r\n");

    // aka.ms 短链接会自动 302 重定向到 download.visualstudio.microsoft.com
    // WinHTTP 默认自动跟随 GET 请求的重定向
    if (!DownloadFile(L"aka.ms", redistUrlPath, tempPath))
    {
        Print(L"[引导] 下载失败！\r\n");
        Print(L"[引导] 请手动下载安装:\r\n");
#if defined(_WIN64)
        Print(L"        https://aka.ms/vs/17/release/vc_redist.x64.exe\r\n");
#else
        Print(L"        https://aka.ms/vs/17/release/vc_redist.x86.exe\r\n");
#endif
        return false;
    }

    Print(L"[引导] 正在静默安装（需要管理员权限）...\r\n");

    SHELLEXECUTEINFOW sei = { 0 };
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;
    sei.lpVerb = L"runas";               // 请求 UAC 提权
    sei.lpFile = tempPath;
    sei.lpParameters = L"/install /quiet /norestart";
    sei.nShow = SW_HIDE;

    if (!ShellExecuteExW(&sei) || (intptr_t)sei.hInstApp <= 32)
    {
        Print(L"[引导] 无法启动安装程序（用户拒绝提权或权限不足）\r\n");
        return false;
    }

    if (sei.hProcess)
    {
        Print(L"[引导] 正在安装，请稍候...\r\n");

        WaitForSingleObject(sei.hProcess, INFINITE);

        DWORD exitCode = 0;
        GetExitCodeProcess(sei.hProcess, &exitCode);
        CloseHandle(sei.hProcess);

        if (exitCode == 0)
        {
            Print(L"[引导] VC++ 运行时安装成功！\r\n");
            DeleteFileW(tempPath);
            return true;
        }
        else if (exitCode == 3010)
        {
            Print(L"[引导] 安装成功，需要重启电脑后生效\r\n");
            DeleteFileW(tempPath);
            return true;
        }
        else
        {
            wchar_t msg[128];
            wsprintfW(msg, L"[引导] 安装失败（错误码: %u）\r\n", exitCode);
            Print(msg);
            return false;
        }
    }

    Print(L"[引导] 安装异常\r\n");
    return false;
}

// ===================== 启动主程序 =====================

static bool LaunchMain()
{
    // 获取 launcher.exe 自身所在目录
    wchar_t dir[MAX_PATH] = { 0 };
    GetModuleFileNameW(NULL, dir, MAX_PATH);
    PathRemoveFileSpecW(dir);

    // 拼接主程序路径
    wchar_t mainExe[MAX_PATH] = { 0 };
    wsprintfW(mainExe, L"%s\\SparkmLauncher.exe", dir);

    if (!PathFileExistsW(mainExe))
    {
        Print(L"[引导] 未找到 SparkmLauncher.exe\r\n");
        Print(L"[引导] 请确保 launcher.exe 和 SparkmLauncher.exe 在同一目录\r\n");
        return false;
    }

    Print(L"[引导] 正在启动主程序...\r\n\r\n");

    STARTUPINFOW si = { 0 };
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_SHOW;
    PROCESS_INFORMATION pi = { 0 };

    wchar_t cmdLine[MAX_PATH] = { 0 };
    wsprintfW(cmdLine, L"\"%s\"", mainExe);

    // 子进程继承当前控制台，输出显示在同一窗口
    if (!CreateProcessW(mainExe, cmdLine, NULL, NULL, FALSE, 0, NULL, dir, &si, &pi))
    {
        wchar_t msg[128];
        wsprintfW(msg, L"[引导] 启动失败（错误码: %u）\r\n", GetLastError());
        Print(msg);
        return false;
    }

    // 等待主程序退出，保持控制台窗口不关闭
    WaitForSingleObject(pi.hProcess, INFINITE);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
}

// ===================== 主函数 =====================

int main()
{
    Print(L"========================================\r\n");
    Print(L"   SparkmLauncher 启动引导程序\r\n");
    Print(L"========================================\r\n\r\n");

    // 1. 检测 VC 运行时
    if (IsVCRuntimeInstalled())
    {
        Print(L"[引导] VC++ 运行时检测通过\r\n\r\n");
    }
    else
    {
        Print(L"[引导] 检测到 VC++ 运行时缺失\r\n");
        if (!InstallVCRuntime())
        {
            Print(L"\r\n[引导] VC++ 运行时安装失败，无法启动主程序\r\n");
            WaitKey();
            return 1;
        }
        Print(L"\r\n");
    }

    // 2. 启动主程序
    if (!LaunchMain())
    {
        WaitKey();
        return 1;
    }

    return 0;
}
