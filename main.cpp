#include <iostream>
#include <cstdlib>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <filesystem>
#include "logparser.h"

#ifdef WINDOWS
#include <windows.h>
#include <codecvt>
#include <locale>
#endif

#define SOFTWARE_VERSION "0.0.1"

#ifdef WINDOWS
char* optarg = NULL;
int optind = 1;

int getopt(int argc, char* const argv[], const char* optstring)
{
    if ((optind >= argc) || (argv[optind][0] != '-') || (argv[optind][0] == 0))
    {
        return -1;
    }

    int opt = argv[optind][1];
    const char* p = strchr(optstring, opt);

    if (p == NULL)
    {
        return '?';
    }
    if (p[1] == ':')
    {
        optind++;
        if (optind >= argc)
        {
            return '?';
        }
        optarg = argv[optind];
        optind++;
    }
    return opt;
}

#else
#include <unistd.h>
#endif

#ifdef WINDOWS
HKEY ReadRegValue(std::wstring key, std::wstring name, std::wstring &val)
{
    HKEY hKey;
    if (RegCreateKeyEx(HKEY_LOCAL_MACHINE, key.c_str(), 0, NULL, REG_OPTION_NON_VOLATILE, KEY_ALL_ACCESS, NULL, &hKey, NULL) != ERROR_SUCCESS)
        return reinterpret_cast<HKEY>INVALID_HANDLE_VALUE;

    DWORD type;
    DWORD cbData;
    if (RegQueryValueEx(hKey, name.c_str(), NULL, &type, NULL, &cbData) != ERROR_SUCCESS)
    {
        // Doesn't exist, but that's ok.
        return hKey;
    }

    if (type != REG_SZ)
    {
        return hKey;
    }

    std::wstring value(cbData / sizeof(char), L'\0');
    if (RegQueryValueEx(hKey, name.c_str(), NULL, NULL, reinterpret_cast<LPBYTE>(&value[0]), &cbData) != ERROR_SUCCESS)
    {
        return hKey;
    }

    size_t firstNull = value.find_first_of(L'\0');
    if (firstNull != std::wstring::npos)
        value.resize(firstNull);

    val = value;
    return hKey;
}

int WriteRegValue(HKEY hkey, std::wstring name, std::wstring val)
{
    if (hkey == INVALID_HANDLE_VALUE) {
        return 0;
    }
    wchar_t* data = const_cast<wchar_t*>(val.c_str());
    if (RegSetValueEx(hkey, name.c_str(), 0, REG_SZ, reinterpret_cast<LPBYTE>(data), static_cast<DWORD>(val.size() *2 + 1)) != ERROR_SUCCESS) {
        return -1;
    }
    return 0;
}

// Convert a wide Unicode string to an UTF8 string
std::string utf8_encode(const std::wstring& wstr)
{
    if (wstr.empty()) return std::string();
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), NULL, 0, NULL, NULL);
    std::string strTo(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), &strTo[0], size_needed, NULL, NULL);
    return strTo;
}

// Convert an UTF8 string to a wide Unicode String
std::wstring utf8_decode(const std::string& str)
{
    if (str.empty()) return std::wstring();
    int size_needed = MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), NULL, 0);
    std::wstring wstrTo(size_needed, 0);
    MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), &wstrTo[0], size_needed);
    return wstrTo;
}

std::vector<std::string> BasicFileOpen() {
    std::vector<std::string> ret;

    std::wstring wdirpath;
    HKEY regKey = ReadRegValue(L"SOFTWARE\\TurboLogger\\TLLogParser", L"LastWorkDir", wdirpath);
    std::string dirpath = utf8_encode(wdirpath);

    const int FNAME_BUF_LEN = (1024*8);
    char filelistbuf[FNAME_BUF_LEN] = { 0 };
    OPENFILENAMEA ofn = { 0 };

    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = NULL;  // If you have a window to center over, put its HANDLE here
    ofn.lpstrFilter = "TurboLogger Log Files\0*.log\0Text Files\0*.txt\0Any File\0*.*\0";
    ofn.lpstrFile = filelistbuf;
    ofn.nMaxFile = FNAME_BUF_LEN;
    ofn.lpstrTitle = "Select a File";
    ofn.Flags = OFN_EXPLORER | OFN_DONTADDTORECENT | OFN_FILEMUSTEXIST | OFN_ALLOWMULTISELECT;
    if (dirpath.size() > 0) {
        ofn.lpstrInitialDir = dirpath.c_str();
    }

    if (GetOpenFileNameA(&ofn))
    {
        // If the user didn't multiselect, we get one string, but it's a full path that we have
        // to split apart. nFileOffset gives the index of the first char of the filename itself.
        // 
        // If the user multiselected, we get back null-separated strings in the filelist
        // buf. The first one is the directory, which we'll save in the registry for future file
        // opens.
        // 
        // We can tell the difference between the two cases simply by counting the nulls.

        int nullcount = 0;
        for (char* p = filelistbuf; *p; ++nullcount, p += strlen(p) + 1);
        //std::cout << "Nulls: " << nullcount << std::endl;

        char* workdir = filelistbuf;
        if (nullcount < 2 && ofn.nFileOffset > 0) {
            filelistbuf[ofn.nFileOffset - 1] = '\0';
            ret.push_back(&filelistbuf[ofn.nFileOffset]);
        } else {
            for (char* p = filelistbuf; *p; p += strlen(p) + 1) {
                if (p == filelistbuf) {
                    continue;
                }
                ret.push_back(p);
            }
        }
        wdirpath = utf8_decode(workdir);
        SetCurrentDirectory(wdirpath.c_str());
        WriteRegValue(regKey, L"LastWorkDir", wdirpath);
    }
    else
    {
        // All this stuff below is to tell you exactly how you messed up above. 
        // Once you've got that fixed, you can often (not always!) reduce it to a 'user cancelled' assumption.
        switch (CommDlgExtendedError())
        {
        case CDERR_DIALOGFAILURE: std::cout << "CDERR_DIALOGFAILURE\n";   break;
        case CDERR_FINDRESFAILURE: std::cout << "CDERR_FINDRESFAILURE\n";  break;
        case CDERR_INITIALIZATION: std::cout << "CDERR_INITIALIZATION\n";  break;
        case CDERR_LOADRESFAILURE: std::cout << "CDERR_LOADRESFAILURE\n";  break;
        case CDERR_LOADSTRFAILURE: std::cout << "CDERR_LOADSTRFAILURE\n";  break;
        case CDERR_LOCKRESFAILURE: std::cout << "CDERR_LOCKRESFAILURE\n";  break;
        case CDERR_MEMALLOCFAILURE: std::cout << "CDERR_MEMALLOCFAILURE\n"; break;
        case CDERR_MEMLOCKFAILURE: std::cout << "CDERR_MEMLOCKFAILURE\n";  break;
        case CDERR_NOHINSTANCE: std::cout << "CDERR_NOHINSTANCE\n";     break;
        case CDERR_NOHOOK: std::cout << "CDERR_NOHOOK\n";          break;
        case CDERR_NOTEMPLATE: std::cout << "CDERR_NOTEMPLATE\n";      break;
        case CDERR_STRUCTSIZE: std::cout << "CDERR_STRUCTSIZE\n";      break;
        case FNERR_BUFFERTOOSMALL: std::cout << "FNERR_BUFFERTOOSMALL\n";  break;
        case FNERR_INVALIDFILENAME: std::cout << "FNERR_INVALIDFILENAME\n"; break;
        case FNERR_SUBCLASSFAILURE: std::cout << "FNERR_SUBCLASSFAILURE\n"; break;
        default: std::cout << "You cancelled.\n";
        }
    }

    if (regKey != INVALID_HANDLE_VALUE) {
        RegCloseKey(regKey);
    }
    return ret;
}
#endif

void showHelp(char *progName)
{
    std::cout << "Usage: " << progName << "[options] file..." << std::endl;
    std::cout << "Convert TurboLogger log files to xlsx (Excel 2007+) files." << std::endl;
    std::cout << "Options:" << std::endl;
    std::cout << "    -h    print this help" << std::endl;
}

int main(int argc, char* argv[])
{
    std::vector<std::string> filelist;

#ifdef WINDOWS
    filelist = BasicFileOpen();
    processFiles(filelist);
    std::cout << "Hit any key to exit.";
    std::cin;
#else
    int c;
    while( ( c = getopt (argc, argv, "h") ) != -1 ) 
    {
        switch(c)
        {
            case 'h':
                if (optarg) showHelp(argv[0]);
                break;
        }
    }

    if (optind == argc) {
        showHelp();
        return 0;
    }

    for (int i = optind; i < argc; ++i) {
        filelist.push_back(argv[i]);
    }
    processFiles(filelist);
#endif
    return 0;
}
