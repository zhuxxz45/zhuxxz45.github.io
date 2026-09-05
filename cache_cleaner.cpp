#include <windows.h>
#include <commctrl.h>
#include <shlobj.h>
#include <string>
#include <vector>
#include <filesystem>
#include <thread>
#include <mutex>
#include <iomanip>
#include <sstream>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")

#define IDI_ICON1 101

namespace fs = std::filesystem;

#define ID_BTN_SCAN         1001
#define ID_BTN_CLEAN        1002
#define ID_LIST_CACHE       1004
#define ID_PROGRESS         1005
#define ID_STATUS           1006
#define ID_EDIT_LOG         1007

struct CacheItem {
    std::wstring name;
    std::wstring path;
    std::wstring category;
    uintmax_t size;
    bool selected;
};

class CacheCleanerGUI {
private:
    HWND hwnd;
    HWND hListCache;
    HWND hProgress;
    HWND hStatus;
    HWND hEditLog;
    std::vector<CacheItem> cacheItems;
    std::mutex mtx;
    bool isScanning;
    bool isCleaning;
    
    void addLog(const std::wstring& msg) {
        std::wstring fullMsg = msg + L"\r\n";
        if (hEditLog) {
            int len = GetWindowTextLengthW(hEditLog);
            SendMessageW(hEditLog, EM_SETSEL, len, len);
            SendMessageW(hEditLog, EM_REPLACESEL, FALSE, (LPARAM)fullMsg.c_str());
        }
    }
    
    uintmax_t getDirSize(const fs::path& path) {
        uintmax_t total = 0;
        WIN32_FIND_DATAW findData;
        HANDLE hFind;
        
        std::wstring searchPath = path.wstring() + L"\\*";
        hFind = FindFirstFileW(searchPath.c_str(), &findData);
        
        if (hFind == INVALID_HANDLE_VALUE) return 0;
        
        do {
            std::wstring fileName = findData.cFileName;
            if (fileName == L"." || fileName == L"..") continue;
            
            fs::path fullPath = path / fileName;
            
            if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                total += getDirSize(fullPath);
            } else {
                LARGE_INTEGER fileSize;
                fileSize.LowPart = findData.nFileSizeLow;
                fileSize.HighPart = findData.nFileSizeHigh;
                total += fileSize.QuadPart;
            }
        } while (FindNextFileW(hFind, &findData));
        
        FindClose(hFind);
        return total;
    }
    
    std::wstring formatSize(uintmax_t size) {
        const wchar_t* units[] = {L"B", L"KB", L"MB", L"GB", L"TB"};
        int unitIndex = 0;
        double sz = (double)size;
        while (sz >= 1024.0 && unitIndex < 4) {
            sz /= 1024.0;
            unitIndex++;
        }
        std::wstringstream ss;
        ss << std::fixed << std::setprecision(2) << sz << L" " << units[unitIndex];
        return ss.str();
    }
    
    bool isAccessible(const fs::path& path) {
        try {
            std::error_code ec;
            return fs::exists(path, ec);
        } catch (...) {
            return false;
        }
    }
    
    void addCacheItem(const std::wstring& name, const fs::path& path, const std::wstring& category) {
        try {
            if (!isAccessible(path)) return;
            
            uintmax_t size = getDirSize(path);
            
            if (size > 1024 * 1024) {
                CacheItem item;
                item.name = name;
                item.path = path.wstring();
                item.category = category;
                item.size = size;
                item.selected = true;
                
                std::lock_guard<std::mutex> lock(mtx);
                cacheItems.push_back(item);
            }
        } catch (...) {}
    }
    
    std::wstring getEnvVar(const wchar_t* name) {
        wchar_t buffer[MAX_PATH * 4];
        DWORD result = GetEnvironmentVariableW(name, buffer, MAX_PATH * 4);
        if (result > 0 && result < MAX_PATH * 4) {
            return std::wstring(buffer);
        }
        return L"";
    }
    
    void scanCaches() {
        isScanning = true;
        cacheItems.clear();
        
        addLog(L"Scanning caches...");
        SendMessageW(hStatus, SB_SETTEXT, 0, (LPARAM)L"Scanning...");
        PostMessage(hwnd, WM_USER + 1, 10, 0);
        
        std::wstring tempPath = getEnvVar(L"TEMP");
        if (tempPath.empty()) tempPath = getEnvVar(L"TMP");
        std::wstring localAppData = getEnvVar(L"LOCALAPPDATA");
        std::wstring appData = getEnvVar(L"APPDATA");
        std::wstring userProfile = getEnvVar(L"USERPROFILE");
        
        // Temp files
        addLog(L"\n=== Scanning temp files ===");
        if (!tempPath.empty()) {
            addCacheItem(L"User Temp Files", fs::path(tempPath), L"System Cache");
        }
        addCacheItem(L"Windows Temp", fs::path(L"C:\\Windows\\Temp"), L"System Cache");
        PostMessage(hwnd, WM_USER + 1, 30, 0);
        
        // Browser caches
        addLog(L"\n=== Scanning browser caches ===");
        if (!localAppData.empty()) {
            fs::path base(localAppData);
            addCacheItem(L"Chrome Cache", base / L"Google" / L"Chrome" / L"User Data" / L"Default" / L"Cache", L"Browser Cache");
            addCacheItem(L"Chrome Code Cache", base / L"Google" / L"Chrome" / L"User Data" / L"Default" / L"Code Cache", L"Browser Cache");
            addCacheItem(L"Edge Cache", base / L"Microsoft" / L"Edge" / L"User Data" / L"Default" / L"Cache", L"Browser Cache");
            addCacheItem(L"Edge Code Cache", base / L"Microsoft" / L"Edge" / L"User Data" / L"Default" / L"Code Cache", L"Browser Cache");
            addCacheItem(L"Firefox Cache", base / L"Mozilla" / L"Firefox" / L"Profiles", L"Browser Cache");
            addCacheItem(L"IE Cache", base / L"Microsoft" / L"Windows" / L"INetCache", L"Browser Cache");
        }
        PostMessage(hwnd, WM_USER + 1, 50, 0);
        
        // Package manager caches
        addLog(L"\n=== Scanning package manager caches ===");
        if (!localAppData.empty()) {
            fs::path base(localAppData);
            addCacheItem(L"pip Cache", base / L"pip" / L"cache", L"Package Manager");
            addCacheItem(L"npm Cache", base / L"npm-cache", L"Package Manager");
            addCacheItem(L"Yarn Cache", base / L"Yarn" / L"Cache", L"Package Manager");
        }
        
        if (!userProfile.empty()) {
            fs::path home(userProfile);
            addCacheItem(L"npm Global Cache", home / L"AppData" / L"Roaming" / L"npm-cache", L"Package Manager");
        }
        PostMessage(hwnd, WM_USER + 1, 70, 0);
        
        // Windows update cache
        addLog(L"\n=== Scanning Windows update cache ===");
        addCacheItem(L"Windows Update Cache", fs::path(L"C:\\Windows\\SoftwareDistribution\\Download"), L"System Cache");
        
        // Thumbnail cache
        if (!appData.empty()) {
            fs::path base(appData);
            addCacheItem(L"Thumbnail Cache", base / L"Microsoft" / L"Windows" / L"Explorer", L"System Cache");
        }
        PostMessage(hwnd, WM_USER + 1, 90, 0);
        
        addLog(L"\nScan complete! Found " + std::to_wstring(cacheItems.size()) + L" items");
        PostMessage(hwnd, WM_USER + 1, 100, 0);
        SendMessageW(hStatus, SB_SETTEXT, 0, (LPARAM)L"Scan Complete");
        
        isScanning = false;
    }
    
    void updateList() {
        ListView_DeleteAllItems(hListCache);
        
        for (size_t i = 0; i < cacheItems.size(); i++) {
            const auto& item = cacheItems[i];
            
            LVITEMW lvi = {0};
            lvi.mask = LVIF_TEXT | LVIF_PARAM;
            lvi.iItem = (int)i;
            lvi.iSubItem = 0;
            lvi.pszText = (LPWSTR)item.name.c_str();
            lvi.lParam = (LPARAM)i;
            ListView_InsertItem(hListCache, &lvi);
            
            ListView_SetItemText(hListCache, i, 1, (LPWSTR)item.category.c_str());
            ListView_SetItemText(hListCache, i, 2, (LPWSTR)formatSize(item.size).c_str());
            ListView_SetItemText(hListCache, i, 3, (LPWSTR)item.path.c_str());
            
            ListView_SetCheckState(hListCache, i, item.selected);
        }
    }
    
    void cleanSelectedCaches() {
        isCleaning = true;
        
        SendMessageW(hStatus, SB_SETTEXT, 0, (LPARAM)L"Cleaning...");
        
        int totalItems = 0;
        for (const auto& item : cacheItems) {
            if (item.selected) totalItems++;
        }
        
        if (totalItems == 0) {
            MessageBoxW(hwnd, L"Please select items to clean!", L"Notice", MB_OK | MB_ICONINFORMATION);
            isCleaning = false;
            return;
        }
        
        int currentItem = 0;
        for (const auto& item : cacheItems) {
            if (!item.selected) continue;
            
            currentItem++;
            int progress = (currentItem * 100) / totalItems;
            PostMessage(hwnd, WM_USER + 2, progress, 0);
            
            addLog(L"Cleaning: " + item.name);
            
            try {
                fs::path itemPath(item.path);
                if (fs::exists(itemPath)) {
                    std::error_code ec;
                    for (const auto& entry : fs::directory_iterator(
                        itemPath, 
                        fs::directory_options::skip_permission_denied, ec)) {
                        try {
                            fs::remove_all(entry.path(), ec);
                        } catch (...) {}
                    }
                }
            } catch (...) {}
        }
        
        addLog(L"Clean complete!");
        SendMessageW(hStatus, SB_SETTEXT, 0, (LPARAM)L"Clean Complete");
        MessageBoxW(hwnd, L"Cache cleaning complete!", L"Complete", MB_OK | MB_ICONINFORMATION);
        
        scanCaches();
        isCleaning = false;
    }
    
public:
    CacheCleanerGUI(HWND hwnd) : hwnd(hwnd), isScanning(false), isCleaning(false) {
        hListCache = GetDlgItem(hwnd, ID_LIST_CACHE);
        hProgress = GetDlgItem(hwnd, ID_PROGRESS);
        hStatus = GetDlgItem(hwnd, ID_STATUS);
        hEditLog = GetDlgItem(hwnd, ID_EDIT_LOG);
        
        LVCOLUMNW lvc = {0};
        lvc.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
        
        lvc.pszText = (LPWSTR)L"Item";
        lvc.cx = 150;
        ListView_InsertColumn(hListCache, 0, &lvc);
        
        lvc.pszText = (LPWSTR)L"Category";
        lvc.cx = 100;
        ListView_InsertColumn(hListCache, 1, &lvc);
        
        lvc.pszText = (LPWSTR)L"Size";
        lvc.cx = 80;
        ListView_InsertColumn(hListCache, 2, &lvc);
        
        lvc.pszText = (LPWSTR)L"Path";
        lvc.cx = 350;
        ListView_InsertColumn(hListCache, 3, &lvc);
        
        ListView_SetExtendedListViewStyle(hListCache, LVS_EX_CHECKBOXES | LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
    }
    
    void handleCommand(WPARAM wParam) {
        switch (LOWORD(wParam)) {
            case ID_BTN_SCAN:
                if (!isScanning) {
                    SendMessage(hProgress, PBM_SETPOS, 0, 0);
                    std::thread([this]() { 
                        try { scanCaches(); } catch (...) {}
                    }).detach();
                }
                break;
                
            case ID_BTN_CLEAN:
                if (!isCleaning && !cacheItems.empty()) {
                    for (size_t i = 0; i < cacheItems.size(); i++) {
                        cacheItems[i].selected = ListView_GetCheckState(hListCache, (int)i);
                    }
                    
                    if (MessageBoxW(hwnd, L"Clean selected caches?", L"Confirm", MB_YESNO | MB_ICONQUESTION) == IDYES) {
                        SendMessage(hProgress, PBM_SETPOS, 0, 0);
                        std::thread([this]() { 
                            try { cleanSelectedCaches(); } catch (...) {}
                        }).detach();
                    }
                } else if (cacheItems.empty()) {
                    MessageBoxW(hwnd, L"Please scan first!", L"Notice", MB_OK | MB_ICONINFORMATION);
                }
                break;
        }
    }
    
    void handleNotify(LPARAM lParam) {
        NMHDR* nmhdr = (NMHDR*)lParam;
        if (nmhdr->idFrom == ID_LIST_CACHE && nmhdr->code == LVN_ITEMCHANGED) {
            NMLISTVIEW* nmlv = (NMLISTVIEW*)lParam;
            if (nmlv->uChanged & LVIF_STATE) {
                bool checked = ListView_GetCheckState(hListCache, nmlv->iItem);
                if (nmlv->iItem >= 0 && nmlv->iItem < (int)cacheItems.size()) {
                    cacheItems[nmlv->iItem].selected = checked;
                }
            }
        }
    }
    
    void handleUserMessage(WPARAM wParam) {
        if (wParam <= 100) {
            SendMessage(hProgress, PBM_SETPOS, (WPARAM)wParam, 0);
            if (wParam == 100) updateList();
        }
    }
    
    void handleCleanProgress(WPARAM wParam) {
        SendMessage(hProgress, PBM_SETPOS, (WPARAM)wParam, 0);
    }
};

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    static CacheCleanerGUI* cleaner = nullptr;
    
    switch (msg) {
        case WM_CREATE:
            {
                CreateWindowW(L"BUTTON", L"Scan Caches", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                    10, 10, 120, 30, hwnd, (HMENU)ID_BTN_SCAN, NULL, NULL);
                CreateWindowW(L"BUTTON", L"Clean Selected", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                    140, 10, 120, 30, hwnd, (HMENU)ID_BTN_CLEAN, NULL, NULL);
                
                CreateWindowW(L"SysListView32", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | LVS_REPORT,
                    10, 50, 680, 250, hwnd, (HMENU)ID_LIST_CACHE, NULL, NULL);
                
                CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY | WS_VSCROLL,
                    10, 310, 680, 120, hwnd, (HMENU)ID_EDIT_LOG, NULL, NULL);
                
                CreateWindowW(L"msctls_progress32", L"", WS_CHILD | WS_VISIBLE | PBS_SMOOTH,
                    10, 440, 680, 20, hwnd, (HMENU)ID_PROGRESS, NULL, NULL);
                
                CreateWindowW(L"msctls_statusbar32", L"", WS_CHILD | WS_VISIBLE,
                    0, 0, 0, 0, hwnd, (HMENU)ID_STATUS, NULL, NULL);
                
                cleaner = new CacheCleanerGUI(hwnd);
            }
            break;
            
        case WM_COMMAND:
            if (cleaner) cleaner->handleCommand(wParam);
            break;
            
        case WM_NOTIFY:
            if (cleaner) cleaner->handleNotify(lParam);
            break;
            
        case WM_USER + 1:
            if (cleaner) cleaner->handleUserMessage(wParam);
            break;
            
        case WM_USER + 2:
            if (cleaner) cleaner->handleCleanProgress(wParam);
            break;
            
        case WM_SIZE:
            {
                RECT rc;
                GetClientRect(hwnd, &rc);
                int width = rc.right - rc.left;
                int height = rc.bottom - rc.top;
                
                HWND hList = GetDlgItem(hwnd, ID_LIST_CACHE);
                HWND hLog = GetDlgItem(hwnd, ID_EDIT_LOG);
                HWND hProgress = GetDlgItem(hwnd, ID_PROGRESS);
                HWND hStatus = GetDlgItem(hwnd, ID_STATUS);
                
                SetWindowPos(hList, NULL, 10, 50, width - 20, height - 200, SWP_NOZORDER);
                SetWindowPos(hLog, NULL, 10, height - 140, width - 20, 90, SWP_NOZORDER);
                SetWindowPos(hProgress, NULL, 10, height - 40, width - 20, 20, SWP_NOZORDER);
                SetWindowPos(hStatus, NULL, 0, height - 20, width, 20, SWP_NOZORDER);
            }
            break;
            
        case WM_DESTROY:
            if (cleaner) delete cleaner;
            PostQuitMessage(0);
            break;
            
        default:
            return DefWindowProc(hwnd, msg, wParam, lParam);
    }
    return 0;
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR lpCmdLine, int nCmdShow) {
    INITCOMMONCONTROLSEX icc = {0};
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_LISTVIEW_CLASSES | ICC_PROGRESS_CLASS | ICC_BAR_CLASSES;
    InitCommonControlsEx(&icc);
    
    WNDCLASSEXW wc = {0};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = L"CacheCleanerClass";
    wc.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_ICON1));
    wc.hIconSm = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_ICON1));
    
    if (!RegisterClassExW(&wc)) {
        MessageBoxW(NULL, L"Window registration failed!", L"Error", MB_ICONERROR);
        return 1;
    }
    
    HWND hwnd = CreateWindowExW(
        0,
        L"CacheCleanerClass",
        L"Cache Cleaner",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        720, 550,
        NULL, NULL, hInstance, NULL
    );
    
    if (!hwnd) {
        MessageBoxW(NULL, L"Window creation failed!", L"Error", MB_ICONERROR);
        return 1;
    }
    
    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);
    
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    
    return (int)msg.wParam;
}