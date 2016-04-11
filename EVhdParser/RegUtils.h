#pragma once

NTSTATUS Reg_GetDwordValue(_In_ HANDLE hKey, _In_ LPCWSTR pszValueName, _Out_ PULONG32 pValue);
NTSTATUS Reg_SetDwordValue(_In_ HANDLE hKey, _In_ LPCWSTR pszValueName, _In_ ULONG32 Value);
NTSTATUS Reg_GetQwordValue(_In_ HANDLE hKey, _In_ LPCWSTR pszValueName, _Out_ PULONG64 pValue);
NTSTATUS Reg_SetQwordValue(_In_ HANDLE hKey, _In_ LPCWSTR pszValueNamem, _In_ ULONG64 Value);
NTSTATUS Reg_GetGuidValue(_In_ HANDLE hKey, _In_ LPCWSTR pszValueName, _Out_ PGUID pValue);
NTSTATUS Reg_SetGuidValue(_In_ HANDLE hKey, _In_ LPCWSTR pszValueName, _In_ PGUID pValue);
NTSTATUS Reg_GetStringValue(_In_ HANDLE hKey, _In_ LPCWSTR pszValueName, _Out_ PWSTR *ppValue);
