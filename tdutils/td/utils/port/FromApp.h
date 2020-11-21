#pragma once
#include "td/utils/common.h"

#ifdef TD_PORT_WINDOWS
#include <Windows.h>
#if !WINAPI_FAMILY_PARTITION(WINAPI_PARTITION_DESKTOP | WINAPI_PARTITION_SYSTEM)
inline HMODULE GetKernelModule() {
  static HMODULE kernelModule;
  if (kernelModule == nullptr) {
    MEMORY_BASIC_INFORMATION mbi;
    if (VirtualQuery(VirtualQuery, &mbi, sizeof(MEMORY_BASIC_INFORMATION))) {
      kernelModule = reinterpret_cast<HMODULE>(mbi.AllocationBase);
    }
  }

  return kernelModule;
}

inline HMODULE LoadLibrary(LPCTSTR lpFileName) {
  typedef HMODULE(WINAPI * pLoadLibrary)(_In_ LPCTSTR);
  static const auto procLoadLibrary = reinterpret_cast<pLoadLibrary>(GetProcAddress(GetKernelModule(), "LoadLibraryW"));

  return procLoadLibrary(lpFileName);
}

inline HMODULE GetFromAppModule() {
  static const HMODULE fromAppModule = LoadLibrary(L"api-ms-win-core-file-fromapp-l1-1-0.dll");
  return fromAppModule;
}

namespace td {
inline HANDLE CreateFile2FromAppW(_In_ LPCWSTR lpFileName, _In_ DWORD dwDesiredAccess, _In_ DWORD dwShareMode,
                                  _In_ DWORD dwCreationDisposition,
                                  _In_opt_ LPCREATEFILE2_EXTENDED_PARAMETERS pCreateExParams) {
  using pCreateFile2FromAppW =
      HANDLE(WINAPI *)(_In_ LPCWSTR lpFileName, _In_ DWORD dwDesiredAccess, _In_ DWORD dwShareMode,
                       _In_ DWORD dwCreationDisposition, _In_opt_ LPCREATEFILE2_EXTENDED_PARAMETERS pCreateExParams);
  static const auto createFile2FromAppW =
      reinterpret_cast<pCreateFile2FromAppW>(GetProcAddress(GetFromAppModule(), "CreateFile2FromAppW"));
  if (createFile2FromAppW != nullptr) {
    return createFile2FromAppW(lpFileName, dwDesiredAccess, dwShareMode, dwCreationDisposition, pCreateExParams);
  }

  return CreateFile2(lpFileName, dwDesiredAccess, dwShareMode, dwCreationDisposition, pCreateExParams);
}

inline BOOL CreateDirectoryFromAppW(_In_ LPCWSTR lpPathName, _In_opt_ LPSECURITY_ATTRIBUTES lpSecurityAttributes) {
  using pCreateDirectoryFromAppW =
      BOOL(WINAPI *)(_In_ LPCWSTR lpPathName, _In_opt_ LPSECURITY_ATTRIBUTES lpSecurityAttributes);
  static const auto createDirectoryFromAppW =
      reinterpret_cast<pCreateDirectoryFromAppW>(GetProcAddress(GetFromAppModule(), "CreateDirectoryFromAppW"));
  if (createDirectoryFromAppW != nullptr) {
    return createDirectoryFromAppW(lpPathName, lpSecurityAttributes);
  }

  return CreateDirectoryW(lpPathName, lpSecurityAttributes);
}

inline BOOL RemoveDirectoryFromAppW(_In_ LPCWSTR lpPathName) {
  using pRemoveDirectoryFromAppW = BOOL(WINAPI *)(_In_ LPCWSTR lpPathName);
  static const auto removeDirectoryFromAppW =
      reinterpret_cast<pRemoveDirectoryFromAppW>(GetProcAddress(GetFromAppModule(), "RemoveDirectoryFromAppW"));
  if (removeDirectoryFromAppW != nullptr) {
    return removeDirectoryFromAppW(lpPathName);
  }

  return RemoveDirectoryW(lpPathName);
}

inline BOOL DeleteFileFromAppW(_In_ LPCWSTR lpFileName) {
  using pDeleteFileFromAppW = BOOL(WINAPI *)(_In_ LPCWSTR lpFileName);
  static const auto deleteFileFromAppW =
      reinterpret_cast<pDeleteFileFromAppW>(GetProcAddress(GetFromAppModule(), "DeleteFileFromAppW"));
  if (deleteFileFromAppW != nullptr) {
    return deleteFileFromAppW(lpFileName);
  }

  return DeleteFileW(lpFileName);
}

inline BOOL MoveFileExFromAppW(_In_ LPCWSTR lpExistingFileName, _In_ LPCWSTR lpNewFileName, _In_ DWORD dwFlags) {
  if (dwFlags == MOVEFILE_REPLACE_EXISTING) {
    DeleteFileFromAppW(lpNewFileName);
  }

  using pMoveFileFromAppW = BOOL(WINAPI *)(_In_ LPCWSTR lpExistingFileName, _In_ LPCWSTR lpNewFileName);
  static const auto moveFileFromAppW =
      reinterpret_cast<pMoveFileFromAppW>(GetProcAddress(GetFromAppModule(), "MoveFileFromAppW"));
  if (moveFileFromAppW != nullptr) {
    return moveFileFromAppW(lpExistingFileName, lpNewFileName);
  }

  return MoveFileEx(lpExistingFileName, lpNewFileName, dwFlags);
}

inline HANDLE FindFirstFileExFromAppW(_In_ LPCWSTR lpFileName, _In_ FINDEX_INFO_LEVELS fInfoLevelId,
                                      _Out_writes_bytes_(sizeof(WIN32_FIND_DATAW)) LPVOID lpFindFileData,
                                      _In_ FINDEX_SEARCH_OPS fSearchOp, _Reserved_ LPVOID lpSearchFilter,
                                      _In_ DWORD dwAdditionalFlags) {
  using pFindFirstFileExFromAppW = HANDLE(WINAPI *)(_In_ LPCWSTR lpFileName, _In_ FINDEX_INFO_LEVELS fInfoLevelId,
                                                    _Out_writes_bytes_(sizeof(WIN32_FIND_DATAW)) LPVOID lpFindFileData,
                                                    _In_ FINDEX_SEARCH_OPS fSearchOp, _Reserved_ LPVOID lpSearchFilter,
                                                    _In_ DWORD dwAdditionalFlags);
  static const auto findFirstFileExFromAppW =
      reinterpret_cast<pFindFirstFileExFromAppW>(GetProcAddress(GetFromAppModule(), "FindFirstFileExFromAppW"));
  if (findFirstFileExFromAppW != nullptr) {
    return findFirstFileExFromAppW(lpFileName, fInfoLevelId, lpFindFileData, fSearchOp, lpSearchFilter,
                                   dwAdditionalFlags);
  }

  return FindFirstFileExW(lpFileName, fInfoLevelId, lpFindFileData, fSearchOp, lpSearchFilter, dwAdditionalFlags);
}
}  // namespace td
#endif
#endif
