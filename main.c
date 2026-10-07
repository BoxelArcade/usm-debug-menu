#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <math.h>
#include <intrin.h>
#include "forwards.h"
#include "slf.h"
#include "slf_functions.h"

#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>
#include <stdint.h>
#pragma comment(lib, "Dinput8.lib")
#pragma comment(lib, "Dxguid.lib")


#define panic(x) {\
	if (NULL == x) \
	{ \
		printf("Quitting as %s is null", #x); \
		exit(-1); \
	} \
}

void* my_malloc(size_t _Size) {
	void* res = malloc(_Size);
	panic(res);
	return res;
}

void* g_game_ptr = NULL;
void* g_world_ptr = (void*)0x0095C770;

DWORD* ai_current_player = NULL;
DWORD* fancy_player_ptr = NULL;

char* injected_pack = NULL;


DWORD for_pack_loading[2];


#undef IsEqualGUID
BOOL WINAPI IsEqualGUID(
	REFGUID rguid1,
	REFGUID rguid2)
{
	return !memcmp(rguid1, rguid2, sizeof(GUID));
}



uint8_t color_ramp_function(float ratio, int period_duration, int cur_time) {

	if (cur_time <= 0 || 4 * period_duration <= cur_time)
		return 0;

	if (cur_time < period_duration) {

		float calc = ratio * cur_time;

		return (uint8_t)(min(calc, 1.0f) * 255);
	}


	if (period_duration <= cur_time && cur_time <= 3 * period_duration) {
		return 255;
	}


	if (cur_time <= 4 * period_duration) {

		int adjusted_time = cur_time - 3 * period_duration;
		float calc = 1.f - ratio * adjusted_time;

		return (uint8_t)(min(calc, 1.0f) * 255);
	}

	// never reached
	return 0;
}


typedef struct _list{
	struct _list* next;
	struct _list* prev;
	void* data;
}list;

typedef struct {
	DWORD unk0;
	DWORD strlen;
	char* actualString;
	BYTE unk[256];//not sure how big
}mString;

#define MAX_CHARS_SAFE 63
#define MAX_CHARS MAX_CHARS_SAFE+1
#define EXTEND_NEW_ENTRIES 20
#define MAX_ELEMENTS_PAGE 18

#pragma pack(1)
typedef struct {
	uint8_t unk[0x50];
	uint8_t status;
	uint8_t unk1[0x53];
	float x;
	float y;
	float z;
	uint8_t unk2[0x10];
	DWORD district_id;
	uint8_t unk3[0x4];
	uint8_t variants;
	uint8_t unk4[0x6B];
}region;


typedef enum {
	NORMAL,
	BOOLEAN_E,
	CUSTOM
}debug_menu_entry_type;


typedef enum {
	LEFT,
	RIGHT,
	ENTER
}custom_key_type;


struct _debug_menu_entry;
typedef void (*custom_string_generator_ptr)(struct _debug_menu_entry* entry);

typedef void (*menu_handler_function)(struct _debug_menu_entry*, custom_key_type key_type);
typedef void (*custom_entry_handler_ptr)(struct _debug_menu_entry* entry, custom_key_type key_type, menu_handler_function menu_handler);
typedef struct _debug_menu_entry {

	char text[MAX_CHARS];
	debug_menu_entry_type entry_type;
	void* data;
	void* data1;
	custom_string_generator_ptr custom_string_generator;
	custom_entry_handler_ptr custom_handler;
}debug_menu_entry;

typedef void (*go_back_function)();

typedef struct {
	char title[MAX_CHARS];
	DWORD capacity;
	DWORD used_slots;
	DWORD window_start;
	DWORD cur_index;
	go_back_function go_back;
	menu_handler_function handler;
	debug_menu_entry* entries;
}debug_menu;



debug_menu* start_debug = NULL;
debug_menu* warp_menu = NULL;
debug_menu* district_variants_menu = NULL;
debug_menu* char_select_menu = NULL;
debug_menu* options_menu = NULL;
debug_menu* script_menu = NULL;
debug_menu* progression_menu = NULL;
debug_menu* add_player_menu = NULL;


debug_menu** all_menus[] = {
	&start_debug,
	&warp_menu,
	&district_variants_menu,
	&char_select_menu,
	&options_menu,
	&script_menu,
	&progression_menu,
	&add_player_menu
};

debug_menu* current_menu = NULL;



void goto_start_debug() {
	current_menu = start_debug;
}



void unlock_region(region* cur_region) {

	cur_region->status &= 0xFE;
}

void remove_debug_menu_entry(debug_menu_entry* entry) {
	

	DWORD to_be = (DWORD)entry;
	for (int i = 0; i < (sizeof(all_menus) / sizeof(void*)); i++) {

		debug_menu *cur = *all_menus[i];

		DWORD start = (DWORD)cur->entries;
		DWORD end = start + cur->used_slots * sizeof(debug_menu_entry);

		if (start <= (DWORD)entry && (DWORD)entry < end) {


			int index = (to_be - start) / sizeof(debug_menu_entry);

			memcpy(&cur->entries[index], &cur->entries[index + 1], cur->used_slots - (index + 1));
			memset(&cur->entries[cur->used_slots - 1], 0, sizeof(debug_menu_entry));
			cur->used_slots--;
			return;
		}
		
	}

	printf("FAILED TO DEALLOCATE AN ENTRY :S %08X\n", (DWORD)entry);

}

void* add_debug_menu_entry(debug_menu* menu, debug_menu_entry* entry) {

	if (menu->used_slots < menu->capacity) {
		void* ret = &menu->entries[menu->used_slots];
		memcpy(ret, entry, sizeof(debug_menu_entry));
		menu->used_slots++;
		return ret;
	}

	DWORD current_entries_size = sizeof(debug_menu_entry) * menu->capacity;
	DWORD new_entries_size = sizeof(debug_menu_entry) * EXTEND_NEW_ENTRIES;


	void* new_ptr = realloc(menu->entries, current_entries_size + new_entries_size);

	panic(new_ptr);
	menu->capacity += EXTEND_NEW_ENTRIES;
	menu->entries = new_ptr;
	memset(&menu->entries[menu->used_slots], 0, new_entries_size);

	return add_debug_menu_entry(menu, entry);
}




debug_menu* create_menu(const char* title, go_back_function go_back, menu_handler_function function, DWORD capacity) {

	debug_menu* menu = my_malloc(sizeof(debug_menu));

	memset(menu, 0, sizeof(debug_menu));

	strncpy(menu->title, title, MAX_CHARS_SAFE);

	menu->capacity = capacity;
	menu->handler = function;
	menu->go_back = go_back;

	DWORD total_entries_size = sizeof(debug_menu_entry) * capacity;
	menu->entries = my_malloc(total_entries_size);
	memset(menu->entries, 0, total_entries_size);

	return menu;

}


int vm_debug_menu_entry_garbage_collection_id = -1;

typedef int (*script_manager_register_allocated_stuff_callback_ptr)(void* func);
script_manager_register_allocated_stuff_callback_ptr script_manager_register_allocated_stuff_callback = (script_manager_register_allocated_stuff_callback_ptr)0x005AFE40;

typedef int (*construct_client_script_libs_ptr)();
construct_client_script_libs_ptr construct_client_script_libs = (construct_client_script_libs_ptr)0x0058F9C0;


void vm_debug_menu_entry_garbage_collection_callback(void* a1, list* lst) {

	list* end = lst->prev;

	for (list* cur = end->next; cur != end; cur = cur->next) {

		debug_menu_entry* entry = ((debug_menu_entry*)cur->data);
		//printf("Will delete %s %08X\n", entry->text, entry);
		remove_debug_menu_entry(entry);
	}
	
}

int construct_client_script_libs_hook() {


	if (vm_debug_menu_entry_garbage_collection_id == -1) {
		int res = script_manager_register_allocated_stuff_callback(vm_debug_menu_entry_garbage_collection_callback);
		vm_debug_menu_entry_garbage_collection_id = res;
	}
	return construct_client_script_libs();
}


typedef (__fastcall* mString_constructor_ptr)(mString* this, void* edx, char* str);
mString_constructor_ptr mString_constructor = (void*)0x00421100;

typedef (__fastcall* mString_finalize_ptr)(mString* this, void* edx, int zero);
mString_finalize_ptr mString_finalize = (void*)0x004209C0;


region** all_regions = (region**)0x0095C924;
DWORD* number_of_allocated_regions = (DWORD*)0x0095C920;

typedef char* (__fastcall* region_get_name_ptr)(void* this);
region_get_name_ptr region_get_name = (region_get_name_ptr)0x00519BB0;


typedef int (__fastcall *region_get_district_variant_ptr)(region* this);
region_get_district_variant_ptr region_get_district_variant = (region_get_district_variant_ptr)0x005503D0;


typedef char(__fastcall* terrain_set_district_variant_ptr)(void* this, void* edx, DWORD district_id, int variant, char one);
terrain_set_district_variant_ptr terrain_set_district_variant = (terrain_set_district_variant_ptr)0x00557480;


typedef void (*us_lighting_switch_time_of_day_ptr)(int time_of_day);
us_lighting_switch_time_of_day_ptr us_lighting_switch_time_of_day = (void*)0x00408790;

void set_text_writeable() {

	const DWORD text_end = 0x86F000;
	const DWORD text_start = 0x401000;

	DWORD old;
	VirtualProtect((void*)text_start, text_end - text_start, PAGE_EXECUTE_READWRITE, &old);
}

void set_rdata_writeable() {

	const DWORD end = 0x91B000;
	const DWORD start = 0x86F564;

	DWORD old;
	VirtualProtect((void*)start, end - start, PAGE_READWRITE, &old);
}

void HookFunc(DWORD callAdd, void* funcAdd, BOOLEAN jump, const unsigned char* reason) {

	//Only works for E8/E9 hooks	
	DWORD jmpOff = (DWORD)funcAdd - callAdd - 5;

	BYTE shellcode[] = { 0, 0, 0, 0, 0 };
	shellcode[0] = jump ? 0xE9 : 0xE8;

	memcpy(&shellcode[1], &jmpOff, sizeof(jmpOff));
	memcpy((void*)callAdd, shellcode, sizeof(shellcode));

	if (reason)
		printf("Hook: %08X -  %s\n", callAdd, reason);

}


void WriteDWORD(DWORD address, void* newValue, const unsigned char* reason) {
	*(DWORD*)address = (DWORD)newValue;
	if (reason)
		printf("Wrote: %08X -  %s\n", (DWORD)address, reason);
}

typedef int (*nflSystemOpenFile_ptr)(HANDLE* hHandle, LPCSTR lpFileName, unsigned int a3, LARGE_INTEGER liDistanceToMove);
nflSystemOpenFile_ptr nflSystemOpenFile_orig = NULL;

nflSystemOpenFile_ptr* nflSystemOpenFile_data = (void*)0x0094985C;


HANDLE USM_handle = INVALID_HANDLE_VALUE;

int nflSystemOpenFile(HANDLE* hHandle, LPCSTR lpFileName, unsigned int a3, LARGE_INTEGER liDistanceToMove) {


	//printf("Opening file %s\n", lpFileName);
	int ret = nflSystemOpenFile_orig(hHandle, lpFileName, a3, liDistanceToMove);


	if (strstr(lpFileName, "ultimate_spiderman.PCPACK")) {

	}
	return ret;
}



typedef int (*ReadOrWrite_ptr)(int a1, HANDLE* a2, int a3, DWORD a4, LPCVOID lpBuffer, DWORD nNumberOfBytesToWrite);
ReadOrWrite_ptr* ReadOrWrite_data = (void*)0x0094986C;
ReadOrWrite_ptr ReadOrWrite_orig = NULL;

int ReadOrWrite(int a1, HANDLE* a2, int a3, DWORD a4, LPCVOID lpBuffer, DWORD nNumberOfBytesToWrite) {

	int ret = ReadOrWrite_orig(a1, a2, a3, a4, lpBuffer, nNumberOfBytesToWrite);

	if (USM_handle == *a2) {
		printf("USM buffer was read %08X\n", (DWORD)lpBuffer);


	}
	return ret;
}


typedef void (*aeps_RenderAll_ptr)();
aeps_RenderAll_ptr aeps_RenderAll_orig = (void*)0x004D9310;

void** nglSysFont = (void**)0x00975208;

typedef void (*nglListAddString_ptr)(void* font, float x, float y, float z, DWORD color, float x_scale, float y_scale, char* format, ...);
nglListAddString_ptr nglListAddString = (void*)0x00779E90;



#define nglColor(r,g,b,a) ( (a << 24) |  (r << 16) | (g << 8) | (b & 255) )


typedef struct {
	BYTE unk[100];
}nglQuad;

typedef void (*nglInitQuad_ptr)(void*);
nglInitQuad_ptr nglInitQuad = (void*)0x0077AC40;


typedef void (*nglSetQuadRect_ptr)(void*, float, float, float, float);
nglSetQuadRect_ptr nglSetQuadRect = (void*)0x0077AD30;


typedef void (*nglSetQuadColor_ptr)(void*, unsigned int);
nglSetQuadColor_ptr nglSetQuadColor = (void*)0x0077AD10;


typedef void (*nglListAddQuad_ptr)(void*);
nglListAddQuad_ptr nglListAddQuad = (void*)0x0077AFE0;


typedef int (*nglListBeginScene_ptr)(int);
nglListBeginScene_ptr nglListBeginScene = (void*)0x0076C970;


typedef void (*nglListEndScene_ptr)();
nglListEndScene_ptr nglListEndScene = (void*)0x00742B50;


typedef void (*nglSetQuadZ_ptr)(void*, float);
nglSetQuadZ_ptr nglSetQuadZ = (void*)0x0077AD70;

typedef void (*nglSetClearFlags_ptr)(int);
nglSetClearFlags_ptr nglSetClearFlags = (void*)0x00769DB0;

void draw_p2_indicator(void);

void aeps_RenderAll() {



	static cur_time = 0;
	int period = 60;
	int duration = 6 * period;
	float ratio = 1.f / period;

	uint8_t red = color_ramp_function(ratio, period, cur_time + 2 * period) + color_ramp_function(ratio, period, cur_time - 4 * period);
	uint8_t green = color_ramp_function(ratio, period, cur_time);
	uint8_t blue = color_ramp_function(ratio, period, cur_time - 2 * period);

	nglListAddString(*nglSysFont, 0.1f, 0.2f, 0.2f, nglColor(red, green, blue, 255), 1.f, 1.f, "Krystalgamer's Debug menu");

	cur_time = (cur_time + 1) % duration;

	draw_p2_indicator();


	aeps_RenderAll_orig();
}


int debug_enabled = 0;
uint32_t keys[256];





typedef int (*nglGetStringDimensions_ptr)(void*, char* EndPtr, int*, int*, float, float);
nglGetStringDimensions_ptr nglGetStringDimensions = (void*)0x007798E0;



void getStringDimensions(char* str, int* width, int* height) {
	nglGetStringDimensions(*nglSysFont, str, width, height, 1.0, 1.0);
}

int getStringHeight(char* str) {
	int height;
	nglGetStringDimensions(*nglSysFont, str, NULL, &height, 1.0, 1.0);
	return height;
}


char* getRealText(debug_menu_entry* entry, char* str) {



	if (entry->entry_type == BOOLEAN_E) {
		BYTE* val = entry->data;
		sprintf(str, "%s: %s", entry->text, *val ? "True" : "False");
		return str;
	}

	if (entry->entry_type == CUSTOM) {
		entry->custom_string_generator(entry);
	}


	return entry->text;
}

void render_current_debug_menu() {



	char text_buffer[128];
#define UP_ARROW " ^ ^ ^ "
#define DOWN_ARROW " v v v "


	int num_elements = min(MAX_ELEMENTS_PAGE, current_menu->used_slots - current_menu->window_start);
	int needs_down_arrow = ((current_menu->window_start + MAX_ELEMENTS_PAGE) < current_menu->used_slots) ? 1 : 0;


	int cur_width, cur_height;
	int debug_width = 0;
	int debug_height = 0;

#define get_and_update(x) {\
	 getStringDimensions(x, &cur_width, &cur_height); \
	 debug_height += cur_height; \
	 debug_width = max(debug_width, cur_width);\
	}
	//printf("new size: %s %d %d (%d %d)\n", x, debug_width, debug_height, cur_width, cur_height); \


	get_and_update(current_menu->title);
	get_and_update(UP_ARROW);





	int total_elements_page = needs_down_arrow ? MAX_ELEMENTS_PAGE : current_menu->used_slots - current_menu->window_start;

	for (int i = 0; i < total_elements_page; i++) {

		debug_menu_entry *entry = &current_menu->entries[current_menu->window_start + i];
		char* cur = getRealText(entry, text_buffer);
		get_and_update(cur);
	}


	if (needs_down_arrow) {
		get_and_update(DOWN_ARROW);
	}

	nglQuad quad;


	int menu_x_start = 20, menu_y_start = 40;
	int menu_x_pad = 24, menu_y_pad = 18;

	nglInitQuad(&quad);
	nglSetQuadRect(&quad, (float)menu_x_start, (float)menu_y_start, (float)(menu_x_start + debug_width + menu_x_pad), (float)(menu_y_start + debug_height + menu_y_pad));
	nglSetQuadColor(&quad, 0xBE0A0A0A);
	nglSetQuadZ(&quad, 0.5f);
	nglListAddQuad(&quad);


	int white_color = nglColor(255, 255, 255, 255);
	int yellow_color = nglColor(255, 255, 0, 255);
	int green_color = nglColor(0, 255, 0, 255);
	int pink_color = nglColor(255, 0, 255, 255);


	int render_height = menu_y_start;
	render_height += 12;
	int render_x = menu_x_start;
	render_x += 8;
	nglListAddString(*nglSysFont, (float)render_x, (float)(render_height), 0.2f, green_color, 1.f, 1.f, current_menu->title);
	render_height += getStringHeight(current_menu->title);


	if (current_menu->window_start) {
		nglListAddString(*nglSysFont, (float)render_x, (float)render_height, 0.2f, pink_color, 1.f, 1.f, UP_ARROW);
	}
	render_height += getStringHeight(UP_ARROW);



	for (int i = 0; i < total_elements_page; i++) {

		int current_color = current_menu->cur_index == i ? yellow_color : white_color;

		debug_menu_entry* entry = &current_menu->entries[current_menu->window_start + i];
		char* cur = getRealText(entry, text_buffer);
		nglListAddString(*nglSysFont, (float)render_x, (float)render_height, 0.2f, current_color, 1.f, 1.f, cur);
		render_height += getStringHeight(cur);
	}

	if (needs_down_arrow) {
		nglListAddString(*nglSysFont, (float)render_x, (float)render_height, 0.2f, pink_color, 1.f, 1.f, DOWN_ARROW);
		render_height += getStringHeight(DOWN_ARROW);
	}




}
void myDebugMenu() {


	if (debug_enabled) {
		render_current_debug_menu();
	}
	nglListEndScene();
}


typedef int (*wndHandler_ptr)(HWND, UINT, WPARAM, LPARAM);
wndHandler_ptr orig_WindowHandler = (void*)0x005941A0;


/*
	STDMETHOD(GetDeviceState)(THIS_ DWORD,LPVOID) PURE;
	STDMETHOD(GetDeviceData)(THIS_ DWORD,LPDIDEVICEOBJECTDATA,LPDWORD,DWORD) PURE;

*/


typedef int(__stdcall* GetDeviceState_ptr)(IDirectInputDevice8*, DWORD, LPVOID);
GetDeviceState_ptr GetDeviceStateOriginal = NULL;



typedef (__fastcall* game_pause_unpause_ptr)(void* this);
game_pause_unpause_ptr game_pause = (game_pause_unpause_ptr)0x0054FBE0;
game_pause_unpause_ptr game_unpause = (game_pause_unpause_ptr)0x0053A880;




typedef (__fastcall* game_get_cur_state_ptr)(void* this);
game_get_cur_state_ptr game_get_cur_state = (game_get_cur_state_ptr)0x005363D0;



typedef (__fastcall* world_dynamics_system_remove_player_ptr)(void* this, void* edx, int number);
world_dynamics_system_remove_player_ptr world_dynamics_system_remove_player = (void*)0x00558550;


typedef (__fastcall* world_dynamics_system_add_player_ptr)(void* this, void* edx, mString* str);
world_dynamics_system_add_player_ptr world_dynamics_system_add_player = (void*)0x0055B400;


DWORD changing_model = 0;
char* current_costume = "ultimate_spiderman";

// 2nd player experiment: spawn an extra hero WITHOUT removing the existing one
DWORD adding_second_player = 0;
char second_costume[64] = "venom";

/*
 * Second hero, reusing the game's own add_player (0x0055B400).
 *
 * What the disassembly shows:
 *   - add_player returns immediately if world[+0x238] (player count) >= 1.
 *   - It stores the new hero entity at world[+0x230 + count*4] and a per-player
 *     controller object at world[+0x234 + count*4]. The world struct only has
 *     room for ONE of each: with count == 1 the second store would land on the
 *     count field itself. So simply NOPing the guard would corrupt the world.
 *   - `this` is only used at +0x230, +0x234, +0x238 and +0x3E0 (current hero name).
 *
 * So we call the real add_player against a SHADOW block that looks like
 * "no players yet" (count = 0), then keep the results in our own variables.
 * The real world struct is never written. Two pieces of global state are
 * touched by the count == 0 path and are snapshotted/restored:
 *   - the 32-byte current-hero-name buffer at [[0x9682E0]+0xC0]+0x454
 *   - the global at 0x959A70 (set to the new controller object for player 0)
 *
 * Known limitations of this first experiment:
 *   - the player index passed to the hero's brain object is 0, so the second
 *     hero will most likely read the SAME input as the first (a mirror)
 *   - the entity is named "HERO" like the first one
 *   - the second hero is not in the world's player list
 */
DWORD* second_hero_entity = NULL;
DWORD* second_hero_ctrl = NULL;

// logs to the console AND appends to usm_2p_log.txt (the console closes when the game dies)
static void twop_log(const char* fmt, ...) {
	va_list args;
	va_start(args, fmt);
	vprintf(fmt, args);
	va_end(args);

	FILE* f = fopen("usm_2p_log.txt", "a");
	if (f) {
		va_start(args, fmt);
		vfprintf(f, fmt, args);
		va_end(args);
		fclose(f);
	}
}

static int second_hero_filter(EXCEPTION_POINTERS* ep) {
	EXCEPTION_RECORD* er = ep->ExceptionRecord;
	CONTEXT* c = ep->ContextRecord;

	twop_log("[2P] EXCEPTION %08X at %08X\n", (unsigned)er->ExceptionCode, (unsigned)(DWORD)er->ExceptionAddress);
	if (er->NumberParameters >= 2)
		twop_log("[2P] fault type: %s of address %08X\n", er->ExceptionInformation[0] ? "WRITE" : "READ", (unsigned)er->ExceptionInformation[1]);
	twop_log("[2P] eax=%08X ecx=%08X edx=%08X ebx=%08X\n", (unsigned)c->Eax, (unsigned)c->Ecx, (unsigned)c->Edx, (unsigned)c->Ebx);
	twop_log("[2P] esi=%08X edi=%08X ebp=%08X esp=%08X\n", (unsigned)c->Esi, (unsigned)c->Edi, (unsigned)c->Ebp, (unsigned)c->Esp);

	// walk the stack looking for return addresses inside USM.exe's code (value right after a CALL instruction)
	DWORD* sp = (DWORD*)c->Esp;
	int printed = 0;
	for (int i = 0; i < 384 && printed < 32; i++) {
		DWORD v = sp[i];
		if (v >= 0x00401000 && v < 0x0086F000) {
			BYTE* b = (BYTE*)v;
			if (b[-5] == 0xE8 || (b[-2] == 0xFF && (b[-1] & 0x38) == 0x10) || (b[-3] == 0xFF && (b[-2] & 0x38) == 0x10) || (b[-6] == 0xFF && (b[-5] & 0x38) == 0x10)) {
				twop_log("[2P]   return address %08X (esp+%X)\n", (unsigned)v, i * 4);
				printed++;
			}
		}
	}
	return EXCEPTION_EXECUTE_HANDLER;
}

// every extra hero we spawned (so hotkeys / later input code can find them)
DWORD* extra_heroes[16];
int extra_hero_count = 0;

// each extra hero also got its own chase camera object (the 2nd object add_player builds)
DWORD* extra_cams[16];
int extra_cam_count = 0;

// Experiment (F9 / F10): each hero has a "brain" object at entity+0x8C whose +0x14 field holds the
// player index passed to 0x4C0CD0 by add_player. Set it for all extra heroes and log what happens.
static void set_extra_heroes_player_index(int idx) {
	for (int i = 0; i < extra_hero_count; i++) {
		DWORD* brain = (DWORD*)extra_heroes[i][0x8C / 4];
		if (!brain) {
			twop_log("[IDX] extra hero %d (%08X) has no brain object\n", i, (unsigned)(DWORD)extra_heroes[i]);
			continue;
		}
		twop_log("[IDX] extra hero %d entity %08X brain %08X: player index %d -> %d\n",
			i, (unsigned)(DWORD)extra_heroes[i], (unsigned)(DWORD)brain, (int)brain[0x14 / 4], idx);
		brain[0x14 / 4] = idx;
	}
	if (!extra_hero_count)
		twop_log("[IDX] no extra heroes spawned yet\n");
}

/*
 * ---- pad objects -------------------------------------------------------------------------
 * [0x967BB0] is a pad manager: vtable, then 4 pad objects (0x9C bytes each) at +4,+8,+0xC,+0x10.
 * Pad i has script id 0xF4240+i at +4, and a per-frame update() (vtable slot 9, 0x58E5C0) that
 * calls get_pad(pad[+0x70], &pad[+8]) to fill its snapshot. Heroes never touch the manager
 * directly, so each hero must keep a pointer to the pad it reads. All of them point at pad 0.
 */
static DWORD* pad_manager_pad(int i) {
	DWORD* mgr = *(DWORD**)0x00967BB0;
	return mgr ? (DWORD*)mgr[1 + i] : NULL;
}

static void log_pads(void) {
	DWORD* mgr = *(DWORD**)0x00967BB0;
	twop_log("[PAD] manager %08X, connected-pad mask %08X\n", (unsigned)(DWORD)mgr, (unsigned)*(DWORD*)0x00965AC8);
	for (int i = 0; i < 4; i++) {
		DWORD* p = pad_manager_pad(i);
		if (!p) {
			twop_log("[PAD]   pad %d = NULL\n", i);
			continue;
		}
		twop_log("[PAD]   pad %d = %08X vtbl %08X id(+4) %u (+0x70) %d (+0x88) %d (+0x90) %d\n", i,
			(unsigned)(DWORD)p, (unsigned)p[0], (unsigned)p[1], (int)p[0x70 / 4], (int)p[0x88 / 4], (int)p[0x90 / 4]);
	}
}

// look for pointers to the pad objects (and their script ids) inside an object's memory
static void scan_object_for_pads(const char* what, DWORD* obj, int bytes) {
	if (!obj)
		return;
	__try {
		for (int off = 0; off < bytes; off += 4) {
			DWORD v = *(DWORD*)((BYTE*)obj + off);
			for (int i = 0; i < 4; i++) {
				if (v == (DWORD)pad_manager_pad(i) && v)
					twop_log("[SCAN] %s+0x%X = pads[%d]\n", what, off, i);
				if (v == 0xF4240u + i)
					twop_log("[SCAN] %s+0x%X = pad script id %u\n", what, off, (unsigned)v);
			}
			if (v == *(DWORD*)0x00967BB0 && v)
				twop_log("[SCAN] %s+0x%X = pad manager\n", what, off);
		}
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		twop_log("[SCAN] %s: read fault while scanning\n", what);
	}
}

static void scan_hero_for_pads(const char* label, DWORD* hero) {
	char name[48];
	if (!hero)
		return;
	sprintf(name, "%s entity %08X", label, (unsigned)(DWORD)hero);
	scan_object_for_pads(name, hero, 0x200);
	DWORD* brain = (DWORD*)hero[0x8C / 4];
	if (brain) {
		sprintf(name, "%s brain %08X", label, (unsigned)(DWORD)brain);
		scan_object_for_pads(name, brain, 0x424);
	}
}

// Experiment (F9 / F10): replace pad pointers found inside the extra heroes with another pad.
static void repoint_extra_heroes_pad(int to_pad) {
	DWORD* to = pad_manager_pad(to_pad);
	DWORD* from = pad_manager_pad(to_pad ? 0 : 1);
	if (!to || !from) {
		twop_log("[PAD] cannot repoint, pad missing\n");
		return;
	}
	twop_log("[PAD] repointing extra heroes: %08X (pad %d) -> %08X (pad %d)\n",
		(unsigned)(DWORD)from, to_pad ? 0 : 1, (unsigned)(DWORD)to, to_pad);
	for (int h = 0; h < extra_hero_count; h++) {
		DWORD* objs[2];
		int sizes[2] = { 0x200, 0x424 };
		objs[0] = extra_heroes[h];
		objs[1] = (DWORD*)extra_heroes[h][0x8C / 4];
		for (int k = 0; k < 2; k++) {
			if (!objs[k])
				continue;
			__try {
				for (int off = 0; off < sizes[k]; off += 4) {
					DWORD* slot = (DWORD*)((BYTE*)objs[k] + off);
					if (*slot == (DWORD)from) {
						*slot = (DWORD)to;
						twop_log("[PAD]   hero %d object %d +0x%X patched\n", h, k, off);
					}
				}
			}
			__except (EXCEPTION_EXECUTE_HANDLER) {
				twop_log("[PAD]   hero %d object %d: read fault\n", h, k);
			}
		}
	}
}

// raw gamepad values as DirectInput delivers them (to learn axis ranges); rate limited
static void log_joy_sample(LPDIJOYSTATE2 j) {
	static int have_base = 0;
	static LONG b[6];
	static DWORD last_tick = 0;
	static int lines = 0;

	LONG cur[6] = { j->lX, j->lY, j->lZ, j->lRx, j->lRy, j->lRz };
	if (!have_base) {
		memcpy(b, cur, sizeof(b));
		have_base = 1;
		twop_log("[JOY] first sample (assumed at rest): X=%ld Y=%ld Z=%ld Rx=%ld Ry=%ld Rz=%ld POV=%u\n",
			cur[0], cur[1], cur[2], cur[3], cur[4], cur[5], (unsigned)j->rgdwPOV[0]);
		return;
	}
	if (lines >= 60)
		return;

	int active = 0;
	for (int i = 0; i < 6; i++) {
		LONG d = cur[i] - b[i];
		if (d > 3000 || d < -3000)
			active = 1;
	}
	int pressed = -1;
	for (int i = 0; i < 32; i++)
		if (j->rgbButtons[i]) {
			pressed = i;
			active = 1;
			break;
		}

	DWORD now = GetTickCount();
	if (active && now - last_tick > 400) {
		last_tick = now;
		lines++;
		twop_log("[JOY] X=%ld Y=%ld Z=%ld Rx=%ld Ry=%ld Rz=%ld POV=%u button=%d\n",
			cur[0], cur[1], cur[2], cur[3], cur[4], cur[5], (unsigned)j->rgdwPOV[0], pressed);
	}
}

/*
 * ---- dual input: keyboard+mouse for player 1, controller for player 2 ---------------------
 * Every pad's update() (vtable entry 0x0088EAA0 -> 0x0058E5C0) calls get_pad(), which re-polls all
 * DirectInput devices. We hook update() and remember which pad is updating (input_pass). The
 * DirectInput hook then hands each pass only its own devices:
 *   pass 0 (pad 0, player 1): keyboard + mouse real, gamepad neutral
 *   pass 1 (pad 1, player 2): gamepad real, keyboard + mouse zeroed
 * The game's own button mapping is applied to both, so no snapshot format is guessed.
 *
 * Evidence from the logs: hero brains hold the owning pad's script id (0xF4240 + pad number) in
 * 19 sub-objects (every 0x34 bytes from brain+0x1C). Extra heroes carry pad 0's id, which is why
 * they mirror player 1. set_dual_input(1) changes those ids to pad 1's id (0xF4241).
 * Pad 1 itself is marked disconnected (+0x88 = 1, garbage +0x70), so it is brought to life with
 * pad 0's controller index. All of this is an experiment and is only active between F9 and F10.
 */
int dual_input_enabled = 0;
volatile int input_pass = 0;

/*
 * Per-hero controller values. The swing steering state (0x473650) reads the stick straight from the
 * shared in-game controller object (mgr+0x129D8 -> +0x18 = value array: count, then 72-byte entries),
 * not through a hero's pad. We poll twice per frame (pad 0 then pad 1), so that shared object holds
 * the LAST poll: player 2's. After each pad update we keep a copy of the array; while a hero's steer
 * state runs we load that hero's copy into the shared object and restore it afterwards.
 */
#define CTRL_SNAP_MAX 4096
static BYTE ctrl_snap[2][CTRL_SNAP_MAX];
static int ctrl_snap_len[2];
static BYTE ctrl_saved[CTRL_SNAP_MAX];
static int ctrl_saved_len = 0;
int ctrl_swap_logs = 0;

// set by our state-method wrappers: the entity whose state code is currently running
DWORD* state_ctx_entity = NULL;

static int is_extra_hero(DWORD* e) {
	if (!e)
		return 0;
	for (int i = 0; i < extra_hero_count; i++)
		if (extra_heroes[i] == e)
			return 1;
	return 0;
}

static BYTE* ingame_controller_array(int* len) {
	DWORD mgr = *(DWORD*)0x00987948;
	DWORD ctrl, count;
	if (!mgr)
		return NULL;
	ctrl = *(DWORD*)(mgr + 0x129D8);
	if (!ctrl)
		return NULL;
	count = *(DWORD*)(ctrl + 0x18);
	if (count == 0 || count > 64)
		return NULL;
	*len = (int)(4 + count * 72);
	if (*len > CTRL_SNAP_MAX)
		return NULL;
	return (BYTE*)(ctrl + 0x18);
}

static void capture_controller_snapshot(int which) {
	int len = 0;
	__try {
		BYTE* arr = ingame_controller_array(&len);
		if (!arr)
			return;
		memcpy(ctrl_snap[which], arr, len);
		ctrl_snap_len[which] = len;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
	}
}

// returns 1 when the shared controller now holds the snapshot of the player that owns `ent`
static int swap_in_controller_values(DWORD* ent) {
	int which = is_extra_hero(ent) ? 1 : 0;
	int len = 0;
	if (!ctrl_snap_len[0] || !ctrl_snap_len[1])
		return 0;
	__try {
		BYTE* arr = ingame_controller_array(&len);
		if (!arr || len != ctrl_snap_len[which])
			return 0;
		memcpy(ctrl_saved, arr, len);
		ctrl_saved_len = len;
		memcpy(arr, ctrl_snap[which], len);
		return 1;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return 0;
	}
}

static void swap_out_controller_values(void) {
	int len = 0;
	__try {
		BYTE* arr = ingame_controller_array(&len);
		if (arr && len == ctrl_saved_len)
			memcpy(arr, ctrl_saved, len);
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
	}
}

typedef void(__fastcall* pad_update_ptr)(void* this, void* edx);
pad_update_ptr pad_update_original = (void*)0x0058E5C0;

void __fastcall pad_update_hook(void* this, void* edx) {
	int pass = 0;
	if (dual_input_enabled && this && this == (void*)pad_manager_pad(1))
		pass = 1;
	input_pass = pass;
	pad_update_original(this, edx);
	input_pass = 0;
}

void install_pad_update_hook(void) {
	DWORD* slot = (DWORD*)0x0088EAA0;
	DWORD old;
	if (*slot != 0x0058E5C0) {
		twop_log("[DUAL] pad update slot holds %08X, not 0058E5C0; hook not installed\n", (unsigned)*slot);
		return;
	}
	if (VirtualProtect(slot, 4, PAGE_READWRITE, &old)) {
		*slot = (DWORD)pad_update_hook;
		VirtualProtect(slot, 4, old, &old);
		twop_log("[DUAL] pad update hook installed\n");
	}
}

static void patch_extra_hero_ids(DWORD from, DWORD to) {
	for (int h = 0; h < extra_hero_count; h++) {
		DWORD* brain = (DWORD*)extra_heroes[h][0x8C / 4];
		int patched = 0;
		if (!brain) {
			twop_log("[DUAL] extra hero %d has no brain\n", h);
			continue;
		}
		__try {
			for (int off = 0; off < 0x424; off += 4) {
				DWORD* slot = (DWORD*)((BYTE*)brain + off);
				if (*slot == from) {
					*slot = to;
					patched++;
				}
			}
		}
		__except (EXCEPTION_EXECUTE_HANDLER) {
			twop_log("[DUAL] extra hero %d: read fault\n", h);
		}
		twop_log("[DUAL] extra hero %d (brain %08X): %d ids %u -> %u\n", h, (unsigned)(DWORD)brain, patched, (unsigned)from, (unsigned)to);
	}
}

static void set_dual_input(int on) {
	DWORD* p0 = pad_manager_pad(0);
	DWORD* p1 = pad_manager_pad(1);
	if (!p0 || !p1) {
		twop_log("[DUAL] pad objects missing\n");
		return;
	}
	if (on == dual_input_enabled)
		return;

	if (on) {
		p1[0x70 / 4] = p0[0x70 / 4];   // same controller index as pad 0
		p1[0x88 / 4] = 0;              // "connected"
		p1[0x8C / 4] = 0;
		dual_input_enabled = 1;
		patch_extra_hero_ids(0xF4240u, 0xF4241u);
	}
	else {
		dual_input_enabled = 0;
		patch_extra_hero_ids(0xF4241u, 0xF4240u);
		p1[0x88 / 4] = 1;
		p1[0x8C / 4] = 1;
	}
	twop_log("[DUAL] dual input %s (pad 1: +0x70=%d +0x88=%d)\n", on ? "ON" : "OFF", (int)p1[0x70 / 4], (int)p1[0x88 / 4]);
}

/*
 * ---- unique names for extra heroes and their cameras ---------------------------------------
 * add_player builds the entity name from the literal "HERO" (0x88A9D0) and the camera name from
 * "CHASE_CAM" (0x88A988); only for player numbers >= 1 does it append the number. We go through the
 * player-0 path, so every extra hero would be called "HERO" and every extra camera "CHASE_CAM", the
 * same as player 1's. The game looks things up by name in 43 places, so duplicates make P1's code
 * find P2. The two call sites that assign those literals (0x55B6A2, 0x55B863) are hooked to hand
 * over the names the game itself would use ("HERO1", "CHASE_CAM1", ...) while an extra hero spawns.
 */
typedef void(__fastcall* mstring_assign_cstr_ptr)(void* this, void* edx, const char* src);
mstring_assign_cstr_ptr mstring_assign_cstr_original = (void*)0x0041FE30;
char extra_hero_name[16] = "HERO1";
char extra_cam_name[24] = "CHASE_CAM1";
int naming_active = 0;

void __fastcall add_player_name_hook(void* this, void* edx, const char* src) {
	if (naming_active) {
		if (src == (const char*)0x0088A9D0)
			src = extra_hero_name;
		else if (src == (const char*)0x0088A988)
			src = extra_cam_name;
	}
	mstring_assign_cstr_original(this, edx, src);
}

void install_add_player_name_hooks(void) {
	HookFunc(0x0055B6A2, add_player_name_hook, 0, "Hooking add_player's hero name assignment");
	HookFunc(0x0055B863, add_player_name_hook, 0, "Hooking add_player's camera name assignment");
}

/*
 * ---- P2 indicator and "bring P2 to me" ----------------------------------------------------
 * Objects keep a 4x4 matrix pointer at +0x14: rows x, y, z axes and the translation at +0x30
 * (floats 12..14). When bit 28 of the dword at +8 is set the game refreshes it first (0x4DB590).
 * The indicator uses the main camera's matrix if it passes a sanity check, and works out which
 * way is "forward" from where player 1 is relative to the camera.
 */
typedef void(__fastcall* entity_update_po_ptr)(void* this, void* edx, int one);

static float* entity_po(DWORD* ent) {
	if (!ent)
		return NULL;
	if ((ent[2] >> 0x1C) & 1)
		((entity_update_po_ptr)0x004DB590)(ent, NULL, 1);
	return (float*)ent[0x14 / 4];
}

static int po_looks_valid(float* po) {
	if (!po)
		return 0;
	for (int r = 0; r < 3; r++) {
		float l = po[r * 4] * po[r * 4] + po[r * 4 + 1] * po[r * 4 + 1] + po[r * 4 + 2] * po[r * 4 + 2];
		if (!(l > 0.8f && l < 1.2f))   // also rejects NaN
			return 0;
	}
	return 1;
}

void draw_p2_indicator(void) {
	static int frame = 0;
	static int logs = 0;

	if (!dual_input_enabled || !extra_hero_count)
		return;
	frame++;

	DWORD* world = *(DWORD**)g_world_ptr;
	if (!world)
		return;
	DWORD* hero0 = (DWORD*)world[0x230 / 4];
	DWORD* hero2 = extra_heroes[0];
	if (!hero0 || !hero2)
		return;

	__try {
		float* p1 = entity_po(hero0);
		float* p2 = entity_po(hero2);
		if (!p1 || !p2)
			return;

		float dx = p2[12] - p1[12], dy = p2[13] - p1[13], dz = p2[14] - p1[14];
		float dist = sqrtf(dx * dx + dy * dy + dz * dz);

		float sx = 320.f, sy = 60.f;
		int have_dir = 0;
		float right = 0, up = 0, fwd = 0;
		int cam_ok = 0;
		float fwd_sign = 1.f;

		DWORD* main_cam = *(DWORD**)0x00959A70;
		float* cam = main_cam ? (float*)main_cam[0x14 / 4] : NULL;
		if (cam && po_looks_valid(cam)) {
			cam_ok = 1;
			// forward = whichever sign of the z axis points from the camera to player 1
			float tx = p1[12] - cam[12], ty = p1[13] - cam[13], tz = p1[14] - cam[14];
			if (tx * cam[8] + ty * cam[9] + tz * cam[10] < 0.f)
				fwd_sign = -1.f;

			right = dx * cam[0] + dy * cam[1] + dz * cam[2];
			up = dx * cam[4] + dy * cam[5] + dz * cam[6];
			fwd = fwd_sign * (dx * cam[8] + dy * cam[9] + dz * cam[10]);
			have_dir = 1;

			if (fwd > 1.f) {
				// in front of the camera: rough perspective projection (field of view is a guess)
				sx = 320.f + 320.f * (right / fwd) / 0.9f;
				sy = 240.f - 240.f * (up / fwd) / 0.7f;
			}
			else {
				// beside or behind: put the marker on the screen edge in that direction
				float len = sqrtf(right * right + up * up) + 0.001f;
				sx = 320.f + (right / len) * 1000.f;
				sy = 240.f - (up / len) * 1000.f;
				if (len < 0.5f)
					sy = 1000.f;
			}
		}

		if (sx < 50.f) sx = 50.f;
		if (sx > 540.f) sx = 540.f;
		if (sy < 60.f) sy = 60.f;
		if (sy > 410.f) sy = 410.f;

		char label[64];
		const char* arrow = "";
		if (have_dir) {
			if (sx <= 60.f) arrow = "<";
			else if (sx >= 530.f) arrow = ">";
			else if (sy <= 70.f) arrow = "^";
			else if (sy >= 400.f) arrow = "v";
		}
		if (sx >= 530.f)
			sprintf(label, "P2 %dm %s", (int)dist, arrow);
		else
			sprintf(label, "%s P2 %dm", arrow, (int)dist);

		nglListAddString(*nglSysFont, sx, sy, 0.2f, nglColor(255, 220, 0, 255), 1.f, 1.f, "%s", label);

		if (logs < 6 && frame % 180 == 0) {
			logs++;
			twop_log("[IND] dist %.1f d=(%.1f %.1f %.1f) camera_ok=%d fwd_sign=%.0f right=%.1f up=%.1f fwd=%.1f -> screen (%.0f, %.0f)\n",
				dist, dx, dy, dz, cam_ok, fwd_sign, right, up, fwd, sx, sy);
		}
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		static int faulted = 0;
		if (!faulted) {
			faulted = 1;
			twop_log("[IND] indicator faulted, disabled for this frame\n");
		}
	}
}

// F8: put every extra hero next to player 1
static void bring_extra_heroes_to_p1(void) {
	DWORD* world = *(DWORD**)g_world_ptr;
	DWORD* hero0 = world ? (DWORD*)world[0x230 / 4] : NULL;
	if (!hero0 || !extra_hero_count) {
		twop_log("[TP] nothing to do\n");
		return;
	}
	__try {
		float* po = entity_po(hero0);
		float m[16];
		if (!po)
			return;
		for (int i = 0; i < extra_hero_count; i++) {
			memcpy(m, po, sizeof(m));
			m[12] += 1.5f * (i + 1);   // small sideways offset so heroes don't overlap
			((void(*)(DWORD, float*, int))0x004F3890)((DWORD)extra_heroes[i], m, 1);
			twop_log("[TP] extra hero %d moved next to player 1\n", i);
		}
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		twop_log("[TP] fault while teleporting\n");
	}
}

/*
 * ---- who is asking? (context resolution for "the hero" lookups) ---------------------------
 * The game finds "the hero" through globals: get_hero(player) at 0x514A50 (77 call sites, which
 * only returns [0x96F7B4] if set, otherwise player 0's hero) and name lookups for "HERO". Neither
 * knows which hero's code is calling, so when player 2 swings, player 2's swing code gets player 1
 * and moves him. Heuristic: scan the call stack for pointers into a hero's own objects (the entity
 * itself, or anything inside its 0x424-byte brain). The innermost match is the hero whose code is
 * running. If it is an extra hero, the lookup returns that hero instead of player 1.
 * F7 toggles this on and off.
 */
int context_mode = 1;
int context_logs = 0;

static int try_read_cstr(const char* p, char* out, int n) {
	int len = 0;
	if (!p)
		return 0;
	__try {
		while (len < n - 1 && p[len] >= 32 && p[len] < 127)
			len++;
		if (len > 0 && p[len] == 0) {
			memcpy(out, p, len);
			out[len] = 0;
			return 1;
		}
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
	}
	return 0;
}

// the lookup's name argument may be an mString (pointer at +8, or the text inline at +12) or a plain char*
static void decode_mstring(void* m, char* out, int n) {
	DWORD ptr_at_8 = 0;
	strcpy(out, "?");
	__try {
		ptr_at_8 = *(DWORD*)((BYTE*)m + 8);
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		ptr_at_8 = 0;
	}
	if (try_read_cstr((const char*)ptr_at_8, out, n))
		return;
	if (try_read_cstr((const char*)m + 12, out, n))
		return;
	try_read_cstr((const char*)m, out, n);
}

static DWORD* context_hero_from_stack(const char* why, DWORD caller) {
	NT_TIB* tib = (NT_TIB*)NtCurrentTeb();
	DWORD* sp = (DWORD*)_AddressOfReturnAddress();
	DWORD* top = (DWORD*)tib->StackBase;
	DWORD* world = *(DWORD**)g_world_ptr;
	DWORD* hero0 = world ? (DWORD*)world[0x230 / 4] : NULL;
	DWORD hero0_brain = hero0 ? hero0[0x8C / 4] : 0;

	__try {
		for (int n = 0; sp + n < top && n < 1024; n++) {
			DWORD v = sp[n];
			if (v < 0x10000)
				continue;

			if (hero0 && (v == (DWORD)hero0 || (hero0_brain && v >= hero0_brain && v < hero0_brain + 0x424)))
				return NULL;   // innermost owner is player 1

			for (int i = 0; i < extra_hero_count; i++) {
				DWORD brain = extra_heroes[i][0x8C / 4];
				if (v == (DWORD)extra_heroes[i] || (brain && v >= brain && v < brain + 0x424)) {
					if (context_logs < 40) {
						context_logs++;
						twop_log("[CTX] %s from %08X: stack owner is extra hero %d (value %08X at +0x%X) -> using it\n",
							why, (unsigned)caller, i, (unsigned)v, n * 4);
					}
					return extra_heroes[i];
				}
			}
		}
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
	}
	return NULL;
}

typedef DWORD*(__fastcall* get_hero_ptr)(void* this, void* edx, int idx);
get_hero_ptr get_hero_original = (void*)0x00514A50;

DWORD* __fastcall get_hero_hook(void* this, void* edx, int idx) {
	DWORD* r = get_hero_original(this, edx, idx);

	if (dual_input_enabled && extra_hero_count && idx == 0 && r) {
		DWORD* world = *(DWORD**)g_world_ptr;
		DWORD* hero0 = world ? (DWORD*)world[0x230 / 4] : NULL;
		if (r == hero0) {
			// state code that our wrappers know belongs to an extra hero gets that hero
			if (is_extra_hero(state_ctx_entity))
				return state_ctx_entity;
			if (context_mode) {
				DWORD* ctx = context_hero_from_stack("get_hero(0)", (DWORD)_ReturnAddress());
				if (ctx)
					return ctx;
			}
		}
	}
	return r;
}

static const DWORD get_hero_sites[] = {
	0x004495DE, 0x004501C2, 0x00457C34, 0x00458F43, 0x0046DA8F, 0x0046DB15, 0x0046DB5A, 0x00473B4B,
	0x0047584C, 0x00475B53, 0x00475C17, 0x0047A86D, 0x004829DD, 0x0049D0D3, 0x004E7338, 0x004EA9BB,
	0x004EAA92, 0x004EAEAF, 0x004F9C2F, 0x005403C4, 0x00552D36, 0x0057B0AB, 0x0057B42E, 0x0057FBB0,
	0x0059B339, 0x0059B376, 0x005BB97F, 0x00619295, 0x00619326, 0x006193DC, 0x00619EC2, 0x00641FFC,
	0x00642146, 0x0064245C, 0x006425A6, 0x0066281A, 0x0066287A, 0x006628DA, 0x0066293A, 0x00665A98,
	0x00679053, 0x00679098, 0x006796C6, 0x0067970F, 0x00687CF8, 0x0068F761, 0x006B9B7A, 0x006B9DF3,
	0x006BBA1D, 0x006BC15C, 0x006BC198, 0x006C2F0E, 0x006C7AD3, 0x006C81B2, 0x006C9A19, 0x006CA351,
	0x006CACCA, 0x006CD1F4, 0x006CFF55, 0x006D05B5, 0x006D1820, 0x006D1B85, 0x006D2EEF, 0x006D2F10,
	0x006D2F4D, 0x006D3359, 0x006D4B88, 0x006D62AA, 0x006D8077, 0x006DA55C, 0x00714859, 0x00714885,
	0x00731147, 0x00731768, 0x007334CD, 0x0073A260, 0x0073CBA5
};

void install_get_hero_hooks(void) {
	int n = (int)(sizeof(get_hero_sites) / sizeof(get_hero_sites[0]));
	for (int i = 0; i < n; i++)
		HookFunc(get_hero_sites[i], get_hero_hook, 0, "Hooking a get_hero call site (context resolution)");
	twop_log("[CTX] get_hero context resolution installed on %d call sites\n", n);
}

/*
 * ---- camera fix: extra heroes must not steal the view -------------------------------------
 * add_player gives every hero a chase camera object named "CHASE_CAM". The camera manager
 * (0x54F8C0 mode switch, 0x552F50 per-frame update) finds its cameras BY NAME with
 * 0x004DC300(name, 0x1D, 0). Extra heroes created through the count == 0 path also get the plain
 * name, so a lookup can return an extra hero's camera and the view jumps to that hero (seen when
 * player 2 web zips). We hook every call site of the lookup and, when it returns one of the extra
 * heroes' cameras, return the main camera ([0x959A70], player 1's) instead.
 */
typedef DWORD*(__cdecl* find_entity_by_name_ptr)(void* name, int type, int flag);
find_entity_by_name_ptr find_entity_by_name_original = (void*)0x004DC300;
#define LOOKUP_NAMES_MAX 48
char lookup_names_seen[LOOKUP_NAMES_MAX][40];
int lookup_names_count = 0;

DWORD* __cdecl find_entity_by_name_hook(void* name, int type, int flag) {
	DWORD* r = find_entity_by_name_original(name, type, flag);

	if (!r || !extra_hero_count)
		return r;

	DWORD* main_cam = *(DWORD**)0x00959A70;
	DWORD* world = *(DWORD**)g_world_ptr;
	DWORD* hero0 = world ? (DWORD*)world[0x230 / 4] : NULL;
	int is_extra_cam = 0;
	int hero_kind = 0;   // 1 = extra hero, 2 = hero 0
	char nm[40];
	int have_name = 0;

	for (int i = 0; i < extra_cam_count; i++)
		if (r == extra_cams[i])
			is_extra_cam = 1;
	for (int i = 0; i < extra_hero_count; i++)
		if (r == extra_heroes[i])
			hero_kind = 1;
	if (r == hero0)
		hero_kind = 2;

	if (is_extra_cam || hero_kind == 2 || lookup_names_count < LOOKUP_NAMES_MAX) {
		decode_mstring(name, nm, sizeof(nm));
		have_name = 1;
	}

	// log every distinct requested name once, to see what the game's code asks for
	if (have_name && lookup_names_count < LOOKUP_NAMES_MAX) {
		int known = 0;
		for (int i = 0; i < lookup_names_count; i++)
			if (!strcmp(lookup_names_seen[i], nm))
				known = 1;
		if (!known) {
			strcpy(lookup_names_seen[lookup_names_count++], nm);
			twop_log("[LOOKUP] '%s' type %d from %08X -> %08X%s%s\n", nm, type, (unsigned)(DWORD)_ReturnAddress(), (unsigned)(DWORD)r,
				is_extra_cam ? " (EXTRA CAMERA)" : "", hero_kind == 2 ? " (hero 0)" : hero_kind == 1 ? " (EXTRA HERO)" : "");
		}
	}

	if (is_extra_cam && type == 0x1D && main_cam)
		return main_cam;

	// code running on an extra hero's own objects that asks for "HERO" gets that hero, not player 1
	if (hero_kind == 2 && dual_input_enabled && have_name && !_stricmp(nm, "HERO")) {
		if (is_extra_hero(state_ctx_entity))
			return state_ctx_entity;
		if (context_mode) {
			DWORD* ctx = context_hero_from_stack("lookup 'HERO'", (DWORD)_ReturnAddress());
			if (ctx)
				return ctx;
		}
	}
	return r;
}

static const DWORD find_entity_sites[] = {
	0x004B64FC, 0x004DCE59, 0x004DCE7A, 0x004DCEAA, 0x004DD55C, 0x0050B9D9, 0x00528BE3, 0x0054AE19,
	0x0054F800, 0x0054F95D, 0x0054FA08, 0x0054FA29, 0x0054FB8D, 0x0054FBAE, 0x005531EC, 0x0055320C,
	0x005533EE, 0x0055D23E, 0x0057840C, 0x005A37F2, 0x005B85A9, 0x005BB1D1, 0x005DD9D5, 0x0065EFBE,
	0x0065F035, 0x0065F455, 0x00660455, 0x00660945, 0x00660A65, 0x00668BC0, 0x00668C1D, 0x006A5C90,
	0x006AA7FB, 0x006AA897, 0x006DF8EB, 0x00707FDE, 0x00708022, 0x0071BE71, 0x0071BEC8, 0x0071BFF0,
	0x0072AA3B, 0x0072ADF9, 0x0072F74D
};

void install_camera_lookup_fix(void) {
	int n = (int)(sizeof(find_entity_sites) / sizeof(find_entity_sites[0]));
	for (int i = 0; i < n; i++)
		HookFunc(find_entity_sites[i], find_entity_by_name_hook, 0, "Hooking a find-entity-by-name call site (camera fix)");
	twop_log("[CAM] camera lookup fix installed on %d call sites\n", n);
}

/*
 * ---- control read probe (logging only) ----------------------------------------------------
 * Every control read in the game goes through 0x00821E90 (get_control_value(array, id) -> float).
 * There are 31 call sites. We hook all of them and log, once per (call site, id), any read that
 * returns a non-trivial value, i.e. while you hold a key. This shows which code reads the movement
 * controls and which controller object (`this`) it reads from.
 */
typedef float(__fastcall* get_control_value_ptr)(void* this, void* edx, unsigned id);
get_control_value_ptr get_control_value_original = (void*)0x00821E90;

#define CONTROL_PROBE_MAX 64
DWORD control_probe_caller[CONTROL_PROBE_MAX];
unsigned control_probe_id[CONTROL_PROBE_MAX];
int control_probe_count = 0;

float __fastcall get_control_value_hook(void* this, void* edx, unsigned id) {
	float v = get_control_value_original(this, edx, id);

	if ((v > 0.3f || v < -0.3f) && control_probe_count < CONTROL_PROBE_MAX) {
		DWORD caller = (DWORD)_ReturnAddress();
		int known = 0;
		for (int i = 0; i < control_probe_count; i++)
			if (control_probe_caller[i] == caller && control_probe_id[i] == id)
				known = 1;
		if (!known) {
			control_probe_caller[control_probe_count] = caller;
			control_probe_id[control_probe_count] = id;
			control_probe_count++;
			twop_log("[CTL] read from %08X: this=%08X id=%u value=%.2f\n", (unsigned)caller, (unsigned)(DWORD)this, id, v);
		}
	}
	return v;
}

static const DWORD control_read_sites[] = {
	0x00473B91, 0x00473BAA, 0x00473BC4, 0x005A507D, 0x005A50B4, 0x005A50E6, 0x005AD4CA,
	0x0081D288, 0x0081D29E, 0x0081D2D0, 0x0081D2E6, 0x0081D318, 0x0081D32E, 0x0081D360,
	0x0081D376, 0x0081D3A8, 0x0081D3BF, 0x0081D3D6, 0x0081D3ED, 0x0081D404, 0x0081D41B,
	0x0081D432, 0x0081D449, 0x0081D460, 0x0081D47F, 0x0081D49A, 0x0081D4B5, 0x0081D4D0,
	0x0081D4EB, 0x0081D506, 0x0081D521
};

void install_control_read_probe(void) {
	for (int i = 0; i < (int)(sizeof(control_read_sites) / sizeof(control_read_sites[0])); i++)
		HookFunc(control_read_sites[i], get_control_value_hook, 0, "Hooking a get_control_value call site (probe)");
	twop_log("[CTL] control read probe installed on %d call sites\n", (int)(sizeof(control_read_sites) / sizeof(control_read_sites[0])));
}

/*
 * ---- input probe (logging only, changes no behaviour) -------------------------------------
 * What the exe shows:
 *   - 0x987948 is the input manager. It holds 10 raw gamepad states (stride 0x110) and an
 *     array of controller object pointers at mgr+0x129D8 (element count at mgr+0x129D0).
 *   - The game only ever fills slot 0 (every call to set_controller 0x8203F0 passes index 0).
 *   - The hero input function (0x00473650, vtable slot at 0x00877498) reads its movement
 *     controls (ids 0x10, 0x12, 0x13) from controllers[0]+0x18 via the getter 0x821E90.
 * The probe logs the controller table, and every distinct `this` that reaches the hero input
 * function, so we can see how to tell hero 2's call apart from hero 1's.
 */
typedef int(__fastcall* get_gamepad_count_ptr)(void* this);
get_gamepad_count_ptr get_gamepad_count = (void*)0x00820080;

static void log_input_state(const char* when) {
	DWORD mgr = *(DWORD*)0x00987948;
	twop_log("[IN] ---- input state (%s) ----\n", when);
	if (!mgr) {
		twop_log("[IN] no input manager\n");
		return;
	}
	twop_log("[IN] manager %08X, controller slot count %u, gamepads detected %d\n",
		(unsigned)mgr, (unsigned)*(DWORD*)(mgr + 0x129D0), get_gamepad_count((void*)mgr));
	for (int i = 0; i < 4; i++)
		twop_log("[IN]   controllers[%d] = %08X\n", i, (unsigned)*(DWORD*)(mgr + 0x129D8 + i * 4));
	twop_log("[IN] controller globals: 965C0C=%08X 965C10=%08X 965C14=%08X 965C1C=%08X\n",
		(unsigned)*(DWORD*)0x00965C0C, (unsigned)*(DWORD*)0x00965C10,
		(unsigned)*(DWORD*)0x00965C14, (unsigned)*(DWORD*)0x00965C1C);
	DWORD c0 = *(DWORD*)(mgr + 0x129D8);
	if (c0)
		twop_log("[IN] controllers[0]: vtable %08X, control count (+0x18) %u\n", (unsigned)*(DWORD*)c0, (unsigned)*(DWORD*)(c0 + 0x18));
}

typedef void(__fastcall* hero_input_fn_ptr)(void* this, void* edx, int arg);
hero_input_fn_ptr hero_input_fn_original = (void*)0x00473650;

DWORD probe_seen[16];
int probe_seen_count = 0;

void __fastcall hero_input_fn_hook(void* this, void* edx, int arg) {
	int known = 0;
	for (int i = 0; i < probe_seen_count; i++)
		if (probe_seen[i] == (DWORD)this) known = 1;

	if (!known && probe_seen_count < 16) {
		DWORD* t = (DWORD*)this;
		DWORD* world = *(DWORD**)g_world_ptr;
		probe_seen[probe_seen_count++] = (DWORD)this;
		twop_log("[IN] hero input fn: new this=%08X vtbl=%08X [+0x14]=%08X [+0x50]=%08X [+0x8C]=%08X  (hero0 entity=%08X, last extra hero=%08X)\n",
			(unsigned)(DWORD)this, (unsigned)t[0], (unsigned)t[0x14 / 4], (unsigned)t[0x50 / 4], (unsigned)t[0x8C / 4],
			(unsigned)(world ? world[0x230 / 4] : 0), (unsigned)(DWORD)second_hero_entity);
	}
	hero_input_fn_original(this, edx, arg);
}

// the function is only reached through a vtable slot, so patch the slot itself
void install_hero_input_probe(void) {
	DWORD* slot = (DWORD*)0x00877498;
	DWORD old;
	if (*slot != 0x00473650) {
		puts("[IN] vtable slot 0x877498 does not hold 0x473650, probe not installed");
		return;
	}
	if (VirtualProtect(slot, 4, PAGE_READWRITE, &old)) {
		*slot = (DWORD)hero_input_fn_hook;
		VirtualProtect(slot, 4, old, &old);
		puts("[IN] hero input probe installed");
	}
}

/*
 * ---- swing probe (logging only) -----------------------------------------------------------
 * brain+0xC holds the locomotion mode: 1 = web zip, 3 = swinging (spiderman_is_swinging() is
 * `brain[0xC] == 3`). Zip became independent once the extra hero was created under pad 1's owner
 * id, swing did not, so something about swing is still shared.
 *   - mode monitor: logs every change of that field for hero 0 and each extra hero
 *   - state probe: the swing state methods at 0x473650 (reads sticks and hero 0's position) and
 *     0x47DDD0 (enter-swing: sets brain[0xC] = 3 on its own entity, this[0x18]) are only reachable
 *     through vtable slots 0x877498 / 0x8774D8, which are patched to log each distinct state object
 *     together with the entity it is bound to.
 */
unsigned tick_count = 0;

/*
 * ---- cross-hero swing -> jump filter -------------------------------------------------------
 * Findings from the logs: when one hero enters swing, the OTHER hero's own state machine takes the
 * ground->jump transition one frame later (no raw input on his pad). Every button read in a hero's
 * transition code goes through the brain query 0x467E10 (this[0xC] = receiving hero, args = output
 * buffer, button index; the buffer's word at +0x32 holds the flags: bit0 held, bit1 pressed, bit5
 * consumed). For SWING_FILTER_FRAMES frames after a hero starts swinging we clear the held/pressed
 * bits of every query that is NOT for the swinging hero, and log each query that would have
 * reported held/pressed so the next log shows exactly which button index leaks.
 * F6 toggles the filter (default OFF now).
 */
#define SWING_FILTER_FRAMES 3
int swing_filter_on = 0;   // refuted by the last log (0 suppressions, spurious jump still happened); F6 turns it on
static DWORD last_query_receiver = 0;      // hero whose update is running (receiver of the last query)
static DWORD swing_owner = 0;              // hero that entered swing most recently
static unsigned swing_frame = 0;
static int trig_logs = 0;
static int filter_logs = 0;
static unsigned filter_hits = 0;
static int diag_busy = 0;                  // set while our own diagnostics call the game's query functions
static void helper_swing_flush(void);      // defined with the ground-transition helper probe
// raw device state seen by the DirectInput hook, per pass (0 = pad 0 / keyboard+mouse, 1 = pad 1 / gamepad)
volatile DWORD raw_buttons[2];
volatile int raw_keys_down[2];

#define MSG_SEEN_MAX 48
static struct { int msg; DWORD owner; DWORD caller; } msg_seen[MSG_SEEN_MAX];
static int msg_seen_count = 0;
static int msg_logs = 0;

static int mode_of_entity(DWORD* ent) {
	int m = -2;
	if (!ent)
		return -3;
	__try {
		DWORD* b = (DWORD*)ent[0x8C / 4];
		m = b ? (int)b[0xC / 4] : -1;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		m = -2;
	}
	return m;
}

static void monitor_modes(void) {
	static int last[17];
	static int init = 0;
	static int lines = 0;
	int cur[17];
	int n = 1 + (extra_hero_count > 16 ? 16 : extra_hero_count);
	DWORD* world = *(DWORD**)g_world_ptr;
	DWORD* hero0 = world ? (DWORD*)world[0x230 / 4] : NULL;

	tick_count++;
	if (!extra_hero_count || lines >= 150)
		return;

	cur[0] = mode_of_entity(hero0);
	for (int i = 1; i < n; i++)
		cur[i] = mode_of_entity(extra_heroes[i - 1]);

	if (!init) {
		init = 1;
		for (int i = 0; i < 17; i++)
			last[i] = -99;
	}
	for (int i = 0; i < n; i++) {
		if (cur[i] != last[i]) {
			msg_seen_count = 0;   // log the distinct messages again after every mode change
			lines++;
			if (i == 0)
				twop_log("[MODE] f=%u hero 0: %d -> %d\n", tick_count, last[i], cur[i]);
			else
				twop_log("[MODE] f=%u extra hero %d: %d -> %d\n", tick_count, i - 1, last[i], cur[i]);
			last[i] = cur[i];
		}
	}
}

typedef int(__fastcall* state_fn_ptr)(void* this, void* edx, int arg);
state_fn_ptr state_473650_original = (void*)0x00473650;
state_fn_ptr state_47DDD0_original = (void*)0x0047DDD0;

static void log_state_object(const char* tag, void* this) {
	static struct { DWORD tag_id; DWORD obj; } seen[96];
	static int count = 0;
	DWORD tag_id = (DWORD)tag;
	DWORD* t = (DWORD*)this;

	for (int i = 0; i < count; i++)
		if (seen[i].tag_id == tag_id && seen[i].obj == (DWORD)this)
			return;
	if (count >= 96)
		return;
	seen[count].tag_id = tag_id;
	seen[count].obj = (DWORD)this;
	count++;

	DWORD ent = 0;
	__try {
		ent = t[0x18 / 4];
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		ent = 0;
	}

	DWORD* world = *(DWORD**)g_world_ptr;
	DWORD* hero0 = world ? (DWORD*)world[0x230 / 4] : NULL;
	char who[40];
	strcpy(who, "other");
	if (ent == (DWORD)hero0)
		strcpy(who, "HERO 0");
	for (int i = 0; i < extra_hero_count; i++)
		if (ent == (DWORD)extra_heroes[i])
			sprintf(who, "EXTRA HERO %d", i);

	twop_log("[ST] %s state object %08X: +0x14=%08X +0x18(entity)=%08X (%s) entity mode=%d\n",
		tag, (unsigned)(DWORD)this, (unsigned)t[0x14 / 4], (unsigned)ent, who, mode_of_entity((DWORD*)ent));
}

int __fastcall state_473650_hook(void* this, void* edx, int arg) {
	DWORD* ent = NULL;
	DWORD* prev_ctx = state_ctx_entity;
	int r;

	log_state_object("ground-update", this);
	__try {
		ent = (DWORD*)((DWORD*)this)[0x18 / 4];
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		ent = NULL;
	}
	if (context_mode && dual_input_enabled && extra_hero_count && ent)
		state_ctx_entity = ent;

	r = state_473650_original(this, edx, arg);

	state_ctx_entity = prev_ctx;
	return r;
}

int __fastcall state_47DDD0_hook(void* this, void* edx, int arg) {
	DWORD* ent = NULL;
	DWORD* prev_ctx = state_ctx_entity;
	int r;

	log_state_object("enter-swing", this);
	__try {
		ent = (DWORD*)((DWORD*)this)[0x18 / 4];
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		ent = NULL;
	}
	if (context_mode && dual_input_enabled && extra_hero_count && ent)
		state_ctx_entity = ent;

	r = state_47DDD0_original(this, edx, arg);

	state_ctx_entity = prev_ctx;
	return r;
}

/*
 * State enter methods (vtable slots, 5 stack args, ret 0x14). Locomotion states come in pairs: enter
 * (ret 0x14) and update (ret 4) in adjacent vtable entries. Mode 1 ground: enter 0x4584E0 / update
 * 0x473650. Mode 3 swing: enter 0x47DA60 / update 0x47DDD0. Mode 6/7 jump: enter 0x469880 / update
 * 0x473E70. Mode 9: enter 0x45D340 / update 0x47DEF0. Each enter is logged with the objects it is
 * bound to, both heroes' modes, and the chain of return addresses that led to it, to find out why one
 * hero's swing start puts the other hero into the jump state.
 */
static int collect_return_addrs(DWORD* out, int max) {
	DWORD* sp = (DWORD*)_AddressOfReturnAddress();
	NT_TIB* tib = (NT_TIB*)NtCurrentTeb();
	DWORD* top = (DWORD*)tib->StackBase;
	int n = 0;
	__try {
		for (int i = 0; sp + i < top && i < 320 && n < max; i++) {
			DWORD v = sp[i];
			if (v >= 0x00401000 && v < 0x0086F000) {
				BYTE* b = (BYTE*)v;
				if (b[-5] == 0xE8 || (b[-2] == 0xFF && (b[-1] & 0x38) == 0x10) ||
					(b[-3] == 0xFF && (b[-2] & 0x38) == 0x10) || (b[-6] == 0xFF && (b[-5] & 0x38) == 0x10))
					out[n++] = v;
			}
		}
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
	}
	return n;
}

static void describe_owner(DWORD v, char* out) {
	DWORD* world = *(DWORD**)g_world_ptr;
	DWORD* hero0 = world ? (DWORD*)world[0x230 / 4] : NULL;
	strcpy(out, "other");
	if (!v) {
		strcpy(out, "null");
		return;
	}
	if (hero0) {
		DWORD b = hero0[0x8C / 4];
		if (v == (DWORD)hero0) {
			strcpy(out, "HERO 0");
			return;
		}
		if (b && v >= b && v < b + 0x424) {
			sprintf(out, "inside HERO 0 brain +0x%X", (unsigned)(v - b));
			return;
		}
	}
	for (int i = 0; i < extra_hero_count; i++) {
		DWORD b = extra_heroes[i][0x8C / 4];
		if (v == (DWORD)extra_heroes[i]) {
			sprintf(out, "EXTRA HERO %d", i);
			return;
		}
		if (b && v >= b && v < b + 0x424) {
			sprintf(out, "inside EXTRA %d brain +0x%X", i, (unsigned)(v - b));
			return;
		}
	}
}

static int enter_logs = 0;

static void log_state_enter(const char* tag, void* this) {
	DWORD* t = (DWORD*)this;
	DWORD ai = 0, ent = 0, ra[7];
	char w14[48], w18[48], line[320];
	DWORD* world = *(DWORD**)g_world_ptr;
	DWORD* hero0 = world ? (DWORD*)world[0x230 / 4] : NULL;
	int n, len;

	if (enter_logs >= 300)
		return;
	enter_logs++;

	__try {
		ai = t[0x14 / 4];
		ent = t[0x18 / 4];
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
	}
	describe_owner(ai, w14);
	describe_owner(ent, w18);
	n = collect_return_addrs(ra, 6);

	len = sprintf(line, "[ENTER] f=%u %s: state %08X +0x14=%08X (%s) +0x18=%08X (%s) | modes hero0=%d extra0=%d | from",
		tick_count, tag, (unsigned)(DWORD)this, (unsigned)ai, w14, (unsigned)ent, w18,
		mode_of_entity(hero0), extra_hero_count ? mode_of_entity(extra_heroes[0]) : -9);
	for (int i = 0; i < n && len < 280; i++)
		len += sprintf(line + len, " %08X", (unsigned)ra[i]);
	twop_log("%s\n", line);
}

typedef int(__fastcall* state_enter_ptr)(void* this, void* edx, int a1, int a2, int a3, int a4, int a5);
state_enter_ptr enter_jump_original = (void*)0x00469880;
state_enter_ptr enter_swing_original = (void*)0x0047DA60;
state_enter_ptr enter_nine_original = (void*)0x0045D340;
state_enter_ptr enter_ground_original = (void*)0x004584E0;

int __fastcall enter_jump_hook(void* this, void* edx, int a1, int a2, int a3, int a4, int a5) {
	log_state_enter("ENTER-JUMP(6/7)", this);
	return enter_jump_original(this, edx, a1, a2, a3, a4, a5);
}
int __fastcall enter_swing_hook(void* this, void* edx, int a1, int a2, int a3, int a4, int a5) {
	swing_owner = last_query_receiver;
	swing_frame = tick_count;
	helper_swing_flush();
	log_state_enter("ENTER-SWING(3)", this);
	return enter_swing_original(this, edx, a1, a2, a3, a4, a5);
}
int __fastcall enter_nine_hook(void* this, void* edx, int a1, int a2, int a3, int a4, int a5) {
	log_state_enter("ENTER(9)", this);
	return enter_nine_original(this, edx, a1, a2, a3, a4, a5);
}
int __fastcall enter_ground_hook(void* this, void* edx, int a1, int a2, int a3, int a4, int a5) {
	log_state_enter("ENTER-GROUND(1)", this);
	return enter_ground_original(this, edx, a1, a2, a3, a4, a5);
}

/*
 * Brain message handler 0x00467E10 (vtable slot 0x87D3C8, thiscall, 2 stack args, ret 8). this[0xC] is the
 * receiving hero entity. The message id (0..17) is mapped through the byte table at 0x467F88 to a case
 * that delivers the message to one of the brain's 0x34-byte event channels (brain+0x18, 0x4C, 0x80, 0xB4,
 * 0xE8, 0x11C, 0x1B8, 0x254, 0x288). The other hero's forced jump enters through case 5 (channel
 * brain+0x1B8, message ids 5, 11, 12). This probe logs each distinct (message, receiver, caller) after
 * every mode change; the FIRST address after "caller" is the real sender (the return address at entry),
 * the rest is only stack residue.
 */
typedef int(__fastcall* brain_msg_ptr)(void* this, void* edx, int msg, int arg);
brain_msg_ptr brain_msg_original = (void*)0x00467E10;

int __fastcall brain_msg_hook(void* this, void* edx, int outbuf, int idx) {
	if (diag_busy)
		return brain_msg_original(this, edx, outbuf, idx);
	DWORD owner = 0;
	DWORD caller = (DWORD)_ReturnAddress();
	int r;

	__try {
		owner = ((DWORD*)this)[0xC / 4];
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		owner = 0;
	}
	last_query_receiver = owner;

	r = brain_msg_original(this, edx, outbuf, idx);

	if (extra_hero_count && outbuf) {
		WORD flags = 0;
		int in_window = swing_owner && owner != swing_owner && tick_count >= swing_frame && tick_count - swing_frame <= SWING_FILTER_FRAMES;

		__try {
			flags = *(WORD*)((BYTE*)outbuf + 0x32);
		}
		__except (EXCEPTION_EXECUTE_HANDLER) {
			flags = 0;
		}

		if (in_window && (flags & 3) && !(flags & 0x20)) {
			char who[48], sw[48];
			describe_owner(owner, who);
			describe_owner(swing_owner, sw);
			if (trig_logs < 120) {
				trig_logs++;
				twop_log("[TRIG] f=%u (swing started f=%u by %s) query by %s: button idx %d flags %04X (held=%d pressed=%d) caller %08X | raw: pad1 buttons %08X, kbd keys down %d | %s\n",
					tick_count, swing_frame, sw, who, idx, (unsigned)flags, flags & 1, (flags >> 1) & 1, (unsigned)caller,
					(unsigned)raw_buttons[1], raw_keys_down[0], swing_filter_on ? "SUPPRESSED" : "passed (filter off)");
			}
			if (swing_filter_on) {
				*(WORD*)((BYTE*)outbuf + 0x32) = (WORD)(flags & ~3);
				filter_hits++;
			}
		}
	}
	return r;
}

/*
 * ---- ground/air transition helper probe (0x006A7110) ----------------------------------------
 * 0x006A7110 (thiscall, one stack arg = action id, ret 4) is what the state updates call to pick
 * the next locomotion mode; the ENTER-JUMP chain `006CD511 004885FC 006CDCC2 006A7349` is the
 * residue of it. It is called from 9 sites (0x44CF4E, 0x47E0DA, 0x47E484, 0x47E5D3, 0x488962,
 * 0x488F0F, 0x489139, 0x48927D, 0x4892EA). `this` = locomotion object: +0x08 = context (lookup
 * table), +0x50 = new mode, +0x54 = previous mode. Static reading of the function:
 *   1. returns false at once when variable K838 (context+0x50 map, key [0x96C838]) is 0
 *   2. [12] = (K834 == 1); the context also yields controller (key [0x95855C]) and entity ([0x96C290])
 *   3. if arg == [0x95836C] and (K830 != 0 or K834 == 1): button 0xB pressed (flags bit1, not bit5)
 *        and K834 != 1  -> mode = 6 (JUMP)         <- the jump branch (0x6A7412)
 *        button 0xB not pressed, button 7 pressed -> modes 5 / 8 / 6 via stick and object checks
 *   4. otherwise the 0x6A7563 branch (modes 0xB, 0x14, 0, or 9 / 0xF / 1 through buttons 0xB and 7)
 * The hook runs the original, then records inputs and the outcome in a ring buffer. A swing start
 * (enter-swing) prints the last 2 frames from the ring and then every call for the next 2 frames.
 */
typedef int(__fastcall* helper_fn)(void* this, void* edx, int arg);
helper_fn helper_original = (void*)0x006A7110;
typedef int(__fastcall* var_lookup_fn)(void* map, void* edx, DWORD key);
typedef void*(__fastcall* ctx_lookup_fn)(void* ctx, void* edx, DWORD key, int one);
typedef void*(__fastcall* btn_query_fn)(void* ctrl, void* edx, void* out, int idx);
typedef void(__fastcall* node_free_fn)(void* node, void* edx);

typedef struct {
	unsigned tick;
	DWORD caller, self, ctx, ent, ctrl, arg, argcmp;
	int ret, mode0, prev0, mode1, prev1, v838, v834, v830, b0B, b07, b01, h0, h1;
	DWORD phys, w0C, t180;   // entity+0x1C object: word +0xC (bit 12), timestamp +0x180
	int f184;                // byte +0x184 (0x4BDE00 reads it)
} helper_rec;

#define HELPER_RING 256
#define HELPER_LINES_MAX 1500
static helper_rec helper_ring[HELPER_RING];
static unsigned helper_ring_n = 0;
static unsigned helper_live_until = 0;
static int helper_lines = 0;

static int helper_button_flags(void* ctrl, int idx) {
	BYTE buf[0x60];
	int flags = -1;
	memset(buf, 0, sizeof(buf));
	__try {
		btn_query_fn q = (btn_query_fn)(*(DWORD**)ctrl)[0x58 / 4];
		q(ctrl, NULL, buf, idx);
		flags = *(WORD*)(buf + 0x32);
		((node_free_fn)0x0048C6F0)(buf, NULL);
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		flags = -2;
	}
	return flags;
}

static int helper_var(void* ctx, DWORD key) {
	int v = -999;
	__try {
		v = ((var_lookup_fn)0x006CDCA0)((BYTE*)ctx + 0x50, NULL, key);
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		v = -998;
	}
	return v;
}

static void helper_print(const helper_rec* r, const char* tag) {
	char who[48];
	if (helper_lines >= HELPER_LINES_MAX)
		return;
	helper_lines++;
	describe_owner(r->ent, who);
	twop_log("[HELP] %s f=%u from %08X this=%08X ctx=%08X ent=%08X (%s) ctrl=%08X arg=%08X (==K95836C: %d) | K838=%d K834=%d K830=%d | btn0B=%04X btn07=%04X btn01=%04X | mode %d->%d prev %d->%d ret=%d | phys=%08X f184=%d (0x4BDE00=%d) bit12(+0xC)=%d t180=%u | hero0=%d extra0=%d\n",
		tag, r->tick, (unsigned)r->caller, (unsigned)r->self, (unsigned)r->ctx, (unsigned)r->ent, who, (unsigned)r->ctrl,
		(unsigned)r->arg, (int)r->argcmp, r->v838, r->v834, r->v830, r->b0B & 0xFFFF, r->b07 & 0xFFFF, r->b01 & 0xFFFF,
		r->mode0, r->mode1, r->prev0, r->prev1, r->ret, (unsigned)r->phys, r->f184, r->f184 != 0, (int)((r->w0C >> 12) & 1), (unsigned)r->t180, r->h0, r->h1);
}

static void helper_swing_flush(void) {
	unsigned n = helper_ring_n < HELPER_RING ? helper_ring_n : HELPER_RING;
	if (!extra_hero_count)
		return;
	twop_log("[HELP] ---- enter-swing at f=%u (owner guess %08X): previous 2 frames, then 2 frames after ----\n", tick_count, (unsigned)swing_owner);
	for (unsigned i = 0; i < n; i++) {
		const helper_rec* r = &helper_ring[(helper_ring_n - n + i) % HELPER_RING];
		if (r->tick + 2 >= tick_count)
			helper_print(r, "before");
	}
	helper_live_until = tick_count + 2;
}

int __fastcall helper_hook(void* this, void* edx, int arg) {
	helper_rec r;
	DWORD* t = (DWORD*)this;
	DWORD caller = (DWORD)_ReturnAddress();
	int ret;

	memset(&r, 0, sizeof(r));
	r.mode0 = r.prev0 = r.mode1 = r.prev1 = -9;
	__try {
		r.mode0 = (int)t[0x50 / 4];
		r.prev0 = (int)t[0x54 / 4];
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
	}

	ret = helper_original(this, edx, arg);

	if (!extra_hero_count || diag_busy)
		return ret;

	diag_busy = 1;
	r.tick = tick_count;
	r.caller = caller;
	r.self = (DWORD)this;
	r.arg = (DWORD)arg;
	r.ret = ret & 0xFF;
	__try {
		DWORD* world = *(DWORD**)g_world_ptr;
		DWORD* hero0 = world ? (DWORD*)world[0x230 / 4] : NULL;
		r.mode1 = (int)t[0x50 / 4];
		r.prev1 = (int)t[0x54 / 4];
		r.ctx = t[8 / 4];
		r.argcmp = (DWORD)arg == *(DWORD*)0x0095836C;
		r.h0 = mode_of_entity(hero0);
		r.h1 = mode_of_entity(extra_heroes[0]);
		if (r.ctx) {
			r.ctrl = (DWORD)((ctx_lookup_fn)0x006A3390)((void*)r.ctx, NULL, *(DWORD*)0x0095855C, 1);
			r.ent = (DWORD)((ctx_lookup_fn)0x006A3390)((void*)r.ctx, NULL, *(DWORD*)0x0096C290, 1);
			r.v838 = helper_var((void*)r.ctx, *(DWORD*)0x0096C838);
			r.v834 = helper_var((void*)r.ctx, *(DWORD*)0x0096C834);
			r.v830 = helper_var((void*)r.ctx, *(DWORD*)0x0096C830);
			if (r.ent) {
				r.phys = *(DWORD*)(r.ent + 0x1C);
				if (r.phys) {
					r.f184 = *(BYTE*)(r.phys + 0x184);
					r.t180 = *(DWORD*)(r.phys + 0x180);
					r.w0C = *(DWORD*)(r.phys + 0xC);
				}
			}
			if (r.ctrl) {
				r.b0B = helper_button_flags((void*)r.ctrl, 0xB);
				r.b07 = helper_button_flags((void*)r.ctrl, 7);
				r.b01 = helper_button_flags((void*)r.ctrl, 1);
			}
		}
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
	}
	diag_busy = 0;

	{
		// log every change of the ground flag (+0x184) / bit 12 per physics object, in any frame
		static struct { DWORD phys; int f; int b12; } seen[8];
		static int nseen = 0, flip_logs = 0;
		if (r.phys) {
			int k = -1;
			for (int i = 0; i < nseen; i++)
				if (seen[i].phys == r.phys)
					k = i;
			if (k < 0 && nseen < 8) {
				k = nseen++;
				seen[k].phys = r.phys;
				seen[k].f = -1;
				seen[k].b12 = -1;
			}
			if (k >= 0 && flip_logs < 300 && (seen[k].f != r.f184 || seen[k].b12 != (int)((r.w0C >> 12) & 1))) {
				char who[48];
				describe_owner(r.ent, who);
				flip_logs++;
				twop_log("[FLAG] f=%u phys %08X (ent %08X %s ctx %08X): f184 %d -> %d, bit12 %d -> %d, t180=%u, from %08X (swing started f=%u) | hero0=%d extra0=%d\n",
					r.tick, (unsigned)r.phys, (unsigned)r.ent, who, (unsigned)r.ctx, seen[k].f, r.f184, seen[k].b12, (int)((r.w0C >> 12) & 1),
					(unsigned)r.t180, (unsigned)caller, swing_frame, r.h0, r.h1);
				seen[k].f = r.f184;
				seen[k].b12 = (int)((r.w0C >> 12) & 1);
			}
		}
	}
	helper_ring[helper_ring_n % HELPER_RING] = r;
	helper_ring_n++;
	if (r.tick <= helper_live_until && r.tick >= helper_live_until - 2)
		helper_print(&r, "after ");
	return ret;
}

static const DWORD helper_sites[] = {
	0x0044CF4E, 0x0047E0DA, 0x0047E484, 0x0047E5D3, 0x00488962, 0x00488F0F, 0x00489139, 0x0048927D, 0x004892EA
};

void install_helper_hooks(void) {
	int n = (int)(sizeof(helper_sites) / sizeof(helper_sites[0]));
	for (int i = 0; i < n; i++)
		HookFunc(helper_sites[i], helper_hook, 0, "Hooking a 0x6A7110 call site (transition helper probe)");
	twop_log("[HELP] transition helper probe installed on %d call sites\n", n);
}

static void patch_vtable_slot(DWORD slot_addr, DWORD expected, void* hook, const char* name) {
	DWORD* slot = (DWORD*)slot_addr;
	DWORD old;
	if (*slot != expected) {
		twop_log("[ST] vtable slot %08X holds %08X, not %08X; %s probe not installed\n", (unsigned)slot_addr, (unsigned)*slot, (unsigned)expected, name);
		return;
	}
	if (VirtualProtect(slot, 4, PAGE_READWRITE, &old)) {
		*slot = (DWORD)hook;
		VirtualProtect(slot, 4, old, &old);
		twop_log("[ST] %s probe installed\n", name);
	}
}

void install_swing_probes(void) {
	patch_vtable_slot(0x00877498, 0x00473650, state_473650_hook, "steer (0x473650)");
	patch_vtable_slot(0x008774D8, 0x0047DDD0, state_47DDD0_hook, "swing update (0x47DDD0)");
	patch_vtable_slot(0x00877170, 0x00469880, enter_jump_hook, "enter-jump (0x469880)");
	patch_vtable_slot(0x008774D0, 0x0047DA60, enter_swing_hook, "enter-swing (0x47DA60)");
	patch_vtable_slot(0x008775C8, 0x0045D340, enter_nine_hook, "enter mode 9 (0x45D340)");
	patch_vtable_slot(0x00877490, 0x004584E0, enter_ground_hook, "enter-ground (0x4584E0)");
	patch_vtable_slot(0x0087D3C8, 0x00467E10, brain_msg_hook, "brain message handler (0x467E10)");
}

/*
 * add_player calls 0x0055A420 (at 0x0055B44E) only for player 0. It copies the costume name
 * into the game state and asks the streamer to load that character's pack. Both logged crashes
 * happen after/inside this path while a first hero is already loaded, so for the second hero we
 * skip the request and reuse the resident pack (same character as the first hero).
 */
DWORD skip_pack_load = 0;

typedef void(__fastcall* load_hero_pack_ptr)(void* this, void* edx, char* name, int flag);
load_hero_pack_ptr load_hero_pack_original = (void*)0x0055A420;

void __fastcall load_hero_pack_hook(void* this, void* edx, char* name, int flag) {
	if (skip_pack_load) {
		twop_log("[2P] skipped pack-load request for '%s' (flag %d)\n", name ? name : "(null)", flag);
		return;
	}
	load_hero_pack_original(this, edx, name, flag);
}

static void spawn_second_hero(const char* costume) {

	DWORD* world = *(DWORD**)g_world_ptr;
	DWORD* hero0 = (DWORD*)world[0x230 / 4];

	DWORD* game_state_obj = *(DWORD**)0x009682E0;
	BYTE* name_buf = NULL;
	BYTE saved_name[32];
	DWORD saved_global = *(DWORD*)0x00959A70;

	if (!hero0) {
		twop_log("[2P] no existing hero, aborting\n");
		return;
	}

	if (game_state_obj) {
		name_buf = (BYTE*)(*(DWORD*)((BYTE*)game_state_obj + 0xC0)) + 0x454;
		memcpy(saved_name, name_buf, sizeof(saved_name));
	}

	// shadow world: intentionally leaked (2KB), never freed in case the engine keeps a pointer
	BYTE* shadow = calloc(1, 0x800);
	panic(shadow);

	((DWORD*)shadow)[0x230 / 4] = (DWORD)hero0;        // spawn reference = existing hero
	((DWORD*)shadow)[0x234 / 4] = world[0x234 / 4];
	((DWORD*)shadow)[0x238 / 4] = 0;                   // "no players yet"
	mString_constructor((mString*)(shadow + 0x3E0), NULL, "");

	twop_log("[2P] ---- new attempt ----\n");
	twop_log("[2P] world %08X, real player count %u, hero0 entity %08X\n",
		(unsigned)(DWORD)world, (unsigned)world[0x238 / 4], (unsigned)(DWORD)hero0);
	if (name_buf) {
		char shown[33];
		memcpy(shown, saved_name, 32);
		shown[32] = 0;
		twop_log("[2P] current hero name in game state: '%s'\n", shown);
	}
	if (name_buf) {
		char current[33];
		memcpy(current, saved_name, 32);
		current[32] = 0;
		if (strcmp(costume, current) != 0) {
			twop_log("[2P] '%s' is not the loaded character pack; spawning '%s' instead (a different pack can't be loaded yet)\n", costume, current);
			strcpy(second_costume, current);
			costume = second_costume;
		}
	}
	twop_log("[2P] spawning '%s' via shadow world %08X\n", costume, (unsigned)(DWORD)shadow);

	mString name;
	mString_constructor(&name, NULL, (char*)costume);

	int ok = 1;
	sprintf(extra_hero_name, "HERO%d", extra_hero_count + 1);
	sprintf(extra_cam_name, "CHASE_CAM%d", extra_hero_count + 1);
	twop_log("[2P] naming the new hero '%s' and its camera '%s'\n", extra_hero_name, extra_cam_name);
	// Everything add_player registers (the brain's 19 sub-objects, listeners, threads) is stamped with the
	// script owner id [[0x9685DC]+0x58], which is pad 0's id (1000000). Create the extra hero as owner
	// 1000001 (pad 1) so it listens on pad 1's channel from the start.
	DWORD* vm = *(DWORD**)0x009685DC;
	DWORD saved_owner = vm ? vm[0x58 / 4] : 0;
	if (vm) {
		twop_log("[2P] script owner id was %u, creating the hero as owner %u (pad 1)\n", (unsigned)saved_owner, 0xF4241u);
		vm[0x58 / 4] = 0xF4241u;
	}
	naming_active = 1;
	skip_pack_load = 1;
	__try {
		world_dynamics_system_add_player(shadow, NULL, &name);
	}
	__except (second_hero_filter(GetExceptionInformation())) {
		ok = 0;
	}
	skip_pack_load = 0;
	naming_active = 0;
	if (vm)
		vm[0x58 / 4] = saved_owner;

	mString_finalize(&name, NULL, 0);

	// put back the global state the count == 0 path overwrote
	if (name_buf)
		memcpy(name_buf, saved_name, sizeof(saved_name));
	*(DWORD*)0x00959A70 = saved_global;

	DWORD new_count = ((DWORD*)shadow)[0x238 / 4];
	DWORD* new_hero = (DWORD*)((DWORD*)shadow)[0x230 / 4];
	DWORD* new_ctrl = (DWORD*)((DWORD*)shadow)[0x234 / 4];

	if (!ok || new_count != 1 || new_hero == hero0) {
		twop_log("[2P] FAILED (ok=%d, shadow count=%u, new hero=%08X)\n",
			ok, (unsigned)new_count, (unsigned)(DWORD)new_hero);
		return;
	}

	second_hero_entity = new_hero;
	second_hero_ctrl = new_ctrl;
	if (extra_hero_count < 16)
		extra_heroes[extra_hero_count++] = new_hero;
	if (extra_cam_count < 16)
		extra_cams[extra_cam_count++] = new_ctrl;
	if (!dual_input_enabled)
		set_dual_input(1);   // first extra hero: bring pad 1 to life (keyboard+mouse = P1, controller = P2)
	else
		patch_extra_hero_ids(0xF4240u, 0xF4241u);
	{
		DWORD* brain = (DWORD*)new_hero[0x8C / 4];
		twop_log("[2P] extra hero entity vtable %08X, brain object %08X, brain player index %d\n",
			(unsigned)new_hero[0], (unsigned)(DWORD)brain, brain ? (int)brain[0x14 / 4] : -1);
	}
	log_input_state("after spawning extra hero");
	log_pads();
	// (the per-offset pad-id scan was only needed to find the 19 owner ids; they are known now)
	twop_log("[2P] SUCCESS: second hero entity %08X, controller object %08X\n",
		(unsigned)(DWORD)new_hero, (unsigned)(DWORD)new_ctrl);
}


typedef (*entity_teleport_abs_po_ptr)(DWORD, float*, int one);
entity_teleport_abs_po_ptr entity_teleport_abs_po = (void*)0x004F3890;


typedef DWORD* (__fastcall* ai_ai_core_get_info_node_ptr)(DWORD* this, void* edx, int a2, char a3);
ai_ai_core_get_info_node_ptr ai_ai_core_get_info_node = (void*)0x006A3390;




typedef struct {
	DWORD unk[32];
}string_hash;

typedef void(__fastcall* string_hash_initialize_ptr)(string_hash* this, void* edx, int a2, char* Str1, int a4);
string_hash_initialize_ptr string_hash_initialize = (void*)0x00547A00;


typedef int(__fastcall* script_object_find_func_ptr)(script_object* this, void* edx, string_hash* a2);
script_object_find_func_ptr script_object_find_func = (void*)0x0058EF80;


typedef DWORD  (__fastcall *script_executable_add_allocated_stuff_ptr)(script_executable* this, void *edx, int a2, int a3, int a4);
script_executable_add_allocated_stuff_ptr script_executable_add_allocated_stuff = (void*)0x005A34B0;


uint8_t __stdcall slf__debug_menu_entry__set_handler__str(slf* function, void* unk) {

	vm_stack_pop(function, 8);

	void** params = (void**)function->stack_ptr;

	debug_menu_entry* entry = params[0];
	char* scrpttext = params[1];

	string_hash strhash;
	string_hash_initialize(&strhash, NULL, 0, scrpttext, 0);


	script_instance* instance = function->thread->instance;
	int functionid = script_object_find_func(instance->object, NULL, *(void**)&strhash);
	entry->data = instance;
	entry->data1 = (void*)functionid;
	
	return 1;
}

uint8_t __stdcall slf__destroy_debug_menu_entry__debug_menu_entry(slf* function, void* unk) {

	vm_stack_pop(function, 4);

	debug_menu_entry** entry = (void*)function->stack_ptr;

	remove_debug_menu_entry(*entry);

	return 1;
}


uint8_t __stdcall slf__create_progression_menu_entry(slf *function, void *unk) {


	vm_stack_pop(function, 8);

	char** strs = (void*)function->stack_ptr;

	//printf("Entry: %s -> %s\n", strs[0], strs[1]);


	string_hash strhash;
	string_hash_initialize(&strhash, NULL, 0, strs[1], 0);


	script_instance* instance = function->thread->instance;
	int functionid = script_object_find_func(instance->object, NULL, *(void**)&strhash);

	debug_menu_entry entry;
	memset(&entry, 0, sizeof(entry));
	entry.entry_type = NORMAL;
	entry.data = instance;
	entry.data1 = (void*)functionid;

	strcpy(entry.text, strs[0]);

	add_debug_menu_entry(progression_menu, &entry);






	/*
	if(function->thread->instance->object->vmexecutable[functionid]->params != 4)
	*/
	
	int push = 0;
	vm_stack_push(function, &push, sizeof(push));
	return 1;
}

uint8_t __stdcall slf__create_debug_menu_entry(slf* function, void* unk) {

	vm_stack_pop(function, 4);

	char** strs = (void*)function->stack_ptr;

	//printf("Entry: %s ", strs[0]);


	debug_menu_entry entry;
	memset(&entry, 0, sizeof(entry));
	entry.entry_type = NORMAL;
	strcpy(entry.text, strs[0]);

	void *res = add_debug_menu_entry(script_menu, &entry);

	script_executable* se = function->thread->vmexecutable->unk_struct->scriptexecutable;
	script_executable_add_allocated_stuff(se, NULL, vm_debug_menu_entry_garbage_collection_id, (int)res, 0);

	//printf("%08X\n", res);

	int push = (int)res;
	vm_stack_push(function, &push, sizeof(push));
	return 1;
}


DWORD modulo(int num, DWORD mod) {
	if (num >= 0) {
		return num % mod;
	}

	int absolute = abs(num);
	if (absolute % mod == 0)
		return 0;


	return mod - absolute % mod;
}


void menu_go_down() {


	if ((current_menu->window_start + MAX_ELEMENTS_PAGE) < current_menu->used_slots) {

		if (current_menu->cur_index < MAX_ELEMENTS_PAGE / 2)
			current_menu->cur_index++;
		else
			current_menu->window_start++;
	}
	else {

		int num_elements = min(MAX_ELEMENTS_PAGE, current_menu->used_slots - current_menu->window_start);
		current_menu->cur_index = modulo(current_menu->cur_index + 1, num_elements);
		if (current_menu->cur_index == 0)
			current_menu->window_start = 0;
	}
}

void menu_go_up() {


	int num_elements = min(MAX_ELEMENTS_PAGE, current_menu->used_slots - current_menu->window_start);
	if (current_menu->window_start) {


		if (current_menu->cur_index > MAX_ELEMENTS_PAGE / 2)
			current_menu->cur_index--;
		else
			current_menu->window_start--;

	}
	else {

		int num_elements = min(MAX_ELEMENTS_PAGE, current_menu->used_slots - current_menu->window_start);
		current_menu->cur_index = modulo(current_menu->cur_index - 1, num_elements);
		if (current_menu->cur_index == (num_elements - 1))
			current_menu->window_start = current_menu->used_slots - num_elements;

	}

}

int sort_warp_entries(const debug_menu_entry* entry1, const debug_menu_entry* entry2) {
	return strcmp(entry1->text, entry2->text);
}


void district_variant_string_generator(debug_menu_entry* entry) {

	int old_variant = (int)entry->data1;
	int current_variant = region_get_district_variant(entry->data);

	if (current_variant == old_variant) {
		return;
	}

	char* region_name = region_get_name(entry->data);

	snprintf(entry->text, MAX_CHARS,"%s: %d", region_name, current_variant);
	entry->data1 = (void*)current_variant;
}




typedef enum {
	MENU_TOGGLE,
	MENU_ACCEPT,
	MENU_BACK,

	MENU_UP,
	MENU_DOWN,
	MENU_LEFT,
	MENU_RIGHT,


	MENU_KEY_MAX
}MenuKey;

uint32_t controllerKeys[MENU_KEY_MAX];

int get_menu_key_value(MenuKey key, int keyboard) {
	if (keyboard) {

		int i = 0;
		switch (key) {
			case MENU_TOGGLE:
				i = DIK_INSERT;
				break;
			case MENU_ACCEPT:
				i = DIK_RETURN;
				break;
			case MENU_BACK:
				i = DIK_ESCAPE;
				break;

			case MENU_UP:
				i = DIK_UPARROW;
				break;
			case MENU_DOWN:
				i = DIK_DOWNARROW;
				break;
			case MENU_LEFT:
				i = DIK_LEFTARROW;
				break;
			case MENU_RIGHT:
				i = DIK_RIGHTARROW;
				break;
		}
		return keys[i];
	}



	return controllerKeys[key];
}


int is_menu_key_pressed(MenuKey key, int keyboard) {
	return (get_menu_key_value(key, keyboard) == 2);
}

int is_menu_key_clicked(MenuKey key, int keyboard) {
	return get_menu_key_value(key, keyboard);
}

void GetDeviceStateHandleKeyboardInput(LPVOID lpvData) {
	BYTE* keysCurrent = lpvData;

	for (int i = 0; i < 256; i++) {

		if (keysCurrent[i]) {
			keys[i]++;
		}
		else {
			keys[i] = 0;
		}
	}

	
}

void read_and_update_controller_key_button(LPDIJOYSTATE2 joy, int index, MenuKey key) {
	int res = 0;
	if (joy->rgbButtons[index]) {
		controllerKeys[key]++;
	}
	else {
		controllerKeys[key] = 0;
	}
}


void read_and_update_controller_key_dpad(LPDIJOYSTATE2 joy, int angle, MenuKey key) {
	
	if (joy->rgdwPOV[0] == 0xFFFFFFFF)
		controllerKeys[key] = 0;
	else
		controllerKeys[key] = (joy->rgdwPOV[0] == angle) ? controllerKeys[key] + 1 : 0;
}


void GetDeviceStateHandleControllerInput(LPVOID lpvData) {
	LPDIJOYSTATE2 joy = lpvData;

	read_and_update_controller_key_button(joy, 1, MENU_ACCEPT);
	read_and_update_controller_key_button(joy, 2, MENU_BACK);
	read_and_update_controller_key_button(joy, 12, MENU_TOGGLE);



	read_and_update_controller_key_dpad(joy, 0, MENU_UP);
	read_and_update_controller_key_dpad(joy, 9000, MENU_RIGHT);
	read_and_update_controller_key_dpad(joy, 18000, MENU_DOWN);
	read_and_update_controller_key_dpad(joy, 27000, MENU_LEFT);

}

DWORD* g_TOD = (void*)0x0091E000;
void time_of_day_name_generator(debug_menu_entry *entry) {

	DWORD lastTOD = (DWORD)entry->data;
	DWORD currentTOD = *g_TOD;
	if (currentTOD == lastTOD) {
		return;
	}

	snprintf(entry->text, MAX_CHARS, "Time of Day: %d", currentTOD);
	lastTOD = currentTOD;
}

void time_of_day_handler(debug_menu_entry *entry, custom_key_type key, menu_handler_function original) {

	DWORD currentTOD = *g_TOD;
	switch (key) {
		case LEFT:
			us_lighting_switch_time_of_day(modulo(currentTOD - 1, 4));
			break;
		case RIGHT:
			us_lighting_switch_time_of_day(modulo(currentTOD + 1, 4));
			break;
	}
}

void menu_setup(int game_state, int keyboard) {

	//debug menu stuff
	if (is_menu_key_pressed(MENU_TOGGLE, keyboard) && (game_state == 6 || game_state == 7)) {


		if (debug_enabled && game_state == 7) {
			game_unpause(g_game_ptr);
			debug_enabled = !debug_enabled;
		}
		else if (!debug_enabled && game_state == 6) {
			game_pause(g_game_ptr);
			debug_enabled = !debug_enabled;
			current_menu = start_debug;
		}

		if (warp_menu->used_slots == 0) {

			debug_menu_entry poi = { "--- WARP TO POI ---", NORMAL, NULL };
			poi.data1 = (void*)1;
			add_debug_menu_entry(warp_menu, &poi);

			for (DWORD i = 0; i < *number_of_allocated_regions; i++) {
				region* cur_region = &(*all_regions)[i];
				char* region_name = region_get_name(cur_region);

				debug_menu_entry warp_entry = { "", NORMAL, cur_region };
				warp_entry.data1 = 0;
				strcpy(warp_entry.text, region_name);
				add_debug_menu_entry(warp_menu, &warp_entry);

				if (cur_region->variants >= 2) {
					debug_menu_entry variant_entry;
					memcpy(&variant_entry, &warp_entry, sizeof(debug_menu_entry));

					variant_entry.data1 = (void*)0xFFFFFFFF;

					variant_entry.entry_type = CUSTOM;
					variant_entry.custom_string_generator = district_variant_string_generator;
					variant_entry.custom_handler = NULL;
					add_debug_menu_entry(district_variants_menu, &variant_entry);
				}
			}
			qsort(warp_menu->entries, *number_of_allocated_regions, sizeof(debug_menu_entry), sort_warp_entries);
			qsort(district_variants_menu->entries, district_variants_menu->used_slots, sizeof(debug_menu_entry), sort_warp_entries);

		}


		if (options_menu->used_slots == 2) {
			BYTE* arr = *(BYTE**)0x96858C;
			debug_menu_entry render_fe = { "Render FE UI ", BOOLEAN_E,  &arr[4 + 0x90] };
			add_debug_menu_entry(options_menu, &render_fe);


			BYTE* flags = *(BYTE**)0x0096858C;
			debug_menu_entry live_in_glass_house = { "Live in Glass House ", BOOLEAN_E,  &flags[4 + 0x7A] };
			add_debug_menu_entry(options_menu, &live_in_glass_house);


			BYTE* god_mode = (void*)0x95A6A8;
			debug_menu_entry god_mode_entry = { "God Mode ", BOOLEAN_E,  &god_mode[0] };
			debug_menu_entry mega_god_mode = { "Mega God Mode ", BOOLEAN_E,  &god_mode[1] };
			debug_menu_entry ultra_god_mode = { "Ultra God Mode ", BOOLEAN_E,  &god_mode[2] };

			add_debug_menu_entry(options_menu, &god_mode_entry);
			add_debug_menu_entry(options_menu, &mega_god_mode);
			add_debug_menu_entry(options_menu, &ultra_god_mode);

			debug_menu_entry time_of_day = {
				.text = "Time of Day",
				.entry_type = CUSTOM,
				.custom_string_generator = time_of_day_name_generator,
				.custom_handler = time_of_day_handler,
				.data = (void*)0xFFFFFFFF
			};
			add_debug_menu_entry(options_menu, &time_of_day);
		}
	}
}

void menu_input_handler(int keyboard, int SCROLL_SPEED) {
	if (is_menu_key_clicked(MENU_DOWN, keyboard)) {


		int key_val = get_menu_key_value(MENU_DOWN, keyboard);
		if (key_val == 1) {
			menu_go_down();
		}
		else if ((key_val >= SCROLL_SPEED) && (key_val % SCROLL_SPEED == 0)) {
			menu_go_down();
		}
	}
	else if (is_menu_key_clicked(MENU_UP, keyboard)) {

		int key_val = get_menu_key_value(MENU_UP, keyboard);
		if (key_val == 1) {
			menu_go_up();
		}
		else if ((key_val >= SCROLL_SPEED) && (key_val % SCROLL_SPEED == 0)) {
			menu_go_up();
		}
	}
	else if (is_menu_key_pressed(MENU_ACCEPT, keyboard)) {
		current_menu->handler(&current_menu->entries[current_menu->window_start + current_menu->cur_index], ENTER);
	}
	else if (is_menu_key_pressed(MENU_BACK, keyboard)) {
		current_menu->go_back();
	}
	else if (is_menu_key_pressed(MENU_LEFT, keyboard) || is_menu_key_pressed(MENU_RIGHT, keyboard)) {

		debug_menu_entry* cur = &current_menu->entries[current_menu->window_start + current_menu->cur_index];
		custom_key_type pressed = (is_menu_key_pressed(MENU_LEFT, keyboard) ? LEFT : RIGHT);

		switch (cur->entry_type) {
			case BOOLEAN_E:
				current_menu->handler(cur, pressed);
				break;
			case CUSTOM:
				if (cur->custom_handler != NULL) {
					cur->custom_handler(cur, pressed, current_menu->handler);
				}
				else {
					current_menu->handler(cur, pressed);
				}
		}
	}
}

HRESULT __stdcall GetDeviceStateHook(IDirectInputDevice8* this, DWORD cbData, LPVOID lpvData) {


	HRESULT res = GetDeviceStateOriginal(this, cbData, lpvData);


	//printf("cbData %d %d %d\n", cbData, sizeof(DIJOYSTATE), sizeof(DIJOYSTATE2));


	
	//keyboard time babyyy
	if (cbData == 256 || cbData == sizeof(DIJOYSTATE2)) {

		
		if (cbData == 256)
			GetDeviceStateHandleKeyboardInput(lpvData);
		else if (cbData == sizeof(DIJOYSTATE2)) {
			GetDeviceStateHandleControllerInput(lpvData);
			log_joy_sample((LPDIJOYSTATE2)lpvData);
		}

		int game_state = 0;
		if (g_game_ptr)
			game_state = game_get_cur_state(g_game_ptr);

		//printf("INSERT %d %d %c\n", keys[DIK_INSERT], game_state, debug_enabled ? 'y' : 'n');

		int keyboard = cbData == 256;
		menu_setup(game_state, keyboard);

		if (debug_enabled) {
			menu_input_handler(keyboard, 5);
		}

	}


	if (debug_enabled) {
		memset(lpvData, 0, cbData);
	}

	if (dual_input_enabled && SUCCEEDED(res)) {
		if (cbData == 256) {
			if (input_pass == 1)
				memset(lpvData, 0, 256);
		}
		else if (cbData == sizeof(DIJOYSTATE2)) {
			if (input_pass == 0) {
				LPDIJOYSTATE2 j = (LPDIJOYSTATE2)lpvData;
				memset(j, 0, sizeof(*j));
				for (int i = 0; i < 4; i++)
					j->rgdwPOV[i] = 0xFFFFFFFF;
			}
		}
		else if (cbData == sizeof(DIMOUSESTATE2) || cbData == sizeof(DIMOUSESTATE)) {
			if (input_pass == 1)
				memset(lpvData, 0, cbData);
		}
	}



	//printf("Device State called %08X %d\n", this, cbData);

	if (SUCCEEDED(res) && dual_input_enabled) {
		int ps = input_pass ? 1 : 0;
		if (cbData == sizeof(DIJOYSTATE2)) {
			LPDIJOYSTATE2 jj = (LPDIJOYSTATE2)lpvData;
			DWORD mask = 0;
			for (int b = 0; b < 32; b++)
				if (jj->rgbButtons[b] & 0x80)
					mask |= 1u << b;
			raw_buttons[ps] = mask;
		}
		else if (cbData == 256) {
			int n = 0;
			for (int b = 0; b < 256; b++)
				if (((BYTE*)lpvData)[b] & 0x80)
					n++;
			raw_keys_down[ps] = n;
		}
	}

	return res;
}

typedef HRESULT(__stdcall* GetDeviceData_ptr)(IDirectInputDevice8*, DWORD, LPDIDEVICEOBJECTDATA, LPDWORD, DWORD);
GetDeviceData_ptr GetDeviceDataOriginal = NULL;

HRESULT __stdcall GetDeviceDataHook(IDirectInputDevice8* this, DWORD cbObjectData, LPDIDEVICEOBJECTDATA rgdod, LPDWORD pdwInOut, DWORD dwFlags) {

	HRESULT res = GetDeviceDataOriginal(this, cbObjectData, rgdod, pdwInOut, dwFlags);

	printf("data\n");
	if (res == DI_OK) {

		printf("All gud\n");
		for (DWORD i = 0; i < *pdwInOut; i++) {


			if (LOBYTE(rgdod[i].dwData) > 0) {

				if (rgdod[i].dwOfs == DIK_ESCAPE) {

					printf("Pressed escaped\n");
					__debugbreak();
				}
			}
		}
	}
	//printf("Device Data called %08X\n", this);

	return res;
}



typedef HRESULT(__stdcall* IDirectInput8CreateDevice_ptr)(IDirectInput8W*, const GUID*, LPDIRECTINPUTDEVICE8W*, LPUNKNOWN);
IDirectInput8CreateDevice_ptr createDeviceOriginal = NULL;

HRESULT  __stdcall IDirectInput8CreateDeviceHook(IDirectInput8W* this, const GUID* guid, LPDIRECTINPUTDEVICE8W* device, LPUNKNOWN unk) {

	//printf("CreateDevice %d %d %d %d %d %d %d\n", *guid, GUID_SysMouse, GUID_SysKeyboard, GUID_SysKeyboardEm, GUID_SysKeyboardEm2, GUID_SysMouseEm, GUID_SysMouseEm2);
	printf("Guid = {%08lX-%04hX-%04hX-%02hhX%02hhX-%02hhX%02hhX%02hhX%02hhX%02hhX%02hhX}\n",
		guid->Data1, guid->Data2, guid->Data3,
		guid->Data4[0], guid->Data4[1], guid->Data4[2], guid->Data4[3],
		guid->Data4[4], guid->Data4[5], guid->Data4[6], guid->Data4[7]);

	HRESULT res = createDeviceOriginal(this, guid, device, unk);


	if (IsEqualGUID(&GUID_SysMouse, guid))
		return res; // ignore mouse

	if (IsEqualGUID(&GUID_SysKeyboard, guid))
		puts("Found the keyboard");
	else
		puts("Hooking something different...maybe a controller");

	DWORD* vtbl = (DWORD*)(*device)->lpVtbl;
	if (!GetDeviceStateOriginal) {
		GetDeviceStateOriginal = (void*)vtbl[9];
		vtbl[9] = (DWORD)GetDeviceStateHook;
	}

	if (!GetDeviceDataOriginal) {
		GetDeviceDataOriginal = (void*)vtbl[10];
		vtbl[10] = (DWORD)GetDeviceDataHook;
	}

	return res;
}

typedef HRESULT(__stdcall* IDirectInput8Release_ptr)(IDirectInput8W*);
IDirectInput8Release_ptr releaseDeviceOriginal = NULL;

HRESULT  __stdcall IDirectInput8ReleaseHook(IDirectInput8W* this) {

	printf("Release\n");

	return releaseDeviceOriginal(this);
}


typedef HRESULT(__stdcall* DirectInput8Create_ptr)(HINSTANCE hinst, DWORD dwVersion, REFIID riidltf, LPVOID* ppvOut, LPUNKNOWN punkOuter);
HRESULT __stdcall HookDirectInput8Create(HINSTANCE hinst, DWORD dwVersion, REFIID riidltf, LPVOID* ppvOut, LPUNKNOWN punkOuter)
{
	DirectInput8Create_ptr caller = *(void**)0x00987944;
	HRESULT res = caller(hinst, dwVersion, riidltf, ppvOut, punkOuter);


	IDirectInput8** iDir = (void*)ppvOut;
	printf("it's me mario %08X %08X\n", (DWORD)ppvOut, (DWORD)(*iDir)->lpVtbl);


	DWORD* vtbl = (DWORD*)(*iDir)->lpVtbl;
	if (!createDeviceOriginal) {
		createDeviceOriginal = (void*)vtbl[3];
		vtbl[3] = (DWORD)IDirectInput8CreateDeviceHook;
	}


	return res;
}


DWORD hookDirectInputAddress = (DWORD)HookDirectInput8Create;


typedef int(__fastcall* game_handle_game_states_ptr)(void* this, void* edx, void* a2);
game_handle_game_states_ptr game_handle_game_states_original = (void*)0x0055D510;

int __fastcall game_handle_game_states(void* this, void* edx, void* a2) {

	if (!g_game_ptr) {
		g_game_ptr = this;
	}

	if (changing_model) {


		changing_model--;

		if (!changing_model) {
			mString str;
			mString_constructor(&str, NULL, current_costume);
			world_dynamics_system_add_player(*(DWORD**)g_world_ptr, NULL, &str);
			mString_finalize(&str, NULL, 0);
			game_unpause(g_game_ptr);
		}
	}

	monitor_modes();

	if (GetAsyncKeyState(VK_F9) & 1)
		set_dual_input(1);
	if (GetAsyncKeyState(VK_F10) & 1)
		set_dual_input(0);
	if (GetAsyncKeyState(VK_F8) & 1)
		bring_extra_heroes_to_p1();
	if (GetAsyncKeyState(VK_F7) & 1) {
		context_mode = !context_mode;
		twop_log("[CTX] context resolution %s\n", context_mode ? "ON" : "OFF");
	}

	if (GetAsyncKeyState(VK_F6) & 1) {
		swing_filter_on = !swing_filter_on;
		twop_log("[TRIG] cross-hero swing filter %s (suppressed so far: %u)\n", swing_filter_on ? "ON" : "OFF", filter_hits);
	}

	if (adding_second_player) {

		adding_second_player--;

		if (!adding_second_player) {
			spawn_second_hero(second_costume);
			game_unpause(g_game_ptr);
		}
	}

	/*
	if (game_get_cur_state(this) == 14)
		__debugbreak();
		*/


		//printf("Current state %d %08X\n", game_get_cur_state(this), g_game_ptr);

	return game_handle_game_states_original(this, edx, a2);
}

typedef void* (__fastcall* sub_41F9D0_ptr)(char* this, void* edx, const char* a2, signed int a3);
sub_41F9D0_ptr sub_41F9D0 = (void*)0x41F9D0;


void* __fastcall sub_41F9D0_hook(char* this, void* edx, const char* a2, signed int a3) {

	//printf("mString:%s\n", a2);


	return sub_41F9D0(this, edx, a2, a3);
}

typedef DWORD(__fastcall* ai_hero_base_state_check_transition_ptr)(DWORD* this, void* edx, DWORD* a2, int a3);
ai_hero_base_state_check_transition_ptr ai_hero_base_state_check_transition = (void*)0x00478D80;

DWORD __fastcall ai_hero_base_state_check_transition_hook(DWORD* this, void* edx, DWORD* a2, int a3) {
	ai_current_player = this;
	return ai_hero_base_state_check_transition(this, edx, a2, a3);
}


typedef DWORD* (__fastcall* get_info_node_ptr)(void* this, void* edx, int a2, char a3);
get_info_node_ptr get_info_node = (void*)0x006A3390;

DWORD* __fastcall get_info_node_hook(void* this, void* edx, int a2, char a3) {

	DWORD* res = get_info_node(this, edx, a2, a3);

	fancy_player_ptr = res;
	return res;
}


typedef int (_fastcall* resource_pack_streamer_load_internal_ptr)(void* this, void* edx, char* str, int a3, int a4, int a5);
resource_pack_streamer_load_internal_ptr resource_pack_streamer_load_internal = (void*)0x0054C580;



uint8_t __fastcall os_developer_options(BYTE *this, void *edx, int flag) {

	char** flag_list = (void*)0x936420;
	char* flag_text = flag_list[flag];
		
	uint8_t res = this[flag + 4];

	if (flag == 0x90) {
		printf("Game wants to know about: %d (%s) -> %d\n", flag, flag_text, res);
		__debugbreak();
	}
	
	
	//this[5 + 4] = 1;
	
	return res;
}

void dump_vtable(const char* name, DWORD* vtable) {
	printf("%s|%08X|%08X\n", name, vtable[0], vtable[1]);
}

void hook_slf_vtable(void* decons, void* action, DWORD* vtable) {
	vtable[0] = (DWORD)decons;
	vtable[1] = (DWORD)action;
}

void hook_slf_vtables() {

	hook_slf_vtable(slf_deconstructor_abs_delay_num, slf_action_abs_delay_num, (void*)0x89a724);
	hook_slf_vtable(slf_deconstructor_acos_num, slf_action_acos_num, (void*)0x89a91c);
	hook_slf_vtable(slf_deconstructor_add_2d_debug_str_vector3d_vector3d_num_str, slf_action_add_2d_debug_str_vector3d_vector3d_num_str, (void*)0x89a860);
	hook_slf_vtable(slf_deconstructor_add_2d_debug_str_vector3d_vector3d_num_str_num, slf_action_add_2d_debug_str_vector3d_vector3d_num_str_num, (void*)0x89a858);
	hook_slf_vtable(slf_deconstructor_add_3d_debug_str_vector3d_vector3d_num_str, slf_action_add_3d_debug_str_vector3d_vector3d_num_str, (void*)0x89a850);
	hook_slf_vtable(slf_deconstructor_add_civilian_info_vector3d_num_num_num, slf_action_add_civilian_info_vector3d_num_num_num, (void*)0x89c5bc);
	hook_slf_vtable(slf_deconstructor_add_civilian_info_entity_entity_num_num_num, slf_action_add_civilian_info_entity_entity_num_num_num, (void*)0x89c5cc);
	hook_slf_vtable(slf_deconstructor_add_debug_cyl_vector3d_vector3d_num, slf_action_add_debug_cyl_vector3d_vector3d_num, (void*)0x89a774);
	hook_slf_vtable(slf_deconstructor_add_debug_cyl_vector3d_vector3d_num_vector3d_num, slf_action_add_debug_cyl_vector3d_vector3d_num_vector3d_num, (void*)0x89a77c);
	hook_slf_vtable(slf_deconstructor_add_debug_line_vector3d_vector3d, slf_action_add_debug_line_vector3d_vector3d, (void*)0x89a764);
	hook_slf_vtable(slf_deconstructor_add_debug_line_vector3d_vector3d_vector3d_num, slf_action_add_debug_line_vector3d_vector3d_vector3d_num, (void*)0x89a76c);
	hook_slf_vtable(slf_deconstructor_add_debug_sphere_vector3d_num, slf_action_add_debug_sphere_vector3d_num, (void*)0x89a754);
	hook_slf_vtable(slf_deconstructor_add_debug_sphere_vector3d_num_vector3d_num, slf_action_add_debug_sphere_vector3d_num_vector3d_num, (void*)0x89a75c);
	hook_slf_vtable(slf_deconstructor_add_glass_house_str, slf_action_add_glass_house_str, (void*)0x89a548);
	hook_slf_vtable(slf_deconstructor_add_glass_house_str_num, slf_action_add_glass_house_str_num, (void*)0x89a550);
	hook_slf_vtable(slf_deconstructor_add_glass_house_str_num_vector3d, slf_action_add_glass_house_str_num_vector3d, (void*)0x89a560);
	hook_slf_vtable(slf_deconstructor_add_glass_house_str_vector3d, slf_action_add_glass_house_str_vector3d, (void*)0x89a558);
	hook_slf_vtable(slf_deconstructor_add_to_console_str, slf_action_add_to_console_str, (void*)0x89a834);
	hook_slf_vtable(slf_deconstructor_add_traffic_model_num_str, slf_action_add_traffic_model_num_str, (void*)0x89c5a4);
	hook_slf_vtable(slf_deconstructor_allow_suspend_thread_num, slf_action_allow_suspend_thread_num, (void*)0x89a594);
	hook_slf_vtable(slf_deconstructor_angle_between_vector3d_vector3d, slf_action_angle_between_vector3d_vector3d, (void*)0x89ba50);
	hook_slf_vtable(slf_deconstructor_apply_donut_damage_vector3d_num_num_num_num_num, slf_action_apply_donut_damage_vector3d_num_num_num_num_num, (void*)0x89a804);
	hook_slf_vtable(slf_deconstructor_apply_radius_damage_vector3d_num_num_num_num, slf_action_apply_radius_damage_vector3d_num_num_num_num, (void*)0x89a7fc);
	hook_slf_vtable(slf_deconstructor_apply_radius_subdue_vector3d_num_num_num_num, slf_action_apply_radius_subdue_vector3d_num_num_num_num, (void*)0x89a80c);
	hook_slf_vtable(slf_deconstructor_assert_num_str, slf_action_assert_num_str, (void*)0x89a518);
	hook_slf_vtable(slf_deconstructor_attach_decal_str_vector3d_num_vector3d_entity, slf_action_attach_decal_str_vector3d_num_vector3d_entity, (void*)0x89a9fc);
	hook_slf_vtable(slf_deconstructor_begin_screen_recording_str_num, slf_action_begin_screen_recording_str_num, (void*)0x89b7b0);
	hook_slf_vtable(slf_deconstructor_blackscreen_off_num, slf_action_blackscreen_off_num, (void*)0x89bcac);
	hook_slf_vtable(slf_deconstructor_blackscreen_on_num, slf_action_blackscreen_on_num, (void*)0x89bca0);
	hook_slf_vtable(slf_deconstructor_bring_up_dialog_box_num_num_elip, slf_action_bring_up_dialog_box_num_num_elip, (void*)0x89bc28);
	hook_slf_vtable(slf_deconstructor_bring_up_dialog_box_debug_str_num_str, slf_action_bring_up_dialog_box_debug_str_num_str, (void*)0x89bc38);
	hook_slf_vtable(slf_deconstructor_bring_up_dialog_box_title_num_num_num_elip, slf_action_bring_up_dialog_box_title_num_num_num_elip, (void*)0x89bc30);
	hook_slf_vtable(slf_deconstructor_bring_up_medal_award_box_num, slf_action_bring_up_medal_award_box_num, (void*)0x89bb10);
	hook_slf_vtable(slf_deconstructor_bring_up_race_announcer, slf_action_bring_up_race_announcer, (void*)0x89bb08);
	hook_slf_vtable(slf_deconstructor_calc_launch_vector_vector3d_vector3d_num_entity, slf_action_calc_launch_vector_vector3d_vector3d_num_entity, (void*)0x89a98c);
	hook_slf_vtable(slf_deconstructor_can_load_pack_str, slf_action_can_load_pack_str, (void*)0x89c3f4);
	hook_slf_vtable(slf_deconstructor_chase_cam, slf_action_chase_cam, (void*)0x89af04);
	hook_slf_vtable(slf_deconstructor_clear_all_grenades, slf_action_clear_all_grenades, (void*)0x89a95c);
	hook_slf_vtable(slf_deconstructor_clear_civilians_within_radius_vector3d_num, slf_action_clear_civilians_within_radius_vector3d_num, (void*)0x89c5e4);
	hook_slf_vtable(slf_deconstructor_clear_controls, slf_action_clear_controls, (void*)0x89bd48);
	hook_slf_vtable(slf_deconstructor_clear_debug_all, slf_action_clear_debug_all, (void*)0x89a79c);
	hook_slf_vtable(slf_deconstructor_clear_debug_cyls, slf_action_clear_debug_cyls, (void*)0x89a794);
	hook_slf_vtable(slf_deconstructor_clear_debug_lines, slf_action_clear_debug_lines, (void*)0x89a78c);
	hook_slf_vtable(slf_deconstructor_clear_debug_spheres, slf_action_clear_debug_spheres, (void*)0x89a784);
	hook_slf_vtable(slf_deconstructor_clear_screen, slf_action_clear_screen, (void*)0x89a944);
	hook_slf_vtable(slf_deconstructor_clear_traffic_within_radius_vector3d_num, slf_action_clear_traffic_within_radius_vector3d_num, (void*)0x89c5dc);
	hook_slf_vtable(slf_deconstructor_col_check_vector3d_vector3d_num, slf_action_col_check_vector3d_vector3d_num, (void*)0x89a868);
	hook_slf_vtable(slf_deconstructor_console_exec_str, slf_action_console_exec_str, (void*)0x89a83c);
	hook_slf_vtable(slf_deconstructor_copy_vector3d_list_vector3d_list_vector3d_list, slf_action_copy_vector3d_list_vector3d_list_vector3d_list, (void*)0x89bed4);
	hook_slf_vtable(slf_deconstructor_cos_num, slf_action_cos_num, (void*)0x89a90c);
	hook_slf_vtable(slf_deconstructor_create_beam, slf_action_create_beam, (void*)0x89abb4);
	hook_slf_vtable(slf_deconstructor_create_credits, slf_action_create_credits, (void*)0x89bae8);
	hook_slf_vtable(slf_deconstructor_create_cut_scene_str, slf_action_create_cut_scene_str, (void*)0x89b7c0);
	//hook_slf_vtable(slf_deconstructor_create_debug_menu_entry_str, slf_action_create_debug_menu_entry_str, (void*)0x89c704);
	//hook_slf_vtable(slf_deconstructor_create_debug_menu_entry_str_str, slf_action_create_debug_menu_entry_str_str, (void*)0x89c70c);
	hook_slf_vtable(slf_deconstructor_create_decal_str_vector3d_num_vector3d, slf_action_create_decal_str_vector3d_num_vector3d, (void*)0x89a9f4);
	hook_slf_vtable(slf_deconstructor_create_entity_str, slf_action_create_entity_str, (void*)0x89af0c);
	hook_slf_vtable(slf_deconstructor_create_entity_str_str, slf_action_create_entity_str_str, (void*)0x89af14);
	hook_slf_vtable(slf_deconstructor_create_entity_in_hero_region_str, slf_action_create_entity_in_hero_region_str, (void*)0x89af2c);
	hook_slf_vtable(slf_deconstructor_create_entity_list, slf_action_create_entity_list, (void*)0x89bfcc);
	hook_slf_vtable(slf_deconstructor_create_entity_tracker_entity, slf_action_create_entity_tracker_entity, (void*)0x89c594);
	hook_slf_vtable(slf_deconstructor_create_item_str, slf_action_create_item_str, (void*)0x89b6b4);
	hook_slf_vtable(slf_deconstructor_create_line_info_vector3d_vector3d, slf_action_create_line_info_vector3d_vector3d, (void*)0x89b708);
	hook_slf_vtable(slf_deconstructor_create_lofi_stereo_sound_inst_str, slf_action_create_lofi_stereo_sound_inst_str, (void*)0x89b818);
	hook_slf_vtable(slf_deconstructor_create_num_list, slf_action_create_num_list, (void*)0x89bf54);
	hook_slf_vtable(slf_deconstructor_create_pfx_str, slf_action_create_pfx_str, (void*)0x89c904);
	hook_slf_vtable(slf_deconstructor_create_pfx_str_vector3d, slf_action_create_pfx_str_vector3d, (void*)0x89c90c);
	hook_slf_vtable(slf_deconstructor_create_polytube, slf_action_create_polytube, (void*)0x89c228);
	hook_slf_vtable(slf_deconstructor_create_polytube_str, slf_action_create_polytube_str, (void*)0x89c230);
	//hook_slf_vtable(slf_deconstructor_create_progression_menu_entry_str_str, slf_action_create_progression_menu_entry_str_str, (void*)0x89c714);
	hook_slf_vtable(slf_deconstructor_create_sound_inst, slf_action_create_sound_inst, (void*)0x89b7f8);
	hook_slf_vtable(slf_deconstructor_create_sound_inst_str, slf_action_create_sound_inst_str, (void*)0x89b800);
	hook_slf_vtable(slf_deconstructor_create_stompable_music_sound_inst_str, slf_action_create_stompable_music_sound_inst_str, (void*)0x89b808);
	hook_slf_vtable(slf_deconstructor_create_str_list, slf_action_create_str_list, (void*)0x89c044);
	hook_slf_vtable(slf_deconstructor_create_taunt_entry_entity_str_num, slf_action_create_taunt_entry_entity_str_num, (void*)0x89c63c);
	hook_slf_vtable(slf_deconstructor_create_taunt_exchange_entity_entity_num_num_num_num_elip, slf_action_create_taunt_exchange_entity_entity_num_num_num_num_elip, (void*)0x89c6b4);
	hook_slf_vtable(slf_deconstructor_create_taunt_exchange_list, slf_action_create_taunt_exchange_list, (void*)0x89c0dc);
	hook_slf_vtable(slf_deconstructor_create_threat_assessment_meter, slf_action_create_threat_assessment_meter, (void*)0x89c6cc);
	hook_slf_vtable(slf_deconstructor_create_time_limited_entity_str_num, slf_action_create_time_limited_entity_str_num, (void*)0x89af3c);
	hook_slf_vtable(slf_deconstructor_create_trigger_entity_num, slf_action_create_trigger_entity_num, (void*)0x89b970);
	hook_slf_vtable(slf_deconstructor_create_trigger_str_vector3d_num, slf_action_create_trigger_str_vector3d_num, (void*)0x89b968);
	hook_slf_vtable(slf_deconstructor_create_trigger_vector3d_num, slf_action_create_trigger_vector3d_num, (void*)0x89b960);
	hook_slf_vtable(slf_deconstructor_create_unstompable_script_cutscene_sound_inst_str, slf_action_create_unstompable_script_cutscene_sound_inst_str, (void*)0x89b810);
	hook_slf_vtable(slf_deconstructor_create_vector3d_list, slf_action_create_vector3d_list, (void*)0x89becc);
	hook_slf_vtable(slf_deconstructor_cross_vector3d_vector3d, slf_action_cross_vector3d_vector3d, (void*)0x89ba38);
	hook_slf_vtable(slf_deconstructor_debug_breakpoint, slf_action_debug_breakpoint, (void*)0x89a510);
	hook_slf_vtable(slf_deconstructor_debug_print_num_str, slf_action_debug_print_num_str, (void*)0x89a528);
	hook_slf_vtable(slf_deconstructor_debug_print_num_vector3d_str, slf_action_debug_print_num_vector3d_str, (void*)0x89a530);
	hook_slf_vtable(slf_deconstructor_debug_print_str, slf_action_debug_print_str, (void*)0x89a520);
	hook_slf_vtable(slf_deconstructor_debug_print_set_background_color_vector3d, slf_action_debug_print_set_background_color_vector3d, (void*)0x89a538);
	hook_slf_vtable(slf_deconstructor_delay_num, slf_action_delay_num, (void*)0x89a71c);
	hook_slf_vtable(slf_deconstructor_destroy_credits, slf_action_destroy_credits, (void*)0x89baf0);
	//hook_slf_vtable(slf_deconstructor_destroy_debug_menu_entry_debug_menu_entry, slf_action_destroy_debug_menu_entry_debug_menu_entry, (void*)0x89c71c);
	hook_slf_vtable(slf_deconstructor_destroy_entity_entity, slf_action_destroy_entity_entity, (void*)0x89af34);
	hook_slf_vtable(slf_deconstructor_destroy_entity_list_entity_list, slf_action_destroy_entity_list_entity_list, (void*)0x89bfd4);
	hook_slf_vtable(slf_deconstructor_destroy_entity_tracker_entity_tracker, slf_action_destroy_entity_tracker_entity_tracker, (void*)0x89c59c);
	hook_slf_vtable(slf_deconstructor_destroy_line_info_line_info, slf_action_destroy_line_info_line_info, (void*)0x89b710);
	hook_slf_vtable(slf_deconstructor_destroy_num_list_num_list, slf_action_destroy_num_list_num_list, (void*)0x89bf5c);
	hook_slf_vtable(slf_deconstructor_destroy_pfx_pfx, slf_action_destroy_pfx_pfx, (void*)0x89c914);
	hook_slf_vtable(slf_deconstructor_destroy_str_list_str_list, slf_action_destroy_str_list_str_list, (void*)0x89c04c);
	hook_slf_vtable(slf_deconstructor_destroy_taunt_entry_taunt_entry, slf_action_destroy_taunt_entry_taunt_entry, (void*)0x89c644);
	hook_slf_vtable(slf_deconstructor_destroy_taunt_exchange_taunt_exchange, slf_action_destroy_taunt_exchange_taunt_exchange, (void*)0x89c6bc);
	hook_slf_vtable(slf_deconstructor_destroy_taunt_exchange_list_taunt_exchange_list, slf_action_destroy_taunt_exchange_list_taunt_exchange_list, (void*)0x89c0e4);
	hook_slf_vtable(slf_deconstructor_destroy_threat_assessment_meter_tam, slf_action_destroy_threat_assessment_meter_tam, (void*)0x89c6d4);
	hook_slf_vtable(slf_deconstructor_destroy_trigger_trigger, slf_action_destroy_trigger_trigger, (void*)0x89b978);
	hook_slf_vtable(slf_deconstructor_destroy_vector3d_list_vector3d_list, slf_action_destroy_vector3d_list_vector3d_list, (void*)0x89bedc);
	hook_slf_vtable(slf_deconstructor_dilated_delay_num, slf_action_dilated_delay_num, (void*)0x89a72c);
	hook_slf_vtable(slf_deconstructor_disable_marky_cam_num, slf_action_disable_marky_cam_num, (void*)0x89a5f4);
	hook_slf_vtable(slf_deconstructor_disable_nearby_occlusion_only_obb_vector3d, slf_action_disable_nearby_occlusion_only_obb_vector3d, (void*)0x89a5e4);
	hook_slf_vtable(slf_deconstructor_disable_player_shadows, slf_action_disable_player_shadows, (void*)0x89a614);
	hook_slf_vtable(slf_deconstructor_disable_subtitles, slf_action_disable_subtitles, (void*)0x89a93c);
	hook_slf_vtable(slf_deconstructor_disable_vibrator, slf_action_disable_vibrator, (void*)0x89a7dc);
	hook_slf_vtable(slf_deconstructor_disable_zoom_map_num, slf_action_disable_zoom_map_num, (void*)0x89bbf0);
	hook_slf_vtable(slf_deconstructor_distance3d_vector3d_vector3d, slf_action_distance3d_vector3d_vector3d, (void*)0x89ba48);
	hook_slf_vtable(slf_deconstructor_distance_chase_widget_set_pos_num, slf_action_distance_chase_widget_set_pos_num, (void*)0x89bb88);
	hook_slf_vtable(slf_deconstructor_distance_chase_widget_turn_off, slf_action_distance_chase_widget_turn_off, (void*)0x89bb80);
	hook_slf_vtable(slf_deconstructor_distance_chase_widget_turn_on_num_num, slf_action_distance_chase_widget_turn_on_num_num, (void*)0x89bb78);
	hook_slf_vtable(slf_deconstructor_distance_race_widget_set_boss_pos_num, slf_action_distance_race_widget_set_boss_pos_num, (void*)0x89bba8);
	hook_slf_vtable(slf_deconstructor_distance_race_widget_set_hero_pos_num, slf_action_distance_race_widget_set_hero_pos_num, (void*)0x89bba0);
	hook_slf_vtable(slf_deconstructor_distance_race_widget_set_types_num_num, slf_action_distance_race_widget_set_types_num_num, (void*)0x89bbb0);
	hook_slf_vtable(slf_deconstructor_distance_race_widget_turn_off, slf_action_distance_race_widget_turn_off, (void*)0x89bb98);
	hook_slf_vtable(slf_deconstructor_distance_race_widget_turn_on, slf_action_distance_race_widget_turn_on, (void*)0x89bb90);
	hook_slf_vtable(slf_deconstructor_district_id_str, slf_action_district_id_str, (void*)0x89c47c);
	hook_slf_vtable(slf_deconstructor_district_name_num, slf_action_district_name_num, (void*)0x89c484);
	hook_slf_vtable(slf_deconstructor_dot_vector3d_vector3d, slf_action_dot_vector3d_vector3d, (void*)0x89ba30);
	hook_slf_vtable(slf_deconstructor_dump_searchable_region_list_str, slf_action_dump_searchable_region_list_str, (void*)0x89a994);
	hook_slf_vtable(slf_deconstructor_enable_ai_num, slf_action_enable_ai_num, (void*)0x89a6cc);
	hook_slf_vtable(slf_deconstructor_enable_civilians_num, slf_action_enable_civilians_num, (void*)0x89c5ec);
	hook_slf_vtable(slf_deconstructor_enable_controls_num, slf_action_enable_controls_num, (void*)0x89bd40);
	hook_slf_vtable(slf_deconstructor_enable_entity_fading_num, slf_action_enable_entity_fading_num, (void*)0x89b05c);
	hook_slf_vtable(slf_deconstructor_enable_interface_num, slf_action_enable_interface_num, (void*)0x89a6c4);
	hook_slf_vtable(slf_deconstructor_enable_marky_cam_num, slf_action_enable_marky_cam_num, (void*)0x89a5bc);
	hook_slf_vtable(slf_deconstructor_enable_mini_map_num, slf_action_enable_mini_map_num, (void*)0x89bbe8);
	hook_slf_vtable(slf_deconstructor_enable_nearby_occlusion_only_obb_vector3d, slf_action_enable_nearby_occlusion_only_obb_vector3d, (void*)0x89a5dc);
	hook_slf_vtable(slf_deconstructor_enable_obb_vector3d_num, slf_action_enable_obb_vector3d_num, (void*)0x89a588);
	hook_slf_vtable(slf_deconstructor_enable_pause_num, slf_action_enable_pause_num, (void*)0x89a6ac);
	hook_slf_vtable(slf_deconstructor_enable_physics_num, slf_action_enable_physics_num, (void*)0x89a6dc);
	hook_slf_vtable(slf_deconstructor_enable_player_shadows, slf_action_enable_player_shadows, (void*)0x89a61c);
	hook_slf_vtable(slf_deconstructor_enable_pois_num, slf_action_enable_pois_num, (void*)0x89a6ec);
	hook_slf_vtable(slf_deconstructor_enable_quad_path_connector_district_num_district_num_num, slf_action_enable_quad_path_connector_district_num_district_num_num, (void*)0x89a578);
	hook_slf_vtable(slf_deconstructor_enable_subtitles, slf_action_enable_subtitles, (void*)0x89a934);
	hook_slf_vtable(slf_deconstructor_enable_tokens_of_type_num_num, slf_action_enable_tokens_of_type_num_num, (void*)0x89b54c);
	hook_slf_vtable(slf_deconstructor_enable_traffic_num, slf_action_enable_traffic_num, (void*)0x89c5fc);
	hook_slf_vtable(slf_deconstructor_enable_user_camera_num, slf_action_enable_user_camera_num, (void*)0x89a5d4);
	hook_slf_vtable(slf_deconstructor_enable_vibrator, slf_action_enable_vibrator, (void*)0x89a7e4);
	hook_slf_vtable(slf_deconstructor_end_current_patrol, slf_action_end_current_patrol, (void*)0x89c4fc);
	hook_slf_vtable(slf_deconstructor_end_cut_scenes, slf_action_end_cut_scenes, (void*)0x89b7c8);
	hook_slf_vtable(slf_deconstructor_end_screen_recording, slf_action_end_screen_recording, (void*)0x89b7b8);
	hook_slf_vtable(slf_deconstructor_entity_col_check_entity_entity, slf_action_entity_col_check_entity_entity, (void*)0x89a88c);
	hook_slf_vtable(slf_deconstructor_entity_exists_str, slf_action_entity_exists_str, (void*)0x89af24);
	hook_slf_vtable(slf_deconstructor_entity_get_entity_tracker_entity, slf_action_entity_get_entity_tracker_entity, (void*)0x89b034);
	hook_slf_vtable(slf_deconstructor_entity_has_entity_tracker_entity, slf_action_entity_has_entity_tracker_entity, (void*)0x89b02c);
	hook_slf_vtable(slf_deconstructor_exit_water_entity, slf_action_exit_water_entity, (void*)0x89a604);
	hook_slf_vtable(slf_deconstructor_find_closest_point_on_a_path_to_point_vector3d, slf_action_find_closest_point_on_a_path_to_point_vector3d, (void*)0x89a570);
	hook_slf_vtable(slf_deconstructor_find_district_for_point_vector3d, slf_action_find_district_for_point_vector3d, (void*)0x89a81c);
	hook_slf_vtable(slf_deconstructor_find_entities_in_radius_entity_list_vector3d_num_num, slf_action_find_entities_in_radius_entity_list_vector3d_num_num, (void*)0x89b46c);
	hook_slf_vtable(slf_deconstructor_find_entity_str, slf_action_find_entity_str, (void*)0x89af1c);
	hook_slf_vtable(slf_deconstructor_find_innermost_district_vector3d, slf_action_find_innermost_district_vector3d, (void*)0x89a824);
	hook_slf_vtable(slf_deconstructor_find_outermost_district_vector3d, slf_action_find_outermost_district_vector3d, (void*)0x89a82c);
	hook_slf_vtable(slf_deconstructor_find_trigger_entity, slf_action_find_trigger_entity, (void*)0x89b950);
	hook_slf_vtable(slf_deconstructor_find_trigger_str, slf_action_find_trigger_str, (void*)0x89b948);
	hook_slf_vtable(slf_deconstructor_find_trigger_in_district_district_str, slf_action_find_trigger_in_district_district_str, (void*)0x89b958);
	hook_slf_vtable(slf_deconstructor_float_random_num, slf_action_float_random_num, (void*)0x89a74c);
	hook_slf_vtable(slf_deconstructor_force_mission_district_str_num, slf_action_force_mission_district_str_num, (void*)0x89c3b4);
	hook_slf_vtable(slf_deconstructor_force_streamer_refresh, slf_action_force_streamer_refresh, (void*)0x89c4ac);
	hook_slf_vtable(slf_deconstructor_format_time_string_num, slf_action_format_time_string_num, (void*)0x89bc74);
	hook_slf_vtable(slf_deconstructor_freeze_hero_num, slf_action_freeze_hero_num, (void*)0x89a5fc);
	hook_slf_vtable(slf_deconstructor_game_ini_get_flag_str, slf_action_game_ini_get_flag_str, (void*)0x89a97c);
	hook_slf_vtable(slf_deconstructor_game_time_advance_num_num, slf_action_game_time_advance_num_num, (void*)0x89c4dc);
	hook_slf_vtable(slf_deconstructor_get_all_execs_thread_count_str, slf_action_get_all_execs_thread_count_str, (void*)0x89a9ec);
	hook_slf_vtable(slf_deconstructor_get_all_instances_thread_count_str, slf_action_get_all_instances_thread_count_str, (void*)0x89a9e4);
	hook_slf_vtable(slf_deconstructor_get_attacker_entity, slf_action_get_attacker_entity, (void*)0x89aa24);
	hook_slf_vtable(slf_deconstructor_get_attacker_member, slf_action_get_attacker_member, (void*)0x89aa2c);
	hook_slf_vtable(slf_deconstructor_get_available_stack_size, slf_action_get_available_stack_size, (void*)0x89c4ec);
	hook_slf_vtable(slf_deconstructor_get_character_packname_list, slf_action_get_character_packname_list, (void*)0x89c354);
	hook_slf_vtable(slf_deconstructor_get_closest_point_on_lane_with_facing_num_vector3d_vector3d_list, slf_action_get_closest_point_on_lane_with_facing_num_vector3d_vector3d_list, (void*)0x89c61c);
	hook_slf_vtable(slf_deconstructor_get_col_hit_ent, slf_action_get_col_hit_ent, (void*)0x89a884);
	hook_slf_vtable(slf_deconstructor_get_col_hit_norm, slf_action_get_col_hit_norm, (void*)0x89a87c);
	hook_slf_vtable(slf_deconstructor_get_col_hit_pos, slf_action_get_col_hit_pos, (void*)0x89a874);
	hook_slf_vtable(slf_deconstructor_get_control_state_num, slf_action_get_control_state_num, (void*)0x89a7f4);
	hook_slf_vtable(slf_deconstructor_get_control_trigger_num, slf_action_get_control_trigger_num, (void*)0x89a7ec);
	hook_slf_vtable(slf_deconstructor_get_current_instance_thread_count_str, slf_action_get_current_instance_thread_count_str, (void*)0x89a9dc);
	hook_slf_vtable(slf_deconstructor_get_current_view_cam_pos, slf_action_get_current_view_cam_pos, (void*)0x89a5b4);
	hook_slf_vtable(slf_deconstructor_get_current_view_cam_x_facing, slf_action_get_current_view_cam_x_facing, (void*)0x89a59c);
	hook_slf_vtable(slf_deconstructor_get_current_view_cam_y_facing, slf_action_get_current_view_cam_y_facing, (void*)0x89a5a4);
	hook_slf_vtable(slf_deconstructor_get_current_view_cam_z_facing, slf_action_get_current_view_cam_z_facing, (void*)0x89a5ac);
	hook_slf_vtable(slf_deconstructor_get_fog_color, slf_action_get_fog_color, (void*)0x89a8f4);
	hook_slf_vtable(slf_deconstructor_get_fog_distance, slf_action_get_fog_distance, (void*)0x89a8fc);
	hook_slf_vtable(slf_deconstructor_get_game_info_num_str, slf_action_get_game_info_num_str, (void*)0x89a8c4);
	hook_slf_vtable(slf_deconstructor_get_game_info_str_str, slf_action_get_game_info_str_str, (void*)0x89a8d4);
	hook_slf_vtable(slf_deconstructor_get_glam_cam_num, slf_action_get_glam_cam_num, (void*)0x89b780);
	hook_slf_vtable(slf_deconstructor_get_global_time_dilation, slf_action_get_global_time_dilation, (void*)0x89a894);
	hook_slf_vtable(slf_deconstructor_get_ini_flag_str, slf_action_get_ini_flag_str, (void*)0x89a94c);
	hook_slf_vtable(slf_deconstructor_get_ini_num_str, slf_action_get_ini_num_str, (void*)0x89a954);
	hook_slf_vtable(slf_deconstructor_get_int_num_num, slf_action_get_int_num_num, (void*)0x89a92c);
	hook_slf_vtable(slf_deconstructor_get_mission_camera_marker_num, slf_action_get_mission_camera_marker_num, (void*)0x89c414);
	hook_slf_vtable(slf_deconstructor_get_mission_camera_transform_marker_num, slf_action_get_mission_camera_transform_marker_num, (void*)0x89c454);
	hook_slf_vtable(slf_deconstructor_get_mission_entity, slf_action_get_mission_entity, (void*)0x89c38c);
	hook_slf_vtable(slf_deconstructor_get_mission_key_posfacing3d, slf_action_get_mission_key_posfacing3d, (void*)0x89c36c);
	hook_slf_vtable(slf_deconstructor_get_mission_key_position, slf_action_get_mission_key_position, (void*)0x89c364);
	hook_slf_vtable(slf_deconstructor_get_mission_marker_num, slf_action_get_mission_marker_num, (void*)0x89c40c);
	hook_slf_vtable(slf_deconstructor_get_mission_nums, slf_action_get_mission_nums, (void*)0x89c3ac);
	hook_slf_vtable(slf_deconstructor_get_mission_patrol_waypoint, slf_action_get_mission_patrol_waypoint, (void*)0x89c384);
	hook_slf_vtable(slf_deconstructor_get_mission_positions, slf_action_get_mission_positions, (void*)0x89c39c);
	hook_slf_vtable(slf_deconstructor_get_mission_strings, slf_action_get_mission_strings, (void*)0x89c3a4);
	hook_slf_vtable(slf_deconstructor_get_mission_transform_marker_num, slf_action_get_mission_transform_marker_num, (void*)0x89c42c);
	hook_slf_vtable(slf_deconstructor_get_mission_trigger, slf_action_get_mission_trigger, (void*)0x89c394);
	hook_slf_vtable(slf_deconstructor_get_missions_key_position_by_index_district_str_num, slf_action_get_missions_key_position_by_index_district_str_num, (void*)0x89c3bc);
	hook_slf_vtable(slf_deconstructor_get_missions_nums_by_index_district_str_num_num_list, slf_action_get_missions_nums_by_index_district_str_num_num_list, (void*)0x89c3cc);
	hook_slf_vtable(slf_deconstructor_get_missions_patrol_waypoint_by_index_district_str_num, slf_action_get_missions_patrol_waypoint_by_index_district_str_num, (void*)0x89c3c4);
	hook_slf_vtable(slf_deconstructor_get_neighborhood_name_num, slf_action_get_neighborhood_name_num, (void*)0x89c54c);
	hook_slf_vtable(slf_deconstructor_get_num_free_slots_str, slf_action_get_num_free_slots_str, (void*)0x89c3e4);
	hook_slf_vtable(slf_deconstructor_get_num_mission_transform_marker, slf_action_get_num_mission_transform_marker, (void*)0x89c434);
	hook_slf_vtable(slf_deconstructor_get_pack_group_str, slf_action_get_pack_group_str, (void*)0x89c3ec);
	hook_slf_vtable(slf_deconstructor_get_pack_size_str, slf_action_get_pack_size_str, (void*)0x89c4e4);
	hook_slf_vtable(slf_deconstructor_get_patrol_difficulty_str, slf_action_get_patrol_difficulty_str, (void*)0x89c534);
	hook_slf_vtable(slf_deconstructor_get_patrol_node_position_by_index_str_num, slf_action_get_patrol_node_position_by_index_str_num, (void*)0x89c52c);
	hook_slf_vtable(slf_deconstructor_get_patrol_start_position_str, slf_action_get_patrol_start_position_str, (void*)0x89c524);
	hook_slf_vtable(slf_deconstructor_get_patrol_unlock_threshold_str, slf_action_get_patrol_unlock_threshold_str, (void*)0x89c53c);
	hook_slf_vtable(slf_deconstructor_get_platform, slf_action_get_platform, (void*)0x89a508);
	hook_slf_vtable(slf_deconstructor_get_render_opt_num_str, slf_action_get_render_opt_num_str, (void*)0x89a8e4);
	hook_slf_vtable(slf_deconstructor_get_spider_reflexes_spiderman_time_dilation, slf_action_get_spider_reflexes_spiderman_time_dilation, (void*)0x89cac4);
	hook_slf_vtable(slf_deconstructor_get_spider_reflexes_world_time_dilation, slf_action_get_spider_reflexes_world_time_dilation, (void*)0x89cad4);
	hook_slf_vtable(slf_deconstructor_get_time_inc, slf_action_get_time_inc, (void*)0x89a7a4);
	hook_slf_vtable(slf_deconstructor_get_time_of_day, slf_action_get_time_of_day, (void*)0x89a974);
	hook_slf_vtable(slf_deconstructor_get_time_of_day_rate, slf_action_get_time_of_day_rate, (void*)0x89a96c);
	hook_slf_vtable(slf_deconstructor_get_token_index_from_id_num_num, slf_action_get_token_index_from_id_num_num, (void*)0x89b554);
	hook_slf_vtable(slf_deconstructor_get_traffic_spawn_point_near_camera_vector3d_list, slf_action_get_traffic_spawn_point_near_camera_vector3d_list, (void*)0x89aa98);
	hook_slf_vtable(slf_deconstructor_greater_than_or_equal_rounded_num_num, slf_action_greater_than_or_equal_rounded_num_num, (void*)0x89bc90);
	hook_slf_vtable(slf_deconstructor_hard_break, slf_action_hard_break, (void*)0x89aa1c);
	hook_slf_vtable(slf_deconstructor_has_substring_str_str, slf_action_has_substring_str_str, (void*)0x89a580);
	hook_slf_vtable(slf_deconstructor_hero, slf_action_hero, (void*)0x89aed4);
	hook_slf_vtable(slf_deconstructor_hero_exists, slf_action_hero_exists, (void*)0x89aedc);
	hook_slf_vtable(slf_deconstructor_hero_type, slf_action_hero_type, (void*)0x89aee4);
	hook_slf_vtable(slf_deconstructor_hide_controller_gauge, slf_action_hide_controller_gauge, (void*)0x89bb20);
	hook_slf_vtable(slf_deconstructor_initialize_encounter_object, slf_action_initialize_encounter_object, (void*)0x89aa0c);
	hook_slf_vtable(slf_deconstructor_initialize_encounter_objects, slf_action_initialize_encounter_objects, (void*)0x89aa04);
	hook_slf_vtable(slf_deconstructor_insert_pack_str, slf_action_insert_pack_str, (void*)0x89c3d4);
	hook_slf_vtable(slf_deconstructor_invoke_pause_menu_unlockables, slf_action_invoke_pause_menu_unlockables, (void*)0x89bc98);
	hook_slf_vtable(slf_deconstructor_is_ai_enabled, slf_action_is_ai_enabled, (void*)0x89a6d4);
	hook_slf_vtable(slf_deconstructor_is_cut_scene_playing, slf_action_is_cut_scene_playing, (void*)0x89b7d0);
	hook_slf_vtable(slf_deconstructor_is_district_loaded_num, slf_action_is_district_loaded_num, (void*)0x89c49c);
	hook_slf_vtable(slf_deconstructor_is_hero_frozen, slf_action_is_hero_frozen, (void*)0x89a60c);
	hook_slf_vtable(slf_deconstructor_is_hero_peter_parker, slf_action_is_hero_peter_parker, (void*)0x89aefc);
	hook_slf_vtable(slf_deconstructor_is_hero_spidey, slf_action_is_hero_spidey, (void*)0x89aeec);
	hook_slf_vtable(slf_deconstructor_is_hero_venom, slf_action_is_hero_venom, (void*)0x89aef4);
	hook_slf_vtable(slf_deconstructor_is_marky_cam_enabled, slf_action_is_marky_cam_enabled, (void*)0x89a5c4);
	hook_slf_vtable(slf_deconstructor_is_mission_active, slf_action_is_mission_active, (void*)0x89c50c);
	hook_slf_vtable(slf_deconstructor_is_mission_loading, slf_action_is_mission_loading, (void*)0x89c514);
	hook_slf_vtable(slf_deconstructor_is_pack_available_str, slf_action_is_pack_available_str, (void*)0x89c404);
	hook_slf_vtable(slf_deconstructor_is_pack_loaded_str, slf_action_is_pack_loaded_str, (void*)0x89c3fc);
	hook_slf_vtable(slf_deconstructor_is_pack_pushed_str, slf_action_is_pack_pushed_str, (void*)0x89c34c);
	hook_slf_vtable(slf_deconstructor_is_path_graph_inside_glass_house_str, slf_action_is_path_graph_inside_glass_house_str, (void*)0x89aaa0);
	hook_slf_vtable(slf_deconstructor_is_patrol_active, slf_action_is_patrol_active, (void*)0x89c504);
	hook_slf_vtable(slf_deconstructor_is_patrol_node_empty_num, slf_action_is_patrol_node_empty_num, (void*)0x89c544);
	hook_slf_vtable(slf_deconstructor_is_paused, slf_action_is_paused, (void*)0x89a6b4);
	hook_slf_vtable(slf_deconstructor_is_physics_enabled, slf_action_is_physics_enabled, (void*)0x89a6e4);
	hook_slf_vtable(slf_deconstructor_is_point_inside_glass_house_vector3d, slf_action_is_point_inside_glass_house_vector3d, (void*)0x89a540);
	hook_slf_vtable(slf_deconstructor_is_point_under_water_vector3d, slf_action_is_point_under_water_vector3d, (void*)0x89aa3c);
	hook_slf_vtable(slf_deconstructor_is_user_camera_enabled, slf_action_is_user_camera_enabled, (void*)0x89a5cc);
	hook_slf_vtable(slf_deconstructor_load_anim_str, slf_action_load_anim_str, (void*)0x89aaf0);
	hook_slf_vtable(slf_deconstructor_load_level_str_vector3d, slf_action_load_level_str_vector3d, (void*)0x89a8ac);
	hook_slf_vtable(slf_deconstructor_lock_all_districts, slf_action_lock_all_districts, (void*)0x89c4c4);
	hook_slf_vtable(slf_deconstructor_lock_district_num, slf_action_lock_district_num, (void*)0x89c494);
	hook_slf_vtable(slf_deconstructor_lock_mission_manager_num, slf_action_lock_mission_manager_num, (void*)0x89c51c);
	hook_slf_vtable(slf_deconstructor_los_check_vector3d_vector3d, slf_action_los_check_vector3d_vector3d, (void*)0x89a814);
	hook_slf_vtable(slf_deconstructor_lower_hotpursuit_indicator_level, slf_action_lower_hotpursuit_indicator_level, (void*)0x89bae0);
	hook_slf_vtable(slf_deconstructor_malor_vector3d_num, slf_action_malor_vector3d_num, (void*)0x89a984);
	hook_slf_vtable(slf_deconstructor_normal_vector3d, slf_action_normal_vector3d, (void*)0x89ba40);
	hook_slf_vtable(slf_deconstructor_pause_game_num, slf_action_pause_game_num, (void*)0x89a6bc);
	hook_slf_vtable(slf_deconstructor_play_credits, slf_action_play_credits, (void*)0x89baf8);
	hook_slf_vtable(slf_deconstructor_play_prerender_str, slf_action_play_prerender_str, (void*)0x89a8b4);
	hook_slf_vtable(slf_deconstructor_pop_pack_str, slf_action_pop_pack_str, (void*)0x89c344);
	hook_slf_vtable(slf_deconstructor_post_message_str_num, slf_action_post_message_str_num, (void*)0x89a73c);
	hook_slf_vtable(slf_deconstructor_pre_roll_all_pfx_num, slf_action_pre_roll_all_pfx_num, (void*)0x89aa34);
	hook_slf_vtable(slf_deconstructor_press_controller_gauge_num, slf_action_press_controller_gauge_num, (void*)0x89bb28);
	hook_slf_vtable(slf_deconstructor_press_controller_gauge_num_num_num, slf_action_press_controller_gauge_num_num_num, (void*)0x89bb30);
	hook_slf_vtable(slf_deconstructor_purge_district_num, slf_action_purge_district_num, (void*)0x89c4bc);
	hook_slf_vtable(slf_deconstructor_push_pack_str, slf_action_push_pack_str, (void*)0x89c334);
	hook_slf_vtable(slf_deconstructor_push_pack_into_district_slot_str, slf_action_push_pack_into_district_slot_str, (void*)0x89c33c);
	hook_slf_vtable(slf_deconstructor_raise_hotpursuit_indicator_level, slf_action_raise_hotpursuit_indicator_level, (void*)0x89bad8);
	hook_slf_vtable(slf_deconstructor_random_num, slf_action_random_num, (void*)0x89a744);
	hook_slf_vtable(slf_deconstructor_remove_civilian_info_num, slf_action_remove_civilian_info_num, (void*)0x89c5c4);
	hook_slf_vtable(slf_deconstructor_remove_civilian_info_entity_entity_num, slf_action_remove_civilian_info_entity_entity_num, (void*)0x89c5d4);
	hook_slf_vtable(slf_deconstructor_remove_glass_house_str, slf_action_remove_glass_house_str, (void*)0x89a568);
	hook_slf_vtable(slf_deconstructor_remove_item_entity_from_world_entity, slf_action_remove_item_entity_from_world_entity, (void*)0x89affc);
	hook_slf_vtable(slf_deconstructor_remove_pack_str, slf_action_remove_pack_str, (void*)0x89c3dc);
	hook_slf_vtable(slf_deconstructor_remove_traffic_model_num, slf_action_remove_traffic_model_num, (void*)0x89c5ac);
	hook_slf_vtable(slf_deconstructor_reset_externed_alses, slf_action_reset_externed_alses, (void*)0x89b064);
	hook_slf_vtable(slf_deconstructor_set_all_anchors_activated_num, slf_action_set_all_anchors_activated_num, (void*)0x89caec);
	hook_slf_vtable(slf_deconstructor_set_blur_num, slf_action_set_blur_num, (void*)0x89a63c);
	hook_slf_vtable(slf_deconstructor_set_blur_blend_mode_num, slf_action_set_blur_blend_mode_num, (void*)0x89a664);
	hook_slf_vtable(slf_deconstructor_set_blur_color_vector3d, slf_action_set_blur_color_vector3d, (void*)0x89a644);
	hook_slf_vtable(slf_deconstructor_set_blur_offset_num_num, slf_action_set_blur_offset_num_num, (void*)0x89a654);
	hook_slf_vtable(slf_deconstructor_set_blur_rot_num, slf_action_set_blur_rot_num, (void*)0x89a65c);
	hook_slf_vtable(slf_deconstructor_set_blur_scale_num_num, slf_action_set_blur_scale_num_num, (void*)0x89a64c);
	hook_slf_vtable(slf_deconstructor_set_clear_color_vector3d, slf_action_set_clear_color_vector3d, (void*)0x89a6f4);
	hook_slf_vtable(slf_deconstructor_set_current_mission_objective_caption_num, slf_action_set_current_mission_objective_caption_num, (void*)0x89cadc);
	hook_slf_vtable(slf_deconstructor_set_default_traffic_hitpoints_num, slf_action_set_default_traffic_hitpoints_num, (void*)0x89c614);
	hook_slf_vtable(slf_deconstructor_set_dialog_box_flavor_num, slf_action_set_dialog_box_flavor_num, (void*)0x89bc5c);
	hook_slf_vtable(slf_deconstructor_set_dialog_box_lockout_time_num, slf_action_set_dialog_box_lockout_time_num, (void*)0x89bc6c);
	hook_slf_vtable(slf_deconstructor_set_engine_property_str_num, slf_action_set_engine_property_str_num, (void*)0x89a99c);
	hook_slf_vtable(slf_deconstructor_set_fov_num, slf_action_set_fov_num, (void*)0x89a62c);
	hook_slf_vtable(slf_deconstructor_set_game_info_num_str_num, slf_action_set_game_info_num_str_num, (void*)0x89a8bc);
	hook_slf_vtable(slf_deconstructor_set_game_info_str_str_str, slf_action_set_game_info_str_str_str, (void*)0x89a8cc);
	hook_slf_vtable(slf_deconstructor_set_global_time_dilation_num, slf_action_set_global_time_dilation_num, (void*)0x89a89c);
	hook_slf_vtable(slf_deconstructor_set_marky_cam_lookat_vector3d, slf_action_set_marky_cam_lookat_vector3d, (void*)0x89a5ec);
	hook_slf_vtable(slf_deconstructor_set_max_streaming_distance_num, slf_action_set_max_streaming_distance_num, (void*)0x89c4b4);
	hook_slf_vtable(slf_deconstructor_set_mission_key_pos_facing_vector3d_vector3d, slf_action_set_mission_key_pos_facing_vector3d_vector3d, (void*)0x89c37c);
	hook_slf_vtable(slf_deconstructor_set_mission_key_position_vector3d, slf_action_set_mission_key_position_vector3d, (void*)0x89c374);
	hook_slf_vtable(slf_deconstructor_set_mission_text_num_elip, slf_action_set_mission_text_num_elip, (void*)0x89bbf8);
	hook_slf_vtable(slf_deconstructor_set_mission_text_box_flavor_num, slf_action_set_mission_text_box_flavor_num, (void*)0x89bc64);
	hook_slf_vtable(slf_deconstructor_set_mission_text_debug_str, slf_action_set_mission_text_debug_str, (void*)0x89bc00);
	hook_slf_vtable(slf_deconstructor_set_parking_density_num, slf_action_set_parking_density_num, (void*)0x89c60c);
	hook_slf_vtable(slf_deconstructor_set_pedestrian_density_num, slf_action_set_pedestrian_density_num, (void*)0x89c5f4);
	hook_slf_vtable(slf_deconstructor_set_render_opt_num_str_num, slf_action_set_render_opt_num_str_num, (void*)0x89a8dc);
	hook_slf_vtable(slf_deconstructor_set_score_widget_score_num, slf_action_set_score_widget_score_num, (void*)0x89bac8);
	hook_slf_vtable(slf_deconstructor_set_sound_category_volume_num_num_num, slf_action_set_sound_category_volume_num_num_num, (void*)0x89a9d4);
	hook_slf_vtable(slf_deconstructor_set_spider_reflexes_blur_num, slf_action_set_spider_reflexes_blur_num, (void*)0x89a674);
	hook_slf_vtable(slf_deconstructor_set_spider_reflexes_blur_blend_mode_num, slf_action_set_spider_reflexes_blur_blend_mode_num, (void*)0x89a69c);
	hook_slf_vtable(slf_deconstructor_set_spider_reflexes_blur_color_vector3d, slf_action_set_spider_reflexes_blur_color_vector3d, (void*)0x89a67c);
	hook_slf_vtable(slf_deconstructor_set_spider_reflexes_blur_offset_num_num, slf_action_set_spider_reflexes_blur_offset_num_num, (void*)0x89a68c);
	hook_slf_vtable(slf_deconstructor_set_spider_reflexes_blur_rot_num, slf_action_set_spider_reflexes_blur_rot_num, (void*)0x89a694);
	hook_slf_vtable(slf_deconstructor_set_spider_reflexes_blur_scale_num_num, slf_action_set_spider_reflexes_blur_scale_num_num, (void*)0x89a684);
	hook_slf_vtable(slf_deconstructor_set_spider_reflexes_hero_meter_depletion_rate_num, slf_action_set_spider_reflexes_hero_meter_depletion_rate_num, (void*)0x89cab4);
	hook_slf_vtable(slf_deconstructor_set_spider_reflexes_spiderman_time_dilation_num, slf_action_set_spider_reflexes_spiderman_time_dilation_num, (void*)0x89cabc);
	hook_slf_vtable(slf_deconstructor_set_spider_reflexes_world_time_dilation_num, slf_action_set_spider_reflexes_world_time_dilation_num, (void*)0x89cacc);
	hook_slf_vtable(slf_deconstructor_set_state_of_the_story_caption_num, slf_action_set_state_of_the_story_caption_num, (void*)0x89cae4);
	hook_slf_vtable(slf_deconstructor_set_target_info_entity_vector3d_vector3d, slf_action_set_target_info_entity_vector3d_vector3d, (void*)0x89b6c4);
	hook_slf_vtable(slf_deconstructor_set_time_of_day_num, slf_action_set_time_of_day_num, (void*)0x89a964);
	hook_slf_vtable(slf_deconstructor_set_traffic_density_num, slf_action_set_traffic_density_num, (void*)0x89c604);
	hook_slf_vtable(slf_deconstructor_set_traffic_model_usage_num_num, slf_action_set_traffic_model_usage_num_num, (void*)0x89c5b4);
	hook_slf_vtable(slf_deconstructor_set_vibration_resume_num, slf_action_set_vibration_resume_num, (void*)0x89a7d4);
	hook_slf_vtable(slf_deconstructor_set_whoosh_interp_rate_num, slf_action_set_whoosh_interp_rate_num, (void*)0x89b504);
	hook_slf_vtable(slf_deconstructor_set_whoosh_pitch_range_num_num, slf_action_set_whoosh_pitch_range_num_num, (void*)0x89b4fc);
	hook_slf_vtable(slf_deconstructor_set_whoosh_speed_range_num_num, slf_action_set_whoosh_speed_range_num_num, (void*)0x89b4ec);
	hook_slf_vtable(slf_deconstructor_set_whoosh_volume_range_num_num, slf_action_set_whoosh_volume_range_num_num, (void*)0x89b4f4);
	hook_slf_vtable(slf_deconstructor_set_zoom_num, slf_action_set_zoom_num, (void*)0x89a624);
	hook_slf_vtable(slf_deconstructor_show_controller_gauge, slf_action_show_controller_gauge, (void*)0x89bb18);
	hook_slf_vtable(slf_deconstructor_show_hotpursuit_indicator_num, slf_action_show_hotpursuit_indicator_num, (void*)0x89bad0);
	hook_slf_vtable(slf_deconstructor_show_score_widget_num, slf_action_show_score_widget_num, (void*)0x89bac0);
	hook_slf_vtable(slf_deconstructor_shut_up_all_ai_voice_boxes, slf_action_shut_up_all_ai_voice_boxes, (void*)0x89b50c);
	hook_slf_vtable(slf_deconstructor_sin_num, slf_action_sin_num, (void*)0x89a904);
	hook_slf_vtable(slf_deconstructor_sin_cos_num, slf_action_sin_cos_num, (void*)0x89a924);
	hook_slf_vtable(slf_deconstructor_soft_load_num, slf_action_soft_load_num, (void*)0x89bcbc);
	hook_slf_vtable(slf_deconstructor_soft_save_num, slf_action_soft_save_num, (void*)0x89bcb4);
	hook_slf_vtable(slf_deconstructor_spiderman_add_hero_points_num, slf_action_spiderman_add_hero_points_num, (void*)0x89caa4);
	hook_slf_vtable(slf_deconstructor_spiderman_bank_stylepoints, slf_action_spiderman_bank_stylepoints, (void*)0x89c91c);
	hook_slf_vtable(slf_deconstructor_spiderman_break_web, slf_action_spiderman_break_web, (void*)0x89c9e4);
	hook_slf_vtable(slf_deconstructor_spiderman_camera_add_shake_num_num_num, slf_action_spiderman_camera_add_shake_num_num_num, (void*)0x89cb34);
	hook_slf_vtable(slf_deconstructor_spiderman_camera_autocorrect_num, slf_action_spiderman_camera_autocorrect_num, (void*)0x89c924);
	hook_slf_vtable(slf_deconstructor_spiderman_camera_clear_fixedstatic, slf_action_spiderman_camera_clear_fixedstatic, (void*)0x89cafc);
	hook_slf_vtable(slf_deconstructor_spiderman_camera_enable_combat_num, slf_action_spiderman_camera_enable_combat_num, (void*)0x89cb24);
	hook_slf_vtable(slf_deconstructor_spiderman_camera_enable_lookaround_num, slf_action_spiderman_camera_enable_lookaround_num, (void*)0x89cb1c);
	hook_slf_vtable(slf_deconstructor_spiderman_camera_set_fixedstatic_vector3d_vector3d, slf_action_spiderman_camera_set_fixedstatic_vector3d_vector3d, (void*)0x89caf4);
	hook_slf_vtable(slf_deconstructor_spiderman_camera_set_follow_entity, slf_action_spiderman_camera_set_follow_entity, (void*)0x89cb2c);
	hook_slf_vtable(slf_deconstructor_spiderman_camera_set_hero_underwater_num, slf_action_spiderman_camera_set_hero_underwater_num, (void*)0x89cb3c);
	hook_slf_vtable(slf_deconstructor_spiderman_camera_set_interpolation_time_num, slf_action_spiderman_camera_set_interpolation_time_num, (void*)0x89cb14);
	hook_slf_vtable(slf_deconstructor_spiderman_camera_set_lockon_min_distance_num, slf_action_spiderman_camera_set_lockon_min_distance_num, (void*)0x89cb04);
	hook_slf_vtable(slf_deconstructor_spiderman_camera_set_lockon_y_offset_num, slf_action_spiderman_camera_set_lockon_y_offset_num, (void*)0x89cb0c);
	hook_slf_vtable(slf_deconstructor_spiderman_charged_jump, slf_action_spiderman_charged_jump, (void*)0x89c98c);
	hook_slf_vtable(slf_deconstructor_spiderman_enable_control_button_num_num, slf_action_spiderman_enable_control_button_num_num, (void*)0x89ca94);
	hook_slf_vtable(slf_deconstructor_spiderman_enable_lockon_num, slf_action_spiderman_enable_lockon_num, (void*)0x89c9ac);
	hook_slf_vtable(slf_deconstructor_spiderman_engage_lockon_num, slf_action_spiderman_engage_lockon_num, (void*)0x89c9b4);
	hook_slf_vtable(slf_deconstructor_spiderman_engage_lockon_num_entity, slf_action_spiderman_engage_lockon_num_entity, (void*)0x89c9bc);
	hook_slf_vtable(slf_deconstructor_spiderman_get_hero_points, slf_action_spiderman_get_hero_points, (void*)0x89ca9c);
	hook_slf_vtable(slf_deconstructor_spiderman_get_max_zip_length, slf_action_spiderman_get_max_zip_length, (void*)0x89c9dc);
	hook_slf_vtable(slf_deconstructor_spiderman_get_spidey_sense_level, slf_action_spiderman_get_spidey_sense_level, (void*)0x89c99c);
	hook_slf_vtable(slf_deconstructor_spiderman_is_crawling, slf_action_spiderman_is_crawling, (void*)0x89c934);
	hook_slf_vtable(slf_deconstructor_spiderman_is_falling, slf_action_spiderman_is_falling, (void*)0x89c964);
	hook_slf_vtable(slf_deconstructor_spiderman_is_jumping, slf_action_spiderman_is_jumping, (void*)0x89c96c);
	hook_slf_vtable(slf_deconstructor_spiderman_is_on_ceiling, slf_action_spiderman_is_on_ceiling, (void*)0x89c944);
	hook_slf_vtable(slf_deconstructor_spiderman_is_on_ground, slf_action_spiderman_is_on_ground, (void*)0x89c94c);
	hook_slf_vtable(slf_deconstructor_spiderman_is_on_wall, slf_action_spiderman_is_on_wall, (void*)0x89c93c);
	hook_slf_vtable(slf_deconstructor_spiderman_is_running, slf_action_spiderman_is_running, (void*)0x89c95c);
	hook_slf_vtable(slf_deconstructor_spiderman_is_sprint_crawling, slf_action_spiderman_is_sprint_crawling, (void*)0x89c984);
	hook_slf_vtable(slf_deconstructor_spiderman_is_sprinting, slf_action_spiderman_is_sprinting, (void*)0x89c974);
	hook_slf_vtable(slf_deconstructor_spiderman_is_swinging, slf_action_spiderman_is_swinging, (void*)0x89c954);
	hook_slf_vtable(slf_deconstructor_spiderman_is_wallsprinting, slf_action_spiderman_is_wallsprinting, (void*)0x89c97c);
	hook_slf_vtable(slf_deconstructor_spiderman_lock_spider_reflexes_off, slf_action_spiderman_lock_spider_reflexes_off, (void*)0x89ca24);
	hook_slf_vtable(slf_deconstructor_spiderman_lock_spider_reflexes_on, slf_action_spiderman_lock_spider_reflexes_on, (void*)0x89ca1c);
	hook_slf_vtable(slf_deconstructor_spiderman_lockon_camera_engaged, slf_action_spiderman_lockon_camera_engaged, (void*)0x89ca0c);
	hook_slf_vtable(slf_deconstructor_spiderman_lockon_mode_engaged, slf_action_spiderman_lockon_mode_engaged, (void*)0x89ca04);
	hook_slf_vtable(slf_deconstructor_spiderman_set_camera_target_entity, slf_action_spiderman_set_camera_target_entity, (void*)0x89ca14);
	hook_slf_vtable(slf_deconstructor_spiderman_set_desired_mode_num_vector3d_vector3d, slf_action_spiderman_set_desired_mode_num_vector3d_vector3d, (void*)0x89c9ec);
	hook_slf_vtable(slf_deconstructor_spiderman_set_health_beep_min_max_cooldown_time_num_num, slf_action_spiderman_set_health_beep_min_max_cooldown_time_num_num, (void*)0x89c9f4);
	hook_slf_vtable(slf_deconstructor_spiderman_set_health_beep_threshold_num, slf_action_spiderman_set_health_beep_threshold_num, (void*)0x89c9fc);
	hook_slf_vtable(slf_deconstructor_spiderman_set_hero_meter_empty_rate_num, slf_action_spiderman_set_hero_meter_empty_rate_num, (void*)0x89cb44);
	hook_slf_vtable(slf_deconstructor_spiderman_set_max_height_num, slf_action_spiderman_set_max_height_num, (void*)0x89c9cc);
	hook_slf_vtable(slf_deconstructor_spiderman_set_max_zip_length_num, slf_action_spiderman_set_max_zip_length_num, (void*)0x89c9d4);
	hook_slf_vtable(slf_deconstructor_spiderman_set_min_height_num, slf_action_spiderman_set_min_height_num, (void*)0x89c9c4);
	hook_slf_vtable(slf_deconstructor_spiderman_set_spidey_sense_level_num, slf_action_spiderman_set_spidey_sense_level_num, (void*)0x89c994);
	hook_slf_vtable(slf_deconstructor_spiderman_set_swing_anchor_max_sticky_time_num, slf_action_spiderman_set_swing_anchor_max_sticky_time_num, (void*)0x89c9a4);
	hook_slf_vtable(slf_deconstructor_spiderman_subtract_hero_points_num, slf_action_spiderman_subtract_hero_points_num, (void*)0x89caac);
	hook_slf_vtable(slf_deconstructor_spiderman_td_set_alternating_wall_run_occurrence_threshold_num, slf_action_spiderman_td_set_alternating_wall_run_occurrence_threshold_num, (void*)0x89ca74);
	hook_slf_vtable(slf_deconstructor_spiderman_td_set_alternating_wall_run_time_threshold_num, slf_action_spiderman_td_set_alternating_wall_run_time_threshold_num, (void*)0x89ca6c);
	hook_slf_vtable(slf_deconstructor_spiderman_td_set_big_air_height_threshold_num, slf_action_spiderman_td_set_big_air_height_threshold_num, (void*)0x89ca34);
	hook_slf_vtable(slf_deconstructor_spiderman_td_set_continuous_air_swings_threshold_num, slf_action_spiderman_td_set_continuous_air_swings_threshold_num, (void*)0x89ca4c);
	hook_slf_vtable(slf_deconstructor_spiderman_td_set_gain_altitude_height_threshold_num, slf_action_spiderman_td_set_gain_altitude_height_threshold_num, (void*)0x89ca54);
	hook_slf_vtable(slf_deconstructor_spiderman_td_set_near_miss_trigger_radius_num, slf_action_spiderman_td_set_near_miss_trigger_radius_num, (void*)0x89ca84);
	hook_slf_vtable(slf_deconstructor_spiderman_td_set_near_miss_velocity_threshold_num, slf_action_spiderman_td_set_near_miss_velocity_threshold_num, (void*)0x89ca8c);
	hook_slf_vtable(slf_deconstructor_spiderman_td_set_orbit_min_radius_threshold_num, slf_action_spiderman_td_set_orbit_min_radius_threshold_num, (void*)0x89ca3c);
	hook_slf_vtable(slf_deconstructor_spiderman_td_set_soft_landing_velocity_threshold_num, slf_action_spiderman_td_set_soft_landing_velocity_threshold_num, (void*)0x89ca5c);
	hook_slf_vtable(slf_deconstructor_spiderman_td_set_super_speed_speed_threshold_num, slf_action_spiderman_td_set_super_speed_speed_threshold_num, (void*)0x89ca7c);
	hook_slf_vtable(slf_deconstructor_spiderman_td_set_swinging_wall_run_time_threshold_num, slf_action_spiderman_td_set_swinging_wall_run_time_threshold_num, (void*)0x89ca64);
	hook_slf_vtable(slf_deconstructor_spiderman_td_set_wall_sprint_time_threshold_num, slf_action_spiderman_td_set_wall_sprint_time_threshold_num, (void*)0x89ca44);
	hook_slf_vtable(slf_deconstructor_spiderman_unlock_spider_reflexes, slf_action_spiderman_unlock_spider_reflexes, (void*)0x89ca2c);
	hook_slf_vtable(slf_deconstructor_spiderman_wait_add_threat_entity_str_num_num, slf_action_spiderman_wait_add_threat_entity_str_num_num, (void*)0x89cb4c);
	hook_slf_vtable(slf_deconstructor_spidey_can_see_vector3d, slf_action_spidey_can_see_vector3d, (void*)0x89c92c);
	hook_slf_vtable(slf_deconstructor_sqrt_num, slf_action_sqrt_num, (void*)0x89a914);
	hook_slf_vtable(slf_deconstructor_start_patrol_str, slf_action_start_patrol_str, (void*)0x89c4f4);
	hook_slf_vtable(slf_deconstructor_stop_all_sounds, slf_action_stop_all_sounds, (void*)0x89b514);
	hook_slf_vtable(slf_deconstructor_stop_credits, slf_action_stop_credits, (void*)0x89bb00);
	hook_slf_vtable(slf_deconstructor_stop_vibration, slf_action_stop_vibration, (void*)0x89a7cc);
	hook_slf_vtable(slf_deconstructor_subtitle_num_num_num_num_num_num, slf_action_subtitle_num_num_num_num_num_num, (void*)0x89bcc4);
	hook_slf_vtable(slf_deconstructor_swap_hero_costume_str, slf_action_swap_hero_costume_str, (void*)0x89c4d4);
	hook_slf_vtable(slf_deconstructor_text_width_str, slf_action_text_width_str, (void*)0x89a7ac);
	hook_slf_vtable(slf_deconstructor_timer_widget_get_count_up, slf_action_timer_widget_get_count_up, (void*)0x89bb70);
	hook_slf_vtable(slf_deconstructor_timer_widget_get_time, slf_action_timer_widget_get_time, (void*)0x89bb60);
	hook_slf_vtable(slf_deconstructor_timer_widget_set_count_up_num, slf_action_timer_widget_set_count_up_num, (void*)0x89bb68);
	hook_slf_vtable(slf_deconstructor_timer_widget_set_time_num, slf_action_timer_widget_set_time_num, (void*)0x89bb58);
	hook_slf_vtable(slf_deconstructor_timer_widget_start, slf_action_timer_widget_start, (void*)0x89bb48);
	hook_slf_vtable(slf_deconstructor_timer_widget_stop, slf_action_timer_widget_stop, (void*)0x89bb50);
	hook_slf_vtable(slf_deconstructor_timer_widget_turn_off, slf_action_timer_widget_turn_off, (void*)0x89bb40);
	hook_slf_vtable(slf_deconstructor_timer_widget_turn_on, slf_action_timer_widget_turn_on, (void*)0x89bb38);
	hook_slf_vtable(slf_deconstructor_to_beam_entity, slf_action_to_beam_entity, (void*)0x89abbc);
	hook_slf_vtable(slf_deconstructor_to_gun_entity, slf_action_to_gun_entity, (void*)0x89b5a0);
	hook_slf_vtable(slf_deconstructor_to_item_entity, slf_action_to_item_entity, (void*)0x89b6bc);
	hook_slf_vtable(slf_deconstructor_to_polytube_entity, slf_action_to_polytube_entity, (void*)0x89c238);
	hook_slf_vtable(slf_deconstructor_to_switch_entity, slf_action_to_switch_entity, (void*)0x89b8e4);
	hook_slf_vtable(slf_deconstructor_trace_str, slf_action_trace_str, (void*)0x89aa14);
	hook_slf_vtable(slf_deconstructor_trigger_is_valid_trigger, slf_action_trigger_is_valid_trigger, (void*)0x89b9b8);
	hook_slf_vtable(slf_deconstructor_turn_off_boss_health, slf_action_turn_off_boss_health, (void*)0x89bbd0);
	hook_slf_vtable(slf_deconstructor_turn_off_hero_health, slf_action_turn_off_hero_health, (void*)0x89bbd8);
	hook_slf_vtable(slf_deconstructor_turn_off_mission_text, slf_action_turn_off_mission_text, (void*)0x89bc20);
	hook_slf_vtable(slf_deconstructor_turn_off_third_party_health, slf_action_turn_off_third_party_health, (void*)0x89bbe0);
	hook_slf_vtable(slf_deconstructor_turn_on_boss_health_num_entity, slf_action_turn_on_boss_health_num_entity, (void*)0x89bbb8);
	hook_slf_vtable(slf_deconstructor_turn_on_hero_health_num_entity, slf_action_turn_on_hero_health_num_entity, (void*)0x89bbc0);
	hook_slf_vtable(slf_deconstructor_turn_on_third_party_health_num_entity, slf_action_turn_on_third_party_health_num_entity, (void*)0x89bbc8);
	hook_slf_vtable(slf_deconstructor_unload_script, slf_action_unload_script, (void*)0x89c35c);
	hook_slf_vtable(slf_deconstructor_unlock_all_exterior_districts, slf_action_unlock_all_exterior_districts, (void*)0x89c4cc);
	hook_slf_vtable(slf_deconstructor_unlock_district_num, slf_action_unlock_district_num, (void*)0x89c48c);
	hook_slf_vtable(slf_deconstructor_vibrate_controller_num, slf_action_vibrate_controller_num, (void*)0x89a7c4);
	hook_slf_vtable(slf_deconstructor_vibrate_controller_num_num, slf_action_vibrate_controller_num_num, (void*)0x89a7bc);
	hook_slf_vtable(slf_deconstructor_vibrate_controller_num_num_num_num_num_num, slf_action_vibrate_controller_num_num_num_num_num_num, (void*)0x89a7b4);
	hook_slf_vtable(slf_deconstructor_vo_delay_num_num_num_num, slf_action_vo_delay_num_num_num_num, (void*)0x89a734);
	hook_slf_vtable(slf_deconstructor_wait_animate_fog_color_vector3d_num, slf_action_wait_animate_fog_color_vector3d_num, (void*)0x89a6fc);
	hook_slf_vtable(slf_deconstructor_wait_animate_fog_distance_num_num, slf_action_wait_animate_fog_distance_num_num, (void*)0x89a704);
	hook_slf_vtable(slf_deconstructor_wait_animate_fog_distances_num_num_num, slf_action_wait_animate_fog_distances_num_num_num, (void*)0x89a70c);
	hook_slf_vtable(slf_deconstructor_wait_change_blur_num_vector3d_num_num_num_num_num_num, slf_action_wait_change_blur_num_vector3d_num_num_num_num_num_num, (void*)0x89a66c);
	hook_slf_vtable(slf_deconstructor_wait_change_spider_reflexes_blur_num_vector3d_num_num_num_num_num_num, slf_action_wait_change_spider_reflexes_blur_num_vector3d_num_num_num_num_num_num, (void*)0x89a6a4);
	hook_slf_vtable(slf_deconstructor_wait_for_streamer_to_reach_equilibrium, slf_action_wait_for_streamer_to_reach_equilibrium, (void*)0x89c4a4);
	hook_slf_vtable(slf_deconstructor_wait_fps_test_num_num_vector3d_vector3d, slf_action_wait_fps_test_num_num_vector3d_vector3d, (void*)0x89a8ec);
	hook_slf_vtable(slf_deconstructor_wait_frame, slf_action_wait_frame, (void*)0x89a714);
	hook_slf_vtable(slf_deconstructor_wait_set_global_time_dilation_num_num, slf_action_wait_set_global_time_dilation_num_num, (void*)0x89a8a4);
	hook_slf_vtable(slf_deconstructor_wait_set_zoom_num_num, slf_action_wait_set_zoom_num_num, (void*)0x89a634);
	hook_slf_vtable(slf_deconstructor_write_to_file_str_str, slf_action_write_to_file_str_str, (void*)0x89a844);


}

void install_patches() {

	HookFunc(0x004EACF0, aeps_RenderAll, 0, "Patching call to aeps::RenderAll");

	HookFunc(0x0052B5D7, myDebugMenu, 0, "Hooking nglListEndScene to inject debug menu");
	//save orig ptr
	nflSystemOpenFile_orig = *nflSystemOpenFile_data;
	*nflSystemOpenFile_data = &nflSystemOpenFile;
	printf("Replaced nflSystemOpenFile %08X -> %08X\n", (DWORD)nflSystemOpenFile_orig, (DWORD)&nflSystemOpenFile);


	ReadOrWrite_orig = *ReadOrWrite_data;
	*ReadOrWrite_data = &ReadOrWrite;
	printf("Replaced ReadOrWrite %08X -> %08X\n", (DWORD)ReadOrWrite_orig, (DWORD)&ReadOrWrite);


	*(DWORD**)0x008218B2 = &hookDirectInputAddress;
	printf("Patching the DirectInput8Create call\n");


	HookFunc(0x0055D742, game_handle_game_states, 0, "Hooking handle_game_states");

	HookFunc(0x00421128, sub_41F9D0_hook, 0, "Hooking sub_41F9D0");

	HookFunc(0x0055B44E, load_hero_pack_hook, 0, "Hooking add_player's call to the hero pack loader (2nd player experiment)");
	install_control_read_probe();
	install_pad_update_hook();
	install_camera_lookup_fix();
	install_add_player_name_hooks();
	install_get_hero_hooks();
	install_swing_probes();
	install_helper_hooks();


	/*
	WriteDWORD(0x00877524, ai_hero_base_state_check_transition_hook, "Hooking check_transition for peter hooded");
	WriteDWORD(0x00877560, ai_hero_base_state_check_transition_hook, "Hooking check_transition for spider-man");
	WriteDWORD(0x0087759C, ai_hero_base_state_check_transition_hook, "Hooking check_transition for venom");
	*/

	HookFunc(0x00478DBF, get_info_node_hook, 0, "Hook get_info_node to get player ptr");


	WriteDWORD(0x0089C710, slf__create_progression_menu_entry, "Hooking first ocurrence of create_progession_menu_entry");
	WriteDWORD(0x0089C718, slf__create_progression_menu_entry, "Hooking second  ocurrence of create_progession_menu_entry");


	WriteDWORD(0x0089AF70, slf__create_debug_menu_entry, "Hooking first ocurrence of create_debug_menu_entry");
	WriteDWORD(0x0089C708, slf__create_debug_menu_entry, "Hooking second  ocurrence of create_debug_menu_entry");


	HookFunc(0x005AD77D, construct_client_script_libs_hook, 0, "Hooking construct_client_script_libs to inject my vm");

	WriteDWORD(0x0089C720, slf__destroy_debug_menu_entry__debug_menu_entry, "Hooking destroy_debug_menu_entry");
	WriteDWORD(0x0089C750, slf__debug_menu_entry__set_handler__str, "Hooking set_handler");

	//HookFunc(0x0054C89C, resource_pack_streamer_load_internal_hook, 0, "Hooking resource_pack_streamer::load_internal to inject interior loading");

	//HookFunc(0x005B87E0, os_developer_options, 1, "Hooking os_developer_options::get_flag");

	/*

	DWORD* windowHandler = 0x005AC48B;
	*windowHandler = WindowHandler;

	DWORD* otherHandler = 0x0076D6D1;
	*otherHandler = 0;

	*/


#ifdef _DEBUG
	hook_slf_vtables();
#endif

}

void close_debug() {
	debug_enabled = 0;
	game_unpause(g_game_ptr);
}

void handle_debug_entry(debug_menu_entry* entry) {
	current_menu = entry->data;
}

typedef char (__fastcall *entity_tracker_manager_get_the_arrow_target_pos_ptr)(DWORD* this, void* edx, DWORD* a2);
entity_tracker_manager_get_the_arrow_target_pos_ptr entity_tracker_manager_get_the_arrow_target_pos = (void*)0x0062EE10;

void handle_warp_entry(debug_menu_entry* entry) {
	
	float position[] = {
		0, -0, 1, 0,
		1, -0, -0, 0,
		0, 1, 0, 0,
		-203, 20, 430, 1
	};

	/*
	DWORD arg1 = *(DWORD*)0x96C158;
	DWORD* some_ptr = ai_ai_core_get_info_node(ai_current_player[5], NULL, arg1, 1);
	printf("WHYYYY %08X %08X\n", fancy_player_ptr, some_ptr);
	*/


	float final_pos[3] = { -203, 20, 430 };
	if (entry->data1 == 0) {
		region* cur_region = entry->data;
		final_pos[0] = cur_region->x;
		final_pos[1] = cur_region->y;
		final_pos[2] = cur_region->z;
		unlock_region(cur_region);
	}
	else {
		int res = entity_tracker_manager_get_the_arrow_target_pos( *(*(DWORD***)0x937B18 + 21), NULL, (void*)final_pos);
		if (!res)
			return;
	}

	position[12] = final_pos[0];
	position[13] = final_pos[1];
	position[14] = final_pos[2];

	close_debug();
	entity_teleport_abs_po(fancy_player_ptr[3], position, 1);
}

void handle_char_select_entry(debug_menu_entry* entry) {

	DWORD* some_number = (*(DWORD**)g_world_ptr) + 142;

	while (*some_number) {
		//printf("some_number %d\n", *some_number);
		world_dynamics_system_remove_player(*(DWORD**)g_world_ptr, NULL, *some_number - 1);
	}

	debug_enabled = 0;
	changing_model = 2;
	current_costume = entry->text;

}


void handle_add_player_select_entry(debug_menu_entry* entry) {

	DWORD* player_count = (*(DWORD**)g_world_ptr) + 142;

	if (!*player_count) {
		twop_log("[2P] no existing player, load into the game world first\n");
		return;
	}

	strncpy(second_costume, entry->text, sizeof(second_costume) - 1);
	second_costume[sizeof(second_costume) - 1] = 0;

	debug_enabled = 0;
	adding_second_player = 2;
}


void handle_options_select_entry(debug_menu_entry* entry) {

	BYTE* val = entry->data;
	*val = !*val;
}

typedef void* (__fastcall* script_instance_add_thread_ptr)(script_instance* this, void* edx, vm_executable* a1, void* a2);
script_instance_add_thread_ptr script_instance_add_thread = (void*)0x005AAD00;

void handle_progression_select_entry(debug_menu_entry* entry) {

	script_instance* instance = entry->data;
	int functionid = (int)entry->data1;

	DWORD addr = (DWORD)entry;

	script_instance_add_thread(instance, NULL, instance->object->vmexecutable[functionid], &addr);



	close_debug();
}

void handle_script_select_entry(debug_menu_entry* entry) {
	handle_progression_select_entry(entry);
}


void handle_distriction_variants_select_entry(debug_menu_entry* entry, custom_key_type key_type) {

	region* reg = entry->data;
	void* terrain_ptr = *(*(DWORD***)g_world_ptr + 0x6B);
	int variants = reg->variants;
	int current_variant = region_get_district_variant(reg);
	DWORD district_id = reg->district_id;

	switch (key_type) {

	case LEFT:
		terrain_set_district_variant(terrain_ptr, NULL, district_id, modulo(current_variant-1, variants), 1);
		break;
	case RIGHT:
		terrain_set_district_variant(terrain_ptr, NULL, district_id, modulo(current_variant+1, variants), 1);
		break;
	default:
		return;
	}
}

void setup_debug_menu() {

	start_debug = create_menu("Debug Menu", close_debug, (menu_handler_function)handle_debug_entry, 2);
	warp_menu = create_menu("Warp", goto_start_debug, (menu_handler_function)handle_warp_entry, 300);
	char_select_menu = create_menu("Char Select", goto_start_debug, (menu_handler_function)handle_char_select_entry, 5);
	options_menu = create_menu("Options", goto_start_debug, (menu_handler_function)handle_options_select_entry, 2);
	script_menu = create_menu("Script", goto_start_debug, (menu_handler_function)handle_script_select_entry, 50);
	progression_menu = create_menu("Progression", goto_start_debug, (menu_handler_function)handle_progression_select_entry, 10);
	district_variants_menu = create_menu("District variants", goto_start_debug, (menu_handler_function)handle_distriction_variants_select_entry, 15);
	add_player_menu = create_menu("Add 2nd Player", goto_start_debug, (menu_handler_function)handle_add_player_select_entry, 10);


	debug_menu_entry warp_entry = { "Warp", NORMAL, warp_menu };
	debug_menu_entry char_select = { "Char Select", NORMAL, char_select_menu };
	debug_menu_entry options_entry = { "Options", NORMAL, options_menu };
	debug_menu_entry script_entry = { "Script", NORMAL, script_menu };
	debug_menu_entry progression_entry = { "Progression", NORMAL, progression_menu };
	debug_menu_entry district_entry = { "District variants", NORMAL, district_variants_menu };
	debug_menu_entry add_player_entry = { "Add 2nd Player", NORMAL, add_player_menu };

	add_debug_menu_entry(start_debug, &warp_entry);
	add_debug_menu_entry(start_debug, &district_entry);
	add_debug_menu_entry(start_debug, &char_select);
	add_debug_menu_entry(start_debug, &add_player_entry);
	add_debug_menu_entry(start_debug, &options_entry);
	add_debug_menu_entry(start_debug, &script_entry);
	add_debug_menu_entry(start_debug, &progression_entry);

	char* costumes[] = {
		"ultimate_spiderman",
		"arachno_man_costume",
		"usm_wrestling_costume",
		"usm_blacksuit_costume",
		"peter_parker",
		"peter_parker_costume",
		"peter_hooded",
		"peter_hooded_costume",
		"venom",
		"venom_spider"
	};


	for (int i = 0; i < sizeof(costumes) / sizeof(char*); i++) {
		debug_menu_entry char_entry;
		char_entry.entry_type = NORMAL;
		strcpy(char_entry.text, costumes[i]);

		add_debug_menu_entry(char_select_menu, &char_entry);
		add_debug_menu_entry(add_player_menu, &char_entry);
	}


	debug_menu_entry show_fps = { "Show FPS", BOOLEAN_E, (void*)0x975848 };
	debug_menu_entry memory_info = { "Memory Info", BOOLEAN_E, (void*)0x975849 };
	

	add_debug_menu_entry(options_menu, &show_fps);
	add_debug_menu_entry(options_menu, &memory_info);


	/*




	for (int i = 0; i < 5; i++) {

		debug_menu_entry asdf;
		sprintf(asdf.text, "entry %d", i);
		printf("AQUI %s\n", asdf.text);

		add_debug_menu_entry(start_debug, &asdf);
	}


	add_debug_menu_entry(start_debug, &teste);
	*/
}


BOOL WINAPI DllMain(HINSTANCE hInstDll, DWORD fdwReason, LPVOID reserverd) {



	if (sizeof(region) != 0x134) {
		__debugbreak();

	}

	memset(keys, 0, sizeof(keys));
	if (fdwReason == DLL_PROCESS_ATTACH) {
		AllocConsole();



		if (!freopen("CONOUT$", "w", stdout)) {
			MessageBoxA(NULL, "Error", "Couldn't allocate console...Closing", 0);
			return FALSE;
		}

		setup_debug_menu();
		set_text_writeable();
		set_rdata_writeable();
		install_patches();

	}
	else if (fdwReason == DLL_PROCESS_DETACH)
		FreeConsole();

	return TRUE;
}

int main() {
	return 0;
}
