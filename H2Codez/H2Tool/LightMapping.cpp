#include "H2Tool.h"
#include "Common/TagInterface.h"
#include "Common/DiscordInterface.h"
#include "util/Patches.h"
#include "util/Numerical.h"
#include "util/Process.h"
#include "util/Time.h"
#include <sstream>

#pragma region h2tool_functions

/* setups intial values for all lightmap/lightprobe settings */
static void lightmap_settings_init(bool setup_bsp_errors)
{
	typedef void __cdecl lightmap_settings_init(char setup_bsp_errors);
	auto lightmap_settings_init_impl = reinterpret_cast<lightmap_settings_init*>(0x4C0DC0);
	lightmap_settings_init_impl(setup_bsp_errors);
}

/* Sets all values relating to qualit from setting table */
static void process_lightmap_quality_settings(const wchar_t *quality_string)
{
	typedef void __cdecl process_lightmap_quality_settings(const wchar_t *quality_string);
	auto process_lightmap_quality_settings_impl = reinterpret_cast<process_lightmap_quality_settings*>(0x4C2990);
	process_lightmap_quality_settings_impl(quality_string);
}

/* Main function that does all lightmap related work */
static void do_light_calculations(const wchar_t *scenario_name, const wchar_t *bsp_name)
{
	DWORD do_light_calculations = 0x4C60E0;
	__asm {
		push bsp_name
		mov ecx, scenario_name
		call do_light_calculations
		add esp, 4
	}
}
#pragma endregion

enum lightmapping_distributed_type : int
{
	local,
	slave,
	master
};

struct lightmap_control
{
	datum editable_bitmap_group; // main lightmap bitmap
	datum output_bitmap_group; // used for lightprobes?
	datum radiance_bitmap_group; // used by slave instances
	datum lightmap_group; // lightmap tag
	lightmapping_distributed_type distributed_type;
};

lightmap_control *global_lightmap_control = reinterpret_cast<lightmap_control*>(0xA73CE0);

static lightmapping_distributed_type global_lightmap_control_distributed_type = local;

char lightmap_log_name[0x100] = "lightmap.log";

struct {
	bool is_first = false;
	DWORD fork_count = 0;
	DWORD current_id = 0;
	DWORD *fork_pids;
	HANDLE *parent_fork_handles;
} forks_info;

static bool number_from_string(const wchar_t *count, size_t &number_out)
{
	try {
		number_out = std::stoul(count, 0, numerical::get_base(wstring_to_string.to_bytes(count)));
		return true;
	}
	catch (const std::exception &e)
	{
		LOG_FUNC("Exception thrown: %s", e.what());
		return false;
	}
}

// set count in process memory
static bool set_slave_count(const wchar_t *count)
{
	try {
		size_t number;
		if (!number_from_string(count, number))
			return false;
		forks_info.fork_count = number - 1;
		WriteValue<DWORD>(0xA73D7C, number);
		return true;
	}
	catch (const std::exception &e)
	{
		LOG_FUNC("Exception thrown: %s", e.what());
		return false;
	}
}

void __cdecl generate_lightmaps_slave(const wchar_t *argv[])
{
	lightmap_settings_init(true);
	process_lightmap_quality_settings(argv[2]);
	if (!LOG_CHECK(set_slave_count(argv[3])))
	{
		printf("Invalid instance count\n");
		return;
	}
	size_t slave_id = std::stoul(argv[4], 0, numerical::get_base(wstring_to_string.to_bytes(argv[4])));
	printf(" == Instance id: %d == \n", slave_id);
	WriteValue<DWORD>(0xA73D78, slave_id);

	sprintf_s(lightmap_log_name, "lightmap_slave_%d.log", slave_id);

	global_lightmap_control_distributed_type = slave;

	do_light_calculations(argv[0], argv[1]);
}

static void ASM_FUNC tool_exit()
{
	ExitProcess(0);
}

static int get_slave_id()
{
	static bool is_first_call = true;
	if (is_first_call)
	{
		/* Setup process for forking */
		DiscordInterface::shutdown(); // minimise the amount of code that could break
		forks_info.parent_fork_handles = new HANDLE[forks_info.fork_count];
		forks_info.fork_pids = new DWORD[forks_info.fork_count];

		for (size_t i = 0; i < forks_info.fork_count; i++)
		{
			printf("old fork_info.current_id: %d\n", forks_info.current_id);
			process::fork_info info;
			LOG_CHECK(process::fork(info));
			if (info.is_parent)
			{
				forks_info.is_first = true;
				forks_info.parent_fork_handles[i] = info.handle_other;
				forks_info.fork_pids[i] = info.pid_other;
			} else {
				NopFill(0x4EA72F, 5); // nop font_dispose
				WriteCall(0x751FEB, tool_exit);
				forks_info.is_first = false;
				break; // only first process is allowed to clone
			}
			forks_info.current_id++;  // make sure every process has a new id
		}
		printf("new fork_info.current_id: %d\n", forks_info.current_id);
		is_first_call = false;
	}
	return forks_info.current_id;
}

// saves ecx and ebx register and sets eax to slave id
static void ASM_FUNC get_slave_id_save_registers()
{
	__asm {
		push ecx
		push ebx

		call get_slave_id

		pop ebx
		pop ecx

		ret
	}
}

static void ASM_FUNC slave_id_cmp_edx()
{
	__asm {
		call get_slave_id_save_registers
		cmp edx, eax
		ret
	}
}

static void ASM_FUNC slave_id_cmp_ebx()
{
	__asm {
		call get_slave_id_save_registers
		cmp eax, ebx
		ret
	}
}

/* Patch code that checks slave_id to use our function */
static void patch_slave_id_access()
{
	NopFill(0x4B3161, 6);
	NopFill(0x4B363F, 6);
	NopFill(0x4DE31B, 6);
	NopFill(0x4C6E8D, 6);

	WriteCall(0x4B3161, slave_id_cmp_edx);
	WriteCall(0x4B363F, slave_id_cmp_edx);
	WriteCall(0x4DE31B, slave_id_cmp_edx);
	WriteCall(0x4B95ED, get_slave_id_save_registers);
	WriteCall(0x4BA378, get_slave_id_save_registers);
	WriteCall(0x4E1460, get_slave_id_save_registers);
	WriteCall(0x4C6E8D, slave_id_cmp_ebx);
}

DWORD __cdecl TAG_SAVE_RADIANCE_FORK(int TAG_INDEX)
{
	std::string name = tags::get_name(TAG_INDEX);
	name = name.substr(0, name.size() - 1); // remove number
	name = name + std::to_string(forks_info.current_id);
	printf("%s\n", name.c_str());
	tags::rename_tag(TAG_INDEX, name);
	return tags::save_tag(TAG_INDEX);
}

void __cdecl generate_lightmaps_fork_slave(const wchar_t *argv[])
{
	PatchCall(0x4C7247, TAG_SAVE_RADIANCE_FORK); // fix name used when saving
	patch_slave_id_access();

	lightmap_settings_init(true);
	process_lightmap_quality_settings(argv[2]);
	if (!LOG_CHECK(set_slave_count(argv[3])))
	{
		printf("Invalid instance count\n");
		return;
	}
	// set slave id to zero to make code work
	WriteValue<DWORD>(0xA73D78, 0);

	sprintf_s(lightmap_log_name, "lightmap_slave_fork.log");

	global_lightmap_control_distributed_type = slave;

	do_light_calculations(argv[0], argv[1]);
}

void __cdecl generate_lightmaps_master(const wchar_t *argv[])
{
	lightmap_settings_init(true);
	process_lightmap_quality_settings(argv[2]);
	if (!LOG_CHECK(set_slave_count(argv[3])))
	{
		printf("Invalid slave count\n");
		return;
	}

	global_lightmap_control_distributed_type = master;
	sprintf_s(lightmap_log_name, "lightmap_master.log");

	do_light_calculations(argv[0], argv[1]);
}

/*
	Starts muiltiple lightmappers so you can't have to
*/
void _cdecl generate_lightmaps_local_multi_process(const wchar_t *argv[])
{
	auto start_time = std::chrono::high_resolution_clock::now();

	size_t slave_count;
	if (!LOG_CHECK(number_from_string(argv[3], slave_count)))
	{
		printf("Failed to get slave count\n");
		return;
	}
	
	// enforce some sanity checks in non-debug builds
	if (!is_debug_build())
	{
		if (slave_count <= 1)
		{
			printf("At least two slave processes are required for multi process mode.\n");
			return;
		}
	}

	printf("== Starting %d farm processes ==\n", slave_count);

	std::wstring common_command_line = L"lightmaps-slave \"" + std::wstring(argv[0]) +  L"\" " + argv[1] + L" " + argv[2] + L" " + argv[3];
	HANDLE *child_handles = new HANDLE[slave_count];
	for (size_t i = 0; i < slave_count; i++)
	{
		std::wstring command_line = common_command_line + L" " + std::to_wstring(i);
		if (!process::newInstance(command_line, &child_handles[i]))
		{
			printf("Failed to start child process %d\n", i);
			return;
		}
	}

	printf(" == Waiting for child processes to exit ==\n");
	WaitForMultipleObjects(slave_count, child_handles, TRUE, INFINITE);

	printf("== Starting merge ==\n");
	flushall(); // flush console output to prevent graphical bugs
	Sleep(1000); // wait a bit
	generate_lightmaps_master(argv);

	auto end_time = std::chrono::high_resolution_clock::now();
	auto time_taken = std::chrono::duration_cast<std::chrono::seconds>(end_time - start_time);
	std::string time_taken_human = beautify_duration(time_taken);
	printf("== Time taken: %s ==", time_taken_human.c_str());
}

// hook lightmap tag save call to save monochrome bitmap and fix lightprobes crashing
DWORD __cdecl TAG_SAVE_LIGHTMAP_HOOK(int TAG_INDEX)
{
	tags::save_tag(global_lightmap_control->editable_bitmap_group);
	tags::save_tag(global_lightmap_control->output_bitmap_group);
	if (!global_lightmap_control->editable_bitmap_group.is_valid()) // fix lightprobes crashing
		global_lightmap_control->editable_bitmap_group = global_lightmap_control->output_bitmap_group;
	return 1;
}

void H2ToolPatches::reenable_lightmap_farming()
{
	// hook lightmap control to work around some logging getting disabled when not in local mode
	constexpr DWORD lightmap_distributed_type_offsets[] = {
		0x4B314F, 0x4B362D, 0x4BB402, 0x4BD5CF,
		0x4BD6C1, 0x4BD877, 0x4BD9C1, 0x4E27AF,
		0x4C7206, 0x4C70D9, 0x4C6FD6, 0x4C6E50,
		0x4C6DAE, 0x4C6B77, 0x4C6A2F, 0x4C1F5F,
		0x4C1039, 0x4BFCA9, 0x4BF6FC
	};

	for (DWORD offset : lightmap_distributed_type_offsets)
		WritePointer(offset + 2, &global_lightmap_control_distributed_type);
	WritePointer(0x4C6F40 + 1, &global_lightmap_control_distributed_type);
	WritePointer(0x4C6E7F + 1, &global_lightmap_control_distributed_type);

	// Fix log name being weird due to type confusion
	WritePointer(0x4C1BDA + 1, "%s_%ws_%s");
	WritePointer(0x4C1B92 + 1, "%s\\tags\\%s_%ws_%s");

	// patch the log names to allow multiple lightmappers to work on one map
	WritePointer(0x4C1BBF + 1, lightmap_log_name);
	WritePointer(0x4C1B76 + 1, lightmap_log_name);

	// disabled as most people have no use for this
	//PatchCall(0x4C70EB, TAG_SAVE_LIGHTMAP_HOOK);
}
