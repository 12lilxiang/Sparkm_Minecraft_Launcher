/*SPARKM LUNCHER V1 纯净版 移除全部微软Live OAuth登录逻辑
仅保留离线账号、LittleSkin第三方皮肤站、文件工具、下载、MD5、JSON配置
*/
// 前置宏：解决winsock冲突+inet_addr废弃警告，必须放在第一行
#define WIN32_LEAN_AND_MEAN
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <commdlg.h>

// C++标准库
#include <fstream>
#include <iostream>
#include <string>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cstring>
#include <ctime>
#include <random>
#include <thread>
#include <mutex>
#include <condition_variable>

// 第三方依赖库
#include <curl/curl.h>
#include <fmt/core.h>
#include <nlohmann/json.hpp>
// OpenSSL MD5加密
#include <openssl/types.h>
#include <openssl/md5.h>
#include <openssl/evp.h>

using json = nlohmann::json;

// 系统库自动链接
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "wsock32.lib")
#pragma comment(lib, "crypt32.lib")
#pragma comment(lib, "wldap32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "Normaliz.lib")
#pragma comment(lib, "libcrypto.lib")
#pragma comment(lib, "libssl.lib")

// ===================== 函数前置声明 =====================
// 1. 控制台工具
void ClearScreen();
void ConsolePause();

// 2. 文件/目录/压缩包操作
bool FileExists(const wchar_t* filePath);
bool MakeDir(const wchar_t* dirPath);
bool CreateEmptyZip(const wchar_t* zipPath);
bool UnzipFile(const wchar_t* zipPath, const wchar_t* destDir);

// 3. 文件/文件夹选择弹窗
bool OpenFileSelectDialog(const wchar_t* filter, wchar_t* outPath, int bufSize);
bool OpenFolderSelectDialog(wchar_t* outDir, int bufSize);

// 4. JSON配置读写
void SaveJsonConfig(const json& data, const char* savePath);
json LoadJsonConfig(const char* loadPath);

// 5. 文件MD5哈希校验
bool GetFileMD5(const wchar_t* filePath, char* outMd5);
bool CheckFileMD5(const wchar_t* filePath, const char* targetMd5);

// 6. CURL网络回调函数
size_t WriteDataCallback(void* buffer, size_t size, size_t count, FILE* file);
size_t WriteStringCallback(char* buffer, size_t size, size_t count, void* userdata);
int DownloadProgressCallback(void* ptr, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow);

// 7. HTTP文件下载封装
bool DownloadFile(const char* url, const char* savePath);
bool DownloadFileWithProgress(const char* url, const char* savePath);

// 8. 离线账号系统
void GenerateOfflineUUID(const char* userName, char* outUUID);
bool SaveOfflineAccount(const char* userName, const char* uuid, const char* saveJsonPath);
bool LoadOfflineAccount(const char* loadJsonPath, char* outName, char* outUUID);

// 9. LittleSkin第三方皮肤站
bool LittleSkinLogin(const char* authServerUrl, const char* userName, char* outAccessToken, char* outUUID);
bool DownloadCustomSkin(const char* skinAPIUrl, const char* saveSkinPath);


bool welome();
bool playgame();
bool skin();
bool gamesetting();
bool download();
bool setting();
bool gamedownload();
bool packdownload();
bool moddownload();
bool shaderdownload();
bool texturedownload();

// 11. VC++运行时检测与自动安装
bool CheckAndInstallVCRuntime();

// ===================== 函数实现 =====================
// ========== 1. 控制台工具 ==========
// 清屏函数
void ClearScreen()
{
    COORD coord = { 0, 0 };
    DWORD writeNum;
    CONSOLE_SCREEN_BUFFER_INFO bufInfo;
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    GetConsoleScreenBufferInfo(hConsole, &bufInfo);
    DWORD totalChar = bufInfo.dwSize.X * bufInfo.dwSize.Y;
    FillConsoleOutputCharacter(hConsole, ' ', totalChar, coord, &writeNum);
    FillConsoleOutputAttribute(hConsole, bufInfo.wAttributes, totalChar, coord, &writeNum);
    SetConsoleCursorPosition(hConsole, coord);
}
// 暂停函数
void ConsolePause()
{
    fmt::print("\n按任意键继续...");
    std::cin.ignore();
    std::cin.get();
}

// ========== 2. 文件/目录/压缩包操作 ==========
bool FileExists(const wchar_t* filePath)
{
    return PathFileExistsW(filePath) == TRUE;
}

bool MakeDir(const wchar_t* dirPath)
{
    return CreateDirectoryW(dirPath, nullptr) == TRUE;
}

bool CreateEmptyZip(const wchar_t* zipPath)
{
    HANDLE hFile = CreateFileW(zipPath, GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE)
        return false;
    const char pkHeader[] = "PK\0\0";
    DWORD writeSize;
    WriteFile(hFile, pkHeader, sizeof(pkHeader) - 1, &writeSize, nullptr);
    CloseHandle(hFile);
    return true;
}

bool UnzipFile(const wchar_t* zipPath, const wchar_t* destDir)
{
    HRESULT coInit = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    IShellDispatch* pShell = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_Shell, nullptr, CLSCTX_INPROC_SERVER, IID_IShellDispatch, (void**)&pShell);
    if (FAILED(hr))
    {
        if (SUCCEEDED(coInit)) CoUninitialize();
        return false;
    }
    VARIANT vZip, vDest;
    VariantInit(&vZip);
    VariantInit(&vDest);
    vZip.vt = VT_BSTR;
    vZip.bstrVal = SysAllocString(zipPath);
    vDest.vt = VT_BSTR;
    vDest.bstrVal = SysAllocString(destDir);
    Folder* pZipFolder = nullptr;
    hr = pShell->NameSpace(vZip, &pZipFolder);
    bool ok = false;
    if (SUCCEEDED(hr) && pZipFolder)
    {
        Folder* pDestFolder = nullptr;
        hr = pShell->NameSpace(vDest, &pDestFolder);
        if (SUCCEEDED(hr) && pDestFolder)
        {
            FolderItems* pItems = nullptr;
            pZipFolder->Items(&pItems);
            IDispatch* pDisp = nullptr;
            pItems->QueryInterface(IID_IDispatch, (void**)&pDisp);
            VARIANT vItemArg, vOpt;
            VariantInit(&vItemArg);
            VariantInit(&vOpt);
            vItemArg.vt = VT_DISPATCH;
            vItemArg.pdispVal = pDisp;
            vOpt.vt = VT_I4;
            vOpt.lVal = 4;
            pDestFolder->CopyHere(vItemArg, vOpt);
            Sleep(800);
            VariantClear(&vItemArg);
            pDisp->Release();
            pItems->Release();
            pDestFolder->Release();
            ok = true;
        }
        pZipFolder->Release();
    }
    VariantClear(&vZip);
    VariantClear(&vDest);
    pShell->Release();
    if (SUCCEEDED(coInit)) CoUninitialize();
    return ok;
}

// ========== 3. 文件/文件夹选择弹窗 ==========
bool OpenFileSelectDialog(const wchar_t* filter, wchar_t* outPath, int bufSize)
{
    ZeroMemory(outPath, bufSize * sizeof(wchar_t));
    OPENFILENAMEW ofn = { 0 };
    ofn.lStructSize = sizeof(OPENFILENAMEW);
    ofn.hwndOwner = NULL;
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = outPath;
    ofn.nMaxFile = bufSize;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (GetOpenFileNameW(&ofn))
        return true;
    return false;
}

bool OpenFolderSelectDialog(wchar_t* outDir, int bufSize)
{
    ZeroMemory(outDir, bufSize * sizeof(wchar_t));
    BROWSEINFOW bi = { 0 };
    bi.hwndOwner = NULL;
    bi.pszDisplayName = outDir;
    bi.lpszTitle = L"请选择文件夹";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    LPITEMIDLIST pidl = SHBrowseForFolderW(&bi);
    if (pidl != nullptr)
    {
        SHGetPathFromIDListW(pidl, outDir);
        CoTaskMemFree(pidl);
        return true;
    }
    return false;
}

// ========== 4. JSON配置读写 ==========
void SaveJsonConfig(const json& data, const char* savePath)
{
    std::ofstream out(savePath);
    out << data.dump(4);
    out.close();
}

json LoadJsonConfig(const char* loadPath)
{
    std::ifstream in(loadPath);
    json res = json::parse(in);
    in.close();
    return res;
}

// ========== 5. MD5文件哈希校验 ==========
bool GetFileMD5(const wchar_t* filePath, char* outMd5)
{
    FILE* fp = _wfopen(filePath, L"rb");
    if (!fp)
        return false;
    EVP_MD_CTX* mdCtx = EVP_MD_CTX_new();
    const EVP_MD* mdAlg = EVP_md5();
    EVP_DigestInit_ex(mdCtx, mdAlg, nullptr);
    unsigned char buf[4096];
    size_t readLen;
    while ((readLen = fread(buf, 1, sizeof(buf), fp)) > 0)
    {
        EVP_DigestUpdate(mdCtx, buf, readLen);
    }
    fclose(fp);
    unsigned char md5Result[EVP_MAX_MD_SIZE];
    unsigned int mdLen;
    EVP_DigestFinal_ex(mdCtx, md5Result, &mdLen);
    EVP_MD_CTX_free(mdCtx);
    for (int i = 0; i < (int)mdLen; i++)
    {
        sprintf(outMd5 + i * 2, "%02x", md5Result[i]);
    }
    outMd5[32] = '\0';
    return true;
}

bool CheckFileMD5(const wchar_t* filePath, const char* targetMd5)
{
    char calcMd5[33] = { 0 };
    if (!GetFileMD5(filePath, calcMd5))
        return false;
    return strcmp(calcMd5, targetMd5) == 0;
}

// ========== 6. CURL网络回调 ==========
size_t WriteDataCallback(void* buffer, size_t size, size_t count, FILE* file)
{
    return fwrite(buffer, size, count, file);
}

size_t WriteStringCallback(char* buffer, size_t size, size_t count, void* userdata)
{
    std::string* res = static_cast<std::string*>(userdata);
    res->append(buffer, size * count);
    return size * count;
}

int DownloadProgressCallback(void* ptr, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow)
{
    (void)ptr;
    (void)ultotal;
    (void)ulnow;
    if (dltotal <= 0)
        return 0;
    double percent = (double)dlnow / (double)dltotal * 100.0;
    int barCount = 30;
    int fillNum = (int)(percent / 100.0 * barCount);
    fmt::print("\r[");
    for (int i = 0; i < fillNum; i++)
        fmt::print("o");
    for (int i = fillNum; i < barCount; i++)
        fmt::print(" ");
    fmt::print("] {:.1f}%", percent);
    fflush(stdout);
    return 0;
}

// ========== 7. 文件下载 ==========
bool DownloadFile(const char* url, const char* savePath)
{
    CURL* curl = curl_easy_init();
    if (!curl)
        return false;
    FILE* fp = fopen(savePath, "wb");
    if (!fp)
    {
        curl_easy_cleanup(curl);
        return false;
    }
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteDataCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    CURLcode ret = curl_easy_perform(curl);
    fclose(fp);
    curl_easy_cleanup(curl);
    return ret == CURLE_OK;
}

bool DownloadFileWithProgress(const char* url, const char* savePath)
{
    CURL* curl = curl_easy_init();
    if (!curl)
        return false;
    FILE* fp = fopen(savePath, "wb");
    if (!fp)
    {
        curl_easy_cleanup(curl);
        return false;
    }
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteDataCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, DownloadProgressCallback);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, nullptr);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    CURLcode ret = curl_easy_perform(curl);
    fmt::print("\n");
    fclose(fp);
    curl_easy_cleanup(curl);
    return ret == CURLE_OK;
}

// ========== 8. 离线账号系统 ==========
void GenerateOfflineUUID(const char* userName, char* outUUID)
{
    std::hash<std::string> hashFunc;
    size_t hashVal = hashFunc(std::string(userName));
    std::mt19937 rng((unsigned int)hashVal);
    std::uniform_int_distribution<int> dist(0, 255);
    unsigned char data[16];
    for (int i = 0; i < 16; i++)
        data[i] = dist(rng);
    data[6] = (data[6] & 0x0F) | 0x40;
    data[8] = (data[8] & 0x3F) | 0x80;
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (int i = 0; i < 16; i++)
    {
        oss << std::setw(2) << (int)data[i];
        if (i == 3 || i == 5 || i == 7 || i == 9)
            oss << "-";
    }
    std::string uuidStr = oss.str();
    strcpy(outUUID, uuidStr.c_str());
}
// ========== 8. 离线账号系统 ==========
bool SaveOfflineAccount(const char* userName, const char* uuid, const char* saveJsonPath)
{
    json acc{};
    acc["offlineName"] = userName;
    acc["offlineUUID"] = uuid;
    SaveJsonConfig(acc, saveJsonPath);
    return true;
}
// 加载离线账号信息
bool LoadOfflineAccount(const char* loadJsonPath, char* outName, char* outUUID)
{
    std::wstring wPath(loadJsonPath, loadJsonPath + strlen(loadJsonPath));
    if (!FileExists(wPath.c_str()))
        return false;
    json j = LoadJsonConfig(loadJsonPath);
    strcpy(outName, j["offlineName"].get<std::string>().c_str());
    strcpy(outUUID, j["offlineUUID"].get<std::string>().c_str());
    return true;
}

// ========== 9. LittleSkin第三方皮肤站 ==========
bool LittleSkinLogin(const char* authServerUrl, const char* userName, char* outAccessToken, char* outUUID)
{
    strcpy(outAccessToken, "skin-token");
    strcpy(outUUID, "custom-uuid");
    return true;
}

bool DownloadCustomSkin(const char* skinAPIUrl, const char* saveSkinPath)
{
    return DownloadFileWithProgress(skinAPIUrl, saveSkinPath);
}

// ========== 10. 菜单功能实现 ==========
bool playgame()
{
    ClearScreen();
    fmt::print("===== 开始游戏 =====\n");
    fmt::print("（功能开发中）\n");
    ConsolePause();
    return true;
}

bool gamesetting()
{
    ClearScreen();
    fmt::print("===== 游戏管理 =====\n");
    fmt::print("（功能开发中）\n");
    ConsolePause();
    return true;
}

bool skin()
{
    ClearScreen();
    fmt::print("===== 账号/皮肤管理 =====\n");
    fmt::print("（功能开发中）\n");
    ConsolePause();
    return true;
}

bool download()
{
    ClearScreen();
    fmt::print(R"(
┌────────────────────────────────────────────────────┐
│         SparkmLauncher V0.1 by Sparkm123           │
└────────────────────────────────────────────────────┘

 ━━━━━━━━━━━ 下载选择菜单 ━━━━━━━━━━━
 提示：输入数字序号，回车确认使用对应功能

  【01】 游戏下载
  【02】 整合包安装
┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄
  【03】 模组下载
  【04】 光影下载
  【05】 材质包下载
  【06】 返回主菜单
  【07】 退出启动器

┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄
请输入功能编号:)");
	int wel;
	std::cin >> wel;
    if (wel == 1) {
		gamedownload();
    }
    else if (wel == 2) {
		packdownload();
    }
    else if (wel == 3) {
		moddownload();
    }
    else if (wel == 4) {
		shaderdownload();
    }
    else if (wel == 5) {
		texturedownload();
    }
    else if (wel == 6) {
		welome();
	}
    else if (wel == 7) {
		ConsolePause();
    }
    else {
        fmt::print("输入错误，请重新输入\n");
        download();
    }
    return true;
}

bool gamedownload()
{
    ClearScreen();
    fmt::print("===== 游戏下载 =====\n");
    fmt::print("（功能开发中）\n");
    ConsolePause();
    return true;
}

bool packdownload()
{
    ClearScreen();
    fmt::print("===== 整合包安装 =====\n");
    fmt::print("（功能开发中）\n");
    ConsolePause();
    return true;
}

bool moddownload()
{
    ClearScreen();
    fmt::print("===== 模组下载 =====\n");
    fmt::print("（功能开发中）\n");
    ConsolePause();
    return true;
}

bool shaderdownload()
{
    ClearScreen();
    fmt::print("===== 光影下载 =====\n");
    fmt::print("（功能开发中）\n");
    ConsolePause();
    return true;
}

bool texturedownload()
{
    ClearScreen();
    fmt::print("===== 材质包下载 =====\n");
    fmt::print("（功能开发中）\n");
    ConsolePause();
    return true;
}

bool setting()
{
    ClearScreen();
    fmt::print("===== 启动器设置 =====\n");
    fmt::print("（功能开发中）\n");
    ConsolePause();
    return true;
}

// ========== 11. VC++运行时检测与自动安装 ==========
bool CheckAndInstallVCRuntime()
{
    // 获取 System32 目录
    wchar_t sysDir[MAX_PATH] = { 0 };
    GetSystemDirectoryW(sysDir, MAX_PATH);

    // 需要检测的 VC 运行时核心 DLL
    const wchar_t* vcDlls[] = {
        L"MSVCP140.dll",
        L"VCRUNTIME140.dll",
        L"VCRUNTIME140_1.dll"
    };
    const int dllCount = 3;

    bool missing = false;
    for (int i = 0; i < dllCount; i++)
    {
        wchar_t fullPath[MAX_PATH] = { 0 };
        swprintf_s(fullPath, MAX_PATH, L"%s\\%s", sysDir, vcDlls[i]);
        if (!FileExists(fullPath))
        {
            // DLL 名是纯 ASCII，直接转 char 打印
            char dllName[MAX_PATH] = { 0 };
            WideCharToMultiByte(CP_ACP, 0, vcDlls[i], -1, dllName, MAX_PATH, NULL, NULL);
            fmt::print("[VC运行时] 缺失: {}\n", dllName);
            missing = true;
        }
    }

    if (!missing)
    {
        fmt::print("[VC运行时] 检测通过，所有依赖库已就绪\n");
        return true;
    }

    fmt::print("[VC运行时] 检测到缺失库，正在下载 VC++ Redistributable 安装包...\n");

    // 获取临时目录
    wchar_t tempDir[MAX_PATH] = { 0 };
    GetTempPathW(MAX_PATH, tempDir);

#ifdef _WIN64
    const char* redistUrl = "https://aka.ms/vs/17/release/vc_redist.x64.exe";
    const wchar_t* redistName = L"vc_redist.x64.exe";
#else
    const char* redistUrl = "https://aka.ms/vs/17/release/vc_redist.x86.exe";
    const wchar_t* redistName = L"vc_redist.x86.exe";
#endif

    // 拼接完整临时路径
    wchar_t tempPathW[MAX_PATH] = { 0 };
    swprintf_s(tempPathW, MAX_PATH, L"%s%s", tempDir, redistName);

    // 转 char* 供 DownloadFile 使用
    char tempPathA[MAX_PATH] = { 0 };
    WideCharToMultiByte(CP_ACP, 0, tempPathW, -1, tempPathA, MAX_PATH, NULL, NULL);

    // 下载 VCRedist 安装包
    if (!DownloadFile(redistUrl, tempPathA))
    {
        fmt::print("[VC运行时] 下载失败！请手动安装 VC++ Redistributable\n");
        fmt::print("下载地址: {}\n", redistUrl);
        ConsolePause();
        return false;
    }

    fmt::print("[VC运行时] 下载完成，正在静默安装（需要管理员权限）...\n");

    // 以管理员权限运行安装程序
    SHELLEXECUTEINFOW sei = { 0 };
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;
    sei.hwnd = NULL;
    sei.lpVerb = L"runas";               // 请求 UAC 提权
    sei.lpFile = tempPathW;
    sei.lpParameters = L"/install /quiet /norestart";
    sei.nShow = SW_HIDE;

    if (!ShellExecuteExW(&sei) || (intptr_t)sei.hInstApp <= 32)
    {
        fmt::print("[VC运行时] 无法启动安装程序（用户拒绝提权或权限不足）\n");
        ConsolePause();
        return false;
    }

    // 等待安装完成
    if (sei.hProcess)
    {
        fmt::print("[VC运行时] 正在安装，请稍候...\n");
        fflush(stdout);

        WaitForSingleObject(sei.hProcess, INFINITE);

        DWORD exitCode = 0;
        GetExitCodeProcess(sei.hProcess, &exitCode);
        CloseHandle(sei.hProcess);

        if (exitCode == 0)
        {
            fmt::print("[VC运行时] 安装成功！\n");
            DeleteFileW(tempPathW);
            return true;
        }
        else if (exitCode == 3010)       // ERROR_SUCCESS_REBOOT_REQUIRED
        {
            fmt::print("[VC运行时] 安装成功，需要重启电脑生效\n");
            DeleteFileW(tempPathW);
            return true;
        }
        else
        {
            fmt::print("[VC运行时] 安装失败（错误码: {}）\n", (int)exitCode);
            ConsolePause();
            return false;
        }
    }

    fmt::print("[VC运行时] 安装异常\n");
    ConsolePause();
    return false;
}

bool welome()
{
    ClearScreen();
    fmt::print(R"(
┌────────────────────────────────────────────────────┐
│         SparkmLauncher V0.1 by Sparkm123           │
└────────────────────────────────────────────────────┘

 ━━━━━━━━━━━ 功能选择菜单 ━━━━━━━━━━━
 提示：输入数字序号，回车确认使用对应功能

  【01】 开始游戏
  【02】 游戏管理
  【03】 账号管理
  【04】 下载中心
  【05】 启动器设置
  【06】 退出程序

┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄
请输入功能编号:)");
    int wel;
    std::cin >> wel;
    if (wel == 1) {
		playgame();
	}else if (wel == 2) {
		gamesetting();
	}else if (wel == 3) {
        skin();
    }else if (wel == 4) {
        download();
	}else if (wel == 5) {
		setting();
	}else if (wel == 6) {
        ConsolePause();
    }else {
		fmt::print("输入错误，请重新输入\n");
        welome();
    }
	return true;
}
int main()
{
    // VC++ 运行时检测由外部 launcher.exe 负责
    curl_global_init(CURL_GLOBAL_ALL);

    welome();

    curl_global_cleanup();
    return 0;
}