
// Include Debuginator, override any appropriate functions
#include <plugin_foundation/assert.h>
#define DEBUGINATOR_IMPLEMENTATION
#define DEBUGINATOR_assert XENSURE
#include "the_debuginator.h"

#include <engine_plugin_api/plugin_api.h>
#include "c_api_the_debuginator.h"
#include <plugin_foundation/allocator.h>
#include <plugin_foundation/hash_function.h>
#include <engine_plugin_api\c_api\c_api_input_controller.h>
#include <engine_plugin_api\c_api\c_api_keyboard.h>
#include <limits.h>

struct TheDebuginatorWrapper {
	TheDebuginator debuginator;
	char id[64];
	ConstWindowPtr window;
	GuiPtr gui;
	uint64_t font;
	MaterialPtr font_material;
	bool memory_owned_by_this_plugin;
	int font_size;
	bool axis_selected_repeat;
	bool axis_scroll_repeat;
	float scroll_repeat_timer;
};

struct InputWrapper {
	InputControllerCApi* api;
	CApiInputControllerPtr controller;

	float time_since_pressed;

	// TODO: Pre-calc hashes.
	bool button(const char* button) {
		using namespace stingray_plugin_foundation;
		return api->button(controller, api->button_id(controller, hash32(button))) > 0;
	}

	bool pressed(const char* button) {
		using namespace stingray_plugin_foundation;
		return api->pressed(controller, api->button_id(controller, hash32(button))) > 0;
	}

	bool pressed_repeat(const char* button) {
		using namespace stingray_plugin_foundation;
		bool pressed = api->button(controller, api->button_id(controller, hash32(button))) > 0;

		// Faux repeat support.
		if (pressed) {
			if (time_since_pressed == -1) {
				time_since_pressed = 0;
				return true;
			}
			else if (time_since_pressed > 0.3f) {
				time_since_pressed = 0.25f;
				return true;
			}
			return false;
		}

		return false;
	}

	CApiVector3 axis(const char *axis) {
		using namespace stingray_plugin_foundation;
		return api->axis(controller, api->axis_id(controller, hash32(axis)), 0);
	}
};

struct PluginMemory {
	AllocatorObject* allocator_object;
	TheDebuginatorWrapper debuginators[8];
	int num_debuginators = 0;
	InputWrapper input_wrapper;
};

static PluginMemory* plugin_memory = nullptr;

// Engine APIs
static AllocatorApi* allocator_api = nullptr;
static ScriptApi* script_api = nullptr;

void set_ui_size(TheDebuginator* debuginator, int font_size, int item_height);

namespace {
	void draw_text(const char* text, DebuginatorVector2* position, DebuginatorColor* color, DebuginatorFont* font, void* app_userdata) {
		TheDebuginatorWrapper* wrapper = (TheDebuginatorWrapper*)app_userdata;
		CApiVector2 gui_position = { position->x,  position->y };
		CApiVector4 gui_color = { (float)color->a, (float)color->r, (float)color->g, (float)color->b };
		gui_position.y = wrapper->debuginator.screen_resolution.y - position->y - wrapper->font_size * 0.75;
		script_api->Gui->text(wrapper->gui, text, wrapper->font, wrapper->font_size, wrapper->font_material, &gui_position, 100 + 1, 0, &gui_color);
	}

	void draw_rect(DebuginatorVector2* position, DebuginatorVector2* size, DebuginatorColor* color, void* app_userdata) {
		TheDebuginatorWrapper* wrapper = (TheDebuginatorWrapper*)app_userdata;
		CApiVector2 gui_position = { position->x,  position->y };
		CApiVector4 gui_color = { (float)color->a, (float)color->r, (float)color->g, (float)color->b };
		gui_position.y = wrapper->debuginator.screen_resolution.y - position->y - size->y;
		script_api->Gui->rect(wrapper->gui, &gui_position, 100, (ConstVector2Ptr)size, &gui_color);
	}

	void word_wrap(const char* text, DebuginatorFont font, float max_width, unsigned* row_count, unsigned* row_lengths, unsigned row_lengths_buffer_size, void* app_userdata) {
		TheDebuginatorWrapper* wrapper = (TheDebuginatorWrapper*)app_userdata;
		*row_count = script_api->Gui->word_wrap(wrapper->gui, text, wrapper->font, wrapper->font_size, max_width, " ", "-+&/", "\n", 0, row_lengths, row_lengths_buffer_size);
	}

	DebuginatorVector2 text_size(const char* text, DebuginatorFont* font, void* app_userdata) {
		TheDebuginatorWrapper* wrapper = (TheDebuginatorWrapper*)app_userdata;
		TextExtentsResult results = script_api->Gui->text_extents(wrapper->gui, text, wrapper->font, wrapper->font_size, 0);
		CApiVector2 text_size = { results.max.x - results.min.x, results.max.y - results.min.y };
		//text_size.x *= 2; // WHYYYY
		text_size.y *= 2;
		return *(DebuginatorVector2*)&text_size;
	}

	// Item callbacks
	void on_change_ui_size(DebuginatorItem* item, void* value, const char* value_title, void* app_userdata) {
		(void)value_title;
		TheDebuginatorWrapper* wrapper = (TheDebuginatorWrapper*)app_userdata;
		int size_category = *(int*)value;
		if (size_category == 0) {
			set_ui_size(&wrapper->debuginator, 14, 22);
		}
		else if (size_category == 1) {
			set_ui_size(&wrapper->debuginator, 20, 30);
		}
		else if (size_category == 2) {
			set_ui_size(&wrapper->debuginator, 32, 40);
		}
		else if (size_category == 3) {
			set_ui_size(&wrapper->debuginator, 64, 70);
		}
	}
}

void setup_api(GetApiFunction get_engine_api, const char* plugin_name) {
	allocator_api = (AllocatorApi*)get_engine_api(ALLOCATOR_API_ID);
	script_api = (ScriptApi*)get_engine_api(C_API_ID);

	if (plugin_memory == nullptr) {
		AllocatorObject* allocator_object = allocator_api->make_plugin_allocator(plugin_name);
		plugin_memory = (PluginMemory*)allocator_api->allocate(allocator_object, sizeof(PluginMemory), 16);
		memset(plugin_memory, 0, sizeof(*plugin_memory));
		plugin_memory->allocator_object = allocator_object;
		plugin_memory->input_wrapper.time_since_pressed = -1;
	}
}

void shutdown_api() {
	if (plugin_memory != nullptr) {
		for (int i = 0; i < plugin_memory->num_debuginators; i++) {
			TheDebuginatorWrapper* wrapper = &plugin_memory->debuginators[i];
			if (wrapper->memory_owned_by_this_plugin) {
				allocator_api->deallocate(plugin_memory->allocator_object, wrapper->debuginator.memory_arena);
			}
		}

		AllocatorObject* allocator_object = plugin_memory->allocator_object;
		allocator_api->deallocate(plugin_memory->allocator_object, plugin_memory);
		allocator_api->destroy_plugin_allocator(allocator_object);
		plugin_memory = nullptr;
	}
}

void* start_reload(GetApiFunction get_engine_api) {
	return plugin_memory;
}

void finish_reload(GetApiFunction get_engine_api, void *state) {
	plugin_memory = (PluginMemory*)state;
}

void update_debuginators(float dt) {
	for (int i = 0; i < plugin_memory->num_debuginators; i++) {
		TheDebuginatorWrapper* wrapper = &plugin_memory->debuginators[i];

		CApiVector2 resolution = script_api->Gui->resolution(NULL, wrapper->window);
		wrapper->debuginator.size.y = resolution.y;
		wrapper->debuginator.screen_resolution.y = resolution.y;

		debuginator_update(&wrapper->debuginator, dt);
		debuginator_draw(&wrapper->debuginator, dt);

		wrapper->scroll_repeat_timer += dt;
	}

	if (plugin_memory->input_wrapper.time_since_pressed != -1) {
		plugin_memory->input_wrapper.time_since_pressed += dt;
	}
}

void destroy_debuginator(TheDebuginator* debuginator);
TheDebuginator* get_debuginator(const char* id);

TheDebuginator* create_debuginator(const char* id, DebuginatorPluginCreateContext* context) {
	if (id == NULL) {
		id = "default";
	}

	XASSERT(plugin_memory->num_debuginators < 8, "Too many debuginators created. Good lord, how many do you need?!");
	XASSERT(get_debuginator(id) == NULL, "Debuginator with that name already exists.");

	TheDebuginatorWrapper* wrapper = &plugin_memory->debuginators[plugin_memory->num_debuginators];
	memset(wrapper, 0, sizeof(TheDebuginatorWrapper));
	strcpy_s(wrapper->id, sizeof(plugin_memory->debuginators[plugin_memory->num_debuginators].id), id);

	wrapper->window = context->window;
	wrapper->font = context->font;
	wrapper->font_material = context->font_material;
	wrapper->gui = context->gui;
	wrapper->font_size = context->font_size > 0 ? context->font_size : 20;

	if (context->config.draw_text == NULL) {
		XASSERT(wrapper->font, "No font defined");
		XASSERT(wrapper->font_material, "No font_material defined");
		XASSERT(wrapper->gui, "No gui defined");
		context->config.draw_text = draw_text;
		context->config.draw_rect = draw_rect;
		context->config.word_wrap = word_wrap;
		context->config.text_size = text_size;
		context->config.app_user_data = wrapper;;
	}

	if (context->config.memory_arena == NULL) {
		wrapper->memory_owned_by_this_plugin = true;
		XASSERT(context->config.memory_arena_capacity != 0, "You must provide a wanted memory size.");
		context->config.memory_arena = (char*)allocator_api->allocate(plugin_memory->allocator_object, context->config.memory_arena_capacity, 16);
	}

	context->config.screen_resolution.x = script_api->Gui->resolution(NULL, wrapper->window).x;
	context->config.screen_resolution.y = script_api->Gui->resolution(NULL, wrapper->window).y;
	context->config.size.y = context->config.screen_resolution.y;

	TheDebuginator* debuginator = &wrapper->debuginator;
	debuginator_create(&context->config, debuginator);

	{
		static const char* uisize_titles[4] = { "Small", "Medium", "Large", "ULTRA LARGE" };
		static int uisize_indices[4] = { 0, 1, 2, 3 };
		DebuginatorItem* uisize_item = debuginator_create_array_item(debuginator, NULL, "Debuginator/UI size",
			"Change font and item size.", on_change_ui_size, wrapper,
			uisize_titles, uisize_indices, 4, sizeof(uisize_indices[0]));

		uisize_item->leaf.default_index = 1;
		uisize_item->leaf.hot_index = 1;
		uisize_item->leaf.active_index = 1;
		debuginator_activate(debuginator, uisize_item, false);
	}

	plugin_memory->num_debuginators++;
	return debuginator;
}

void destroy_debuginator(TheDebuginator* debuginator) {
	if (debuginator == NULL) {
		debuginator = get_debuginator("default");
	}

	for (int i = 0; i < plugin_memory->num_debuginators; i++) {
		if (debuginator == &plugin_memory->debuginators[i].debuginator) {
			TheDebuginatorWrapper* wrapper = &plugin_memory->debuginators[i];
			if (wrapper->memory_owned_by_this_plugin) {
				allocator_api->deallocate(plugin_memory->allocator_object, debuginator->memory_arena);
			}

			plugin_memory->debuginators[i] = plugin_memory->debuginators[--plugin_memory->num_debuginators];
			break;
		}
	}
}

TheDebuginator* get_debuginator(const char* id) {
	if (id == NULL) {
		id = "default";
	}

	for (int i = 0; i < plugin_memory->num_debuginators; i++) {
		if (strcmp(id, plugin_memory->debuginators[i].id) == 0) {
			return &plugin_memory->debuginators[i].debuginator;
		}
	}

	return NULL;
}

void handle_default_input(TheDebuginator* debuginator, unsigned devices) {
	if (debuginator == NULL) {
		debuginator = get_debuginator("default");
	}

	TheDebuginatorWrapper* wrapper = (TheDebuginatorWrapper*)debuginator;

	InputControllerCApi* api = script_api->Input->InputController;
	InputWrapper& input_wrapper = plugin_memory->input_wrapper;
	input_wrapper.api = api;

	if (!script_api->Window->has_focus(wrapper->window)) {
		input_wrapper.time_since_pressed = -1;
		return;
	}

	while (devices & Debuginator_Keyboard) { // So we can break out of the scope
		devices &= ~Debuginator_Keyboard;
		CApiInputControllerPtr keyboard = script_api->Input->keyboard();
		input_wrapper.controller = keyboard;

		if (api->any_released(input_wrapper.controller) != UINT_MAX) {
			input_wrapper.time_since_pressed = -1;
		}

		if (!debuginator_is_open(debuginator)) {
			if (input_wrapper.pressed("right")) {
				debuginator_set_open(debuginator, true);
			}

			break;
		}

		bool ctrl_pressed = input_wrapper.button("left ctrl") ||
							input_wrapper.button("right ctrl");

		if (input_wrapper.pressed_repeat("up")) {
			bool long_move = ctrl_pressed;
			debuginator_move_to_prev_leaf(debuginator, long_move);
			debuginator->focus_height = debuginator->default_focus_height;
		} 
		else if (input_wrapper.pressed_repeat("down")) {
			bool long_move = ctrl_pressed;
			debuginator_move_to_next_leaf(debuginator, long_move);
			debuginator->focus_height = debuginator->default_focus_height;
		}
		else if (input_wrapper.pressed_repeat("home")) {
			debuginator_move_to_root(debuginator);
			debuginator->focus_height = default_focus_height;
		}
		else if (input_wrapper.pressed_repeat("end")) {
			debuginator_move_to_root(debuginator);
			debuginator_move_sibling_previous(debuginator);
			debuginator->focus_height = default_focus_height;
		}
		else if (input_wrapper.pressed("left")) {
			DebuginatorItem* hot_item = debuginator_get_hot_item(debuginator);
			if (debuginator_is_open(debuginator) && (hot_item->is_folder || (!hot_item->is_folder && hot_item->leaf.is_expanded))) {
				debuginator_move_to_parent(debuginator);
			}
		}
		else if (input_wrapper.pressed("escape") || input_wrapper.pressed("delete")) {
			debuginator_set_open(debuginator, false);
		}
		else if (input_wrapper.pressed("enter")) {
			debuginator_move_to_child(debuginator, false);	
		}

		if (input_wrapper.pressed_repeat("backspace")) {
			const char* filter = debuginator_get_filter(debuginator);
			int filter_length = (int)strlen(filter);

			if (filter_length > 0) {
				char new_filter[64] = { 0 };
				memcpy(new_filter, filter, filter_length);
				const char* filter = debuginator_get_filter(debuginator);
				int filter_length = (int)strlen(filter);
				if (filter_length > 0) {
					new_filter[--filter_length] = '\0';
				}
				debuginator_update_filter(debuginator, new_filter);
			}
			else if (debuginator_is_filtering_enabled(debuginator)) {
				debuginator_set_filtering_enabled(debuginator, false);
			}
		}

		if (ctrl_pressed && input_wrapper.pressed_repeat("w")) {
			debuginator_update_filter(debuginator, "");
		} else {
			unsigned int num_key_strokes;
			const int* keystrokes = script_api->Input->Keyboard->keystrokes(keyboard, &num_key_strokes);
			if (num_key_strokes > 0) {
				const char* filter = debuginator_get_filter(debuginator);
				int filter_length = (int)strlen(filter);
				if (filter_length + (int)num_key_strokes < wrapper->font_size) {
					char new_filter[64] = { 0 };
					memcpy(new_filter, filter, filter_length);
					int real_strokes = 0;
					for (unsigned int i = 0; i < num_key_strokes; ++i) {
						if (32 <= keystrokes[i] && keystrokes[i] <= 125) {
							new_filter[filter_length++] = (char)keystrokes[i];
							++real_strokes;
						}
					}

					if (real_strokes > 0) {
						if (!debuginator_is_filtering_enabled(debuginator)) {
							debuginator_set_filtering_enabled(debuginator, true);
						}
						debuginator_update_filter(debuginator, new_filter);
						// Update focus height to match new hot item
						float max_height = debuginator_total_height(debuginator);
						int active_height = 0;
						debuginator__distance_to_hot_item(debuginator->root, debuginator->hot_item, debuginator->item_height, &active_height);
						float height_pixels = debuginator->focus_height * debuginator->size.y - active_height;
						if (height_pixels < -(max_height - (1.f - default_focus_height) * debuginator->size.y)) {
							debuginator->focus_height = -(max_height - (1.f - default_focus_height) * debuginator->size.y - active_height) / debuginator->size.y;
						} else if (height_pixels > default_focus_height * debuginator->size.y) {
							debuginator->focus_height = (default_focus_height * debuginator->size.y + active_height) / debuginator->size.y;
						}
					}
				}
			}
		}
	}

	if (devices & Debuginator_Mouse) {
		CApiInputControllerPtr mouse = script_api->Input->mouse();
		input_wrapper.controller = mouse;

		if (api->any_released(input_wrapper.controller) != UINT_MAX) {
			input_wrapper.time_since_pressed = -1;
		}


		if (debuginator_is_open(debuginator)) {
			CApiVector3 scroll = input_wrapper.axis("wheel");
			if (scroll.y != 0) {
				debuginator->focus_height += scroll.y * 0.05f;
				float max_height = debuginator_total_height(debuginator);
				int active_height = 0;
				debuginator__distance_to_hot_item(debuginator->root, debuginator->hot_item, debuginator->item_height, &active_height);
				float height_pixels = debuginator->focus_height * debuginator->size.y - active_height;
				if (height_pixels < -(max_height - (1.f - default_focus_height) * debuginator->size.y)) {
					debuginator->focus_height = -(max_height - (1.f - default_focus_height) * debuginator->size.y - active_height) / debuginator->size.y;
				} else if (height_pixels > default_focus_height * debuginator->size.y) {
					debuginator->focus_height = (default_focus_height * debuginator->size.y + active_height) / debuginator->size.y;
				}
			}

	if (devices & Debuginator_Mouse) {
		CApiInputControllerPtr mouse = script_api->Input->mouse();
		input_wrapper.controller = mouse;

		if (api->any_released(input_wrapper.controller) != UINT_MAX) {
			input_wrapper.time_since_pressed = -1;
		}


		CApiVector3 pos = input_wrapper.axis("cursor");
		bool inside_rect = pos.x > debuginator->top_left.x && pos.x < debuginator->top_left.x + debuginator->size.x && pos.y > debuginator->top_left.y && pos.y < debuginator->top_left.y + debuginator->size.y;
		if (debuginator_is_open(debuginator) && inside_rect) {
			CApiVector3 scroll = input_wrapper.axis("wheel");
			if (scroll.y != 0) {
				debuginator->focus_height += scroll.y * 0.05f;
				float max_height = debuginator_total_height(debuginator);
				int active_height = 0;
				debuginator__distance_to_hot_item(debuginator->root, debuginator->hot_item, debuginator->item_height, &active_height);
				float height_pixels = debuginator->focus_height * debuginator->size.y - active_height;
				if (height_pixels < -(max_height - (1.f - debuginator->default_focus_height) * debuginator->size.y)) {
					debuginator->focus_height = -(max_height - (1.f - debuginator->default_focus_height) * debuginator->size.y - active_height) / debuginator->size.y;
				} else if (height_pixels > debuginator->default_focus_height * debuginator->size.y) {
					debuginator->focus_height = (debuginator->default_focus_height * debuginator->size.y + active_height) / debuginator->size.y;
				}
			}

			if (input_wrapper.pressed_repeat("left")) {
				float y = debuginator->screen_resolution.y - pos.y;
				if (pos.x > debuginator->top_left.x && pos.x < debuginator->top_left.x + debuginator->size.x) {
					debuginator_activate_closest_by_height(debuginator, y);
				}
			}
		}
	}

	if (devices & Debuginator_Gamepad) {
		int xbox_style_pads = 0;

#if !defined(PS4)
		xbox_style_pads = script_api->Input->num_pads();
#endif
		for (int i = 0; i < xbox_style_pads; i++) {
			input_wrapper.controller = script_api->Input->pad(i);

			if (!api->active(input_wrapper.controller)) {
				continue;
			}

			if (api->any_released(input_wrapper.controller) != UINT_MAX) {
				input_wrapper.time_since_pressed = -1;
			}

			if (!debuginator_is_open(debuginator)) {
				if (input_wrapper.pressed("start")) {
					debuginator_set_open(debuginator, true);
				}

				continue;
			}

			CApiVector3 left_stick = input_wrapper.axis("left");
			float DEADZONE = 0.4f;
			if (fabs(left_stick.x) > DEADZONE) {
				if (wrapper->axis_selected_repeat) {
					left_stick.x = 0.f;
				} else {
					wrapper->axis_selected_repeat = true;
				}
			} else {
				wrapper->axis_selected_repeat = false;
			}
			float SCROLL_TIMEOUT = 0.25f;
			float SCROLL_REPEAT_TIMEOUT = 0.05f;
			if (fabs(left_stick.y) > DEADZONE) {
				if (wrapper->axis_scroll_repeat && wrapper->scroll_repeat_timer < 0.f) {
					left_stick.y = 0.f;
				} else if (!wrapper->axis_scroll_repeat) {
					wrapper->scroll_repeat_timer = -SCROLL_TIMEOUT;
					wrapper->axis_scroll_repeat = true;
				} else {
					wrapper->scroll_repeat_timer = -SCROLL_REPEAT_TIMEOUT;
					wrapper->axis_scroll_repeat = true;
				}
			} else {
				wrapper->axis_scroll_repeat = false;
			}

			if (input_wrapper.pressed_repeat("d_up") || left_stick.y > DEADZONE) {
				bool long_move = false;
				debuginator_move_to_prev_leaf(debuginator, long_move);
				debuginator->focus_height = debuginator->default_focus_height;
			}
			else if (input_wrapper.pressed_repeat("d_down") || left_stick.y < -DEADZONE) {
				bool long_move = false;
				debuginator_move_to_next_leaf(debuginator, long_move);
				debuginator->focus_height = debuginator->default_focus_height;
			}
			else if (input_wrapper.pressed("d_left") || input_wrapper.pressed("b")) {
				DebuginatorItem* hot_item = debuginator_get_hot_item(debuginator);
				if (debuginator_is_open(debuginator) && !hot_item->leaf.is_expanded) {
					debuginator_set_open(debuginator, false);
				}
				else if (!hot_item->is_folder && hot_item->leaf.is_expanded) {
					debuginator_move_to_parent(debuginator);
				}
			}
			else if (input_wrapper.pressed("start")) {
				debuginator_set_open(debuginator, false);
			}
			else if (input_wrapper.pressed("d_right") || input_wrapper.pressed("a") || fabs(left_stick.x) > DEADZONE) {
				debuginator_move_to_child(debuginator, false);
			}
			else if (input_wrapper.pressed_repeat("left_shoulder")) {
				bool long_move = true;
				debuginator_move_to_prev_leaf(debuginator, long_move);
				debuginator->focus_height = debuginator->default_focus_height;
			}
			else if (input_wrapper.pressed_repeat("right_shoulder")) {
				bool long_move = true;
				debuginator_move_to_next_leaf(debuginator, long_move);
				debuginator->focus_height = debuginator->default_focus_height;
			}
			else if (input_wrapper.pressed("x")) {
				debuginator_move_to_child(debuginator, true);
			}
		}

		// Todo generalize this
		int num_ps4_pads = 0;
#if defined(WINDOWSPC)
		num_ps4_pads = script_api->Input->num_windows_ps4_pads();
#elif defined(PS4)
		num_ps4_pads = script_api->Input->num_pads();
#endif
		for (int i = 0; i < num_ps4_pads; i++) {
#if defined(WINDOWSPC)
			input_wrapper.controller = script_api->Input->windows_ps4_pad(i);
#else
			input_wrapper.controller = script_api->Input->pad(i);
#endif

			if (!api->active(input_wrapper.controller)) {
				continue;
			}

			if (api->any_released(input_wrapper.controller) != UINT_MAX) {
				input_wrapper.time_since_pressed = -1;
			}

			if (!debuginator_is_open(debuginator)) {
				if (input_wrapper.pressed("options")) {
					debuginator_set_open(debuginator, true);
				}

				continue;
			}
			
			CApiVector3 left_stick = input_wrapper.axis("left");
			float DEADZONE = 0.4f;
			if (fabs(left_stick.x) > DEADZONE) {
				if (wrapper->axis_selected_repeat) {
					left_stick.x = 0.f;
				} else {
					wrapper->axis_selected_repeat = true;
				}
			} else {
				wrapper->axis_selected_repeat = false;
			}
			float SCROLL_TIMEOUT = 0.25f;
			float SCROLL_REPEAT_TIMEOUT = 0.05f;
			if (fabs(left_stick.y) > DEADZONE) {
				if (wrapper->axis_scroll_repeat && wrapper->scroll_repeat_timer < 0.f) {
					left_stick.y = 0.f;
				} else if (!wrapper->axis_scroll_repeat) {
					wrapper->scroll_repeat_timer = -SCROLL_TIMEOUT;
					wrapper->axis_scroll_repeat = true;
				} else {
					wrapper->scroll_repeat_timer = -SCROLL_REPEAT_TIMEOUT;
					wrapper->axis_scroll_repeat = true;
				}
			} else {
				wrapper->axis_scroll_repeat = false;
			}

			if (input_wrapper.pressed_repeat("up") || left_stick.y > DEADZONE) {
				bool long_move = false;
				debuginator_move_to_prev_leaf(debuginator, long_move);
				debuginator->focus_height = debuginator->default_focus_height;
			}
			else if (input_wrapper.pressed_repeat("down") || left_stick.y < -DEADZONE) {
				bool long_move = false;
				debuginator_move_to_next_leaf(debuginator, long_move);
				debuginator->focus_height = debuginator->default_focus_height;
			}
			else if (input_wrapper.pressed("left") || input_wrapper.pressed("circle")) {
				DebuginatorItem* hot_item = debuginator_get_hot_item(debuginator);
				if (debuginator_is_open(debuginator) && !hot_item->leaf.is_expanded) {
					debuginator_set_open(debuginator, false);
				}
				else if (!hot_item->is_folder && hot_item->leaf.is_expanded) {
					debuginator_move_to_parent(debuginator);
				}
			}
			else if (input_wrapper.pressed("options")) {
				debuginator_set_open(debuginator, false);
			}
			else if (input_wrapper.pressed("right") || input_wrapper.pressed("cross") || fabs(left_stick.x) > DEADZONE) {
				debuginator_move_to_child(debuginator, false);
			}
			else if (input_wrapper.pressed_repeat("l1")) {
				bool long_move = true;
				debuginator_move_to_prev_leaf(debuginator, long_move);
				debuginator->focus_height = debuginator->default_focus_height;
			}
			else if (input_wrapper.pressed_repeat("r1")) {
				bool long_move = true;
				debuginator_move_to_next_leaf(debuginator, long_move);
				debuginator->focus_height = debuginator->default_focus_height;
			}
			else if (input_wrapper.pressed("square")) {
				debuginator_move_to_child(debuginator, true);
			}
		}
	}
}

void set_ui_size(TheDebuginator* debuginator, int font_size, int item_height) {
	if (debuginator == NULL) {
		debuginator = get_debuginator("default");
	}

	debuginator_set_item_height(debuginator, item_height);

	TheDebuginatorWrapper* wrapper = (TheDebuginatorWrapper*)debuginator;
	wrapper->font_size = font_size;
}

void get_debuginator_api(TheDebuginatorApi* api) {
	api->create_debuginator = create_debuginator;
	api->destroy_debuginator = destroy_debuginator;
	api->get_debuginator = get_debuginator;
	api->handle_default_input = handle_default_input;

	api->get_default_config = debuginator_get_default_config;
	api->is_open = debuginator_is_open;
	api->set_open = debuginator_set_open;
	api->create_array_item = debuginator_create_array_item;
	api->create_bool_item = debuginator_create_bool_item;
	api->create_preset_item = debuginator_create_preset_item;
	api->new_folder_item = debuginator_new_folder_item;
	api->get_item = debuginator_get_item;
	api->set_hot_item = debuginator_set_hot_item;
	api->get_hot_item = debuginator_get_hot_item;
	api->remove_item = debuginator_remove_item;
	api->remove_item_by_path = debuginator_remove_item_by_path;
	api->save = debuginator_save;
	api->load_item = debuginator_load_item;
	api->set_default_value = debuginator_set_default_value;
	api->set_edit_type = debuginator_set_edit_type;
	api->activate = debuginator_activate;
	api->move_to_next_leaf = debuginator_move_to_next_leaf;
	api->move_to_prev_leaf = debuginator_move_to_prev_leaf;
	api->move_to_child = debuginator_move_to_child;
	api->move_to_parent = debuginator_move_to_parent;
	api->is_filtering_enabled = debuginator_is_filtering_enabled;
	api->set_filtering_enabled = debuginator_set_filtering_enabled;
	api->get_filter = debuginator_get_filter;
	api->update_filter = debuginator_update_filter;
	api->set_item_height = debuginator_set_item_height;
	api->set_size = debuginator_set_size;
}
