#include "stdafx.h"
#include "H2Sapien.h"
#include "H2ToolsCommon.h"
#include "Patches.h"
#include "resource.h"

typedef HWND(__fastcall *create_main_window)(HMENU thisptr, int __unused, HWND hWndParent, HMENU hMenu, LPCWSTR lpWindowName, DWORD dwStyle, DWORD dwExStyle, HMENU oldmenu, LPVOID lpParam);
create_main_window create_main_window_orginal;

//bool __fastcall load_main_window(int thisptr,int unused, int a2, int a3, int a4, int *a5)
typedef bool(__fastcall *load_main_window)(int thisptr, int unused, int a2, int a3, int a4, int *a5);
load_main_window load_main_window_orginal;

typedef int (__thiscall *main_window_input)(void *thisptr, int a2, UINT uMsg, int hMenu, LPARAM lParam, int a6, int a7);
main_window_input main_window_input_orginal;

bool run_script(char *script_text);

char command_script[0x100];
INT_PTR CALLBACK SapienRunCommandProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	if (uMsg == WM_CLOSE) {
		EndDialog(hwndDlg, 0);
		return true;
	}
	if (uMsg == WM_COMMAND && LOWORD(wParam)) {
		if (GetDlgItemText(hwndDlg, SAPIEN_COMMAND, command_script, sizeof(command_script)))
			run_script(command_script);
		EndDialog(hwndDlg, 0);
		return true;
	}
	return false;
}

int __fastcall main_window_input_hook(void *thisptr, BYTE _, int a2, UINT uMsg, int hMenu, LPARAM lParam, int a6, int a7)
{
	if (uMsg == WM_COMMAND) {
		switch (hMenu) {
			case SAPIEN_FILE_NEWINSTANCE:
			{
				return H2CommonPatches::newInstance();
			}
			case SAPIEN_OPEN_RUN_COMMAND_DIALOG:
			{
				DialogBoxParam(g_hModule, MAKEINTRESOURCE(SAPIEN_COMMAND_DIALOG), 0, SapienRunCommandProc, 0);
				return 1;
			}
		}
	}
	return main_window_input_orginal(thisptr, a2, uMsg, hMenu, lParam, a6, a7);
}

bool __fastcall load_main_window_hook(int thisptr, int unused, int a2, int a3, int a4, int *a5)
{
	int menu_ptr = thisptr + 12;
	HMENU menu = CAST_PTR(HMENU,menu_ptr);
	menu = LoadMenu(g_hModule, MAKEINTRESOURCE(SAPIEN_MENU));
	return load_main_window_orginal(thisptr, 0, a2, a3, a4, a5);
}

HWND __fastcall create_main_window_hook(HMENU thisptr, int __unused, HWND hWndParent, HMENU hMenu, LPCWSTR lpWindowName, DWORD dwStyle, DWORD dwExStyle, HMENU oldmenu, LPVOID lpParam)
{
	HMENU new_menu = LoadMenu(g_hModule, MAKEINTRESOURCE(SAPIEN_MENU));
	return create_main_window_orginal(thisptr, 0, hWndParent, hMenu, lpWindowName, dwStyle, dwExStyle, new_menu, lpParam);
}

bool run_script(char *script_text)
{
	bool return_data;
	int run_command_console = 0x4EC020;
	int esp_backup;

	__asm {
		mov esp_backup, esp
		push 1
		mov	esi, script_text
		call run_command_console
		mov return_data, al
		mov esp, esp_backup
	}
	return return_data;
}

void H2SapienPatches::Init()
{
#pragma region Patches
	// stop the default menu overwriting our custom one
	NopFill(0x47AD09, 0x15);
	// Stop sapien from getting a mutex on the main directory
	NopFill(0x409D3D, 0xD);
#pragma endregion

#pragma region Hooks

	DetourTransactionBegin();
	DetourUpdateThread(GetCurrentThread());

	create_main_window_orginal =CAST_PTR(create_main_window,0x469030);
	DetourAttach(&(PVOID&)create_main_window_orginal, create_main_window_hook);

	load_main_window_orginal = CAST_PTR(load_main_window,0x47ACE0);
	DetourAttach(&(PVOID&)load_main_window_orginal, load_main_window_hook);

	main_window_input_orginal = CAST_PTR(main_window_input, 0x475B60);
	DetourAttach(&(PVOID&)main_window_input_orginal, main_window_input_hook);

	DetourTransactionCommit();
#pragma endregion
}