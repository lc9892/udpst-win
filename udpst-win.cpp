//
// Copyright (c) 2025, Len Ciavattone
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// 1. Redistributions of source code must retain the above copyright notice,
//    this list of conditions and the following disclaimer.
//
// 2. Redistributions in binary form must reproduce the above copyright notice,
//    this list of conditions and the following disclaimer in the documentation
//    and/or other materials provided with the distribution.
//
// 3. Neither the name of the copyright holder nor the names of its
//    contributors may be used to endorse or promote products derived from this
//    software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.
//
//
// UDP Speed Test for Windows, udpst-win.cpp
//
// This file provides the core Windows functionality needed to utilize the
// standard Linux-based udpst source files in a Win32/Win64 application.
//
// IMPORTANT: To enable authentication with Visual Studio, add
// AUTH_KEY_ENABLE as a preprocessor defintition for "All Configurations" &
// "All Platforms" under project properties.
//
// Author               Date            Comments
// ----------------     ----------      ----------------------------------
// Len Ciavattone       11/29/2025      Created for OB-UDPST version 9.0.0
// Len Ciavattone       12/15/2025      Add max bandwidth support in server
//                                      mode and bimodal prefix support
//
//

#pragma comment(lib, "Ws2_32.lib")
#pragma comment(lib, "IPHLPAPI.lib")
#ifdef AUTH_KEY_ENABLE
#pragma comment(lib, "bcrypt.lib") 
#endif
#pragma warning(disable : 4996)
#pragma warning(disable : 6385)
#pragma warning(disable : 6386)

#include <stdio.h>
#include <stdint.h>
#include <time.h>
#include <Winsock2.h>
#include <Ws2tcpip.h>
#include <Ws2ipdef.h>
#include <mswsock.h>
#include <iphlpapi.h>
#ifdef AUTH_KEY_ENABLE
#include <bcrypt.h>
#endif
#include "strsafe.h"
#include "framework.h"
#include "udpst-win.h"
#include "udpst-win_common.h"
#include "udpst/cJSON.h"
#include "udpst/udpst_common.h"
#include "udpst/udpst_protocol.h"
#include "udpst/udpst.h"
extern "C" {
#include "udpst/udpst_control.h"
#include "udpst/udpst_data.h"
#include "udpst/udpst_srates.h"
#include "udpst_control_alt2.h"
#include "udpst_data_alt2.h"
#include "udpst_srates_alt2.h"
}

//
// Constants
//
#ifdef _WIN64
#define PLATFORM_BITS "64"
#else
#define PLATFORM_BITS "32"
#endif
#define SOFTWARE_TITLE_WIN "UDP Speed Test for Windows"
#define WINDOWS_VER "1.0.3"
#define WM_SOCKET (WM_USER + 1)
#define MAX_LOADSTRING 100
#define MAX_LINELEN 256
#define TEXT_BUFFER_SIZE 2048 // Must be power of 2 (2^N)
#define TEXT_BUFFER_MASK (TEXT_BUFFER_SIZE - 1)
#define WRITE_FD_NOSCROLL -2
#define TIME_LIMITED_TESTING
//#define USE_TRANSMITPACKETS // Disabled (underperforms standard send with non-server Windows editions)

//
// Global Variables
//
HINSTANCE hInst;
WCHAR szTitle[MAX_LOADSTRING];
WCHAR szWindowClass[MAX_LOADSTRING];
HWND hWndPrimary;
HGDIOBJ hObjFont = GetStockObject(SYSTEM_FIXED_FONT);
COLORREF rgbDarkBG = 0x00000000, rgbDarkText = 0x00BBBBBB;
COLORREF rgbLightBG = 0x00FFFFFF, rgbLightText = 0x004A4A4A;
COLORREF rgbBG = rgbDarkBG, rgbText = rgbDarkText;
int iSendBlocked = 0;
int yClientGlobal = 0, yCharGlobal = 0;
char szServerName[256];
void (*NextLocalAction)(void) = NULL;
size_t nCharCount;
int iLineCount = 0, iLineNext = 0;
struct TextLine {
        int iLineLength;
        char szLineBuffer[MAX_LINELEN];
} TextBuffer[TEXT_BUFFER_SIZE];
#ifdef USE_TRANSMITPACKETS
TRANSMIT_PACKETS_ELEMENT tpe[MAX_BURST_SIZE];
LPFN_TRANSMITPACKETS LpfnTransmitpackets;
#endif

//
// Globals for standard udpst require C calling conventions
//
extern "C" {
        int errConn = -1, monConn = -1, aggConn = -1;
        char scratch[STRING_SIZE];
        struct configuration conf;
        struct repository repo;
        struct connection *conn;
        const char *boolText[] = {"Disabled", "Enabled"};
        const char *rateAdjAlgo[] = { "B", "C" }; // Aligned to CHTA_RA_ALGO_x
        //
        cJSON *json_top = NULL, *json_output = NULL, *json_siArray = NULL;
        char json_errbuf[STRING_SIZE], json_errbuf2[STRING_SIZE];
}

//
// Function prototypes
//
ATOM             MyRegisterClass(HINSTANCE hInstance);
BOOL             InitInstance(HINSTANCE, int);
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK About(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK SetupDialog(HWND, UINT, WPARAM, LPARAM);
BOOL             ProcessSetupDialog(HWND, BOOL);
void             FormatSetupDialog(HWND, BOOL);
BOOL             ParameterCheck(int, int, int, const wchar_t*);
void             InitRepository(BOOL);
void             ProcessTimers(void);
void             StartTest(void);
void             ShowSendingRates(void);
void             OnAsyncSocket(WPARAM, LPARAM);
void             OutputBanner(void);
void             CopyToClipboard(void);
int              SetupTransmitPackets(SOCKET);
void             GetIpAddresses(void);

//
// WinMain entry
//
int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
        _In_opt_ HINSTANCE hPrevInstance,
        _In_ LPWSTR    lpCmdLine,
        _In_ int       nCmdShow) {
        UNREFERENCED_PARAMETER(hPrevInstance);
        UNREFERENCED_PARAMETER(lpCmdLine);

        //
        // Initialize global strings
        //
        LoadStringW(hInstance, IDS_APP_TITLE, szTitle, MAX_LOADSTRING);
        LoadStringW(hInstance, IDC_UDPSTWIN, szWindowClass, MAX_LOADSTRING);
        MyRegisterClass(hInstance);

        //
        // Application initialization
        //
        if (!InitInstance(hInstance, nCmdShow)) {
                return FALSE;
        }
        HACCEL hAccelTable = LoadAccelerators(hInstance, MAKEINTRESOURCE(IDC_UDPSTWIN));

        //
        // Winsock initialization
        //
        WSADATA wsaData;
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
                return FALSE;
        }

        //
        // Initialize non-zero configuration data
        //
        memset(&conf, 0, sizeof(struct configuration));
        conf.controlPort = DEF_CONTROL_PORT;
        conf.dsTesting = TRUE;
        // --- Allocate and initialize connections ---
        conf.maxConnections = MAX_SERVER_CONN;
        conn = (struct connection *) malloc(sizeof(struct connection) * conf.maxConnections);
        for (int i = 0; i < conf.maxConnections; i++)
            init_conn(i, FALSE);
        // -------------------------------------------
        conf.addrFamily = AF_UNSPEC;
        conf.minConnCount = DEF_MC_COUNT;
        conf.maxConnCount = DEF_MC_COUNT;
        conf.errSuppress = TRUE;
        conf.jumboStatus = FALSE; // Opposite default from Linux
        conf.traditionalMTU = FALSE;
        conf.rateAdjAlgo = DEF_RA_ALGO;
        conf.useOwDelVar = DEF_USE_OWDELVAR;
        conf.ignoreOooDup = DEF_IGNORE_OOODUP;
        conf.seqNumAdjust = DEF_SEQNUM_ADJ;
        conf.dscpEcn = DEF_DSCPECN_BYTE; // Not using (or allowing) non-zero ToS bytes
        conf.srIndexConf = DEF_SRINDEX_CONF;
        conf.testIntTime = DEF_TESTINT_TIME;
        conf.subIntPeriod = DEF_SUBINT_PERIOD;
        conf.sockSndBuf = DEF_SOCKET_BUF;
        conf.sockRcvBuf = DEF_SOCKET_BUF;
        conf.lowThresh = DEF_LOW_THRESH;
        conf.upperThresh = DEF_UPPER_THRESH;
        conf.trialInt = DEF_TRIAL_INT;
        conf.slowAdjThresh = DEF_SLOW_ADJ_TH;
        conf.highSpeedDelta = DEF_HS_DELTA;
        conf.seqErrThresh = DEF_SEQ_ERR_TH;
        conf.logFileMax = DEF_LOGFILE_MAX * 1000;

        //
        // Initialize non-zero repository data
        //
        InitRepository(FALSE);

        //
        // Create pseudo console connection
        //
        errConn = new_conn(0, NULL, 0, T_CONSOLE, &null_action, &null_action);
        aggConn = errConn; // Use initial connection as aggregate connection

        //
        // Output one-time info message
        //
        nCharCount = sprintf(scratch, SOFTWARE_TITLE_WIN " (" WINDOWS_VER ") [" PLATFORM_BITS "-bit"
#ifdef _DEBUG
                ", Debug"
#endif
                "]\n\n"
                " - Built using OB-UDPST " SOFTWARE_VER " core components\n\n"
                " - Client testing restricted to a single connection (one flow)\n\n"
                " - Test results are limited to text output (no JSON)\n\n"
                "REMINDER: For more accurate results, do not interact with the application while a test is in progress.\n"
                "=========================================================================================================");
        write_alt(-1, scratch, nCharCount);

        //
        // Primary message loop
        //
        MSG msg;
        FILETIME systemFileTime;
        ULARGE_INTEGER sysTime, sysTimeLast;
        GetSystemTimePreciseAsFileTime(&systemFileTime);
        sysTimeLast.LowPart = systemFileTime.dwLowDateTime;
        sysTimeLast.HighPart = systemFileTime.dwHighDateTime;
        srand(sysTimeLast.LowPart);
        while (true) {
                if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
                        if (msg.message == WM_QUIT)
                                return (int) msg.wParam;
                        if (!TranslateAccelerator(msg.hwnd, hAccelTable, &msg)) {
                                TranslateMessage(&msg);
                                DispatchMessage(&msg);
                        }
                }

                //
                // Get system time and simulated a 100 microsecond base timer
                //
                GetSystemTimePreciseAsFileTime(&systemFileTime);
                sysTime.LowPart = systemFileTime.dwLowDateTime;
                sysTime.HighPart = systemFileTime.dwHighDateTime;
                if (sysTime.QuadPart - sysTimeLast.QuadPart >= 1000ULL) { // 100 microseconds
                        sysTimeLast.LowPart = sysTime.LowPart;
                        sysTimeLast.HighPart = sysTime.HighPart;

                        //
                        // Convert 100 nanosecond intervals into timespec seconds and nanoseconds
                        //
                        uint64_t nsUnixEpoch = (sysTime.QuadPart - 116444736000000000ULL) * 100ULL;
                        repo.systemClock.tv_sec = (time_t) (nsUnixEpoch / 1000000000ULL);
                        repo.systemClock.tv_nsec = (long) (nsUnixEpoch % 1000000000ULL);

                        //
                        // Process next local action if defined
                        //
                        if (NextLocalAction) {
                                (NextLocalAction) ();
                                NextLocalAction = NULL;
                        }

                        //
                        // Process standard udpst timers
                        //
                        ProcessTimers();
                }
        }
}

void InitRepository(BOOL reset) {
    struct sendingRate* sr;
    char* sbuf;
    char* dbuf;
    struct serverId sid;

    if (reset) {
        sr = repo.sendingRates;
        sbuf = repo.sndBuffer;
        dbuf = repo.defBuffer;
        memcpy(&sid, &repo.server[0], sizeof(struct serverId)); // Save existing server definittion
    } else {
        sr = (struct sendingRate*)calloc(1, MAX_SENDING_RATES * sizeof(struct sendingRate));
        sbuf = (char*)calloc(1, SND_BUFFER_SIZE);
        dbuf = (char*)calloc(1, DEF_BUFFER_SIZE);
        memset(&sid, 0, sizeof(struct serverId));
    }
    memset(&repo, 0, sizeof(struct repository));
    repo.sendingRates = sr;
    repo.sndBuffer = sbuf;
    repo.defBuffer = dbuf;
    memcpy(&repo.server[0], &sid, sizeof(struct serverId));
    if (repo.server[0].name != NULL)
        repo.serverCount = 1; // Update count if server restored
    repo.maxConnIndex = -1; // No connections allocated
    repo.endTimeStatus = -1; // Default to errored exit
    repo.intfFD = -1; // No file descriptor

    return;
}

//
// Timer processing for standard udpst timers
//
void ProcessTimers() {
        int i;

        for (i = 0; i <= repo.maxConnIndex; i++) {
                if (tspecisset(&conn[i].endTime)) {
                        if (tspeccmp(&repo.systemClock, &conn[i].endTime, > )) {
                                nCharCount = 0;
                                if (repo.isServer) {
                                        if (conf.maxBandwidth > 0) {
                                                // Adjust current upstream/downstream bandwidth
                                                if (conn[i].testType == TEST_TYPE_US) {
                                                        if ((repo.usBandwidth -= conn[i].maxBandwidth) < 0)
                                                                repo.usBandwidth = 0;
                                                } else {
                                                        if ((repo.dsBandwidth -= conn[i].maxBandwidth) < 0)
                                                                repo.dsBandwidth = 0;
                                                }
                                                if (conf.verbose) {
                                                        nCharCount = sprintf(scratch, "[%d]End time reached (New USBW: %d, DSBW: %d)\n", i, repo.usBandwidth,
                                                                repo.dsBandwidth);
                                                        write_alt(-1, scratch, nCharCount);
                                                }
                                        }
                                }
                                if (nCharCount == 0 && conf.verbose) {
                                        nCharCount = sprintf(scratch, "[%d]End time reached", i);
                                        write_alt(-1, scratch, nCharCount);
                                }
                                init_conn(i, TRUE);
                                if (i == errConn) { // Reset to initial state 
                                    InitRepository(TRUE);
                                    errConn = new_conn(0, NULL, 0, T_CONSOLE, &null_action, &null_action);
                                    aggConn = errConn; // Use initial connection as aggregate connection
                                }
                                continue;
                        }
                }
                if (conn[i].state != S_DATA)
                        continue;

                if (tspecisset(&conn[i].timer1Thresh)) {
                        if (tspeccmp(&repo.systemClock, &conn[i].timer1Thresh, > )) {
                                (conn[i].timer1Action)(i);
                        }
                }
                if (tspecisset(&conn[i].timer2Thresh)) {
                        if (tspeccmp(&repo.systemClock, &conn[i].timer2Thresh, > )) {
                                (conn[i].timer2Action)(i);
                        }
                }
                if (tspecisset(&conn[i].timer3Thresh)) {
                        if (tspeccmp(&repo.systemClock, &conn[i].timer3Thresh, > )) {
                                (conn[i].timer3Action)(i);
                        }
                }
        }
        return;
}

//
//  Register the window class
//
ATOM MyRegisterClass(HINSTANCE hInstance) {
        WNDCLASSEXW wcex;
        HBRUSH hBrush = CreateSolidBrush(rgbBG);

        wcex.cbSize = sizeof(WNDCLASSEX);

        wcex.style = CS_HREDRAW | CS_VREDRAW;
        wcex.lpfnWndProc = WndProc;
        wcex.cbClsExtra = 0;
        wcex.cbWndExtra = 0;
        wcex.hInstance = hInstance;
        wcex.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_UDPSTWIN));
        wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wcex.hbrBackground = hBrush;
        wcex.lpszMenuName = MAKEINTRESOURCEW(IDC_UDPSTWIN);
        wcex.lpszClassName = szWindowClass;
        wcex.hIconSm = LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDI_UDPSTWIN));

        DeleteObject(hBrush);
        return RegisterClassExW(&wcex);
}

//
// Save instance handle and create main window
//
BOOL InitInstance(HINSTANCE hInstance, int nCmdShow) {
        hInst = hInstance;

        HWND hWnd = CreateWindowW(szWindowClass, szTitle, WS_OVERLAPPEDWINDOW,
                CW_USEDEFAULT, 0, CW_USEDEFAULT, 0, nullptr, nullptr, hInstance, nullptr);

        if (!hWnd) {
                return FALSE;
        }
        hWndPrimary = hWnd;

        ShowWindow(hWnd, nCmdShow);
        UpdateWindow(hWnd);

        return TRUE;
}

//
// Message handler for About dialog
//
INT_PTR CALLBACK About(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam) {
        UNREFERENCED_PARAMETER(lParam);
        switch (message) {
        case WM_INITDIALOG:
                return (INT_PTR) TRUE;

        case WM_COMMAND:
                if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL) {
                        EndDialog(hDlg, LOWORD(wParam));
                        return (INT_PTR) TRUE;
                }
                break;
        }
        return (INT_PTR) FALSE;
}

//
// Message handler for Setup dialog
//
INT_PTR CALLBACK SetupDialog(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam) {
        UNREFERENCED_PARAMETER(lParam);

        switch (message) {
        case WM_INITDIALOG:
                ProcessSetupDialog(hDlg, TRUE);
                FormatSetupDialog(hDlg, repo.isServer);
                return (INT_PTR) TRUE;
        case WM_COMMAND:
                if (LOWORD(wParam) == IDC_SERVER) {
                        FormatSetupDialog(hDlg, TRUE);

                } else if (LOWORD(wParam) == IDC_UPSTREAM || LOWORD(wParam) == IDC_DOWNSTREAM) {
                        FormatSetupDialog(hDlg, FALSE);

                } else if (LOWORD(wParam) == ID_START_TEST || LOWORD(wParam) == ID_SHOW_SR) {
                        if (ProcessSetupDialog(hDlg, FALSE))
                                return (INT_PTR) TRUE;

                        //
                        // Build/rebuild sending rate table
                        //
                        repo.maxSendingRates = repo.hSpeedThresh = 0;
                        memset(repo.sendingRates, 0, MAX_SENDING_RATES * sizeof(struct sendingRate));
                        if ((nCharCount = def_sending_rates()) > 0) {
                                write_alt(-1, scratch, nCharCount);
                                EndDialog(hDlg, LOWORD(wParam));
                                return (INT_PTR) TRUE;
                        }

                        //
                        // Set immediate next action based on command
                        //
                        if (LOWORD(wParam) == ID_START_TEST) {
                                if (repo.server[0].name || repo.isServer) {
                                        NextLocalAction = &StartTest;
                                        EndDialog(hDlg, LOWORD(wParam));
                                } else {
                                        MessageBox(NULL, L"Server Hostname or IP Address", L"Parameter Required", MB_OK | MB_ICONINFORMATION);
                                }
                        } else if (LOWORD(wParam) == ID_SHOW_SR) {
                                NextLocalAction = &ShowSendingRates;
                                EndDialog(hDlg, LOWORD(wParam));
                        }
                } else if (LOWORD(wParam) == IDCANCEL) {
                        EndDialog(hDlg, LOWORD(wParam));
                }
                return (INT_PTR) TRUE;
                break;
        }
        return (INT_PTR) FALSE;
}

//
// Format Setup dialog by enabling/disabling controls based on type
//
void FormatSetupDialog(HWND hDlg, BOOL isServer) {
        BOOL isClient = TRUE;

        if (isServer)
                isClient = FALSE;

        EnableWindow(GetDlgItem(hDlg, IDC_TESTINTTIME), isClient);
        EnableWindow(GetDlgItem(hDlg, IDC_USEOWDELVAR), isClient);
        EnableWindow(GetDlgItem(hDlg, IDC_IGNOREOOODUP), isClient);
        EnableWindow(GetDlgItem(hDlg, IDC_BIMODALCOUNT), isClient);
        EnableWindow(GetDlgItem(hDlg, IDC_RATEADJALGO), isClient);
        EnableWindow(GetDlgItem(hDlg, IDC_LOWTHRESH), isClient);
        EnableWindow(GetDlgItem(hDlg, IDC_UPPERTHRESH), isClient);
        EnableWindow(GetDlgItem(hDlg, IDC_SRINDEXCONF), isClient);
        return;
}

//
// Process Setup dialog for initialization, test start, and showing sending rates
//
BOOL ProcessSetupDialog(HWND hDlg, BOOL initDialog) {
        int i, var;
        BOOL bTranslated;

        if (initDialog) {
                if (repo.isServer) {
                        var = IDC_SERVER;
                } else if (conf.usTesting) {
                        var = IDC_UPSTREAM;
                } else if (conf.dsTesting) {
                        var = IDC_DOWNSTREAM;
                }
                CheckRadioButton(hDlg, IDC_UPSTREAM, IDC_SERVER, var);
        } else {
                if (IsDlgButtonChecked(hDlg, IDC_UPSTREAM) == BST_CHECKED) {
                        conf.usTesting = TRUE;
                        conf.dsTesting = FALSE;
                        repo.isServer = FALSE;
                } else if (IsDlgButtonChecked(hDlg, IDC_DOWNSTREAM) == BST_CHECKED) {
                        conf.usTesting = FALSE;
                        conf.dsTesting = TRUE;
                        repo.isServer = FALSE;
                } else {
                        conf.usTesting = FALSE;
                        conf.dsTesting = FALSE;
                        repo.isServer = TRUE;
                }
        }

        if (initDialog) {
                if (conf.addrFamily == AF_UNSPEC) {
                        var = IDC_ANYADDRFAMILY;
                } else if (conf.addrFamily == AF_INET) {
                        var = IDC_IPV4ONLY;
                } else {
                        var = IDC_IPV6ONLY;
                }
                CheckRadioButton(hDlg, IDC_ANYADDRFAMILY, IDC_IPV6ONLY, var);
        } else {
                if (IsDlgButtonChecked(hDlg, IDC_ANYADDRFAMILY) == BST_CHECKED) {
                        conf.addrFamily = AF_UNSPEC;
                        conf.ipv6Only = FALSE;
                } else if (IsDlgButtonChecked(hDlg, IDC_IPV4ONLY) == BST_CHECKED) {
                        conf.addrFamily = AF_INET;
                        conf.ipv6Only = FALSE;
                } else {
                        conf.addrFamily = AF_INET6;
                        conf.ipv6Only = TRUE;
                }
        }

        if (initDialog) {
                if (repo.server[0].name) {
                        SetDlgItemTextA(hDlg, IDC_SERVERNAME, szServerName);
                }
                SetDlgItemInt(hDlg, IDC_CONTROLPORT, conf.controlPort, FALSE);
        } else {
                repo.server[0].name = NULL;
                *repo.server[0].ip = '\0';
                if (GetDlgItemTextA(hDlg, IDC_SERVERNAME, szServerName, sizeof(szServerName)) > 0) {
                        repo.server[0].name = szServerName;
                }
                var = GetDlgItemInt(hDlg, IDC_CONTROLPORT, &bTranslated, FALSE);
                if (ParameterCheck(var, MIN_CONTROL_PORT, MAX_CONTROL_PORT, L"Control Port"))
                        return TRUE;
                conf.controlPort = var;
                repo.server[0].port = conf.controlPort;
                repo.serverCount = 1;
        }

        if (initDialog) {
                if (conf.verbose)
                        CheckDlgButton(hDlg, IDC_VERBOSE, BST_CHECKED);
                else
                        CheckDlgButton(hDlg, IDC_VERBOSE, BST_UNCHECKED);
        }
 else {
         if (IsDlgButtonChecked(hDlg, IDC_VERBOSE) == BST_CHECKED) {
                 conf.verbose = TRUE;
                 monConn = errConn;
         }
         else {
                 conf.verbose = FALSE;
                 monConn = -1;
         }
        }

        if (initDialog) {
                if (conf.showLossRatio)
                        CheckDlgButton(hDlg, IDC_LOSSRATIO, BST_CHECKED);
                else
                        CheckDlgButton(hDlg, IDC_LOSSRATIO, BST_UNCHECKED);
        }
        else {
                if (IsDlgButtonChecked(hDlg, IDC_LOSSRATIO) == BST_CHECKED)
                        conf.showLossRatio = TRUE;
                else
                        conf.showLossRatio = FALSE;
        }

        if (initDialog) {
                if (conf.traditionalMTU)
                        CheckDlgButton(hDlg, IDC_TRADITIONALMTU, BST_CHECKED);
                else
                        CheckDlgButton(hDlg, IDC_TRADITIONALMTU, BST_UNCHECKED);
        }
        else {
                if (IsDlgButtonChecked(hDlg, IDC_TRADITIONALMTU) == BST_CHECKED)
                        conf.traditionalMTU = TRUE;
                else
                        conf.traditionalMTU = FALSE;
        }

        if (initDialog) {
                if (conf.jumboStatus)
                        CheckDlgButton(hDlg, IDC_DISABLEJUMBO, BST_UNCHECKED);
                else
                        CheckDlgButton(hDlg, IDC_DISABLEJUMBO, BST_CHECKED);
        }
        else {
                if (IsDlgButtonChecked(hDlg, IDC_DISABLEJUMBO) == BST_CHECKED)
                        conf.jumboStatus = FALSE;
                else
                        conf.jumboStatus = TRUE;
        }

        if (initDialog) {
                if (conf.ignoreOooDup)
                        CheckDlgButton(hDlg, IDC_IGNOREOOODUP, BST_CHECKED);
                else
                        CheckDlgButton(hDlg, IDC_IGNOREOOODUP, BST_UNCHECKED);
        }
        else if (repo.isServer) {
                conf.ignoreOooDup = DEF_IGNORE_OOODUP;
        }
        else {
                if (IsDlgButtonChecked(hDlg, IDC_IGNOREOOODUP) == BST_CHECKED)
                        conf.ignoreOooDup = TRUE;
                else
                        conf.ignoreOooDup = FALSE;
        }

        if (initDialog) {
                SetDlgItemInt(hDlg, IDC_TESTINTTIME, conf.testIntTime, FALSE);
        }
        else if (repo.isServer) {
                conf.testIntTime = MAX_TESTINT_TIME;
        }
        else {
                var = GetDlgItemInt(hDlg, IDC_TESTINTTIME, &bTranslated, FALSE);
                if (ParameterCheck(var, MIN_TESTINT_TIME, MAX_TESTINT_TIME, L"Test Interval Time"))
                        return TRUE;
                conf.testIntTime = var;
        }

        if (initDialog) {
                if (conf.maxBandwidth > 0) {
                        SetDlgItemInt(hDlg, IDC_MAXBANDWIDTH, conf.maxBandwidth, FALSE);
                }
                else {
                        SetDlgItemTextA(hDlg, IDC_MAXBANDWIDTH, "");
                }
        }
        else {
                var = GetDlgItemInt(hDlg, IDC_MAXBANDWIDTH, &bTranslated, FALSE);
                if (!bTranslated)
                        var = 0;
                else if (ParameterCheck(var, MIN_REQUIRED_BW, MAX_CLIENT_BW, L"Max Bandwidth Required"))
                        return TRUE;
                conf.maxBandwidth = var;
        }

        if (initDialog) {
                if (conf.bimodalCount != DEF_BIMODAL_COUNT) {
                        i = 0;
                        if (conf.srAdjSuppCount > 0)
                                scratch[i++] = '-';
                        sprintf(&scratch[i], "%d", conf.bimodalCount);
                        SetDlgItemTextA(hDlg, IDC_BIMODALCOUNT, scratch);
                } else {
                        SetDlgItemTextA(hDlg, IDC_BIMODALCOUNT, "");
                }
        } else if (repo.isServer) {
                conf.bimodalCount = DEF_BIMODAL_COUNT;
                conf.srAdjSuppCount = 0;
        } else {
                CHAR* endptr;
                int suppcnt = 0;
                var = DEF_BIMODAL_COUNT;
                if (GetDlgItemTextA(hDlg, IDC_BIMODALCOUNT, scratch, STRING_SIZE) > 0) {
                        i = 0;
                        if (scratch[i] == '-')
                                i++;
                        var = DEF_BIMODAL_COUNT; // Invalid as entry
                        if (scratch[i] != '\0') {
                                var = (int) strtol(&scratch[i], &endptr, 10);
                                if (*endptr != '\0') // Trailing junk invalidates entry
                                        var = DEF_BIMODAL_COUNT;
                        }
                        if (ParameterCheck(var, MIN_BIMODAL_COUNT, MAX_BIMODAL_COUNT, L"Bimodal Initial Sub-intervals"))
                                return TRUE;
                        if (var >= (conf.testIntTime * MSECINSEC) / conf.subIntPeriod) {
                                MessageBox(NULL, L"Bimodal count must be less than total sub-intervals", L"Parameter Out-of-Range", MB_OK | MB_ICONINFORMATION);
                                return TRUE;
                        }
                        if (i > 0) // If '-' prefix was present
                                suppcnt = var;
                }
                conf.bimodalCount = var;
                conf.srAdjSuppCount = suppcnt;
        }

#ifdef AUTH_KEY_ENABLE
        if (initDialog) {
                SetDlgItemTextA(hDlg, IDC_AUTHKEY, conf.authKey);
                SetDlgItemInt(hDlg, IDC_AUTHKEYID, conf.keyId, FALSE);
        } else {
                GetDlgItemTextA(hDlg, IDC_AUTHKEY, conf.authKey, sizeof(conf.authKey));
                var = GetDlgItemInt(hDlg, IDC_AUTHKEYID, &bTranslated, FALSE);
                if (ParameterCheck(var, MIN_KEY_ID, MAX_KEY_ID, L"Key ID")) {
                        return TRUE;
                } else if (*conf.authKey == '\0' && var != DEF_KEY_ID) {
                        MessageBox(NULL, L"Authentication Key", L"Parameter Required", MB_OK | MB_ICONINFORMATION);
                        return TRUE;
                }
                conf.keyId = var;
        }
#else
        EnableWindow(GetDlgItem(hDlg, IDC_AUTHKEY), FALSE);
        EnableWindow(GetDlgItem(hDlg, IDC_AUTHKEYID), FALSE);
#endif

        if (initDialog) {
                HWND hWndComboBox = GetDlgItem(hDlg, IDC_RATEADJALGO);
                TCHAR Algo[16];
                for (i = CHTA_RA_ALGO_MIN; i <= CHTA_RA_ALGO_MAX; i++) {
                        memset(&Algo, 0, sizeof(Algo));
                        wcscpy_s(Algo, sizeof(Algo) / sizeof(TCHAR), (TCHAR*)rateAdjAlgo[i]);
                        SendMessage(hWndComboBox, (UINT)CB_ADDSTRING, (WPARAM)0, (LPARAM)Algo);
                }
                SendMessage(hWndComboBox, CB_SETCURSEL, (WPARAM)conf.rateAdjAlgo, (LPARAM)0);
        } else {
                HWND hWndComboBox = GetDlgItem(hDlg, IDC_RATEADJALGO);
                conf.rateAdjAlgo = (int) SendMessage(hWndComboBox, (UINT) CB_GETCURSEL, (WPARAM)0, (LPARAM)0);
        }

        if (initDialog) {
                if (conf.useOwDelVar)
                        CheckDlgButton(hDlg, IDC_USEOWDELVAR, BST_CHECKED);
                else
                        CheckDlgButton(hDlg, IDC_USEOWDELVAR, BST_UNCHECKED);
                SetDlgItemInt(hDlg, IDC_LOWTHRESH, conf.lowThresh, FALSE);
                SetDlgItemInt(hDlg, IDC_UPPERTHRESH, conf.upperThresh, FALSE);
        } else if (repo.isServer) {
                conf.useOwDelVar = DEF_USE_OWDELVAR;
                conf.lowThresh = DEF_LOW_THRESH;
                conf.upperThresh = DEF_UPPER_THRESH;
        } else {
                if (IsDlgButtonChecked(hDlg, IDC_USEOWDELVAR) == BST_CHECKED)
                        conf.useOwDelVar = TRUE;
                else
                        conf.useOwDelVar = FALSE;

                var = GetDlgItemInt(hDlg, IDC_LOWTHRESH, &bTranslated, FALSE);
                if (ParameterCheck(var, MIN_LOW_THRESH, MAX_LOW_THRESH, L"Low Delay Variation Threshold"))
                        return TRUE;
                conf.lowThresh = var;

                var = GetDlgItemInt(hDlg, IDC_UPPERTHRESH, &bTranslated, FALSE);
                if (ParameterCheck(var, MIN_UPPER_THRESH, MAX_UPPER_THRESH, L"Upper Delay Variation Threshold"))
                        return TRUE;
                conf.upperThresh = var;
        }

        if (initDialog) {
                if (conf.srIndexConf != DEF_SRINDEX_CONF) {
                        i = 0;
                        if (conf.srIndexIsStart)
                                scratch[i++] = SRIDX_ISSTART_PREFIX;
                        sprintf(&scratch[i], "%d", conf.srIndexConf);
                        SetDlgItemTextA(hDlg, IDC_SRINDEXCONF, scratch);
                } else {
                        SetDlgItemTextA(hDlg, IDC_SRINDEXCONF, "");
                }
        } else if (repo.isServer) {
                conf.srIndexConf = MAX_SRINDEX_CONF;
                conf.srIndexIsStart = FALSE;
        } else {
                CHAR* endptr;
                BOOL bvar = FALSE;
                var = DEF_SRINDEX_CONF;
                if (GetDlgItemTextA(hDlg, IDC_SRINDEXCONF, scratch, STRING_SIZE) > 0) {
                        i = 0;
                        if (scratch[i] == SRIDX_ISSTART_PREFIX) {
                                i++;
                                bvar = TRUE;
                        }
                        var = DEF_SRINDEX_CONF; // Invalid as entry
                        if (scratch[i] != '\0') {
                                var = (int) strtol(&scratch[i], &endptr, 10);
                                if (*endptr != '\0') // Trailing junk invalidates entry
                                        var = DEF_SRINDEX_CONF;
                        }
                        if (ParameterCheck(var, MIN_SRINDEX_CONF, MAX_SRINDEX_CONF, L"Sending Rate Index"))
                                return TRUE;
                }
                conf.srIndexConf = var;
                conf.srIndexIsStart = bvar;
        }

        //
        // Enable buttons if no test in progress, else disable
        //
        if (initDialog) {
                if (repo.maxConnIndex < 1) {
                        EnableWindow(GetDlgItem(hDlg, ID_START_TEST), TRUE);
                        EnableWindow(GetDlgItem(hDlg, ID_SHOW_SR), TRUE);
                } else {
                        EnableWindow(GetDlgItem(hDlg, ID_START_TEST), FALSE);
                        EnableWindow(GetDlgItem(hDlg, ID_SHOW_SR), FALSE);
                }
        }
        return FALSE;
}

//
// Generic parameter check for numeric values
//
BOOL ParameterCheck(int param, int min, int max, const wchar_t *name) {
        wchar_t msg[128];
        if (param < min || param > max) {
                wsprintfW(msg, L"%wS\n\nRange: %d - %d", name, min, max);
                MessageBox(NULL, (LPCWSTR) msg, L"Option Value Invalid", MB_OK | MB_ICONERROR);
                return TRUE;
        }
        return FALSE;
}

//
// Processes messages for the main window
//
LRESULT CALLBACK WndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
        HDC hdc;
        PAINTSTRUCT ps;
        TEXTMETRIC tm;
        SCROLLINFO si;

        static int xClient;     // Width of client area 
        static int yClient;     // Height of client area 
        static int xClientMax;  // Maximum width of client area 
        static int xChar;       // Horizontal scrolling unit 
        static int yChar;       // Vertical scrolling unit 
        static int xUpper;      // Average width of uppercase letters 
        static int xPos;        // Current horizontal scrolling position 
        static int yPos;        // Current vertical scrolling position 

        switch (uMsg) {
        case WM_CREATE:
        {
                //
                // Get the handle to the client area's device context
                //
                hdc = GetDC(hWnd);
                SelectObject(hdc, hObjFont);

                //
                // Extract font dimensions from the text metrics
                //
                GetTextMetrics(hdc, &tm);
                xChar = tm.tmAveCharWidth;
                xUpper = (tm.tmPitchAndFamily & 1 ? 3 : 2) * xChar / 2;
                yChar = tm.tmHeight + tm.tmExternalLeading;
                yCharGlobal = yChar;
                xClientMax = 100 * xChar + 25 * xUpper; // Max width (lowercase + uppercase)

                ReleaseDC(hWnd, hdc);
                return 0;
        }
        case WM_DESTROY:
        {
                WSACleanup();
                PostQuitMessage(0);
                return 0;
        }
        case WM_SIZE:
        {
                //
                // Retrieve the dimensions of the client area
                //
                yClient = HIWORD(lParam);
                xClient = LOWORD(lParam);
                yClientGlobal = yClient;

                //
                // Set the vertical scrolling range and page size
                //
                si.cbSize = sizeof(si);
                si.fMask = SIF_RANGE | SIF_PAGE;
                si.nMin = 0;
                si.nMax = iLineCount - 1;
                si.nPage = yClient / yChar;
                SetScrollInfo(hWnd, SB_VERT, &si, TRUE);

                //
                // Set the horizontal scrolling range and page size
                //
                si.cbSize = sizeof(si);
                si.fMask = SIF_RANGE | SIF_PAGE;
                si.nMin = 0;
                si.nMax = 2 + xClientMax / xChar;
                si.nPage = xClient / xChar;
                SetScrollInfo(hWnd, SB_HORZ, &si, TRUE);
                return 0;
        }
        case WM_PAINT:
        {
                //
                // Prepare the window for painting
                //
                hdc = BeginPaint(hWnd, &ps);
                SelectObject(hdc, hObjFont);
                SetTextColor(hdc, rgbText);
                SetBkColor(hdc, rgbBG);

                //
                // Get vertical scroll bar position
                //
                si.cbSize = sizeof(si);
                si.fMask = SIF_POS;
                GetScrollInfo(hWnd, SB_VERT, &si);
                yPos = si.nPos;

                //
                // Get horizontal scroll bar position
                //
                GetScrollInfo(hWnd, SB_HORZ, &si);
                xPos = si.nPos;

                //
                // Find painting limits based on text output
                //
                int iFirstLine = max(0, yPos + (ps.rcPaint.top / yChar));
                int iLastLine = min(iLineCount - 1, yPos + (ps.rcPaint.bottom / yChar));
                int iOutputLines = iLastLine - iFirstLine + 1;
                int yDelta = iFirstLine - yPos;
                int i, j = iFirstLine;
                if (iLineCount >= TEXT_BUFFER_SIZE) {
                        j = (j + iLineNext) & TEXT_BUFFER_MASK; // Adjust for circular buffer
                }

                //
                // Output text
                //
                for (i = 0; i < iOutputLines; i++) {
                        int x = xChar * (1 - xPos);
                        int y = yChar * yDelta++;

                        TextOutA(hdc, x, y, TextBuffer[j].szLineBuffer, TextBuffer[j].iLineLength);
                        j = ++j & TEXT_BUFFER_MASK; // Adjust for circular buffer
                }
                EndPaint(hWnd, &ps);
                return 0;
        }
        case WM_KEYDOWN:
        {
                WORD wScrollNotify = -1;
                switch (wParam) {
                case VK_UP:
                        wScrollNotify = SB_LINEUP;
                        break;
                case VK_PRIOR:
                        wScrollNotify = SB_PAGEUP;
                        break;
                case VK_NEXT:
                        wScrollNotify = SB_PAGEDOWN;
                        break;
                case VK_DOWN:
                        wScrollNotify = SB_LINEDOWN;
                        break;
                case VK_HOME:
                        wScrollNotify = SB_TOP;
                        break;
                case VK_END:
                        wScrollNotify = SB_BOTTOM;
                        break;
                }
                if (wScrollNotify != -1)
                        SendMessage(hWnd, WM_VSCROLL, MAKELONG(wScrollNotify, 0), 0L);
                return 0;
        }
        case WM_ERASEBKGND:
        {
                RECT rc;
                HBRUSH hBrush = CreateSolidBrush(rgbBG);

                hdc = (HDC) wParam;
                GetClientRect(hWnd, &rc);
                FillRect(hdc, &rc, hBrush);
                DeleteObject(hBrush);
                return TRUE;
        }
        case WM_COMMAND:
        {
                int wmId = LOWORD(wParam);
                //
                // Parse menu selections
                //
                switch (wmId) {
                case IDM_TEST_SETUP:
                        DialogBox(hInst, MAKEINTRESOURCE(IDD_TESTSETUP), hWnd, SetupDialog);
                        break;
                case IDM_EDIT_ERASEBUFFER:
                        iLineCount = iLineNext = 0;
                        ShowScrollBar(hWnd, SB_BOTH, FALSE);
                        InvalidateRect(hWndPrimary, NULL, TRUE);
                        break;
                case IDM_EDIT_COPYTOCB:
                        CopyToClipboard();
                        break;
                case IDM_VIEW_LOCALIP:
                        GetIpAddresses();
                        break;
                case IDM_VIEW_DARKBG:
                        rgbBG = rgbDarkBG;
                        rgbText = rgbDarkText;
                        InvalidateRect(hWndPrimary, NULL, TRUE);
                        break;
                case IDM_VIEW_LIGHTBG:
                        rgbBG = rgbLightBG;
                        rgbText = rgbLightText;
                        InvalidateRect(hWndPrimary, NULL, TRUE);
                        break;
                case IDM_ABOUT:
                        DialogBox(hInst, MAKEINTRESOURCE(IDD_ABOUTBOX), hWnd, About);
                        break;
                case IDM_EXIT:
                        DestroyWindow(hWnd);
                        break;
                default:
                        return DefWindowProc(hWnd, uMsg, wParam, lParam);
                }
                return 0;
        }
        case WM_HSCROLL:
        {
                //
                // Get all the vertial scroll bar information
                //
                si.cbSize = sizeof(si);
                si.fMask = SIF_ALL;

                //
                // Save the position for comparison later
                //
                GetScrollInfo(hWnd, SB_HORZ, &si);
                xPos = si.nPos;
                switch (LOWORD(wParam)) {
                case SB_LINELEFT:
                        si.nPos -= 1;
                        break;
                case SB_LINERIGHT:
                        si.nPos += 1;
                        break;
                case SB_PAGELEFT:
                        si.nPos -= si.nPage;
                        break;
                case SB_PAGERIGHT:
                        si.nPos += si.nPage;
                        break;
                case SB_THUMBTRACK:
                        si.nPos = si.nTrackPos;
                        break;
                default:
                        break;
                }

                //
                // Set the position and then retrieve it (due to adjustments by Windows it may not be the same as the value set)
                //
                si.fMask = SIF_POS;
                SetScrollInfo(hWnd, SB_HORZ, &si, TRUE);
                GetScrollInfo(hWnd, SB_HORZ, &si);

                //
                // If the position has changed, scroll the window
                //
                if (si.nPos != xPos) {
                        ScrollWindow(hWnd, xChar * (xPos - si.nPos), 0, NULL, NULL);
                }
                return 0;
        }
        case WM_VSCROLL:
        {
                int iMouseScroll;

                //
                // Get all the vertial scroll bar information
                //
                si.cbSize = sizeof(si);
                si.fMask = SIF_ALL;
                GetScrollInfo(hWnd, SB_VERT, &si);

                //
                // Save the position for comparison later on
                //
                yPos = si.nPos;
                switch (LOWORD(wParam)) {
                case SB_TOP:
                        si.nPos = si.nMin;
                        break;
                case SB_BOTTOM:
                        si.nPos = si.nMax;
                        break;
                case SB_LINEUP:
                        if ((iMouseScroll = HIWORD(wParam)) == 0)
                                si.nPos -= 1;
                        else
                                si.nPos -= iMouseScroll; // Custom HIWORD use for mouse scroll
                        break;
                case SB_LINEDOWN:
                        if ((iMouseScroll = HIWORD(wParam)) == 0)
                                si.nPos += 1;
                        else
                                si.nPos += iMouseScroll; // Custom HIWORD use for mouse scroll
                        break;
                case SB_PAGEUP:
                        si.nPos -= si.nPage;
                        break;
                case SB_PAGEDOWN:
                        si.nPos += si.nPage;
                        break;
                case SB_THUMBTRACK:
                        si.nPos = si.nTrackPos;
                        break;
                default:
                        break;
                }

                //
                // Set the position and then retrieve it (due to adjustments by Windows it may not be the same as the value set)
                //
                si.fMask = SIF_POS;
                SetScrollInfo(hWnd, SB_VERT, &si, TRUE);
                GetScrollInfo(hWnd, SB_VERT, &si);

                //
                // If the position has changed, scroll window and update it
                //
                if (si.nPos != yPos) {
                        ScrollWindow(hWnd, 0, yChar * (yPos - si.nPos), NULL, NULL);
                        UpdateWindow(hWnd);
                }
                return 0;
        }
        case WM_MOUSEWHEEL:
        {
                INT zDelta = GET_WHEEL_DELTA_WPARAM(wParam);

                if (zDelta >= WHEEL_DELTA) {
                        SendMessage(hWnd, WM_VSCROLL, MAKELONG(SB_LINEUP, 3), 0L);   // Custom HIWORD use for mouse scroll
                } else if (zDelta <= WHEEL_DELTA) {
                        SendMessage(hWnd, WM_VSCROLL, MAKELONG(SB_LINEDOWN, 3), 0L); // Custom HIWORD use for mouse scroll
                }
                return 0;
        }
        case WM_SOCKET:
                OnAsyncSocket(wParam, lParam);
                return 0;
        }
        return DefWindowProc(hWnd, uMsg, wParam, lParam);
}

//
// Output standard udpst looking banner
//
void OutputBanner(void) {
        int i, j, var;

        write_alt(-1, "\n", 1); // Add newline prefix in this environment

        var = sprintf(scratch, SOFTWARE_TITLE "\nSoftware Ver: %s", SOFTWARE_VER);
        if (repo.isServer)
                var += sprintf(&scratch[var], ", Protocol Ver: %d-%d", PROTOCOL_MIN, PROTOCOL_VER);
        else
                var += sprintf(&scratch[var], ", Protocol Ver: %d", PROTOCOL_VER); // Client is always the latest
        var += sprintf(&scratch[var], ", Built: " __DATE__ " " __TIME__);
#ifdef RATE_LIMITING
        var += sprintf(&scratch[var], ", Rate Limiting via '-B mbps'");
#endif // RATE_LIMITING
        scratch[var++] = '\n';
        var = (int) write_alt(-1, scratch, var);
        //
        var = 0;
        if (conf.ipv6Only)
                var = IPV6_ADDSIZE;
        if (conf.traditionalMTU)
                i = MAX_TPAYLOAD_SIZE - var;
        else
                i = MAX_PAYLOAD_SIZE - var;
        if (conf.jumboStatus)
                j = MAX_JPAYLOAD_SIZE - var;
        else
                j = i;
        if (repo.isServer)
                var = sprintf(scratch, "Mode: Server, Payload Default[Max]: %d[%d]", i, j);
        else
                var = sprintf(scratch, "Mode: Client, Payload Default[Max]: %d[%d]", i, j);
#ifdef AUTH_KEY_ENABLE
        var += sprintf(&scratch[var], ", Auth: Available");
#else
        var += sprintf(&scratch[var], ", Auth: Unavailable");
#endif // AUTH_KEY_ENABLE
#ifdef ADD_HEADER_CSUM
        var += sprintf(&scratch[var], ", Checksum: On");
#else
        var += sprintf(&scratch[var], ", Checksum: Off");
#endif // ADD_HEADER_CSUM
        var += sprintf(&scratch[var], ", Optimizations: N/A");
/*
#ifdef HAVE_SENDMMSG
        var += sprintf(&scratch[var], " SendMMsg()");
#ifdef HAVE_GSO
        var += sprintf(&scratch[var], "+GSO");
#endif // HAVE_GSO
#endif // HAVE_SENDMMSG
#ifdef HAVE_RECVMMSG
        var += sprintf(&scratch[var], " RecvMMsg()+Trunc");
#endif // HAVE_RECVMMSG
*/
        scratch[var++] = '\n';
        var = (int) write_alt(-1, scratch, var);
}

//
// Copy text buffer to clipboard
//
void CopyToClipboard(void) {
        char *lpszCopy;
        HGLOBAL hglbCopy;
        SIZE_T bufsize = 0;
        struct TextLine *tl;
        int i, j;

        if (!OpenClipboard(hWndPrimary))
                return;
        EmptyClipboard();

        //
        // Calculate needed size
        //
        j = 0;
        if (iLineCount >= TEXT_BUFFER_SIZE)
                j = iLineNext;
        for (i = 0; i < iLineCount; i++) {
                tl = &TextBuffer[j];
                bufsize += tl->iLineLength + 2; // Add space for CR/LF
                j = ++j & TEXT_BUFFER_MASK;
        }
        if (bufsize == 0) {
                CloseClipboard();
                return;
        }
        bufsize++; // Add 1 for final NULL

        if ((hglbCopy = GlobalAlloc(GMEM_MOVEABLE, bufsize)) == NULL) {
                CloseClipboard();
                return;
        }

        if ((lpszCopy = (char *) GlobalLock(hglbCopy)) == NULL) {
                GlobalFree(hglbCopy);
                CloseClipboard();
                return;
        }

        //
        // Copy text buffer
        //
        j = 0;
        if (iLineCount >= TEXT_BUFFER_SIZE)
                j = iLineNext;
        for (i = 0; i < iLineCount; i++) {
                tl = &TextBuffer[j];
                memcpy(lpszCopy, tl->szLineBuffer, tl->iLineLength);
                lpszCopy += tl->iLineLength;
                *lpszCopy++ = '\r';
                *lpszCopy++ = '\n';
                j = ++j & TEXT_BUFFER_MASK;
        }
        *lpszCopy = '\0';
        GlobalUnlock(hglbCopy);

        SetClipboardData(CF_TEXT, hglbCopy);
        CloseClipboard();
        return;
}

//
// Alternate write function to store strings in circular text buffer
//
size_t write_alt(int fd, const char *buf, size_t count) {
        int i, j = 0;
        char *s = (char *) buf;
        SCROLLINFO si;
        struct TextLine *tl;

        //
        // Process embedded newlines into separate line buffer entries
        //
        for (i = 0; i < (int) count; i += j) {
                j = 0;
                tl = &TextBuffer[iLineNext];
                while (*s) {
                        if (*s == '\n')
                                break;
                        tl->szLineBuffer[j++] = *s++;
                        if (j == MAX_LINELEN - 1)
                                break;
                }
                tl->iLineLength = j;
                tl->szLineBuffer[j] = '\0';
                if (*s == '\n')
                        j++;

                if (iLineCount < TEXT_BUFFER_SIZE)
                        iLineCount++;
                iLineNext = ++iLineNext & TEXT_BUFFER_MASK;
                s++;
        }

        //
        // Scroll display to always show last entries
        //
        if (fd != WRITE_FD_NOSCROLL) { // Skip if requested
                //
                // Set the vertical scrolling range and page size
                //
                if (yClientGlobal > 0 && yCharGlobal > 0) {
                        si.cbSize = sizeof(si);
                        si.fMask = SIF_RANGE | SIF_PAGE;
                        si.nMin = 0;
                        si.nMax = iLineCount - 1;
                        si.nPage = yClientGlobal / yCharGlobal;
                        SetScrollInfo(hWndPrimary, SB_VERT, &si, TRUE);
                        //
                        // Scroll text to bottom
                        //
                        SendMessage(hWndPrimary, WM_VSCROLL, MAKELONG(SB_BOTTOM, 0), 0L);
                }
        }
        InvalidateRect(hWndPrimary, NULL, TRUE); // Redraw client area
        return count;
}

//
// Alternate sendmmsg() to support standard udpst function call
//
int sendmmsg(int sockfd, struct mmsghdr *msgvec, unsigned int vlen, int flags) {
        unsigned int i;

#ifdef USE_TRANSMITPACKETS
        for (i = 0; i < vlen; i++) {
                tpe[i].dwElFlags = TP_ELEMENT_MEMORY | TP_ELEMENT_EOP;
                tpe[i].cLength = (ULONG) msgvec[i].msg_hdr.msg_iov->iov_len;
                tpe[i].pBuffer = (PVOID) msgvec[i].msg_hdr.msg_iov->iov_base;
        }
        if (!((LPFN_TRANSMITPACKETS) LpfnTransmitpackets)((SOCKET) sockfd, tpe, vlen, 0xFFFFFFF, NULL, 0)) {
                nCharCount = sprintf(scratch, "ERROR: TransmitPacket failure (%d)", WSAGetLastError());
                write_alt(-1, scratch, nCharCount);
                iSendBlocked++;
                return 0;
        }
#else
        for (i = 0; i < vlen; i++) {
                int var = send(sockfd, (const char *) msgvec[i].msg_hdr.msg_iov->iov_base, (int) msgvec[i].msg_hdr.msg_iov->iov_len, 0);
                if (var == SOCKET_ERROR) {
                        //
                        // If error is WSAEWOULDBLOCK then no data could be placed in send buffer, else a real error has been encountered
                        //
                        INT iWSAError;
                        if ((iWSAError = WSAGetLastError()) == WSAEWOULDBLOCK) {
                                iSendBlocked++;
                        } else {
                                nCharCount = sprintf(scratch, "ERROR: Send failure (%d)", iWSAError);
                                write_alt(-1, scratch, nCharCount);
                        }
                        return (int) i;
                }
        }
#endif
        return (int) vlen;
}

//
// Custom socket error handler for Windows
//
int socket_error(int connindex, int error, char *optext) {
        int var = 0, WSAError;

        if ((WSAError = WSAGetLastError()) != WSAEWOULDBLOCK) {
                var = sprintf(scratch, "[%d]%s ERROR: %d\n", connindex, optext, WSAError);
        }
        return var;
}

//
// Custom receive truncation handler for Windows
//
int receive_trunc(int error, int requested, int expected) {
        int var = 0, WSAError;

        if ((WSAError = WSAGetLastError()) == WSAEMSGSIZE && requested == expected) {
                var = requested;
        }
        return var;
}

//
// Redefined fcntl() call to invoke WSAAsyncSelect()
//
int fcntl(int fd, int cmd, ...) {
        if (cmd == F_SETFL) {
                if (WSAAsyncSelect(fd, hWndPrimary, WM_SOCKET, FD_READ | FD_WRITE) == SOCKET_ERROR) {
                        nCharCount = sprintf(scratch, "WSAAsyncSelect for F_SETFL failed (%d)", WSAGetLastError());
                        write_alt(-1, scratch, nCharCount);
                        return -1;
                }
        }
        return 0;
}

//
// Redefined random() call to invoke rand()
//
long int random(void) {
        int rvar;

        rvar = rand();
        rvar |= rvar << 16; // Copy value to upper half for 32 bits

        return rvar;
}

//
// Custom HMAC function for standard udpst authentication
//
#ifdef AUTH_KEY_ENABLE
unsigned char *HMAC(const char *EVP_MD, const void *key, int key_len, const unsigned char *d, size_t n,
        unsigned char *md, unsigned int *md_len) {

        BCRYPT_ALG_HANDLE hAlg = NULL;
        BCRYPT_HASH_HANDLE hHash = NULL;

        //
        // Proceed through stages of HMAC-SHA256 creation
        //
        nCharCount = 0;
        *md_len = AUTH_DIGEST_LENGTH;
        if (strcmp(EVP_MD, EVP_sha256())) {
                nCharCount = sprintf(scratch, "HMAC ERROR: Invalid EVP_MD specified");

        } else if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, NULL, BCRYPT_ALG_HANDLE_HMAC_FLAG))) {
                nCharCount = sprintf(scratch, "HMAC ERROR: BCryptOpenAlgorithmProvider failure");

        } else if (!BCRYPT_SUCCESS(BCryptCreateHash(hAlg, &hHash, NULL, 0, (PUCHAR) key, (ULONG) key_len, 0))) {
                nCharCount = sprintf(scratch, "HMAC ERROR: BCryptCreateHash failure");

        } else if (!BCRYPT_SUCCESS(BCryptHashData(hHash, (PUCHAR) d, (ULONG) n, 0))) {
                nCharCount = sprintf(scratch, "HMAC ERROR: BCryptHashData failure");

        } else if (!BCRYPT_SUCCESS(BCryptFinishHash(hHash, (PUCHAR) md, (ULONG) *md_len, 0))) {
                nCharCount = sprintf(scratch, "HMAC ERROR: BCryptFinishHash failure");
        }
        if (nCharCount > 0) {
                write_alt(-1, scratch, nCharCount);
        }

        //
        // Cleanup resources
        //
        if (hHash)
                BCryptDestroyHash(hHash);
        if (hAlg)
                BCryptCloseAlgorithmProvider(hAlg, 0);

        return md;
}

//
// Output individual authentication keys of length SHA256_KEY_LEN from derived key material.
//
// Return Values: 0 = Failure, 1 = Success
//
int kdf_hmac_sha256 (
        char *Kin,                      // Input key (null-terminated)
        uint32_t authUnixTime,          // Timestamp used as info
        unsigned char *cAuthKey,        // Output: 32-byte client key
        unsigned char *sAuthKey)        // Output: 32-byte server key
{
        BCRYPT_ALG_HANDLE hAlg = NULL;
        BCRYPT_HASH_HANDLE hHash = NULL;

        int status = 0;
        const DWORD kcount = 2;                                 // Key count (client and server)
        const DWORD L_bits = SHA256_KEY_LEN * kcount * 8;       // Bits of key material (512)
        const ULONG Kin_len = (ULONG) strlen(Kin);              // Null-terminated string

        //
        // Open HMAC-SHA256 algorithm
        //
        nCharCount = 0;
        if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, NULL, BCRYPT_ALG_HANDLE_HMAC_FLAG))) {
                nCharCount = sprintf(scratch, "KDF ERROR: BCryptOpenAlgorithmProvider failure");
        }
        if (nCharCount > 0) {
                write_alt(-1, scratch, nCharCount);
                if (hAlg)
                        BCryptCloseAlgorithmProvider(hAlg, 0);
                return status;
        }

        //
        // Build the fixed part: salt || 0x00 || info || [L]_32be
        //
        BYTE fixed[32];                 // Oversized buffer
        //
        const char salt[] = "UDPSTP";   // Constant for application
        const int salt_len = (int) strlen(salt);
        memcpy(fixed, salt, salt_len);  // Add salt
        //
        fixed[salt_len] = 0x00;         // Add separator
        //
        char info_str[16];              // Unix time is 10 digits
        const int info_len = snprintf(info_str, sizeof(info_str), "%u", authUnixTime);
        memcpy(fixed + salt_len + 1, info_str, info_len); // Add info
        //
        const BYTE L_be[4] = {          // Add bit count as 32-bit big-endian
                (BYTE)((L_bits >> 24) & 0xFF),
                (BYTE)((L_bits >> 16) & 0xFF),
                (BYTE)((L_bits >> 8) & 0xFF),
                (BYTE)(L_bits & 0xFF)
        };
        memcpy(fixed + salt_len + 1 + info_len, L_be, 4);       // Add [L]_32be
        //
        const DWORD fixed_len = salt_len + 1 + info_len + 4;    // Fixed length

        //
        // Buffer for derived key material
        //
        unsigned char derived[SHA256_KEY_LEN * kcount] = { 0 };

        //
        // Counter loop for key material
        //
        DWORD pos = 0;
        nCharCount = 0;
        for (DWORD i = 1; i <= kcount; i++) {
                BYTE input[32]; // Total input buffer (oversized)

                //
                // Counter as 32-bit big-endian
                //
                BYTE counter[4] = {
                        (BYTE)((i >> 24) & 0xFF),
                        (BYTE)((i >> 16) & 0xFF),
                        (BYTE)((i >> 8) & 0xFF),
                        (BYTE)(i & 0xFF)
                };
                memcpy(input, counter, 4);              // Add counter
                memcpy(input + 4, fixed, fixed_len);    // Add fixed part
                const ULONG input_len = fixed_len + 4;  // Total input length

                if (!BCRYPT_SUCCESS(BCryptCreateHash(hAlg, &hHash, NULL, 0, (PUCHAR) Kin, Kin_len, 0))) {
                        nCharCount = sprintf(scratch, "KDF ERROR: BCryptCreateHash failure");
                        break;
                }
                if (!BCRYPT_SUCCESS(BCryptHashData(hHash, (PUCHAR) input, input_len, 0))) {
                        nCharCount = sprintf(scratch, "KDF ERROR: BCryptHashData failure");
                        break;
                }
                if (!BCRYPT_SUCCESS(BCryptFinishHash(hHash, (PUCHAR) (derived + pos), SHA256_KEY_LEN, 0))) {
                        nCharCount = sprintf(scratch, "KDF ERROR: BCryptFinishHash failure");
                        break;
                }
                BCryptDestroyHash(hHash);
                hHash = NULL;

                pos += SHA256_KEY_LEN;
        }
        if (nCharCount > 0) {
                write_alt(-1, scratch, nCharCount);
        } else {
                status = 1;
        }

        //
        // Split key material into client and server keys
        //
        memcpy(cAuthKey, derived, SHA256_KEY_LEN);
        memcpy(sAuthKey, derived + SHA256_KEY_LEN, SHA256_KEY_LEN);

        //
        // Cleanup resources
        //
        if (hHash)
                BCryptDestroyHash(hHash);
        if (hAlg)
                BCryptCloseAlgorithmProvider(hAlg, 0);
        
        return status;
}
#endif

//
// Query the function pointer for the TransmitPacket function
//
#ifdef USE_TRANSMITPACKETS
int SetupTransmitPackets(SOCKET s) {
        int var;
        DWORD bytesReturned;
        GUID TransmitPacketsGuid = WSAID_TRANSMITPACKETS;

        var = WSAIoctl(s, SIO_GET_EXTENSION_FUNCTION_POINTER, &TransmitPacketsGuid, sizeof(GUID), &LpfnTransmitpackets,
                sizeof(PVOID), &bytesReturned, NULL, NULL);
        if (var == SOCKET_ERROR) {
                nCharCount = sprintf(scratch, "ERROR: TransmitPacket pointer query failure (%d)", WSAGetLastError());
                write_alt(-1, scratch, nCharCount);
                return -1;
        }
        return 0;
}
#endif

//
// Initiate testing as either a client (sending a setup request) or a server (awaiting setup requests)
//
void StartTest() {
        int i, var;
        iSendBlocked = 0;

#ifdef TIME_LIMITED_TESTING
        char month[8] = {0};
        struct tm t = {0};
        static const char month_names[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
        var = sscanf(__DATE__, "%s %d %d", month, &t.tm_mday, &t.tm_year);
        month[3] = '\0';
        t.tm_mon = (int) ((strstr(month_names, month) - month_names) / 3);
        t.tm_year -= 1900;
        t.tm_isdst = -1;
        if (repo.systemClock.tv_sec > (mktime(&t) + (30 * 86400))) { // Limit to ~30 days after build
                nCharCount = sprintf(scratch, "\nTesting with this version is no longer available!");
                write_alt(-1, scratch, nCharCount);
                return;
        }
#endif
        OutputBanner();

        if (repo.server[0].name != NULL) {
                if ((nCharCount = sock_mgmt(-1, repo.server[0].name, 0, repo.server[0].ip, SMA_LOOKUP)) != 0) {
                        write_alt(-1, scratch, nCharCount);
                        return;
                }
        }

        if (repo.isServer) {
                if ((i = new_conn(-1, repo.server[0].ip, repo.server[0].port, T_UDP, &recv_proc, &service_setupreq)) < 0) {
                        nCharCount = sprintf(scratch, "ERROR: Unable to open control port for setup requests");
                        write_alt(-1, scratch, nCharCount);
                        return;
                } else if (monConn >= 0) {
                        nCharCount = sprintf(scratch, "[%d]Awaiting setup requests on %s:%d\n", i, conn[i].locAddr, conn[i].locPort);
                        write_alt(-1, scratch, nCharCount);
                }
        } else {
                if ((i = new_conn(-1, NULL, 0, T_UDP, &recv_proc, &service_setupresp)) < 0) {
                        nCharCount = sprintf(scratch, "ERROR: Unable to create test connection");
                        write_alt(-1, scratch, nCharCount);
                        return;
                }
        }
#ifdef USE_TRANSMITPACKETS
        if (SetupTransmitPackets(conn[i].fd) < 0) {
                init_conn(i, TRUE);
                return;
        }
#endif

        if (!repo.isServer) {
                if (send_setupreq(i, 0, 0) < 0) {
                        nCharCount = sprintf(scratch, "ERROR: Unable to send setup request");
                        write_alt(-1, scratch, nCharCount);
                        init_conn(i, TRUE);
                        return;
                }
        }
        return;
}

//
// Display sending rate table
//
void ShowSendingRates() {
        OutputBanner();
        show_sending_rates(WRITE_FD_NOSCROLL);
        write_alt(-1, NULL, 0);
        return;
}

//
// Handle socket notifications
//
void OnAsyncSocket(WPARAM wParam, LPARAM lParam) {
        int i = -1;
        int pristatus, secstatus;

        if (wParam == 0 && lParam == 0)
                return;

        for (int j = 0; j <= repo.maxConnIndex; j++) {
                if (wParam == conn[j].fd) {
                        i = j;
                        break;
                }
        }
        if (i < 0)
                return;

        INT iError = WSAGETSELECTERROR(lParam);
        INT iEvent = WSAGETSELECTEVENT(lParam);
        if (iError) {
                init_conn(i, TRUE);
                nCharCount = sprintf(scratch, "OnAsyncSocket %d Error (%d)", iEvent, iError);
                write_alt(-1, scratch, nCharCount);
                return;
        }

        if (iEvent == FD_READ) {
                do {
                        secstatus = 0;
                        pristatus = (conn[i].priAction)(i);
                        if (pristatus > 0)
                                secstatus = (conn[i].secAction)(i);

                        if ((pristatus < 0) || (secstatus < 0)) {
                                init_conn(i, TRUE);
                        }
                } while (pristatus > 0 && secstatus == 0); // Process until empty
        }
        return;
}

//
// Query and display IP addresses on all network adapters
//
void GetIpAddresses(void) {
        IP_ADAPTER_ADDRESSES *adapter_addresses = NULL;
        IP_ADAPTER_ADDRESSES *adapter = NULL;
        ULONG adapter_addresses_bufsize = 16384; // Per Microsoft, recommended initial size is 15KB
        ULONG status = ERROR_SUCCESS;

        //
        // Obtain buffer and query adapter addresses (attempts are limited)
        //
        int i = 0;
        do {
                if ((adapter_addresses = (IP_ADAPTER_ADDRESSES *) HeapAlloc(GetProcessHeap(), 0, adapter_addresses_bufsize)) == NULL) {
                        nCharCount = sprintf(scratch, "ERROR: HeapAlloc failed while getting adapter addresses\n");
                        write_alt(-1, scratch, nCharCount);
                        return;
                }
                status = GetAdaptersAddresses(AF_UNSPEC,
                        GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER | GAA_FLAG_SKIP_FRIENDLY_NAME,
                        NULL, adapter_addresses, &adapter_addresses_bufsize);
                if (status != ERROR_BUFFER_OVERFLOW)
                        break;

                //
                // Free this buffer to try again if a larger one is needed (adapter_addresses_bufsize was updated)
                //
                HeapFree(GetProcessHeap(), 0, adapter_addresses);
                adapter_addresses = NULL;
        } while (++i < 3);

        //
        // Check status before proceeding
        //
        if (status != ERROR_SUCCESS) {
                nCharCount = sprintf(scratch, "ERROR: GetAdaptersAddresses failed (%d)\n", status);
                write_alt(-1, scratch, nCharCount);
                HeapFree(GetProcessHeap(), 0, adapter_addresses);
                return;
        }

        //
        // Step through returned adapters
        //
        for (adapter = adapter_addresses; NULL != adapter; adapter = adapter->Next) {
                //
                // Skip loopbacks
                //
                //if (adapter->IfType == IF_TYPE_SOFTWARE_LOOPBACK)
                //        continue;

                //
                // Output adapter header
                //
                nCharCount = sprintf(scratch, "\n%wS (IfIndex: %d)\n  %wS\n",
                        adapter->FriendlyName, adapter->IfIndex, adapter->Description);
                write_alt(WRITE_FD_NOSCROLL, scratch, nCharCount);

                //
                // Process addresses based on type
                //
                for (IP_ADAPTER_UNICAST_ADDRESS *address = adapter->FirstUnicastAddress; NULL != address; address = address->Next) {
                        auto family = address->Address.lpSockaddr->sa_family;
                        if (AF_INET == family) {
                                SOCKADDR_IN *ipv4 = reinterpret_cast<SOCKADDR_IN *>(address->Address.lpSockaddr);

                                char str_buffer[INET_ADDRSTRLEN] = {0};
                                inet_ntop(AF_INET, &(ipv4->sin_addr), str_buffer, INET_ADDRSTRLEN);
                                nCharCount = sprintf(scratch, "    %s\n", str_buffer);
                                write_alt(WRITE_FD_NOSCROLL, scratch, nCharCount);

                        } else if (AF_INET6 == family) {
                                SOCKADDR_IN6 *ipv6 = reinterpret_cast<SOCKADDR_IN6 *>(address->Address.lpSockaddr);

                                char str_buffer[INET6_ADDRSTRLEN] = {0};
                                inet_ntop(AF_INET6, &(ipv6->sin6_addr), str_buffer, INET6_ADDRSTRLEN);
                                nCharCount = sprintf(scratch, "    %s\n", str_buffer);
                                write_alt(WRITE_FD_NOSCROLL, scratch, nCharCount);
                        }
                }
        }
        write_alt(-1, NULL, 0);

        HeapFree(GetProcessHeap(), 0, adapter_addresses);
        return;
}
