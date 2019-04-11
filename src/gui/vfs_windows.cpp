/*
* Copyright(C) 2018 by AMCO
* Copyright(C) 2018 by Jonathan Ponciano <jponcianovera@ciencias.unam.mx>
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
*(at your option) any later version.
*
* This program is distributed in the hope that it will be useful, but
* WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
* or FITNESS FOR A PARTICULAR PURPOSE.See the GNU General Public License
* for more details.
*/

/*
Dokan : user-mode file system library for Windows

Copyright (C) 2015 - 2018 Adrien J. <liryna.stark@gmail.com> and Maxime C. <maxime@islog.com>
Copyright (C) 2007 - 2011 Hiroki Asakawa <info@dokan-dev.net>

http://dokan-dev.github.io

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
*/


#include <QDir>
#include <QDebug>
#include "configfile.h"
#include <thread>

#include <errno.h>
#include <stddef.h>		/* _threadid variable */
#include <process.h>	/* _beginthread, _endthread */
#include <time.h>		/* time, _ctime */

#include "vfs_windows.h"
#include "common/asserts.h"
#include "common/syncjournaldb.h"
#include <qmutex.h>

#include <thread>
#include <tlhelp32.h>
#include <vector>
#include <QFileInfo>

#include <QMessageBox>
#include <QStandardPaths>
#include <QApplication>

#include <shlobj_core.h>

namespace OCC {

	QMutex _mutexMirrorFindFiles;

	VfsWindows* VfsWindows::_instance = 0;
	static DWORD explorer_process_pid = 0;
	static int i_deleted = 0;

//#define WIN10_ENABLE_LONG_PATH
#ifdef WIN10_ENABLE_LONG_PATH
//dirty but should be enough
#define DOKAN_MAX_PATH 32768
#else
#define DOKAN_MAX_PATH MAX_PATH
#endif // DEBUG

BOOL g_UseStdErr;
BOOL g_DebugMode;
BOOL g_HasSeSecurityPrivilege;
BOOL g_ImpersonateCallerUser;

static QString transformPath(QString oldPath) {
	QString newPath = oldPath.replace(0, 1, QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/cachedFiles/");
	return newPath;
}

static void DbgPrint(LPCWSTR format, ...) {
	if (g_DebugMode) {
		const WCHAR *outputString;
		WCHAR *buffer = NULL;
		size_t length;
		va_list argp;

		va_start(argp, format);
		length = _vscwprintf(format, argp) + 1;
		buffer = (WCHAR*)_malloca(length * sizeof(WCHAR));
		if (buffer) {
			vswprintf_s(buffer, length, format, argp);
			outputString = buffer;
		}
		else {
			outputString = format;
		}
		if (g_UseStdErr)
			fputws(outputString, stderr);
		else
			OutputDebugStringW(outputString);
		if (buffer)
			_freea(buffer);
		va_end(argp);
		if (g_UseStdErr)
			fflush(stderr);
	}
}

static WCHAR RootDirectory[DOKAN_MAX_PATH] = L"C:";
static WCHAR MountPoint[DOKAN_MAX_PATH] = L"M:\\";
static WCHAR UNCName[DOKAN_MAX_PATH] = L"";

static void GetFilePath(PWCHAR filePath, ULONG numberOfElements,
	LPCWSTR FileName) {
	wcsncpy_s(filePath, numberOfElements, RootDirectory, wcslen(RootDirectory));
	size_t unclen = wcslen(UNCName);
	if (unclen > 0 && _wcsnicmp(FileName, UNCName, unclen) == 0) {
		if (_wcsnicmp(FileName + unclen, L".", 1) != 0) {
			wcsncat_s(filePath, numberOfElements, FileName + unclen,
				wcslen(FileName) - unclen);
		}
	}
	else {
		wcsncat_s(filePath, numberOfElements, FileName, wcslen(FileName));
	}
}

static void PrintUserName(PDOKAN_FILE_INFO DokanFileInfo) {
	HANDLE handle;
	UCHAR buffer[1024];
	DWORD returnLength;
	WCHAR accountName[256];
	WCHAR domainName[256];
	DWORD accountLength = sizeof(accountName) / sizeof(WCHAR);
	DWORD domainLength = sizeof(domainName) / sizeof(WCHAR);
	PTOKEN_USER tokenUser;
	SID_NAME_USE snu;

	if (!g_DebugMode)
		return;

	handle = DokanOpenRequestorToken(DokanFileInfo);
	if (handle == INVALID_HANDLE_VALUE) {
		DbgPrint(L"  DokanOpenRequestorToken failed\n");
		return;
	}

	if (!GetTokenInformation(handle, TokenUser, buffer, sizeof(buffer),
		&returnLength)) {
		DbgPrint(L"  GetTokenInformaiton failed: %d\n", GetLastError());
		CloseHandle(handle);
		return;
	}

	CloseHandle(handle);

	tokenUser = (PTOKEN_USER)buffer;
	if (!LookupAccountSid(NULL, tokenUser->User.Sid, accountName, &accountLength,
		domainName, &domainLength, &snu)) {
		DbgPrint(L"  LookupAccountSid failed: %d\n", GetLastError());
		return;
	}

	DbgPrint(L"  AccountName: %s, DomainName: %s\n", accountName, domainName);
}

static DWORD getExplorerID()
{
    if (explorer_process_pid > 0) {
        return explorer_process_pid;
    } else {
        std::wstring ExplorerProcessName = L"explorer.exe";

        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);

        PROCESSENTRY32W entry;
        entry.dwSize = sizeof entry;

        if (!Process32FirstW(snap, &entry)) {
            return 0;
        }

        do {
            if (std::wstring(entry.szExeFile) == ExplorerProcessName) {
                explorer_process_pid = entry.th32ProcessID;
            }
        } while (Process32NextW(snap, &entry));

        return explorer_process_pid;
        /*for (int i(0); i < pids.size(); ++i) {
        std::cout << pids[i] << std::endl;
        }*/
    }
}

static void determinesTypeOfOperation(LPCWSTR FileName, PDOKAN_FILE_INFO DokanFileInfo, DWORD DesiredAccess)
{

QString QSFileName;
#ifdef UNICODE
    QSFileName = QString::fromWCharArray(FileName);
#else
    QSFileName = QString::fromLocal8Bit(FileName);
#endif
	QSFileName.replace("\\","/");

    int da = (int)DesiredAccess;
    if (da == 1048576 || da == 128 || da == 1048704 || da == 131200 || da == 1048577 || da == 1179785 || da == 1048705) 
        return;

	//if(DokanFileInfo->ProcessId != ((ULONG)GetCurrentProcess()))
	if (DokanFileInfo->ProcessId == getExplorerID())
	{
        QVariantMap error;

		if (da == 1179776)		//< OpenFile
		{
			QSFileName.replace(0, 1, QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/cachedFiles/");
			VfsWindows::instance()->openFileAtPath(QSFileName, error);
		}
		else if (da == 65536)	//< DeleteFile
		{
			QSFileName.replace(0, 1, QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/cachedFiles/");
			VfsWindows::instance()->deleteFileAtPath(QSFileName, error);
		}
	}
}

static void TypeOfOperation_DeleteDirectory(LPCWSTR FileName, PDOKAN_FILE_INFO DokanFileInfo)
{
QString QSFileName;
#ifdef UNICODE
    QSFileName = QString::fromWCharArray(FileName);
#else
    QSFileName = QString::fromLocal8Bit(FileName);
#endif
	QSFileName.replace("\\", "/");

    QVariantMap error;
	
    if (i_deleted == 0) {
        i_deleted++;
        VfsWindows::instance()->startDeleteDirectoryAtPath(QSFileName, error);
    } else {
        VfsWindows::instance()->endDeleteDirectoryAtPath(QSFileName, error);
        i_deleted = 0;
    }
}

static BOOL AddSeSecurityNamePrivilege() {
	HANDLE token = 0;
	DbgPrint(
		L"## Attempting to add SE_SECURITY_NAME privilege to process token ##\n");
	DWORD err;
	LUID luid;
	if (!LookupPrivilegeValue(0, SE_SECURITY_NAME, &luid)) {
		err = GetLastError();
		if (err != ERROR_SUCCESS) {
			DbgPrint(L"  failed: Unable to lookup privilege value. error = %u\n",
				err);
			return FALSE;
		}
	}

	LUID_AND_ATTRIBUTES attr;
	attr.Attributes = SE_PRIVILEGE_ENABLED;
	attr.Luid = luid;

	TOKEN_PRIVILEGES priv;
	priv.PrivilegeCount = 1;
	priv.Privileges[0] = attr;

	if (!OpenProcessToken(GetCurrentProcess(),
		TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token)) {
		err = GetLastError();
		if (err != ERROR_SUCCESS) {
			DbgPrint(L"  failed: Unable obtain process token. error = %u\n", err);
			return FALSE;
		}
	}

	TOKEN_PRIVILEGES oldPriv;
	DWORD retSize;
	AdjustTokenPrivileges(token, FALSE, &priv, sizeof(TOKEN_PRIVILEGES), &oldPriv,
		&retSize);
	err = GetLastError();
	if (err != ERROR_SUCCESS) {
		DbgPrint(L"  failed: Unable to adjust token privileges: %u\n", err);
		CloseHandle(token);
		return FALSE;
	}

	BOOL privAlreadyPresent = FALSE;
	for (unsigned int i = 0; i < oldPriv.PrivilegeCount; i++) {
		if (oldPriv.Privileges[i].Luid.HighPart == luid.HighPart &&
			oldPriv.Privileges[i].Luid.LowPart == luid.LowPart) {
			privAlreadyPresent = TRUE;
			break;
		}
	}
	DbgPrint(privAlreadyPresent ? L"  success: privilege already present\n"
		: L"  success: privilege added\n");
	if (token)
		CloseHandle(token);
	return TRUE;
}

#define MirrorCheckFlag(val, flag)                                             \
  if (val & flag) {                                                            \
    DbgPrint(L"\t" L#flag L"\n");                                              \
  }

static NTSTATUS DOKAN_CALLBACK
MirrorCreateFile(LPCWSTR FileName, PDOKAN_IO_SECURITY_CONTEXT SecurityContext,
	ACCESS_MASK DesiredAccess, ULONG FileAttributes,
	ULONG ShareAccess, ULONG CreateDisposition,
	ULONG CreateOptions, PDOKAN_FILE_INFO DokanFileInfo) {

	determinesTypeOfOperation(FileName, DokanFileInfo, DesiredAccess);

	//< Capture CreateFile Virtual File System Operation

	QString QSFileName;
	#ifdef UNICODE
		QSFileName = QString::fromWCharArray(FileName);
	#else
		QSFileName = QString::fromLocal8Bit(FileName);
	#endif

	WCHAR filePath[DOKAN_MAX_PATH];
	HANDLE handle;
	DWORD fileAttr;
	NTSTATUS status = STATUS_SUCCESS;
	DWORD creationDisposition;
	DWORD fileAttributesAndFlags;
	DWORD error = 0;
	SECURITY_ATTRIBUTES securityAttrib;
	ACCESS_MASK genericDesiredAccess;
	// userTokenHandle is for Impersonate Caller User Option
	HANDLE userTokenHandle = INVALID_HANDLE_VALUE;

	securityAttrib.nLength = sizeof(securityAttrib);
	securityAttrib.lpSecurityDescriptor =
		SecurityContext->AccessState.SecurityDescriptor;
	securityAttrib.bInheritHandle = FALSE;

	DokanMapKernelToUserCreateFileFlags(
		DesiredAccess, FileAttributes, CreateOptions, CreateDisposition,
		&genericDesiredAccess, &fileAttributesAndFlags, &creationDisposition);

	GetFilePath(filePath, DOKAN_MAX_PATH, FileName);

	DbgPrint(L"CreateFile : %s\n", filePath);

	PrintUserName(DokanFileInfo);

	/*
	if (ShareMode == 0 && AccessMode & FILE_WRITE_DATA)
	ShareMode = FILE_SHARE_WRITE;
	else if (ShareMode == 0)
	ShareMode = FILE_SHARE_READ;
	*/

	DbgPrint(L"\tShareMode = 0x%x\n", ShareAccess);

	MirrorCheckFlag(ShareAccess, FILE_SHARE_READ);
	MirrorCheckFlag(ShareAccess, FILE_SHARE_WRITE);
	MirrorCheckFlag(ShareAccess, FILE_SHARE_DELETE);

	DbgPrint(L"\tDesiredAccess = 0x%x\n", DesiredAccess);

	MirrorCheckFlag(DesiredAccess, GENERIC_READ);
	MirrorCheckFlag(DesiredAccess, GENERIC_WRITE);
	MirrorCheckFlag(DesiredAccess, GENERIC_EXECUTE);

	MirrorCheckFlag(DesiredAccess, DELETE);
	MirrorCheckFlag(DesiredAccess, FILE_READ_DATA);
	MirrorCheckFlag(DesiredAccess, FILE_READ_ATTRIBUTES);
	MirrorCheckFlag(DesiredAccess, FILE_READ_EA);
	MirrorCheckFlag(DesiredAccess, READ_CONTROL);
	MirrorCheckFlag(DesiredAccess, FILE_WRITE_DATA);
	MirrorCheckFlag(DesiredAccess, FILE_WRITE_ATTRIBUTES);
	MirrorCheckFlag(DesiredAccess, FILE_WRITE_EA);
	MirrorCheckFlag(DesiredAccess, FILE_APPEND_DATA);
	MirrorCheckFlag(DesiredAccess, WRITE_DAC);
	MirrorCheckFlag(DesiredAccess, WRITE_OWNER);
	MirrorCheckFlag(DesiredAccess, SYNCHRONIZE);
	MirrorCheckFlag(DesiredAccess, FILE_EXECUTE);
	MirrorCheckFlag(DesiredAccess, STANDARD_RIGHTS_READ);
	MirrorCheckFlag(DesiredAccess, STANDARD_RIGHTS_WRITE);
	MirrorCheckFlag(DesiredAccess, STANDARD_RIGHTS_EXECUTE);

	// When filePath is a directory, needs to change the flag so that the file can
	// be opened.
	fileAttr = GetFileAttributes(filePath);

	if (fileAttr != INVALID_FILE_ATTRIBUTES
		&& fileAttr & FILE_ATTRIBUTE_DIRECTORY) {
		if (!(CreateOptions & FILE_NON_DIRECTORY_FILE)) {
			DokanFileInfo->IsDirectory = TRUE;
			// Needed by FindFirstFile to list files in it
			// TODO: use ReOpenFile in MirrorFindFiles to set share read temporary
			ShareAccess |= FILE_SHARE_READ;
		}
		else { // FILE_NON_DIRECTORY_FILE - Cannot open a dir as a file
			DbgPrint(L"\tCannot open a dir as a file\n");
			return STATUS_FILE_IS_A_DIRECTORY;
		}
	}

	DbgPrint(L"\tFlagsAndAttributes = 0x%x\n", fileAttributesAndFlags);

	MirrorCheckFlag(fileAttributesAndFlags, FILE_ATTRIBUTE_ARCHIVE);
	MirrorCheckFlag(fileAttributesAndFlags, FILE_ATTRIBUTE_COMPRESSED);
	MirrorCheckFlag(fileAttributesAndFlags, FILE_ATTRIBUTE_DEVICE);
	MirrorCheckFlag(fileAttributesAndFlags, FILE_ATTRIBUTE_DIRECTORY);
	MirrorCheckFlag(fileAttributesAndFlags, FILE_ATTRIBUTE_ENCRYPTED);
	MirrorCheckFlag(fileAttributesAndFlags, FILE_ATTRIBUTE_HIDDEN);
	MirrorCheckFlag(fileAttributesAndFlags, FILE_ATTRIBUTE_INTEGRITY_STREAM);
	MirrorCheckFlag(fileAttributesAndFlags, FILE_ATTRIBUTE_NORMAL);
	MirrorCheckFlag(fileAttributesAndFlags, FILE_ATTRIBUTE_NOT_CONTENT_INDEXED);
	MirrorCheckFlag(fileAttributesAndFlags, FILE_ATTRIBUTE_NO_SCRUB_DATA);
	MirrorCheckFlag(fileAttributesAndFlags, FILE_ATTRIBUTE_OFFLINE);
	MirrorCheckFlag(fileAttributesAndFlags, FILE_ATTRIBUTE_READONLY);
	MirrorCheckFlag(fileAttributesAndFlags, FILE_ATTRIBUTE_REPARSE_POINT);
	MirrorCheckFlag(fileAttributesAndFlags, FILE_ATTRIBUTE_SPARSE_FILE);
	MirrorCheckFlag(fileAttributesAndFlags, FILE_ATTRIBUTE_SYSTEM);
	MirrorCheckFlag(fileAttributesAndFlags, FILE_ATTRIBUTE_TEMPORARY);
	MirrorCheckFlag(fileAttributesAndFlags, FILE_ATTRIBUTE_VIRTUAL);
	MirrorCheckFlag(fileAttributesAndFlags, FILE_FLAG_WRITE_THROUGH);
	MirrorCheckFlag(fileAttributesAndFlags, FILE_FLAG_OVERLAPPED);
	MirrorCheckFlag(fileAttributesAndFlags, FILE_FLAG_NO_BUFFERING);
	MirrorCheckFlag(fileAttributesAndFlags, FILE_FLAG_RANDOM_ACCESS);
	MirrorCheckFlag(fileAttributesAndFlags, FILE_FLAG_SEQUENTIAL_SCAN);
	MirrorCheckFlag(fileAttributesAndFlags, FILE_FLAG_DELETE_ON_CLOSE);
	MirrorCheckFlag(fileAttributesAndFlags, FILE_FLAG_BACKUP_SEMANTICS);
	MirrorCheckFlag(fileAttributesAndFlags, FILE_FLAG_POSIX_SEMANTICS);
	MirrorCheckFlag(fileAttributesAndFlags, FILE_FLAG_OPEN_REPARSE_POINT);
	MirrorCheckFlag(fileAttributesAndFlags, FILE_FLAG_OPEN_NO_RECALL);
	MirrorCheckFlag(fileAttributesAndFlags, SECURITY_ANONYMOUS);
	MirrorCheckFlag(fileAttributesAndFlags, SECURITY_IDENTIFICATION);
	MirrorCheckFlag(fileAttributesAndFlags, SECURITY_IMPERSONATION);
	MirrorCheckFlag(fileAttributesAndFlags, SECURITY_DELEGATION);
	MirrorCheckFlag(fileAttributesAndFlags, SECURITY_CONTEXT_TRACKING);
	MirrorCheckFlag(fileAttributesAndFlags, SECURITY_EFFECTIVE_ONLY);
	MirrorCheckFlag(fileAttributesAndFlags, SECURITY_SQOS_PRESENT);

	if (creationDisposition == CREATE_NEW) {
		DbgPrint(L"\tCREATE_NEW\n");
		/*QSFileName.replace(0, 1, QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/cachedFiles/");
		QVariantMap error;
		VfsWindows::instance()->openFileAtPath(QSFileName, error);*/
	}
	else if (creationDisposition == OPEN_ALWAYS) {
		DbgPrint(L"\tOPEN_ALWAYS\n");
	}
	else if (creationDisposition == CREATE_ALWAYS) {
		DbgPrint(L"\tCREATE_ALWAYS\n");
	}
	else if (creationDisposition == OPEN_EXISTING) {
		DbgPrint(L"\tOPEN_EXISTING\n");
		/*QSFileName.replace(0, 1, QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/cachedFiles/");
		QVariantMap error;
		VfsWindows::instance()->openFileAtPath(QSFileName, error);*/
	}
	else if (creationDisposition == TRUNCATE_EXISTING) {
		DbgPrint(L"\tTRUNCATE_EXISTING\n");
	}
	else {
		DbgPrint(L"\tUNKNOWN creationDisposition!\n");
	}

	if (g_ImpersonateCallerUser) {
		userTokenHandle = DokanOpenRequestorToken(DokanFileInfo);

		if (userTokenHandle == INVALID_HANDLE_VALUE) {
			DbgPrint(L"  DokanOpenRequestorToken failed\n");
			// Should we return some error?
		}
	}

	if (DokanFileInfo->IsDirectory) {
		// It is a create directory request

		if (creationDisposition == CREATE_NEW ||
			creationDisposition == OPEN_ALWAYS) {

			if (g_ImpersonateCallerUser && userTokenHandle != INVALID_HANDLE_VALUE) {
				// if g_ImpersonateCallerUser option is on, call the ImpersonateLoggedOnUser function.
				if (!ImpersonateLoggedOnUser(userTokenHandle)) {
					// handle the error if failed to impersonate
					DbgPrint(L"\tImpersonateLoggedOnUser failed.\n");
				}
			}

			//We create folder
			if (!CreateDirectory(filePath, &securityAttrib)) {
				error = GetLastError();
				// Fail to create folder for OPEN_ALWAYS is not an error
				if (error != ERROR_ALREADY_EXISTS ||
					creationDisposition == CREATE_NEW) {
					DbgPrint(L"\terror code = %d\n\n", error);
					status = DokanNtStatusFromWin32(error);
				}
			}

			if (g_ImpersonateCallerUser && userTokenHandle != INVALID_HANDLE_VALUE) {
				// Clean Up operation for impersonate
				DWORD lastError = GetLastError();
				if (status != STATUS_SUCCESS) //Keep the handle open for CreateFile
					CloseHandle(userTokenHandle);
				RevertToSelf();
				SetLastError(lastError);
			}
		}

		if (status == STATUS_SUCCESS) {

			//Check first if we're trying to open a file as a directory.
			if (fileAttr != INVALID_FILE_ATTRIBUTES &&
				!(fileAttr & FILE_ATTRIBUTE_DIRECTORY) &&
				(CreateOptions & FILE_DIRECTORY_FILE)) {
				return STATUS_NOT_A_DIRECTORY;
			}

			if (g_ImpersonateCallerUser && userTokenHandle != INVALID_HANDLE_VALUE) {
				// if g_ImpersonateCallerUser option is on, call the ImpersonateLoggedOnUser function.
				if (!ImpersonateLoggedOnUser(userTokenHandle)) {
					// handle the error if failed to impersonate
					DbgPrint(L"\tImpersonateLoggedOnUser failed.\n");
				}
			}

			// FILE_FLAG_BACKUP_SEMANTICS is required for opening directory handles
			handle =
				CreateFile(filePath, genericDesiredAccess, ShareAccess,
					&securityAttrib, OPEN_EXISTING,
					fileAttributesAndFlags | FILE_FLAG_BACKUP_SEMANTICS, NULL);

			if (g_ImpersonateCallerUser && userTokenHandle != INVALID_HANDLE_VALUE) {
				// Clean Up operation for impersonate
				DWORD lastError = GetLastError();
				CloseHandle(userTokenHandle);
				RevertToSelf();
				SetLastError(lastError);
			}

			if (handle == INVALID_HANDLE_VALUE) {
				error = GetLastError();
				DbgPrint(L"\terror code = %d\n\n", error);

				status = DokanNtStatusFromWin32(error);
			}
			else {
				DokanFileInfo->Context =
					(ULONG64)handle; // save the file handle in Context

									 // Open succeed but we need to inform the driver
									 // that the dir open and not created by returning STATUS_OBJECT_NAME_COLLISION
				if (creationDisposition == OPEN_ALWAYS &&
					fileAttr != INVALID_FILE_ATTRIBUTES)
					return STATUS_OBJECT_NAME_COLLISION;
			}
		}

		if ((status == STATUS_SUCCESS) && (QSFileName.compare("\\") != 0) && (DokanFileInfo->ProcessId != ((ULONG)GetCurrentProcess())))
		{
			if (creationDisposition == CREATE_NEW) 
					{
						//DbgPrint(L"\tFCREATE_NEW\n");
						QSFileName.replace(0, 1, QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/cachedFiles/");
						qDebug() << Q_FUNC_INFO << " Prepare CREATE_NEW D Explorer via: " << QSFileName;
						QVariantMap error;
						VfsWindows::instance()->createDirectoryAtPath(QSFileName, error);
					}
		}
	}
	else {
		// It is a create file request

		// Cannot overwrite a hidden or system file if flag not set
		if (fileAttr != INVALID_FILE_ATTRIBUTES &&
			((!(fileAttributesAndFlags & FILE_ATTRIBUTE_HIDDEN) &&
			(fileAttr & FILE_ATTRIBUTE_HIDDEN)) ||
				(!(fileAttributesAndFlags & FILE_ATTRIBUTE_SYSTEM) &&
				(fileAttr & FILE_ATTRIBUTE_SYSTEM))) &&
					(creationDisposition == TRUNCATE_EXISTING ||
						creationDisposition == CREATE_ALWAYS))
			return STATUS_ACCESS_DENIED;

		// Cannot delete a read only file
		if ((fileAttr != INVALID_FILE_ATTRIBUTES &&
			(fileAttr & FILE_ATTRIBUTE_READONLY) ||
			(fileAttributesAndFlags & FILE_ATTRIBUTE_READONLY)) &&
			(fileAttributesAndFlags & FILE_FLAG_DELETE_ON_CLOSE))
			return STATUS_CANNOT_DELETE;

		// Truncate should always be used with write access
		if (creationDisposition == TRUNCATE_EXISTING)
			genericDesiredAccess |= GENERIC_WRITE;

		if (g_ImpersonateCallerUser && userTokenHandle != INVALID_HANDLE_VALUE) {
			// if g_ImpersonateCallerUser option is on, call the ImpersonateLoggedOnUser function.
			if (!ImpersonateLoggedOnUser(userTokenHandle)) {
				// handle the error if failed to impersonate
				DbgPrint(L"\tImpersonateLoggedOnUser failed.\n");
			}
		}

		handle = CreateFile(
			filePath,
			genericDesiredAccess, // GENERIC_READ|GENERIC_WRITE|GENERIC_EXECUTE,
			ShareAccess,
			&securityAttrib, // security attribute
			creationDisposition,
			fileAttributesAndFlags, // |FILE_FLAG_NO_BUFFERING,
			NULL);                  // template file handle

		if (g_ImpersonateCallerUser && userTokenHandle != INVALID_HANDLE_VALUE) {
			// Clean Up operation for impersonate
			DWORD lastError = GetLastError();
			CloseHandle(userTokenHandle);
			RevertToSelf();
			SetLastError(lastError);
		}

		if (handle == INVALID_HANDLE_VALUE) {
			error = GetLastError();
			DbgPrint(L"\terror code = %d\n\n", error);

			status = DokanNtStatusFromWin32(error);
		}
		else {

			//Need to update FileAttributes with previous when Overwrite file
			if (fileAttr != INVALID_FILE_ATTRIBUTES &&
				creationDisposition == TRUNCATE_EXISTING) {
				SetFileAttributes(filePath, fileAttributesAndFlags | fileAttr);
			}

			DokanFileInfo->Context =
				(ULONG64)handle; // save the file handle in Context

			if (creationDisposition == OPEN_ALWAYS ||
				creationDisposition == CREATE_ALWAYS) {
				error = GetLastError();
				if (error == ERROR_ALREADY_EXISTS) {
					DbgPrint(L"\tOpen an already existing file\n");
					// Open succeed but we need to inform the driver
					// that the file open and not created by returning STATUS_OBJECT_NAME_COLLISION
					status = STATUS_OBJECT_NAME_COLLISION;
				}
			}

//////////////////////////////////////////////////////////////////////////////////////////////////////
			if ((status == STATUS_SUCCESS) && (QSFileName.compare("\\") != 0) && (DokanFileInfo->ProcessId != ((ULONG)GetCurrentProcess()) ))
			{
				if (creationDisposition == CREATE_NEW) {
					//DbgPrint(L"\tFCREATE_NEW\n");
					QSFileName.replace(0, 1, QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/cachedFiles/");
					qDebug() << Q_FUNC_INFO << " Prepare CREATE_NEW F Explorer via: " << QSFileName;
					QVariantMap error;
					VfsWindows::instance()->createFileAtPath(QSFileName, error);
				}
				else if (creationDisposition == OPEN_ALWAYS) {
					DbgPrint(L"\tFOPEN_ALWAYS\n");
				}
				else if (creationDisposition == CREATE_ALWAYS) {
					DbgPrint(L"\tFCREATE_ALWAYS\n");
					if (error == 0 && creationDisposition == 2 && DesiredAccess == 1507743)
					{
						QSFileName.replace(0, 1, QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/cachedFiles/");
						qDebug() << Q_FUNC_INFO << " 2 Prepare CREATE_NEW F CMD-copy via: " << QSFileName;
						QVariantMap error;
						VfsWindows::instance()->createFileAtPath(QSFileName, error);
					}

					qDebug() << Q_FUNC_INFO << " OPEN_ALWAYS FileName: " << QSFileName << " error: " << error << " creationDisposition: " << creationDisposition << " DesiredAccess: " << DesiredAccess;
				}
				else if (creationDisposition == OPEN_EXISTING) {
					DbgPrint(L"\tFOPEN_EXISTING\n");
					qDebug() << Q_FUNC_INFO << " OPEN_EXISTING FileName: " << QSFileName << " error: " << error << " creationDisposition: " << creationDisposition << " DesiredAccess: " << DesiredAccess;
				}
				else if (creationDisposition == TRUNCATE_EXISTING) {
					DbgPrint(L"\tFTRUNCATE_EXISTING\n");
					qDebug() << Q_FUNC_INFO << " TRUNCATE_EXISTING FileName: " << QSFileName << " error: " << error << " creationDisposition: " << creationDisposition << " DesiredAccess: " << DesiredAccess;
				}
				else {
					DbgPrint(L"\tFUNKNOWN creationDisposition!\n");
					qDebug() << Q_FUNC_INFO << " UNKNOWN creationDisposition! FileName: " << QSFileName << " error: " << error << " creationDisposition: " << creationDisposition << " DesiredAccess: " << DesiredAccess;
				}
			}
//////////////////////////////////////////////////////////////////////////////////////////////////////


		}
	}
	
	DbgPrint(L"\n");

	/*
	//////////////////////////////////////////////////////////////////////////////////////////////////////
	if ((status == STATUS_SUCCESS) && (QSFileName.compare("\\") != 0) && (DokanFileInfo->ProcessId != ((ULONG)GetCurrentProcess())))
	{
		if (creationDisposition == CREATE_NEW) {
			//DbgPrint(L"\tFCREATE_NEW\n");
			qDebug() << Q_FUNC_INFO << " Prepare CREATE_NEW Explorer via: " << QSFileName;
			QSFileName.replace(0, 1, QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/cachedFiles/");
			QVariantMap error;
			if (DokanFileInfo->IsDirectory)
				VfsWindows::instance()->createDirectoryAtPath(QSFileName, error);
			else
				VfsWindows::instance()->createFileAtPath(QSFileName, error);
		}
		else if (creationDisposition == OPEN_ALWAYS) {
			DbgPrint(L"\tFOPEN_ALWAYS\n");
		}
		else if (creationDisposition == CREATE_ALWAYS) {
			DbgPrint(L"\tFCREATE_ALWAYS\n");
			if (error == 0 && creationDisposition == 2 && DesiredAccess == 1507743)
			{
				qDebug() << Q_FUNC_INFO << " 2 Prepare CREATE_NEW CMD-copy via: " << QSFileName;
				QSFileName.replace(0, 1, QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/cachedFiles/");
				QVariantMap error;
				if (DokanFileInfo->IsDirectory)
					VfsWindows::instance()->createDirectoryAtPath(QSFileName, error);
				else
					VfsWindows::instance()->createFileAtPath(QSFileName, error);

				//VfsWindows::instance()->createFileAtPath(QSFileName, error);
			}

			qDebug() << Q_FUNC_INFO << " OPEN_ALWAYS FileName: " << QSFileName << " error: " << error << " creationDisposition: " << creationDisposition << " DesiredAccess: " << DesiredAccess;
		}
		else if (creationDisposition == OPEN_EXISTING) {
			DbgPrint(L"\tFOPEN_EXISTING\n");
			qDebug() << Q_FUNC_INFO << " OPEN_EXISTING FileName: " << QSFileName << " error: " << error << " creationDisposition: " << creationDisposition << " DesiredAccess: " << DesiredAccess;

		}
		else if (creationDisposition == TRUNCATE_EXISTING) {
			DbgPrint(L"\tFTRUNCATE_EXISTING\n");
			qDebug() << Q_FUNC_INFO << " TRUNCATE_EXISTING FileName: " << QSFileName << " error: " << error << " creationDisposition: " << creationDisposition << " DesiredAccess: " << DesiredAccess;
		}
		else {
			DbgPrint(L"\tFUNKNOWN creationDisposition!\n");
			qDebug() << Q_FUNC_INFO << " UNKNOWN creationDisposition! FileName: " << QSFileName << " error: " << error << " creationDisposition: " << creationDisposition << " DesiredAccess: " << DesiredAccess;
		}
	}
	//////////////////////////////////////////////////////////////////////////////////////////////////////
	*/

	return status;
}

#pragma warning(push)
#pragma warning(disable : 4305)

static void DOKAN_CALLBACK MirrorCloseFile(LPCWSTR FileName,
	PDOKAN_FILE_INFO DokanFileInfo) {

	//< Capture CloseFile Virtual File System Operation

	QString QSFileName;
#ifdef UNICODE
	QSFileName = QString::fromWCharArray(FileName);
#else
	QSFileName = QString::fromLocal8Bit(FileName);
#endif

	/*VfsWindows *m_VfsWindows = NULL;
	m_VfsWindows = VfsWindows::instance();
	if (m_VfsWindows)
	{
		if (QSFileName.compare("\\") != 0)
		{
			//m_VfsWindows->getOperationCloseFile(QSFileName, QString("MirrorCloseFile"), QString("InProcess..."));
		}
	}
	else
	{
	//lockerMirrorCloseFile.unlock();
	return;	
	}
	//lockerMirrorCloseFile.unlock();
	*/

	WCHAR filePath[DOKAN_MAX_PATH];
	GetFilePath(filePath, DOKAN_MAX_PATH, FileName);

	if (DokanFileInfo->Context) {
		DbgPrint(L"CloseFile: %s\n", filePath);
		DbgPrint(L"\terror : not cleanuped file\n\n");
		CloseHandle((HANDLE)DokanFileInfo->Context);
		DokanFileInfo->Context = 0;
	}
	else {
		DbgPrint(L"Close: %s\n\n", filePath);
	}
}

static void DOKAN_CALLBACK MirrorCleanup(LPCWSTR FileName,
	PDOKAN_FILE_INFO DokanFileInfo) {

	//< Capture Cleanup Virtual File System Operation

	QString QSFileName;
#ifdef UNICODE
	QSFileName = QString::fromWCharArray(FileName);
#else
	QSFileName = QString::fromLocal8Bit(FileName);
#endif
	
	/*
	//QMutexLocker lockerMirrorCleanup(&_mutexMirrorCleanup);
	VfsWindows *m_VfsWindows = NULL;
	m_VfsWindows = VfsWindows::instance();
	if (m_VfsWindows)
	{
		if (QSFileName.compare("\\") != 0)
		{
			//m_VfsWindows->getOperationCleanup(QSFileName, QString("MirrorCleanup"), QString("InProcess..."));
		}
	}
	else
	{
	//lockerMirrorCleanup.unlock();
	return;	
	}
	//lockerMirrorCleanup.unlock();
	*/

	WCHAR filePath[DOKAN_MAX_PATH];
	GetFilePath(filePath, DOKAN_MAX_PATH, FileName);

	if (DokanFileInfo->Context) {
		DbgPrint(L"Cleanup: %s\n\n", filePath);
		CloseHandle((HANDLE)(DokanFileInfo->Context));
		DokanFileInfo->Context = 0;
	}
	else {
		DbgPrint(L"Cleanup: %s\n\tinvalid handle\n\n", filePath);
	}

	if (DokanFileInfo->DeleteOnClose) {
		// Should already be deleted by CloseHandle
		// if open with FILE_FLAG_DELETE_ON_CLOSE
		DbgPrint(L"\tDeleteOnClose\n");
		if (DokanFileInfo->IsDirectory) {
			DbgPrint(L"  DeleteDirectory ");
			if (!RemoveDirectory(filePath)) {
				DbgPrint(L"error code = %d\n\n", GetLastError());
			}
			else {
				DbgPrint(L"success\n\n");
			}
		}
		else {
			DbgPrint(L"  DeleteFile ");
			if (DeleteFile(filePath) == 0) {
				DbgPrint(L" error code = %d\n\n", GetLastError());
			}
			else {
				DbgPrint(L"success\n\n");
			}
		}
	}
}

static NTSTATUS DOKAN_CALLBACK MirrorReadFile(LPCWSTR FileName, LPVOID Buffer,
	DWORD BufferLength,
	LPDWORD ReadLength,
	LONGLONG Offset,
	PDOKAN_FILE_INFO DokanFileInfo) {

	//< Capture ReadFile Virtual File System Operation

	QString QSFileName;
#ifdef UNICODE
	QSFileName = QString::fromWCharArray(FileName);
#else
	QSFileName = QString::fromLocal8Bit(FileName);
#endif

	//QMutexLocker lockerMirrorReadFile(&_mutexMirrorReadFile);
	VfsWindows *m_VfsWindows = NULL;
	m_VfsWindows = VfsWindows::instance();
	if (m_VfsWindows)
	{
		if (QSFileName.compare("\\") != 0)
		{
			//m_VfsWindows->getOperationReadFile(QSFileName, QString("MirrorReadFile"), QString("InProcess..."));
			if (DokanFileInfo->ProcessId != ((ULONG)GetCurrentProcess()))
			{
				//qDebug() << " YYY openFileAtPath  Offset: " << Offset << " BufferLength: " << BufferLength << " ReadLength:" << *ReadLength;

				//int notw = (int)*ReadLength;
				//if (((notw + Offset) == BufferLength) && !QSFileName.contains("Zone.Identifier") && (QSFileName.count(QLatin1Char('.')) <= 2))
				if ((Offset == 0) && (QSFileName.count(QLatin1Char('.')) <= 2) && (DokanFileInfo->ProcessId != getExplorerID()) && (BufferLength == 8192))
				{
					qDebug() << " ZZZ openFileAtPath  Offset: " << Offset << " BufferLength: " << BufferLength << " ReadLength:" << *ReadLength;
					/*DWORD BufferLength,
						LPDWORD ReadLength,
						LONGLONG Offset,*/
						
					QVariantMap error;
					QString QSFileNametoReal = QSFileName;
					QSFileNametoReal.replace(0, 1, QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/cachedFiles/");
					VfsWindows::instance()->openFileAtPath(QSFileNametoReal, error);
				}
			}
		}
	}
	else
	{
	//lockerMirrorReadFile.unlock();
	return 0;	
	}
	//lockerMirrorReadFile.unlock();

	WCHAR filePath[DOKAN_MAX_PATH];
	HANDLE handle = (HANDLE)DokanFileInfo->Context;
	ULONG offset = (ULONG)Offset;
	BOOL opened = FALSE;

	GetFilePath(filePath, DOKAN_MAX_PATH, FileName);

	DbgPrint(L"ReadFile : %s\n", filePath);

	if (!handle || handle == INVALID_HANDLE_VALUE) {
		DbgPrint(L"\tinvalid handle, cleanuped?\n");
		handle = CreateFile(filePath, GENERIC_READ, FILE_SHARE_READ, NULL,
			OPEN_EXISTING, 0, NULL);
		if (handle == INVALID_HANDLE_VALUE) {
			DWORD error = GetLastError();
			DbgPrint(L"\tCreateFile error : %d\n\n", error);
			return DokanNtStatusFromWin32(error);
		}
		opened = TRUE;
	}

	LARGE_INTEGER distanceToMove;
	distanceToMove.QuadPart = Offset;
	if (!SetFilePointerEx(handle, distanceToMove, NULL, FILE_BEGIN)) {
		DWORD error = GetLastError();
		DbgPrint(L"\tseek error, offset = %d\n\n", offset);
		if (opened)
			CloseHandle(handle);
		return DokanNtStatusFromWin32(error);
	}

	if (!ReadFile(handle, Buffer, BufferLength, ReadLength, NULL)) {
		DWORD error = GetLastError();
		DbgPrint(L"\tread error = %u, buffer length = %d, read length = %d\n\n",
			error, BufferLength, *ReadLength);
		if (opened)
			CloseHandle(handle);
		return DokanNtStatusFromWin32(error);

	}
	else {
		/*QMutexLocker lockerMirrorReadFile(&_mutexMirrorReadFile);
				VfsWindows *m_VfsWindows = NULL;
				m_VfsWindows = VfsWindows::instance();
				if (m_VfsWindows)
				{
					if (QSFileName.compare("\\") != 0)
					{
						//m_VfsWindows->getOperationReadFile(QSFileName, QString("MirrorReadFile"), QString("InProcess..."));
						if (DokanFileInfo->ProcessId != ((ULONG)GetCurrentProcess()))
						{
							int notw = (int)*ReadLength;
							if (((notw + Offset) == BufferLength) && !QSFileName.contains("Zone.Identifier"))
							{
								QVariantMap error;
								QString QSFileNametoReal = QSFileName;
								QSFileNametoReal.replace(0, 1, QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/cachedFiles/");
								VfsWindows::instance()->openFileAtPath(QSFileNametoReal, error);
							}
						}
					}
				}
				else
				{
				lockerMirrorReadFile.unlock();
				return 0;
				}
			lockerMirrorReadFile.unlock();
		*/
		
		DbgPrint(L"\tByte to read: %d, Byte read %d, offset %d\n\n", BufferLength,
			*ReadLength, offset);
	}

	if (opened)
		CloseHandle(handle);

	return STATUS_SUCCESS;
}

static NTSTATUS DOKAN_CALLBACK MirrorWriteFile(LPCWSTR FileName, LPCVOID Buffer,
	DWORD NumberOfBytesToWrite,
	LPDWORD NumberOfBytesWritten,
	LONGLONG Offset,
	PDOKAN_FILE_INFO DokanFileInfo) {

	//< Capture WriteFile Virtual File System Operation

	QString QSFileName;
#ifdef UNICODE
	QSFileName = QString::fromWCharArray(FileName);
#else
	QSFileName = QString::fromLocal8Bit(FileName);
#endif

	/*	QVariantMap error;
	QMutexLocker lockerMirrorWriteFile(&_mutexMirrorWriteFile);
		VfsWindows *m_VfsWindows = NULL;
		m_VfsWindows = VfsWindows::instance();
		if (m_VfsWindows)
		{
			if (QSFileName.compare("\\") != 0)
				{
				QString QSFileNameToWriteFuse = QSFileName;
				QSFileNameToWriteFuse.replace(0, 1, QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/cachedFiles/");
				VfsWindows::instance()->writeFileAtPath(QSFileNameToWriteFuse, error);
				}
		}
		else
		{
		lockerMirrorWriteFile.unlock();
		return 0;	
		}
	lockerMirrorWriteFile.unlock();
	*/

	WCHAR filePath[DOKAN_MAX_PATH];
	HANDLE handle = (HANDLE)DokanFileInfo->Context;
	BOOL opened = FALSE;

	GetFilePath(filePath, DOKAN_MAX_PATH, FileName);

	DbgPrint(L"WriteFile : %s, offset %I64d, length %d\n", filePath, Offset,
		NumberOfBytesToWrite);

	// reopen the file
	if (!handle || handle == INVALID_HANDLE_VALUE) {
		DbgPrint(L"\tinvalid handle, cleanuped?\n");
		handle = CreateFile(filePath, GENERIC_WRITE, FILE_SHARE_WRITE, NULL,
			OPEN_EXISTING, 0, NULL);
		if (handle == INVALID_HANDLE_VALUE) {
			DWORD error = GetLastError();
			DbgPrint(L"\tCreateFile error : %d\n\n", error);
			return DokanNtStatusFromWin32(error);
		}
		opened = TRUE;
	}

	UINT64 fileSize = 0;
	DWORD fileSizeLow = 0;
	DWORD fileSizeHigh = 0;
	fileSizeLow = GetFileSize(handle, &fileSizeHigh);
	if (fileSizeLow == INVALID_FILE_SIZE) {
		DWORD error = GetLastError();
		DbgPrint(L"\tcan not get a file size error = %d\n", error);
		if (opened)
			CloseHandle(handle);
		return DokanNtStatusFromWin32(error);
	}

	fileSize = ((UINT64)fileSizeHigh << 32) | fileSizeLow;

	LARGE_INTEGER distanceToMove;
	if (DokanFileInfo->WriteToEndOfFile) {
		LARGE_INTEGER z;
		z.QuadPart = 0;
		if (!SetFilePointerEx(handle, z, NULL, FILE_END)) {
			DWORD error = GetLastError();
			DbgPrint(L"\tseek error, offset = EOF, error = %d\n", error);
			if (opened)
				CloseHandle(handle);
			return DokanNtStatusFromWin32(error);
		}
	}
	else {
		// Paging IO cannot write after allocate file size.
		if (DokanFileInfo->PagingIo) {
			if ((UINT64)Offset >= fileSize) {
				*NumberOfBytesWritten = 0;
				if (opened)
					CloseHandle(handle);
				return STATUS_SUCCESS;
			}

			if (((UINT64)Offset + NumberOfBytesToWrite) > fileSize) {
				UINT64 bytes = fileSize - Offset;
				if (bytes >> 32) {
					NumberOfBytesToWrite = (DWORD)(bytes & 0xFFFFFFFFUL);
				}
				else {
					NumberOfBytesToWrite = (DWORD)bytes;
				}
			}
		}

		if ((UINT64)Offset > fileSize) {
			// In the mirror sample helperZeroFileData is not necessary. NTFS will
			// zero a hole.
			// But if user's file system is different from NTFS( or other Windows's
			// file systems ) then  users will have to zero the hole themselves.
		}

		distanceToMove.QuadPart = Offset;
		if (!SetFilePointerEx(handle, distanceToMove, NULL, FILE_BEGIN)) {
			DWORD error = GetLastError();
			DbgPrint(L"\tseek error, offset = %I64d, error = %d\n", Offset, error);
			if (opened)
				CloseHandle(handle);
			return DokanNtStatusFromWin32(error);
		}
	}

	if (!WriteFile(handle, Buffer, NumberOfBytesToWrite, NumberOfBytesWritten,
		NULL)) {
		DWORD error = GetLastError();
		DbgPrint(L"\twrite error = %u, buffer length = %d, write length = %d\n",
			error, NumberOfBytesToWrite, *NumberOfBytesWritten);
		if (opened)
			CloseHandle(handle);
		return DokanNtStatusFromWin32(error);

	}
	else {
		DbgPrint(L"\twrite %d, offset %I64d\n\n", *NumberOfBytesWritten, Offset);
	}

	// close the file when it is reopened
	if (opened)
		CloseHandle(handle);

	QVariantMap error;
	//QMutexLocker lockerMirrorWriteFile(&_mutexMirrorWriteFile);
	VfsWindows *m_VfsWindows = NULL;
	m_VfsWindows = VfsWindows::instance();
	if (m_VfsWindows)
	{
		if (QSFileName.compare("\\") != 0)
		{
			;
			int notw = (int)NumberOfBytesToWrite;
			if( ((notw + Offset) == fileSize) && !QSFileName.contains("Zone.Identifier") )
			{
				QSFileName.replace(0, 1, QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/cachedFiles/");
				VfsWindows::instance()->writeFileAtPath(QSFileName, error);
			}
		}
	}
	else
	{
		//lockerMirrorWriteFile.unlock();
		return 0;
	}
	//lockerMirrorWriteFile.unlock();

	return STATUS_SUCCESS;
}

static NTSTATUS DOKAN_CALLBACK
MirrorFlushFileBuffers(LPCWSTR FileName, PDOKAN_FILE_INFO DokanFileInfo) {


	//< Capture FlushFileBuffers Virtual File System Operation

	QString QSFileName;
#ifdef UNICODE
	QSFileName = QString::fromWCharArray(FileName);
#else
	QSFileName = QString::fromLocal8Bit(FileName);
#endif

	/*
	//QMutexLocker lockerMirrorFlushFileBuffers(&_mutexMirrorFlushFileBuffers);
		VfsWindows *m_VfsWindows = NULL;
		m_VfsWindows = VfsWindows::instance();
		if (m_VfsWindows)
		{
			if (QSFileName.compare("\\") != 0)
			{

			}
				//m_VfsWindows->getOperationFlushFileBuffers(QSFileName, QString("MirrorFlushFileBuffers"), QString("InProcess..."));
		}
		else
		{
		//lockerMirrorFlushFileBuffers.unlock();
		return 0;	
		}
		*/
	//lockerMirrorFlushFileBuffers.unlock();

	WCHAR filePath[DOKAN_MAX_PATH];
	HANDLE handle = (HANDLE)DokanFileInfo->Context;

	GetFilePath(filePath, DOKAN_MAX_PATH, FileName);

	DbgPrint(L"FlushFileBuffers : %s\n", filePath);

	if (!handle || handle == INVALID_HANDLE_VALUE) {
		DbgPrint(L"\tinvalid handle\n\n");
		return STATUS_SUCCESS;
	}

	if (FlushFileBuffers(handle)) {
		return STATUS_SUCCESS;
	}
	else {
		DWORD error = GetLastError();
		DbgPrint(L"\tflush error code = %d\n", error);
		return DokanNtStatusFromWin32(error);
	}
}

static NTSTATUS DOKAN_CALLBACK MirrorGetFileInformation(
	LPCWSTR FileName, LPBY_HANDLE_FILE_INFORMATION HandleFileInformation,
	PDOKAN_FILE_INFO DokanFileInfo) {

	//< Capture GetFileInformation Virtual File System Operation

	QString QSFileName;
#ifdef UNICODE
	QSFileName = QString::fromWCharArray(FileName);
#else
	QSFileName = QString::fromLocal8Bit(FileName);
#endif

	//QMutexLocker lockerMirrorGetFileInformation(&_mutexMirrorGetFileInformation);
		/*VfsWindows *m_VfsWindows = NULL;
		m_VfsWindows = VfsWindows::instance();
		if (m_VfsWindows)
		{
			if(QSFileName.compare("\\") != 0)
				{
				}
				//m_VfsWindows->getOperationGetFileInformation(QSFileName, QString("MirrorGetFileInformation"), QString("InProcess..."));
		}
		else
		{
		//lockerMirrorGetFileInformation.unlock();
		return 0;	
		}
		*/
	//lockerMirrorGetFileInformation.unlock();

	WCHAR filePath[DOKAN_MAX_PATH];
	HANDLE handle = (HANDLE)DokanFileInfo->Context;
	BOOL opened = FALSE;

	GetFilePath(filePath, DOKAN_MAX_PATH, FileName);

	DbgPrint(L"GetFileInfo : %s\n", filePath);

	if (!handle || handle == INVALID_HANDLE_VALUE) {
		DbgPrint(L"\tinvalid handle, cleanuped?\n");
		handle = CreateFile(filePath, GENERIC_READ, FILE_SHARE_READ, NULL,
			OPEN_EXISTING, 0, NULL);
		if (handle == INVALID_HANDLE_VALUE) {
			DWORD error = GetLastError();
			DbgPrint(L"\tCreateFile error : %d\n\n", error);
			return DokanNtStatusFromWin32(error);
		}
		opened = TRUE;
	}

	if (!GetFileInformationByHandle(handle, HandleFileInformation)) {
		DbgPrint(L"\terror code = %d\n", GetLastError());

		// FileName is a root directory
		// in this case, FindFirstFile can't get directory information
		if (wcslen(FileName) == 1) {
			DbgPrint(L"  root dir\n");
			HandleFileInformation->dwFileAttributes = GetFileAttributes(filePath);

		}
		else {
			WIN32_FIND_DATAW find;
			ZeroMemory(&find, sizeof(WIN32_FIND_DATAW));
			HANDLE findHandle = FindFirstFile(filePath, &find);
			if (findHandle == INVALID_HANDLE_VALUE) {
				DWORD error = GetLastError();
				DbgPrint(L"\tFindFirstFile error code = %d\n\n", error);
				if (opened)
					CloseHandle(handle);
				return DokanNtStatusFromWin32(error);
			}
			HandleFileInformation->dwFileAttributes = find.dwFileAttributes;
			HandleFileInformation->ftCreationTime = find.ftCreationTime;
			HandleFileInformation->ftLastAccessTime = find.ftLastAccessTime;
			HandleFileInformation->ftLastWriteTime = find.ftLastWriteTime;
			HandleFileInformation->nFileSizeHigh = find.nFileSizeHigh;
			HandleFileInformation->nFileSizeLow = find.nFileSizeLow;
			DbgPrint(L"\tFindFiles OK, file size = %d\n", find.nFileSizeLow);
			FindClose(findHandle);
		}
	}
	else {
		DbgPrint(L"\tGetFileInformationByHandle success, file size = %d\n",
			HandleFileInformation->nFileSizeLow);
	}

	DbgPrint(L"FILE ATTRIBUTE  = %d\n", HandleFileInformation->dwFileAttributes);

	if (opened)
		CloseHandle(handle);

	return STATUS_SUCCESS;
}


static NTSTATUS DOKAN_CALLBACK
MirrorFindFiles(LPCWSTR FileName,
	PFillFindData FillFindData, // function pointer
	PDOKAN_FILE_INFO DokanFileInfo) {



	//< Capture FindFiles Virtual File System Operation

	QString QSFileName;
#ifdef UNICODE
	QSFileName = QString::fromWCharArray(FileName);
#else
	QSFileName = QString::fromLocal8Bit(FileName);
#endif

	QVariantMap b_error;
	QMutexLocker lockerMirrorFindFiles(&_mutexMirrorFindFiles);
		VfsWindows *m_VfsWindows = NULL;
		m_VfsWindows = VfsWindows::instance();
		if (m_VfsWindows)
		{
			QSFileName.replace("\\", "/");

			qDebug() << " gfds QSFileName: " << QSFileName << "DokanFileInfo->ProcessId : " << DokanFileInfo->ProcessId << " GetCurrentProcess(): " << (ULONG)GetCurrentProcess() << " getExplorerID(): " << getExplorerID();

			//if (DokanFileInfo->ProcessId != getExplorerID())
			//{
				QStringList *contents = m_VfsWindows->contentsOfDirectoryAtPath(QSFileName, b_error);
				qDebug() << Q_FUNC_INFO << " FileName: " << QSFileName;
			//}
		}
		else
		{
		lockerMirrorFindFiles.unlock();
		return 0;	
		}
	lockerMirrorFindFiles.unlock();

	WCHAR filePath[DOKAN_MAX_PATH];
	size_t fileLen;
	HANDLE hFind;
	WIN32_FIND_DATAW findData;
	DWORD error;
	int count = 0;

	GetFilePath(filePath, DOKAN_MAX_PATH, FileName);

	DbgPrint(L"FindFiles : %s\n", filePath);

	fileLen = wcslen(filePath);
	if (filePath[fileLen - 1] != L'\\') {
		filePath[fileLen++] = L'\\';
	}
	filePath[fileLen] = L'*';
	filePath[fileLen + 1] = L'\0';

	hFind = FindFirstFile(filePath, &findData);

	if (hFind == INVALID_HANDLE_VALUE) {
		error = GetLastError();
		DbgPrint(L"\tinvalid file handle. Error is %u\n\n", error);
		return DokanNtStatusFromWin32(error);
	}

	// Root folder does not have . and .. folder - we remove them
	BOOLEAN rootFolder = (wcscmp(FileName, L"\\") == 0);
	do {
		if (!rootFolder || (wcscmp(findData.cFileName, L".") != 0 &&
			wcscmp(findData.cFileName, L"..") != 0))
			FillFindData(&findData, DokanFileInfo);
		count++;
	} while (FindNextFile(hFind, &findData) != 0);

	error = GetLastError();
	FindClose(hFind);

	if (error != ERROR_NO_MORE_FILES) {
		DbgPrint(L"\tFindNextFile error. Error is %u\n\n", error);
		return DokanNtStatusFromWin32(error);
	}

	DbgPrint(L"\tFindFiles return %d entries in %s\n\n", count, filePath);

	return STATUS_SUCCESS;
}

static NTSTATUS DOKAN_CALLBACK
MirrorDeleteFile(LPCWSTR FileName, PDOKAN_FILE_INFO DokanFileInfo) {

	//< Capture DeleteFile Virtual File System Operation

	QString QSFileName;
#ifdef UNICODE
	QSFileName = QString::fromWCharArray(FileName);
#else
	QSFileName = QString::fromLocal8Bit(FileName);
#endif

	/*
	//QMutexLocker lockerMirrorDeleteFile(&_mutexMirrorDeleteFile);
	VfsWindows *m_VfsWindows = NULL;
	m_VfsWindows = VfsWindows::instance();
	if (m_VfsWindows)
	{
		if(QSFileName.compare("\\") != 0)
			{ 
			}
			//m_VfsWindows->getOperationDeleteFile(QSFileName, QString("MirrorDeleteFile"), QString("InProcess..."));
	}
	else
	{
	//lockerMirrorDeleteFile.unlock();
	return 0;
	}
	//lockerMirrorDeleteFile.unlock();
	*/

	WCHAR filePath[DOKAN_MAX_PATH];
	HANDLE handle = (HANDLE)DokanFileInfo->Context;

	GetFilePath(filePath, DOKAN_MAX_PATH, FileName);
	DbgPrint(L"DeleteFile %s - %d\n", filePath, DokanFileInfo->DeleteOnClose);

	DWORD dwAttrib = GetFileAttributes(filePath);

	if (dwAttrib != INVALID_FILE_ATTRIBUTES &&
		(dwAttrib & FILE_ATTRIBUTE_DIRECTORY))
		return STATUS_ACCESS_DENIED;

	if (handle && handle != INVALID_HANDLE_VALUE) {
		FILE_DISPOSITION_INFO fdi;
		fdi.DeleteFile = DokanFileInfo->DeleteOnClose;
		if (!SetFileInformationByHandle(handle, FileDispositionInfo, &fdi,
			sizeof(FILE_DISPOSITION_INFO)))
			return DokanNtStatusFromWin32(GetLastError());
	}

	return STATUS_SUCCESS;
}

static NTSTATUS DOKAN_CALLBACK
MirrorDeleteDirectory(LPCWSTR FileName, PDOKAN_FILE_INFO DokanFileInfo) {

    TypeOfOperation_DeleteDirectory(FileName, DokanFileInfo);

	//< Capture DeleteDirectory Virtual File System Operation

	QString QSFileName;
#ifdef UNICODE
	QSFileName = QString::fromWCharArray(FileName);
#else
	QSFileName = QString::fromLocal8Bit(FileName);
#endif

	/*
	//QMutexLocker lockerMirrorDeleteDirectory(&_mutexMirrorDeleteDirectory);
	VfsWindows *m_VfsWindows = NULL;
	m_VfsWindows = VfsWindows::instance();
	if (m_VfsWindows)
	{
		if (QSFileName.compare("\\") != 0)
		{

		}
			//m_VfsWindows->getOperationDeleteDirectory(QSFileName, QString("MirrorDeleteDirectory"), QString("InProcess..."));
	}
	else
	{
	//lockerMirrorDeleteDirectory.unlock();
	return 0;
	}
	//lockerMirrorDeleteDirectory.unlock();
	*/
	
	WCHAR filePath[DOKAN_MAX_PATH];
	// HANDLE	handle = (HANDLE)DokanFileInfo->Context;
	HANDLE hFind;
	WIN32_FIND_DATAW findData;
	size_t fileLen;

	ZeroMemory(filePath, sizeof(filePath));
	GetFilePath(filePath, DOKAN_MAX_PATH, FileName);

	DbgPrint(L"DeleteDirectory %s - %d\n", filePath,
		DokanFileInfo->DeleteOnClose);

	if (!DokanFileInfo->DeleteOnClose)
		//Dokan notify that the file is requested not to be deleted.
		return STATUS_SUCCESS;

	fileLen = wcslen(filePath);
	if (filePath[fileLen - 1] != L'\\') {
		filePath[fileLen++] = L'\\';
	}
	filePath[fileLen] = L'*';
	filePath[fileLen + 1] = L'\0';

	hFind = FindFirstFile(filePath, &findData);

	if (hFind == INVALID_HANDLE_VALUE) {
		DWORD error = GetLastError();
		DbgPrint(L"\tDeleteDirectory error code = %d\n\n", error);
		return DokanNtStatusFromWin32(error);
	}

	do {
		if (wcscmp(findData.cFileName, L"..") != 0 &&
			wcscmp(findData.cFileName, L".") != 0) {
			FindClose(hFind);
			DbgPrint(L"\tDirectory is not empty: %s\n", findData.cFileName);
			return STATUS_DIRECTORY_NOT_EMPTY;
		}
	} while (FindNextFile(hFind, &findData) != 0);

	DWORD error = GetLastError();

	FindClose(hFind);

	if (error != ERROR_NO_MORE_FILES) {
		DbgPrint(L"\tDeleteDirectory error code = %d\n\n", error);
		return DokanNtStatusFromWin32(error);
	}

	return STATUS_SUCCESS;
}

static NTSTATUS DOKAN_CALLBACK
MirrorMoveFile(LPCWSTR OldFileName, /* existing file name */ LPCWSTR NewFileName, BOOL ReplaceIfExisting, PDOKAN_FILE_INFO DokanFileInfo) {
	//< Capture MoveFile Virtual File System Operation

	QString oldFileName;
	QString newFileName;

#ifdef UNICODE
	oldFileName = QString::fromWCharArray(OldFileName);
	newFileName = QString::fromWCharArray(NewFileName);
#else
	oldFileName = QString::fromLocal8Bit(OldFileName);
	newFileName = QString::fromLocal8Bit(NewFileName);
#endif
	
	WCHAR filePath[DOKAN_MAX_PATH];
	WCHAR newFilePath[DOKAN_MAX_PATH];
	HANDLE handle;
	DWORD bufferSize;
	BOOL result;
	size_t newFilePathLen;

	PFILE_RENAME_INFO renameInfo = NULL;

	GetFilePath(filePath, DOKAN_MAX_PATH, OldFileName);
	GetFilePath(newFilePath, DOKAN_MAX_PATH, NewFileName);

	DbgPrint(L"MoveFile %s -> %s\n\n", filePath, newFilePath);
	handle = (HANDLE)DokanFileInfo->Context;
	if (!handle || handle == INVALID_HANDLE_VALUE) {
		DbgPrint(L"\tinvalid handle\n\n");
		return STATUS_INVALID_HANDLE;
	}

	newFilePathLen = wcslen(newFilePath);

	// the PFILE_RENAME_INFO struct has space for one WCHAR for the name at
	// the end, so that
	// accounts for the null terminator

	bufferSize = (DWORD)(sizeof(FILE_RENAME_INFO) +
		newFilePathLen * sizeof(newFilePath[0]));

	renameInfo = (PFILE_RENAME_INFO)malloc(bufferSize);
	if (!renameInfo) {
		return STATUS_BUFFER_OVERFLOW;
	}
	ZeroMemory(renameInfo, bufferSize);

	renameInfo->ReplaceIfExists =
		ReplaceIfExisting
		? TRUE
		: FALSE; // some warning about converting BOOL to BOOLEAN
	renameInfo->RootDirectory = NULL; // hope it is never needed, shouldn't be
	renameInfo->FileNameLength =
		(DWORD)newFilePathLen *
		sizeof(newFilePath[0]); // they want length in bytes

	wcscpy_s(renameInfo->FileName, newFilePathLen + 1, newFilePath);

	result = SetFileInformationByHandle(handle, FileRenameInfo, renameInfo,
		bufferSize);

	free(renameInfo);

	if (result) {
		if (oldFileName.compare("\\") != 0)
		{
			if (DokanFileInfo->IsDirectory)
			{
				//< Move directory from oldPath to newPath with real path
				QVariantMap error;
				VfsWindows::instance()->moveDirectoryAtPath(transformPath(oldFileName), transformPath(newFileName), error);
			}
			else
			{
				//< Move file from oldPath to newPath with real path
				QVariantMap error;
				VfsWindows::instance()->moveFileAtPath(transformPath(oldFileName), transformPath(newFileName), error);
				VfsWindows::instance()->ignoredList.append(transformPath(QString::fromWCharArray(OldFileName)));

				CleanIgnoredTask *task = new CleanIgnoredTask();
				QThreadPool::globalInstance()->start(task); // takes ownership and deletes
			}
		}
		return STATUS_SUCCESS;
	}
	else {
		DWORD error = GetLastError();
		DbgPrint(L"\tMoveFile error = %u\n", error);
		return DokanNtStatusFromWin32(error);
	}
}

static NTSTATUS DOKAN_CALLBACK MirrorLockFile(LPCWSTR FileName,
	LONGLONG ByteOffset,
	LONGLONG Length,
	PDOKAN_FILE_INFO DokanFileInfo) {

	//< Capture LockFile Virtual File System Operation

	QString QSFileName;
#ifdef UNICODE
	QSFileName = QString::fromWCharArray(FileName);
#else
	QSFileName = QString::fromLocal8Bit(FileName);
#endif

	/*
	//QMutexLocker lockerMirrorLockFile(&_mutexMirrorLockFile);
	VfsWindows *m_VfsWindows = NULL;
	m_VfsWindows = VfsWindows::instance();
	if (m_VfsWindows)
	{
		if (QSFileName.compare("\\") != 0)
		{
		}
			//m_VfsWindows->getOperationLockFile(QSFileName, QString("MirrorLockFile"), QString("InProcess..."));
	}
	else
	{
		//lockerMirrorLockFile.unlock();
		return 0;
	}
	//lockerMirrorLockFile.unlock();
	*/

	WCHAR filePath[DOKAN_MAX_PATH];
	HANDLE handle;
	LARGE_INTEGER offset;
	LARGE_INTEGER length;

	GetFilePath(filePath, DOKAN_MAX_PATH, FileName);

	DbgPrint(L"LockFile %s\n", filePath);

	handle = (HANDLE)DokanFileInfo->Context;
	if (!handle || handle == INVALID_HANDLE_VALUE) {
		DbgPrint(L"\tinvalid handle\n\n");
		return STATUS_INVALID_HANDLE;
	}

	length.QuadPart = Length;
	offset.QuadPart = ByteOffset;

	if (!LockFile(handle, offset.LowPart, offset.HighPart, length.LowPart,
		length.HighPart)) {
		DWORD error = GetLastError();
		DbgPrint(L"\terror code = %d\n\n", error);
		return DokanNtStatusFromWin32(error);
	}

	DbgPrint(L"\tsuccess\n\n");
	return STATUS_SUCCESS;
}

static NTSTATUS DOKAN_CALLBACK MirrorSetEndOfFile(
	LPCWSTR FileName, LONGLONG ByteOffset, PDOKAN_FILE_INFO DokanFileInfo) {

	//< Capture SetEndOfFile Virtual File System Operation

	QString QSFileName;
#ifdef UNICODE
	QSFileName = QString::fromWCharArray(FileName);
#else
	QSFileName = QString::fromLocal8Bit(FileName);
#endif
	
	/*
	//QMutexLocker lockerMirrorSetEndOfFile(&_mutexMirrorSetEndOfFile);
	VfsWindows *m_VfsWindows = NULL;
	m_VfsWindows = VfsWindows::instance();
	if (m_VfsWindows)
	{
		if (QSFileName.compare("\\") != 0)
		{

		}
			//m_VfsWindows->getOperationSetEndOfFile(QSFileName, QString("MirrorSetEndOfFile"), QString("InProcess..."));
	}
	else
	{
		//lockerMirrorSetEndOfFile.unlock();
		return 0;
	}
	*/
	//lockerMirrorSetEndOfFile.unlock();

	WCHAR filePath[DOKAN_MAX_PATH];
	HANDLE handle;
	LARGE_INTEGER offset;

	GetFilePath(filePath, DOKAN_MAX_PATH, FileName);

	DbgPrint(L"SetEndOfFile %s, %I64d\n", filePath, ByteOffset);

	handle = (HANDLE)DokanFileInfo->Context;
	if (!handle || handle == INVALID_HANDLE_VALUE) {
		DbgPrint(L"\tinvalid handle\n\n");
		return STATUS_INVALID_HANDLE;
	}

	offset.QuadPart = ByteOffset;
	if (!SetFilePointerEx(handle, offset, NULL, FILE_BEGIN)) {
		DWORD error = GetLastError();
		DbgPrint(L"\tSetFilePointer error: %d, offset = %I64d\n\n", error,
			ByteOffset);
		return DokanNtStatusFromWin32(error);
	}

	if (!SetEndOfFile(handle)) {
		DWORD error = GetLastError();
		DbgPrint(L"\tSetEndOfFile error code = %d\n\n", error);
		return DokanNtStatusFromWin32(error);
	}

	return STATUS_SUCCESS;
}

static NTSTATUS DOKAN_CALLBACK MirrorSetAllocationSize(
	LPCWSTR FileName, LONGLONG AllocSize, PDOKAN_FILE_INFO DokanFileInfo) {

	//< Capture SetAllocationSize Virtual File System Operation

	QString QSFileName;
#ifdef UNICODE
	QSFileName = QString::fromWCharArray(FileName);
#else
	QSFileName = QString::fromLocal8Bit(FileName);
#endif
	
	/*
	//QMutexLocker lockerMirrorSetAllocationSize(&_mutexMirrorSetAllocationSize);
	VfsWindows *m_VfsWindows = NULL;
	m_VfsWindows = VfsWindows::instance();
	if (m_VfsWindows)
	{
		if (QSFileName.compare("\\") != 0)
		{
		}
			//m_VfsWindows->getOperationSetAllocationSize(QSFileName, QString("MirrorSetAllocationSize"), QString("InProcess..."));
	}
	else
	{
		//lockerMirrorSetAllocationSize.unlock();
		return 0;
	}
	//lockerMirrorSetAllocationSize.unlock();
	*/

	WCHAR filePath[DOKAN_MAX_PATH];
	HANDLE handle;
	LARGE_INTEGER fileSize;

	GetFilePath(filePath, DOKAN_MAX_PATH, FileName);

	DbgPrint(L"SetAllocationSize %s, %I64d\n", filePath, AllocSize);

	handle = (HANDLE)DokanFileInfo->Context;
	if (!handle || handle == INVALID_HANDLE_VALUE) {
		DbgPrint(L"\tinvalid handle\n\n");
		return STATUS_INVALID_HANDLE;
	}

	if (GetFileSizeEx(handle, &fileSize)) {
		if (AllocSize < fileSize.QuadPart) {
			fileSize.QuadPart = AllocSize;
			if (!SetFilePointerEx(handle, fileSize, NULL, FILE_BEGIN)) {
				DWORD error = GetLastError();
				DbgPrint(L"\tSetAllocationSize: SetFilePointer eror: %d, "
					L"offset = %I64d\n\n",
					error, AllocSize);
				return DokanNtStatusFromWin32(error);
			}
			if (!SetEndOfFile(handle)) {
				DWORD error = GetLastError();
				DbgPrint(L"\tSetEndOfFile error code = %d\n\n", error);
				return DokanNtStatusFromWin32(error);
			}
		}
	}
	else {
		DWORD error = GetLastError();
		DbgPrint(L"\terror code = %d\n\n", error);
		return DokanNtStatusFromWin32(error);
	}
	return STATUS_SUCCESS;
}

static NTSTATUS DOKAN_CALLBACK MirrorSetFileAttributes(
	LPCWSTR FileName, DWORD FileAttributes, PDOKAN_FILE_INFO DokanFileInfo) {
	UNREFERENCED_PARAMETER(DokanFileInfo);

	//< Capture SetFileAttributes Virtual File System Operation

	QString QSFileName;
#ifdef UNICODE
	QSFileName = QString::fromWCharArray(FileName);
#else
	QSFileName = QString::fromLocal8Bit(FileName);
#endif
	
	/*
	//QMutexLocker lockerMirrorSetFileAttributes(&_mutexMirrorSetFileAttributes);
	VfsWindows *m_VfsWindows = NULL;
	m_VfsWindows = VfsWindows::instance();
	if (m_VfsWindows)
	{
		if (QSFileName.compare("\\") != 0)
		{

		}
			//m_VfsWindows->getOperationSetFileAttributes(QSFileName, QString("MirrorSetFileAttributes"), QString("InProcess..."));
	}
	else
	{
		//lockerMirrorSetFileAttributes.unlock();
		return 0;
	}
	//lockerMirrorSetFileAttributes.unlock();
	*/

	WCHAR filePath[DOKAN_MAX_PATH];

	GetFilePath(filePath, DOKAN_MAX_PATH, FileName);

	DbgPrint(L"SetFileAttributes %s 0x%x\n", filePath, FileAttributes);

	if (FileAttributes != 0) {
		if (!SetFileAttributes(filePath, FileAttributes)) {
			DWORD error = GetLastError();
			DbgPrint(L"\terror code = %d\n\n", error);
			return DokanNtStatusFromWin32(error);
		}
	}
	else {
		// case FileAttributes == 0 :
		// MS-FSCC 2.6 File Attributes : There is no file attribute with the value 0x00000000
		// because a value of 0x00000000 in the FileAttributes field means that the file attributes for this file MUST NOT be changed when setting basic information for the file
		DbgPrint(L"Set 0 to FileAttributes means MUST NOT be changed. Didn't call "
			L"SetFileAttributes function. \n");
	}

	DbgPrint(L"\n");
	return STATUS_SUCCESS;
}

static NTSTATUS DOKAN_CALLBACK
MirrorSetFileTime(LPCWSTR FileName, CONST FILETIME *CreationTime,
	CONST FILETIME *LastAccessTime, CONST FILETIME *LastWriteTime,
	PDOKAN_FILE_INFO DokanFileInfo) {

	//< Capture SetFileTime Virtual File System Operation

	QString QSFileName;
#ifdef UNICODE
	QSFileName = QString::fromWCharArray(FileName);
#else
	QSFileName = QString::fromLocal8Bit(FileName);
#endif
	
	//QMutexLocker lockerMirrorSetFileTime(&_mutexMirrorSetFileTime);
	/*VfsWindows *m_VfsWindows = NULL;
	m_VfsWindows = VfsWindows::instance();
	if (m_VfsWindows)
	{
		if (QSFileName.compare("\\") != 0)
		{

		}
			//m_VfsWindows->getOperationSetFileTime(QSFileName, QString("MirrorSetFileTime"), QString("InProcess..."));
	}
	else
	{
		//lockerMirrorSetFileTime.unlock();
		return 0;
	}
	*/
	//lockerMirrorSetFileTime.unlock();

	WCHAR filePath[DOKAN_MAX_PATH];
	HANDLE handle;

	GetFilePath(filePath, DOKAN_MAX_PATH, FileName);

	DbgPrint(L"SetFileTime %s\n", filePath);

	handle = (HANDLE)DokanFileInfo->Context;

	if (!handle || handle == INVALID_HANDLE_VALUE) {
		DbgPrint(L"\tinvalid handle\n\n");
		return STATUS_INVALID_HANDLE;
	}

	if (!SetFileTime(handle, CreationTime, LastAccessTime, LastWriteTime)) {
		DWORD error = GetLastError();
		DbgPrint(L"\terror code = %d\n\n", error);
		return DokanNtStatusFromWin32(error);
	}

	DbgPrint(L"\n");
	return STATUS_SUCCESS;
}

static NTSTATUS DOKAN_CALLBACK
MirrorUnlockFile(LPCWSTR FileName, LONGLONG ByteOffset, LONGLONG Length,
	PDOKAN_FILE_INFO DokanFileInfo) {

	//< Capture UnlockFile Virtual File System Operation

	QString QSFileName;
#ifdef UNICODE
	QSFileName = QString::fromWCharArray(FileName);
#else
	QSFileName = QString::fromLocal8Bit(FileName);
#endif
	
	//QMutexLocker lockerMirrorUnlockFile(&_mutexMirrorUnlockFile);
	/*VfsWindows *m_VfsWindows = NULL;
	m_VfsWindows = VfsWindows::instance();
	if (m_VfsWindows)
	{
		if (QSFileName.compare("\\") != 0)
		{
		}
			//m_VfsWindows->getOperationUnlockFile(QSFileName, QString("MirrorUnlockFile"), QString("InProcess..."));
	}
	else
	{
		//lockerMirrorUnlockFile.unlock();
		return 0;
	}
	//lockerMirrorUnlockFile.unlock();
	*/

	WCHAR filePath[DOKAN_MAX_PATH];
	HANDLE handle;
	LARGE_INTEGER length;
	LARGE_INTEGER offset;

	GetFilePath(filePath, DOKAN_MAX_PATH, FileName);

	DbgPrint(L"UnlockFile %s\n", filePath);

	handle = (HANDLE)DokanFileInfo->Context;
	if (!handle || handle == INVALID_HANDLE_VALUE) {
		DbgPrint(L"\tinvalid handle\n\n");
		return STATUS_INVALID_HANDLE;
	}

	length.QuadPart = Length;
	offset.QuadPart = ByteOffset;

	if (!UnlockFile(handle, offset.LowPart, offset.HighPart, length.LowPart,
		length.HighPart)) {
		DWORD error = GetLastError();
		DbgPrint(L"\terror code = %d\n\n", error);
		return DokanNtStatusFromWin32(error);
	}

	DbgPrint(L"\tsuccess\n\n");
	return STATUS_SUCCESS;
}

static NTSTATUS DOKAN_CALLBACK MirrorGetFileSecurity(
	LPCWSTR FileName, PSECURITY_INFORMATION SecurityInformation,
	PSECURITY_DESCRIPTOR SecurityDescriptor, ULONG BufferLength,
	PULONG LengthNeeded, PDOKAN_FILE_INFO DokanFileInfo) {

	//< Capture GetFileSecurity Virtual File System Operation

	QString QSFileName;
#ifdef UNICODE
	QSFileName = QString::fromWCharArray(FileName);
#else
	QSFileName = QString::fromLocal8Bit(FileName);
#endif
	
	//QMutexLocker lockerMirrorGetFileSecurity(&_mutexMirrorGetFileSecurity);
	/*VfsWindows *m_VfsWindows = NULL;
	m_VfsWindows = VfsWindows::instance();
	if (m_VfsWindows)
	{
		if (QSFileName.compare("\\") != 0)
		{

		}
			//m_VfsWindows->getOperationGetFileSecurity(QSFileName, QString("MirrorGetFileSecurity"), QString("InProcess..."));
	}
	else
	{
		//lockerMirrorGetFileSecurity.unlock();
		return 0;
	}
	*/
	//lockerMirrorGetFileSecurity.unlock();

	WCHAR filePath[DOKAN_MAX_PATH];
	BOOLEAN requestingSaclInfo;

	UNREFERENCED_PARAMETER(DokanFileInfo);

	GetFilePath(filePath, DOKAN_MAX_PATH, FileName);

	DbgPrint(L"GetFileSecurity %s\n", filePath);

	MirrorCheckFlag(*SecurityInformation, FILE_SHARE_READ);
	MirrorCheckFlag(*SecurityInformation, OWNER_SECURITY_INFORMATION);
	MirrorCheckFlag(*SecurityInformation, GROUP_SECURITY_INFORMATION);
	MirrorCheckFlag(*SecurityInformation, DACL_SECURITY_INFORMATION);
	MirrorCheckFlag(*SecurityInformation, SACL_SECURITY_INFORMATION);
	MirrorCheckFlag(*SecurityInformation, LABEL_SECURITY_INFORMATION);
	MirrorCheckFlag(*SecurityInformation, ATTRIBUTE_SECURITY_INFORMATION);
	MirrorCheckFlag(*SecurityInformation, SCOPE_SECURITY_INFORMATION);
	MirrorCheckFlag(*SecurityInformation,
		PROCESS_TRUST_LABEL_SECURITY_INFORMATION);
	MirrorCheckFlag(*SecurityInformation, BACKUP_SECURITY_INFORMATION);
	MirrorCheckFlag(*SecurityInformation, PROTECTED_DACL_SECURITY_INFORMATION);
	MirrorCheckFlag(*SecurityInformation, PROTECTED_SACL_SECURITY_INFORMATION);
	MirrorCheckFlag(*SecurityInformation, UNPROTECTED_DACL_SECURITY_INFORMATION);
	MirrorCheckFlag(*SecurityInformation, UNPROTECTED_SACL_SECURITY_INFORMATION);

	requestingSaclInfo = ((*SecurityInformation & SACL_SECURITY_INFORMATION) ||
		(*SecurityInformation & BACKUP_SECURITY_INFORMATION));

	if (!g_HasSeSecurityPrivilege) {
		*SecurityInformation &= ~SACL_SECURITY_INFORMATION;
		*SecurityInformation &= ~BACKUP_SECURITY_INFORMATION;
	}

	DbgPrint(L"  Opening new handle with READ_CONTROL access\n");
	HANDLE handle = CreateFile(
		filePath,
		READ_CONTROL | ((requestingSaclInfo && g_HasSeSecurityPrivilege)
			? ACCESS_SYSTEM_SECURITY
			: 0),
		FILE_SHARE_WRITE | FILE_SHARE_READ | FILE_SHARE_DELETE,
		NULL, // security attribute
		OPEN_EXISTING,
		FILE_FLAG_BACKUP_SEMANTICS, // |FILE_FLAG_NO_BUFFERING,
		NULL);

	if (!handle || handle == INVALID_HANDLE_VALUE) {
		DbgPrint(L"\tinvalid handle\n\n");
		int error = GetLastError();
		return DokanNtStatusFromWin32(error);
	}

	if (!GetUserObjectSecurity(handle, SecurityInformation, SecurityDescriptor,
		BufferLength, LengthNeeded)) {
		int error = GetLastError();
		if (error == ERROR_INSUFFICIENT_BUFFER) {
			DbgPrint(L"  GetUserObjectSecurity error: ERROR_INSUFFICIENT_BUFFER\n");
			CloseHandle(handle);
			return STATUS_BUFFER_OVERFLOW;
		}
		else {
			DbgPrint(L"  GetUserObjectSecurity error: %d\n", error);
			CloseHandle(handle);
			return DokanNtStatusFromWin32(error);
		}
	}

	// Ensure the Security Descriptor Length is set
	DWORD securityDescriptorLength =
		GetSecurityDescriptorLength(SecurityDescriptor);
	DbgPrint(L"  GetUserObjectSecurity return true,  *LengthNeeded = "
		L"securityDescriptorLength \n");
	*LengthNeeded = securityDescriptorLength;

	CloseHandle(handle);

	return STATUS_SUCCESS;
}

static NTSTATUS DOKAN_CALLBACK MirrorSetFileSecurity(
	LPCWSTR FileName, PSECURITY_INFORMATION SecurityInformation,
	PSECURITY_DESCRIPTOR SecurityDescriptor, ULONG SecurityDescriptorLength,
	PDOKAN_FILE_INFO DokanFileInfo) {

	//< Capture SetFileSecurity Virtual File System Operation

	QString QSFileName;
#ifdef UNICODE
	QSFileName = QString::fromWCharArray(FileName);
#else
	QSFileName = QString::fromLocal8Bit(FileName);
#endif
	
	//QMutexLocker lockerMirrorSetFileSecurity(&_mutexMirrorSetFileSecurity);
	/*VfsWindows *m_VfsWindows = NULL;
	m_VfsWindows = VfsWindows::instance();
	if (m_VfsWindows)
	{
		if (QSFileName.compare("\\") != 0)
		{

		}
			//m_VfsWindows->getOperationSetFileSecurity(QSFileName, QString("MirrorSetFileSecurity"), QString("InProcess..."));
	}
	else
	{
		//lockerMirrorSetFileSecurity.unlock();
		return 0;
	}
	*/
	//lockerMirrorSetFileSecurity.unlock();

	HANDLE handle;
	WCHAR filePath[DOKAN_MAX_PATH];

	UNREFERENCED_PARAMETER(SecurityDescriptorLength);

	GetFilePath(filePath, DOKAN_MAX_PATH, FileName);

	DbgPrint(L"SetFileSecurity %s\n", filePath);

	handle = (HANDLE)DokanFileInfo->Context;
	if (!handle || handle == INVALID_HANDLE_VALUE) {
		DbgPrint(L"\tinvalid handle\n\n");
		return STATUS_INVALID_HANDLE;
	}

	if (!SetUserObjectSecurity(handle, SecurityInformation, SecurityDescriptor)) {
		int error = GetLastError();
		DbgPrint(L"  SetUserObjectSecurity error: %d\n", error);
		return DokanNtStatusFromWin32(error);
	}
	return STATUS_SUCCESS;
}

static NTSTATUS DOKAN_CALLBACK MirrorGetVolumeInformation(
	LPWSTR VolumeNameBuffer, DWORD VolumeNameSize, LPDWORD VolumeSerialNumber,
	LPDWORD MaximumComponentLength, LPDWORD FileSystemFlags,
	LPWSTR FileSystemNameBuffer, DWORD FileSystemNameSize,
	PDOKAN_FILE_INFO DokanFileInfo) {

	//QMutexLocker lockerMirrorGetVolumeInformation(&_mutexMirrorGetVolumeInformation);
	/*VfsWindows *m_VfsWindows = NULL;
	m_VfsWindows = VfsWindows::instance();
	if (m_VfsWindows)
	{
			//m_VfsWindows->getOperationCreateFile(QString("VolumeInformation"), QString("MirrorGetVolumeInformation"), QString("InProcess..."));
	}
	else
	{
		//lockerMirrorGetVolumeInformation.unlock();
		return 0;
	}
	*/
	//lockerMirrorGetVolumeInformation.unlock();

	UNREFERENCED_PARAMETER(DokanFileInfo);

	WCHAR volumeRoot[4];
	DWORD fsFlags = 0;

	QString applicationName = QApplication::applicationName() + " FS";
	wchar_t *VolName = new wchar_t[applicationName.length() + 1];
	applicationName.toWCharArray(VolName);
	VolName[applicationName.length()] = 0;

	wcscpy_s(VolumeNameBuffer, VolumeNameSize, VolName);

	if (VolumeSerialNumber)
		*VolumeSerialNumber = 0x19831116;
	if (MaximumComponentLength)
		*MaximumComponentLength = 255;
	if (FileSystemFlags)
		*FileSystemFlags = FILE_CASE_SENSITIVE_SEARCH | FILE_CASE_PRESERVED_NAMES |
		FILE_SUPPORTS_REMOTE_STORAGE | FILE_UNICODE_ON_DISK |
		FILE_PERSISTENT_ACLS | FILE_NAMED_STREAMS;

	volumeRoot[0] = RootDirectory[0];
	volumeRoot[1] = ':';
	volumeRoot[2] = '\\';
	volumeRoot[3] = '\0';

	if (GetVolumeInformation(volumeRoot, NULL, 0, NULL, MaximumComponentLength,
		&fsFlags, FileSystemNameBuffer,
		FileSystemNameSize)) {

		if (FileSystemFlags)
			*FileSystemFlags &= fsFlags;

		if (MaximumComponentLength) {
			DbgPrint(L"GetVolumeInformation: max component length %u\n",
				*MaximumComponentLength);
		}
		if (FileSystemNameBuffer) {
			DbgPrint(L"GetVolumeInformation: file system name %s\n",
				FileSystemNameBuffer);
		}
		if (FileSystemFlags) {
			DbgPrint(L"GetVolumeInformation: got file system flags 0x%08x,"
				L" returning 0x%08x\n",
				fsFlags, *FileSystemFlags);
		}
	}
	else {

		DbgPrint(L"GetVolumeInformation: unable to query underlying fs,"
			L" using defaults.  Last error = %u\n",
			GetLastError());

		// File system name could be anything up to 10 characters.
		// But Windows check few feature availability based on file system name.
		// For this, it is recommended to set NTFS or FAT here.
		wcscpy_s(FileSystemNameBuffer, FileSystemNameSize, L"NTFS");
	}

	return STATUS_SUCCESS;
}

void VfsWindows::setNumberOfBytes(unsigned long long numberOfBytes)
{
	this->numberOfBytes = numberOfBytes;
}

unsigned long long VfsWindows::getNumberOfBytes()
{
	return numberOfBytes;
}

void VfsWindows::setNumberOfFreeBytes(unsigned long long numberOfFreeBytes)
{
	this->numberOfFreeBytes = numberOfFreeBytes;
}

unsigned long long VfsWindows::getNumberOfFreeBytes()
{
	return numberOfFreeBytes;
}

//Uncomment for personalize disk space
static NTSTATUS DOKAN_CALLBACK MirrorDokanGetDiskFreeSpace(PULONGLONG FreeBytesAvailable, PULONGLONG TotalNumberOfBytes, PULONGLONG TotalNumberOfFreeBytes, PDOKAN_FILE_INFO DokanFileInfo)
{
	UNREFERENCED_PARAMETER(DokanFileInfo);

	*FreeBytesAvailable = (ULONGLONG)(512 * 1024 * 1024);
	*TotalNumberOfBytes = 9223372036854775807;
	*TotalNumberOfFreeBytes = 9223372036854775807;
	
	/*
	// @Capacity
	*TotalNumberOfBytes = VfsWindows::instance()->getNumberOfBytes();
	// @Used space
	/////////*TotalNumberOfFreeBytes = (ULONGLONG)1024L * 1024 * 10;
	*TotalNumberOfFreeBytes = VfsWindows::instance()->getNumberOfFreeBytes();
	// @Free space
	*FreeBytesAvailable = (ULONGLONG)(*TotalNumberOfBytes - *TotalNumberOfFreeBytes); /// *1024 * 1024 * 10;
	*/

	return STATUS_SUCCESS;
}

//Uncomment for personalize disk space
/*
static NTSTATUS DOKAN_CALLBACK MirrorDokanGetDiskFreeSpace(PULONGLONG FreeBytesAvailable, PULONGLONG TotalNumberOfBytes, PULONGLONG TotalNumberOfFreeBytes, PDOKAN_FILE_INFO DokanFileInfo)
{
	UNREFERENCED_PARAMETER(DokanFileInfo);
	
	*FreeBytesAvailable = (ULONGLONG)(512 * 1024 * 1024);
	*TotalNumberOfBytes = 9223372036854775807;
	*TotalNumberOfFreeBytes = 9223372036854775807;
	
	return STATUS_SUCCESS;
}
*/

/**
* Avoid #include <winternl.h> which as conflict with FILE_INFORMATION_CLASS
* definition.
* This only for MirrorFindStreams. Link with ntdll.lib still required.
*
* Not needed if you're not using NtQueryInformationFile!
*
* BEGIN
*/
#pragma warning(push)
#pragma warning(disable : 4201)
typedef struct _IO_STATUS_BLOCK {
	union {
		NTSTATUS Status;
		PVOID Pointer;
	} DUMMYUNIONNAME;

	ULONG_PTR Information;
} IO_STATUS_BLOCK, *PIO_STATUS_BLOCK;
#pragma warning(pop)

NTSYSCALLAPI NTSTATUS NTAPI NtQueryInformationFile(
	_In_ HANDLE FileHandle, _Out_ PIO_STATUS_BLOCK IoStatusBlock,
	_Out_writes_bytes_(Length) PVOID FileInformation, _In_ ULONG Length,
	_In_ FILE_INFORMATION_CLASS FileInformationClass);
/**
* END
*/

NTSTATUS DOKAN_CALLBACK
MirrorFindStreams(LPCWSTR FileName, PFillFindStreamData FillFindStreamData,
	PDOKAN_FILE_INFO DokanFileInfo) {

	//< Capture FindStreams Virtual File System Operation

	QString QSFileName;
#ifdef UNICODE
	QSFileName = QString::fromWCharArray(FileName);
#else
	QSFileName = QString::fromLocal8Bit(FileName);
#endif
	
	/*
	//QMutexLocker lockerMirrorFindStreams(&_mutexMirrorFindStreams);
	VfsWindows *m_VfsWindows = NULL;
	m_VfsWindows = VfsWindows::instance();
	if (m_VfsWindows)
	{
		if (QSFileName.compare("\\") != 0)
		{

		}
			//m_VfsWindows->getOperationFindStreams(QSFileName, QString("MirrorFindStreams"), QString("InProcess..."));
	}
	else
	{
		//lockerMirrorFindStreams.unlock();
		return 0;
	}
	*/
	//lockerMirrorFindStreams.unlock();

	WCHAR filePath[DOKAN_MAX_PATH];
	HANDLE hFind;
	WIN32_FIND_STREAM_DATA findData;
	DWORD error;
	int count = 0;

	GetFilePath(filePath, DOKAN_MAX_PATH, FileName);

	DbgPrint(L"FindStreams :%s\n", filePath);

	hFind = FindFirstStreamW(filePath, FindStreamInfoStandard, &findData, 0);

	if (hFind == INVALID_HANDLE_VALUE) {
		error = GetLastError();
		DbgPrint(L"\tinvalid file handle. Error is %u\n\n", error);
		return DokanNtStatusFromWin32(error);
	}

	FillFindStreamData(&findData, DokanFileInfo);
	count++;

	while (FindNextStreamW(hFind, &findData) != 0) {
		FillFindStreamData(&findData, DokanFileInfo);
		count++;
	}

	error = GetLastError();
	FindClose(hFind);

	if (error != ERROR_HANDLE_EOF) {
		DbgPrint(L"\tFindNextStreamW error. Error is %u\n\n", error);
		return DokanNtStatusFromWin32(error);
	}

	DbgPrint(L"\tFindStreams return %d entries in %s\n\n", count, filePath);

	return STATUS_SUCCESS;
}

static NTSTATUS DOKAN_CALLBACK MirrorMounted(PDOKAN_FILE_INFO DokanFileInfo) {
	UNREFERENCED_PARAMETER(DokanFileInfo);

	DbgPrint(L"Mounted\n");
	return STATUS_SUCCESS;
}

static NTSTATUS DOKAN_CALLBACK MirrorUnmounted(PDOKAN_FILE_INFO DokanFileInfo) {
	UNREFERENCED_PARAMETER(DokanFileInfo);

	DbgPrint(L"Unmounted\n");
	return STATUS_SUCCESS;
}

#pragma warning(pop)

BOOL WINAPI CtrlHandler(DWORD dwCtrlType) {
	switch (dwCtrlType) {
	case CTRL_C_EVENT:
	case CTRL_BREAK_EVENT:
	case CTRL_CLOSE_EVENT:
	case CTRL_LOGOFF_EVENT:
	case CTRL_SHUTDOWN_EVENT:
		SetConsoleCtrlHandler(CtrlHandler, FALSE);
		DokanRemoveMountPoint(MountPoint);
		return TRUE;
	default:
		return FALSE;
	}
}

void ShowUsage() {
	// clang-format off
	fprintf(stderr, "mirror.exe\n"
		"  /r RootDirectory (ex. /r c:\\test)\t\t Directory source to mirror.\n"
		"  /l MountPoint (ex. /l m)\t\t\t Mount point. Can be M:\\ (drive letter) or empty NTFS folder C:\\mount\\dokan .\n"
		"  /t ThreadCount (ex. /t 5)\t\t\t Number of threads to be used internally by Dokan library.\n\t\t\t\t\t\t More threads will handle more event at the same time.\n"
		"  /d (enable debug output)\t\t\t Enable debug output to an attached debugger.\n"
		"  /s (use stderr for output)\t\t\t Enable debug output to stderr.\n"
		"  /n (use network drive)\t\t\t Show device as network device.\n"
		"  /m (use removable drive)\t\t\t Show device as removable media.\n"
		"  /w (write-protect drive)\t\t\t Read only filesystem.\n"
		"  /o (use mount manager)\t\t\t Register device to Windows mount manager.\n\t\t\t\t\t\t This enables advanced Windows features like recycle bin and more...\n"
		"  /c (mount for current session only)\t\t Device only visible for current user session.\n"
		"  /u (UNC provider name ex. \\localhost\\myfs)\t UNC name used for network volume.\n"
		"  /p (Impersonate Caller User)\t\t\t Impersonate Caller User when getting the handle in CreateFile for operations.\n\t\t\t\t\t\t This option requires administrator right to work properly.\n"
		"  /a Allocation unit size (ex. /a 512)\t\t Allocation Unit Size of the volume. This will behave on the disk file size.\n"
		"  /k Sector size (ex. /k 512)\t\t\t Sector Size of the volume. This will behave on the disk file size.\n"
		"  /f User mode Lock\t\t\t\t Enable Lockfile/Unlockfile operations. Otherwise Dokan will take care of it.\n"
		"  /i (Timeout in Milliseconds ex. /i 30000)\t Timeout until a running operation is aborted and the device is unmounted.\n\n"
		"Examples:\n"
		"\tmirror.exe /r C:\\Users /l M:\t\t\t# Mirror C:\\Users as RootDirectory into a drive of letter M:\\.\n"
		"\tmirror.exe /r C:\\Users /l C:\\mount\\dokan\t# Mirror C:\\Users as RootDirectory into NTFS folder C:\\mount\\dokan.\n"
		"\tmirror.exe /r C:\\Users /l M: /n /u \\myfs\\myfs1\t# Mirror C:\\Users as RootDirectory into a network drive M:\\. with UNC \\\\myfs\\myfs1\n\n"
		"Unmount the drive with CTRL + C in the console or alternatively via \"dokanctl /u MountPoint\".\n");
	// clang-format on
}

void CleanIgnoredTask::run() {
	Sleep(3500);

	VfsWindows::instance()->ignoredList.clear();
}

void VfsWindows::slotSyncFinish()
{
    _mutex.lock();
    _syncCondition.wakeAll();
    _mutex.unlock();

/////////////////
	SHChangeNotify(SHCNE_UPDATEDIR, SHCNF_PATH | SHCNF_FLUSHNOWAIT, QString("X:/").toStdWString().data(), NULL);
/////////////////
}

void VfsWindows::createFileAtPath(QString path, QVariantMap &error)
{
    //emit createItem(path);
}

void VfsWindows::moveFileAtPath(QString path, QString npath,QVariantMap &error)
{
    //TODO: remove old path
	emit move(path, npath);
}

void VfsWindows::createDirectoryAtPath(QString path, QVariantMap &error)
{
    //emit createItem(path);
}

void VfsWindows::moveDirectoryAtPath(QString path, QString npath, QVariantMap &error)
{
	//TODO remove old path
	emit move(path, npath);
}

void VfsWindows::openFileAtPath(QString path, QVariantMap &error)
{
    //QString relative_prefix = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/cachedFiles/";
    //QString relative_path = path;
    //relative_path.replace(0, relative_prefix.length(), QString(""));

		//< Alway ONLY FIRST >TIME when download file-open set SYNCMODE_ONLINE.
 //   if (SyncJournalDb::instance()->getSyncMode(relative_path) == SyncJournalDb::SYNCMODE_NONE)
 //       SyncJournalDb::instance()->setSyncMode(relative_path, SyncJournalDb::SYNCMODE_ONLINE);
 //
	//	//< Set when file was opened or updated
 //   SyncJournalDb::instance()->updateLastAccess(relative_path);

	//if (SyncJournalDb::instance()->getSyncMode(relative_path) == SyncJournalDb::SYNCMODE_ONLINE)
 //       qDebug() << " clfCase relative_path: " << relative_path << "SYNCMODE_ONLINE" \
 //                << " LastAccess: " << SyncJournalDb::instance()->secondsSinceLastAccess(relative_path);
 //   else if (SyncJournalDb::instance()->getSyncMode(relative_path) == SyncJournalDb::SYNCMODE_ONLINE)
 //       qDebug() << " clfCase relative_path: " << relative_path << "SYNCMODE_OFFLINE" \
 //                << " LastAccess: " << SyncJournalDb::instance()->secondsSinceLastAccess(relative_path);



	_mutex.lock();
    emit openFile(path);
    _syncCondition.wait(&_mutex);
/////////////////
	//char fgv[5];
	//fgv[0] = 'X';
	//fgv[1] = ':';
	//fgv[2] = '\\';
	//fgv[3] = 0;
	////fgv[4] = 0;

	//path.replace(0, relative_prefix.length(), fgv);

	//// SI ESTA EN LA RAIZ SE VA ASI
 //       // 01-16 14:04:25:827 [ debug default ] 5928 OCC::VfsWindows::openFileAtPath:  gbh path:  "X:\\0706075.pdf"


 //       QString name = path;
 //       path.replace("\\", "/");
 //       name.replace("\\", "/");


 //       int pos = name.lastIndexOf(QChar('/'));

 //       qDebug() << " gbh path: " << path;
 //       qDebug() << " gbh fgv: " << fgv;
 //       qDebug() << " gbh name.left(pos): " << name.left(pos);

 //       SHChangeNotify(SHCNE_UPDATEITEM, SHCNF_PATH | SHCNF_FLUSHNOWAIT, path.toStdWString().data(), NULL);
 //       SHChangeNotify(SHCNE_UPDATEDIR, SHCNF_PATH | SHCNF_FLUSHNOWAIT, name.left(pos).toStdWString().data(), NULL);
        //	SHChangeNotify(SHCNE_UPDATEDIR, SHCNF_PATH | SHCNF_FLUSHNOWAIT, QString("X:/").toStdWString().data(), NULL);
        _mutex.unlock();
}

void VfsWindows::writeFileAtPath(QString path, QVariantMap &error)
{
 //   QString relative_prefix = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/cachedFiles/";
 //   QString relative_path = path;
 //   relative_path.replace(0, relative_prefix.length(), QString(""));

	//		//< Set when file was opened or updated
 //   SyncJournalDb::instance()->updateLastAccess(relative_path);

	//if (SyncJournalDb::instance()->getSyncMode(relative_path) == SyncJournalDb::SYNCMODE_ONLINE)
 //       qDebug() << " clfCase relative_path: " << relative_path << "SYNCMODE_ONLINE" \
 //                << " LastAccess: " << SyncJournalDb::instance()->secondsSinceLastAccess(relative_path);
 //   else if (SyncJournalDb::instance()->getSyncMode(relative_path) == SyncJournalDb::SYNCMODE_ONLINE)
 //       qDebug() << " clfCase relative_path: " << relative_path << "SYNCMODE_OFFLINE" \
 //                << " LastAccess: " << SyncJournalDb::instance()->secondsSinceLastAccess(relative_path);

	emit writeFile(path);
}

void VfsWindows::deleteFileAtPath(QString path, QVariantMap &error)
{
    emit deleteItem(path);
}

void VfsWindows::startDeleteDirectoryAtPath(QString path, QVariantMap &error)
{
}

void VfsWindows::endDeleteDirectoryAtPath(QString path, QVariantMap &error)
{
	emit deleteItem(path);
}

QStringList *VfsWindows::contentsOfDirectoryAtPath(QString path, QVariantMap &error)
{
	qDebug() << Q_FUNC_INFO << " rootPath: " << rootPath;

	_mutex.lock();
	emit startRemoteFileListJob(path);
	_dirCondition.wait(&_mutex);
	_mutex.unlock();

	while (!_fileListMap.contains(path)) {
		if(!_fileListMap.keys().isEmpty())
			qDebug() << Q_FUNC_INFO << " ERROR paso buscado" << path << "keys" << _fileListMap.keys();
	}

	if (_fileListMap.value(path)->code != 0)
	{
		qDebug() << Q_FUNC_INFO << " return nullptr";
		return NULL;
	}

	for(unsigned long i=0; i <_fileListMap.value(path)->list.size(); i++)
   	{
		QString root = rootPath;
		root.endsWith("/") ? root.truncate(root.length() -1) : root;
		QString completePath = root + (path.endsWith("/") ? path :(path+"/")) + QString::fromLatin1(_fileListMap.value(path)->list.at(i)->path);

		if (!ignoredList.empty()) {
			foreach(QString item, ignoredList)
			{
				if (item == completePath) {
					completePath = "";
				}
			}
		}

		QFileInfo fi(completePath);
		if (!fi.exists())
		{
			if(_fileListMap.value(path)->list.at(i)->type == ItemTypeFile)
			{
				qDebug() << "kkF:" << completePath;
				QFile file(completePath);			//< Create empty file
				if (file.open(QIODevice::ReadWrite))
					file.close();
			}
			else if (_fileListMap.value(path)->list.at(i)->type == ItemTypeDirectory)
			{
				qDebug() << "kkD:" << completePath;
				if (!QDir(completePath).exists()) {
					QDir().mkdir(completePath);
				}
			}
			emit addToFileTree(_fileListMap.value(path)->list.at(i)->type, completePath);
			SyncWrapper::instance()->setFileRecord(_fileListMap.value(path)->list.at(i).get(), rootPath + (path.endsWith("/")?path:(path+"/")));
		}
		//if (_fileListMap.value(path)->list.at(i)->type == ItemTypeFile)
		//qDebug() << Q_FUNC_INFO << "results: " << r->name << r->type;
	}
	
	delete(_fileListMap.value(path));
	_fileListMap.remove(path);

	return 0;
}

void VfsWindows::folderFileListFinish(OCC::DiscoveryDirectoryResult *dr)
{
	if (dr)
	{
		QString ruta = dr->path;
		_fileListMap.insert(dr->path, dr);
        _mutex.lock();
        _dirCondition.wakeAll();
        _mutex.unlock();
	}
	else
		qDebug() << "Error in obtaining the results, comes null";
}

void VfsWindows::initialize(QString rootPath, WCHAR mountLetter, AccountState *accountState_)
{
	this->rootPath = rootPath;
	this->mountLetter = mountLetter;

	ConfigFile cfg;
	QDir path_mirror(cfg.defaultFileStreamMirrorPath());
	while (!path_mirror.exists())
	{
		qDebug() << "\n Dokan: " << Q_FUNC_INFO << " !path_mirror.exists()" << cfg.defaultFileStreamMirrorPath();
		path_mirror.mkdir(cfg.defaultFileStreamMirrorPath());
		SetFileAttributes((const wchar_t *)cfg.defaultFileStreamMirrorPath().utf16(), FILE_ATTRIBUTE_HIDDEN);
		Sleep(100);
	}

	_remotefileListJob = new OCC::DiscoveryFolderFileList(accountState_->account());
	_remotefileListJob->setParent(this);
	connect(this, &VfsWindows::startRemoteFileListJob, _remotefileListJob, &OCC::DiscoveryFolderFileList::doGetFolderContent);
	connect(_remotefileListJob, &OCC::DiscoveryFolderFileList::gotDataSignal, this, &VfsWindows::folderFileListFinish);

	// "talk" to the sync engine
	_syncWrapper = OCC::SyncWrapper::instance();
	connect(this, &VfsWindows::addToFileTree, _syncWrapper, &OCC::SyncWrapper::updateFileTree, Qt::QueuedConnection);
	connect(_syncWrapper, &OCC::SyncWrapper::syncFinish, this, &VfsWindows::slotSyncFinish, Qt::QueuedConnection);

	connect(this, &VfsWindows::createItem, _syncWrapper, &OCC::SyncWrapper::createItemAtPath, Qt::QueuedConnection);
	connect(this, &VfsWindows::openFile, _syncWrapper, &OCC::SyncWrapper::openFileAtPath, Qt::QueuedConnection);
	connect(this, &VfsWindows::writeFile, _syncWrapper, &OCC::SyncWrapper::writeFileAtPath, Qt::QueuedConnection);
	connect(this, &VfsWindows::deleteItem, _syncWrapper, &OCC::SyncWrapper::deleteItemAtPath, Qt::QueuedConnection);
	connect(this, &VfsWindows::move, _syncWrapper, &OCC::SyncWrapper::moveItemAtPath, Qt::QueuedConnection);
}

VfsWindows::VfsWindows()
{

}

VfsWindows::~VfsWindows()
{
	_instance = 0;
}

VfsWindows *VfsWindows::instance()
{
	if (_instance == 0)
	{
		_instance = new VfsWindows();
	}

	return _instance;
}

struct DokanaMainparams
{
	PDOKAN_OPTIONS		m_DokanOptions;
	PDOKAN_OPERATIONS	m_DokanOperations;
};

void ThreadFunc(void *params) {

qDebug() << "\n dbg_sync " << Q_FUNC_INFO << " Befor DokanMain";
	int status = DokanMain(((struct DokanaMainparams*)params)->m_DokanOptions,
		((struct DokanaMainparams*)params)->m_DokanOperations);
qDebug() << "\n dbg_sync " << Q_FUNC_INFO << " After DokanMain";

	switch (status) {
	case DOKAN_SUCCESS:
		qDebug() << "\n dbg_sync " << Q_FUNC_INFO << " ThreadFunc Success";
		break;
	case DOKAN_ERROR:
		qDebug() << "\n dbg_sync " << Q_FUNC_INFO << " ThreadFunc Error";
		break;
	case DOKAN_DRIVE_LETTER_ERROR:
		qDebug() << "\n dbg_sync " << Q_FUNC_INFO << " ThreadFunc Bad Drive letter";
		break;
	case DOKAN_DRIVER_INSTALL_ERROR:
		qDebug() << "\n dbg_sync " << Q_FUNC_INFO << " ThreadFunc Can't install driver";
		break;
	case DOKAN_START_ERROR:
		qDebug() << "\n dbg_sync " << Q_FUNC_INFO << " ThreadFunc Driver something wrong";
		break;
	case DOKAN_MOUNT_ERROR:
		qDebug() << "\n dbg_sync " << Q_FUNC_INFO << " ThreadFunc Can't assign a drive letter";
		break;
	case DOKAN_MOUNT_POINT_ERROR:
		qDebug() << "\n dbg_sync " << Q_FUNC_INFO << " ThreadFunc Mount point error";
		break;
	case DOKAN_VERSION_ERROR:
		qDebug() << "\n dbg_sync " << Q_FUNC_INFO << " ThreadFunc Version error";
		break;
	default:
		qDebug() << "\n dbg_sync " << Q_FUNC_INFO << " ThreadFunc Unknown error";
		break;
	}

/*
qDebug() << "\n dbg_sync " << Q_FUNC_INFO << " Success 1";

	if(((struct DokanaMainparams*)params)->m_DokanOptions)
		free(((struct DokanaMainparams*)params)->m_DokanOptions);

qDebug() << "\n dbg_sync " << Q_FUNC_INFO << " Success 2";

	if(((struct DokanaMainparams*)params)->m_DokanOperations)
		free(((struct DokanaMainparams*)params)->m_DokanOperations);

qDebug() << "\n dbg_sync " << Q_FUNC_INFO << " Success 3";

*/	
}

bool VfsWindows::removeRecursively(const QString & dirName)
{
	bool result = true;
	QDir dir(dirName);

	if (dir.exists(dirName)) {
		Q_FOREACH(QFileInfo info, dir.entryInfoList(QDir::NoDotAndDotDot | QDir::System | QDir::Hidden | QDir::AllDirs | QDir::Files, QDir::DirsFirst)) {
			if (info.isDir()) {
				result = removeRecursively(info.absoluteFilePath());
			} else {
				result = QFile::remove(info.absoluteFilePath());
				Sleep(200);
				if (!result) {
					const QFile::Permissions permissions = QFile::permissions(info.absoluteFilePath());
					if (!(permissions & QFile::WriteUser)) {
						result = QFile::setPermissions(info.absoluteFilePath(), permissions | QFile::WriteUser) && QFile::remove(info.absoluteFilePath());
					}
				}
			}

			if (!result) {
				return result;
			}
		}
		result = dir.rmdir(dirName);
		Sleep(200);
		//result = true;
	}
	return result;
}

bool VfsWindows::removeDir()
{
	ConfigFile cfg;
	QDir mirror_path(cfg.defaultFileStreamMirrorPath());
	return mirror_path.removeRecursively();
}

// TODO hardkode letter drive
void VfsWindows::unmount()
{
	bool unmount_result = DokanUnmount(mountLetter);
	qDebug() << Q_FUNC_INFO << "Unmount Result: " << unmount_result;
}

void VfsWindows::mount()
{
	QString letter = QString(mountLetter);
	qDebug() << Q_FUNC_INFO << " INIT ::rootPath: " << rootPath << " letter: " << letter;
	wcscpy((WCHAR*)(RootDirectory), rootPath.toStdWString().c_str());
	wcscpy((WCHAR*)(MountPoint), letter.toStdWString().c_str());

	int status;
	ULONG command;
	PDOKAN_OPERATIONS dokanOperations = (PDOKAN_OPERATIONS)malloc(sizeof(DOKAN_OPERATIONS));
	if (dokanOperations == NULL) {
		qDebug() << "\n dbg_sync " << Q_FUNC_INFO << " dokanOperations is NULL";
		return;
		//return EXIT_FAILURE;
	}

	PDOKAN_OPTIONS dokanOptions = (PDOKAN_OPTIONS)malloc(sizeof(DOKAN_OPTIONS));
	if (dokanOptions == NULL) {
		qDebug() << "\n dbg_sync " << Q_FUNC_INFO << " dokanOptions is NULL";
		return;
		//free(dokanOperations);
		//return EXIT_FAILURE;
	}

	g_DebugMode = FALSE;
	g_UseStdErr = FALSE;

	/*
		6  DokanOptions options = new DokanOptions
		7  {
		8     DriveLetter = 'Z',
		9     DebugMode = true,
		10     UseStdErr = true,
		11     NetworkDrive = false,
		12     Removable = true,     // provides an "eject"-menu to unmount
		13     UseKeepAlive = true,  // auto-unmount
		14     ThreadCount = 0,      // 0 for default, 1 for debugging
		15     VolumeLabel = "MyDokanDrive"
		16  };
	*/

	ZeroMemory(dokanOptions, sizeof(DOKAN_OPTIONS));
	dokanOptions->Version = DOKAN_VERSION;
	dokanOptions->ThreadCount = 50;			// < Set by file stream support, 
											// < recompile DokanLib DOKAN_MAX_THREAD 501
											// < update dokanc.h
	// dokanOptions->ThreadCount = 0;		// use default

	dokanOptions->MountPoint = MountPoint;

	if (wcscmp(UNCName, L"") != 0 && !(dokanOptions->Options & DOKAN_OPTION_NETWORK)) {
		fwprintf(stderr, L"  Warning: UNC provider name should be set on network drive only.\n");
		qDebug() << "\n dbg_sync " << Q_FUNC_INFO << " Warning: UNC provider name should be set on network drive only.";
	}

	if (dokanOptions->Options & DOKAN_OPTION_NETWORK && dokanOptions->Options & DOKAN_OPTION_MOUNT_MANAGER) {
		fwprintf(stderr, L"Mount manager cannot be used on network drive.\n");
		qDebug() << "\n dbg_sync " << Q_FUNC_INFO << " Mount manager cannot be used on network drive.";
		if(dokanOperations)
			free(dokanOperations);
		if(dokanOptions)
			free(dokanOptions);
		//return EXIT_FAILURE;
	}

	if (!(dokanOptions->Options & DOKAN_OPTION_MOUNT_MANAGER) &&
		wcscmp(MountPoint, L"") == 0) {
		fwprintf(stderr, L"Mount Point required.\n");
		qDebug() << "\n dbg_sync " << Q_FUNC_INFO << " Mount manager cannot be used on network drive.";
		if (dokanOperations)
			free(dokanOperations);
		if (dokanOptions)
			free(dokanOptions);
		//return EXIT_FAILURE;
	}

	if ((dokanOptions->Options & DOKAN_OPTION_MOUNT_MANAGER) && (dokanOptions->Options & DOKAN_OPTION_CURRENT_SESSION)) {
		fwprintf(stderr, L"Mount Manager always mount the drive for all user sessions.\n");
		qDebug() << "\n dbg_sync " << Q_FUNC_INFO << " Mount Manager always mount the drive for all user sessions.";
		if (dokanOperations)
			free(dokanOperations);
		if (dokanOptions)
			free(dokanOptions);
		//return EXIT_FAILURE;
	}

	if (!SetConsoleCtrlHandler(CtrlHandler, TRUE)) {
		fwprintf(stderr, L"Control Handler is not set.\n");
		qDebug() << "\n dbg_sync " << Q_FUNC_INFO << " Control Handler is not set.";
	}

	// Add security name privilege. Required here to handle GetFileSecurity
	// properly.
	g_HasSeSecurityPrivilege = AddSeSecurityNamePrivilege();
	if (!g_HasSeSecurityPrivilege) {
		qDebug() << "\n dbg_sync " << Q_FUNC_INFO << " Failed to add security privilege to process.";
		fwprintf(stderr, L"Failed to add security privilege to process\n");
		fwprintf(stderr, L"\t=> GetFileSecurity/SetFileSecurity may not work properly\n");
		fwprintf(stderr, L"\t=> Please restart mirror sample with administrator " L"rights to fix it\n");
	}

	if (g_ImpersonateCallerUser && !g_HasSeSecurityPrivilege) {
		qDebug() << "\n dbg_sync " << Q_FUNC_INFO << " Impersonate Caller User requires administrator right...";
		fwprintf(stderr, L"Impersonate Caller User requires administrator right to " L"work properly\n");
		fwprintf(stderr, L"\t=> Other users may not use the drive properly\n");
		fwprintf(stderr, L"\t=> Please restart mirror sample with administrator " L"rights to fix it\n");
	}

	if (g_DebugMode) {
		dokanOptions->Options |= DOKAN_OPTION_DEBUG;
	}

	if (g_UseStdErr) {
		dokanOptions->Options |= DOKAN_OPTION_STDERR;
	}

	dokanOptions->Options |= DOKAN_OPTION_ALT_STREAM;

	ZeroMemory(dokanOperations, sizeof(DOKAN_OPERATIONS));

	dokanOperations->ZwCreateFile = MirrorCreateFile;
	dokanOperations->Cleanup = MirrorCleanup;
	dokanOperations->CloseFile = MirrorCloseFile;
	dokanOperations->ReadFile = MirrorReadFile;
	dokanOperations->WriteFile = MirrorWriteFile;
	dokanOperations->FlushFileBuffers = MirrorFlushFileBuffers;
	dokanOperations->GetFileInformation = MirrorGetFileInformation;
	dokanOperations->FindFiles = MirrorFindFiles;
	dokanOperations->FindFilesWithPattern = NULL;
	dokanOperations->SetFileAttributes = MirrorSetFileAttributes;
	dokanOperations->SetFileTime = MirrorSetFileTime;
	dokanOperations->DeleteFile = MirrorDeleteFile;
	dokanOperations->DeleteDirectory = MirrorDeleteDirectory;
	dokanOperations->MoveFile = MirrorMoveFile;
	dokanOperations->SetEndOfFile = MirrorSetEndOfFile;
	dokanOperations->SetAllocationSize = MirrorSetAllocationSize;
	dokanOperations->LockFile = MirrorLockFile;
	dokanOperations->UnlockFile = MirrorUnlockFile;
	dokanOperations->GetFileSecurity = MirrorGetFileSecurity;
	dokanOperations->SetFileSecurity = MirrorSetFileSecurity;
	//< dokanOperations->GetDiskFreeSpace = NULL; // MirrorDokanGetDiskFreeSpace;
	dokanOperations->GetDiskFreeSpace = MirrorDokanGetDiskFreeSpace;
	dokanOperations->GetVolumeInformation = MirrorGetVolumeInformation;
	dokanOperations->Unmounted = MirrorUnmounted;
	
	dokanOperations->FindStreams = MirrorFindStreams;
	dokanOperations->Mounted = MirrorMounted;
	
	struct DokanaMainparams *m_params = (struct DokanaMainparams*) malloc(sizeof(struct DokanaMainparams));
	m_params->m_DokanOptions = dokanOptions;
	m_params->m_DokanOperations = dokanOperations;

	//< create thread
	//HANDLE handles[1];
	//handles[0] = (HANDLE)_beginthread(ThreadFunc, 0, (void*)m_params); 

    std::thread t(ThreadFunc, (void*)m_params);
    t.detach();

	Sleep(1000);
	ConfigFile cfg;
	cfg.createAuxiliarDirectories();
	qDebug() << Q_FUNC_INFO << " END::mount";
}

} // namespace OCC