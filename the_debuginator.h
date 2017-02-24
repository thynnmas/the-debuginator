#ifndef INCLUDE_THE_DEBUGINATOR_H
#define INCLUDE_THE_DEBUGINATOR_H

/*
# THE DEBUGINATOR

## Usage
	// Create
	static void on_debuginator_save(DebuginatorItemDefinition* item, void* value, const char* value_title) {
		game_save_debug_setting(item->path, value_title);
	}

	DebuginatorItemDefinition item_buffer[256];
	TheDebuginator debuginator = debuginator_create(item_buffer, 256, on_debuginator_save);

	// Load settings at startup
	void parse_command_line(int argc, char** argv) {
		if (argc > 2 && strcmp(argv[1], "--do_the_bool") == 0) {
			debuginator_pre_load_setting(debuginator, "Test/Bool with function callback", "True");
		}
	}

	// Add debug menu item for reals
	// (can happen later, and multiple times if you want to override something)
	static void on_item_changed(DebuginatorItemDefinition* item, void* value, const char* value_title) {
		bool new_value = *(bool*)value;
		printf("Item %s changed value to (%d) %s", item->title, value, value_title);
	}

	DEBUGINATOR_create_bool_item(debuginator, "Test/Bool with function callback",
	"Calls on_item_changed when user changes the value in the menu.",
		on_item_changed, NULL);

	// Validate configuration
	// debuginator_validate(&debuginator);
*/

#ifndef DEBUGINATOR_assert
#include <assert.h>
#define DEBUGINATOR_assert assert;
#endif

#ifndef DEBUGINATOR_memcpy
#include <string.h>
#define DEBUGINATOR_memcpy memcpy
#endif

#ifndef __cplusplus
#include <stdbool.h>
#endif

#ifndef DEBUGINATOR_FREE_LIST_CAPACITY
#define DEBUGINATOR_FREE_LIST_CAPACITY 256
#endif

#ifndef DEBUGINATOR_max_title_length
#define DEBUGINATOR_max_title_length 20
#endif

#ifndef DEBUGINATOR_debug_print
#define DEBUGINATOR_debug_print 
#endif

/*
enum DebuginatorUpdateType {
	// Set content at init, will never change again.
	DebuginatorUpdateType_Never,

	// Will update when item is expanded or when cursor enters it
	DebuginatorUpdateType_OnShow,

	// Will update every frame when cursor is on it or its children
	DebuginatorUpdateType_WhenHot,
};
*/

//struct DebuginatorItem;
typedef struct DebuginatorFolderData {
	struct DebuginatorItem* first_child;
	struct DebuginatorItem* hot_child;
} DebuginatorFolderData;

typedef struct DebuginatorItem DebuginatorItem;

typedef void (*DebuginatorOnItemChangedCallback)(DebuginatorItem* item, void* value, const char* value_title);

typedef struct DebuginatorLeafData {
	const char* description;

	bool is_active;
	char* hot_value;
	int hot_index;
	int active_index;

	const char** value_titles;
	const char** value_descriptions;
	void* values;

	int num_values;
	size_t array_element_size;

	DebuginatorOnItemChangedCallback on_item_changed_callback;
} DebuginatorLeafData;

typedef struct DebuginatorItem {
	char title[DEBUGINATOR_max_title_length];
	bool is_folder;
	void* user_data;

	DebuginatorItem* next_sibling;
	DebuginatorItem* parent;

	union {
		DebuginatorLeafData leaf;
		DebuginatorFolderData folder;
#pragma warning(suppress: 4201) // Unnamed union
	};
} DebuginatorItem;

typedef struct TheDebuginator {
	DebuginatorItem* root;
	DebuginatorItem* hot_item;

	size_t item_buffer_capacity;
	size_t item_buffer_size;
	DebuginatorItem* item_buffer;

	size_t free_list_size;
	size_t free_list[DEBUGINATOR_FREE_LIST_CAPACITY];
} TheDebuginator;

DebuginatorItem* debuginator_new_leaf_item(TheDebuginator* debuginator) {
	DEBUGINATOR_assert(debuginator->item_buffer_size < debuginator->item_buffer_capacity);
	return &debuginator->item_buffer[debuginator->item_buffer_size++];
}

DebuginatorItem* debuginator_get_free_item(TheDebuginator* debuginator) {
	DebuginatorItem* item;
	if (debuginator->free_list_size > 0) {
		size_t free_index = debuginator->free_list[--debuginator->free_list_size];
		DEBUGINATOR_assert(0 <= free_index && free_index < debuginator->item_buffer_capacity);
		item = &debuginator->item_buffer[free_index];
	}
	else {
		DEBUGINATOR_assert(debuginator->item_buffer_size < debuginator->item_buffer_capacity);
		item = &debuginator->item_buffer[debuginator->item_buffer_size++];
	}

	memset(item, 0, sizeof(*item));
	return item;
}

void debuginator_set_title(DebuginatorItem* item, const char* title, size_t title_length) {
	if (title_length == 0) {
		title_length = strlen(title);
	}

	if (title_length >= DEBUGINATOR_max_title_length) {
#pragma warning(suppress: 4996)
		strncpy(item->title, title, DEBUGINATOR_max_title_length - 3);
		item->title[DEBUGINATOR_max_title_length - 3] = '.';
		item->title[DEBUGINATOR_max_title_length - 2] = '.';
		item->title[DEBUGINATOR_max_title_length - 1] = '\0';
	}
	else {
#pragma warning(suppress: 4996)
		strncpy(item->title, title, title_length);
	}
}

void debuginator_set_parent(DebuginatorItem* item, DebuginatorItem* parent) {
	if (parent == NULL)
		return;

	DEBUGINATOR_assert(item->parent == NULL || item->parent == parent);
	item->parent = parent;
	if (parent->folder.first_child == NULL) {
		parent->folder.first_child = item;
	}
	else {
		DebuginatorItem* last_sibling = parent->folder.first_child;
		while (last_sibling != NULL)
		{
			if (last_sibling == item) {
				// Item was already in parent
				return;
			}

			if (last_sibling->next_sibling == NULL) {
				// Found the last child, set item as the new last one
				last_sibling->next_sibling = item;
				return;
			}

			last_sibling = last_sibling->next_sibling;
		}
	}
}

DebuginatorItem* debuginator_new_folder_item(TheDebuginator* debuginator, DebuginatorItem* parent, const char* title, size_t title_length) {
	DEBUGINATOR_assert(debuginator->item_buffer_size < debuginator->item_buffer_capacity);
	DebuginatorItem* folder_item;
	if (debuginator->free_list_size > 0) {
		folder_item = &debuginator->item_buffer[--debuginator->free_list_size];
	}
	else {
		folder_item = &debuginator->item_buffer[debuginator->item_buffer_size++];
	}

	folder_item->is_folder = true;
	debuginator_set_title(folder_item, title, title_length);
	debuginator_set_parent(folder_item, parent);
	return folder_item;
}

DebuginatorItem* debuginator_get_item(TheDebuginator* debuginator, DebuginatorItem* parent, const char* path, bool create_if_not_exist) {
	parent = parent == NULL ? debuginator->root : parent;
	const char* temp_path = path;
	while (true) {
		const char* next_slash = strchr(temp_path, '/');
		size_t path_part_length = next_slash ? next_slash - temp_path : strlen(temp_path);

		DebuginatorItem* current_item = NULL;
		DebuginatorItem* parent_child = parent->folder.first_child;
		while (parent_child) {
			const char* item_title = parent_child->title;
			size_t title_length = strlen(item_title); // strlen :(
			if (path_part_length >= DEBUGINATOR_max_title_length 
				&& title_length == DEBUGINATOR_max_title_length - 1 
				&& item_title[DEBUGINATOR_max_title_length - 2] == '.'
				&& item_title[DEBUGINATOR_max_title_length - 3] == '.') {
				path_part_length = title_length = DEBUGINATOR_max_title_length - 3;
			}

			if (title_length == path_part_length && memcmp(parent_child->title, temp_path, title_length * sizeof(char)) == 0) {
				current_item = parent_child;
				break;
			}

			parent_child = parent_child->next_sibling;
		}

		if (current_item == NULL && !create_if_not_exist) {
			return NULL;
		}

		// If current_item is set, it means the item already existed and we're just going to reuse it
		if (next_slash == NULL) {
			// Found the last part of the path
			if (current_item == NULL) {
				current_item = debuginator_get_free_item(debuginator);
				debuginator_set_title(current_item, temp_path, 0);
				debuginator_set_parent(current_item, parent);
			}
			
			return current_item;
		}
		else {
			// Found a folder
			if (current_item == NULL) {
				// Parent item doesn't exist yet
				parent = debuginator_new_folder_item(debuginator, parent, temp_path, next_slash - temp_path);
			}
			else {
				parent = current_item;
			}
			temp_path = next_slash + 1;
		}
	}

	DEBUGINATOR_assert(false);
	return NULL;
}

DebuginatorItem* debuginator_create_array_item(TheDebuginator* debuginator,
	DebuginatorItem* parent, const char* path, const char* description,
	DebuginatorOnItemChangedCallback on_item_changed_callback, void* user_data,
	const char** value_titles, void* values, int num_values, size_t value_size) {

	DebuginatorItem* item = debuginator_get_item(debuginator, parent, path, true);
	item->is_folder = false;
	item->leaf.num_values = num_values;
	item->leaf.values = values;
	item->leaf.array_element_size = value_size;
	item->leaf.value_titles = value_titles;
	item->leaf.on_item_changed_callback = on_item_changed_callback;
	item->user_data = user_data;
	item->leaf.description = description;

	if (item->leaf.hot_index >= num_values) {
		item->leaf.hot_index = num_values - 1;
	}

	//TODO preserve hot item
	return item;
}

void debuginator_set_hot_item(TheDebuginator* debuginator, const char* path) {
	DebuginatorItem* item = debuginator_get_item(debuginator, NULL, path, false);
	if (item == NULL) {
		return;
	}

	debuginator->hot_item = item;
	item->parent->folder.hot_child = item;
}

void debuginator_remove_item(TheDebuginator* debuginator, const char* path) {
	DebuginatorItem* item = debuginator_get_item(debuginator, NULL, path, false);
	if (item == NULL) {
		return;
	}

	DebuginatorItem* parent = item->parent;
	DebuginatorItem* parent_child = parent->folder.first_child;
	DebuginatorItem* previous_child = NULL;
	while (true) {
		if (parent_child == item) {
			if (previous_child == NULL) {
				parent->folder.first_child = item->next_sibling;
			}
			else {
				previous_child->next_sibling = item->next_sibling;
			}
			break;
		}

		previous_child = parent_child;
		parent_child = parent_child->next_sibling;
	}

	if (parent->folder.hot_child == item) {
		if (item->next_sibling == NULL) {
			parent->folder.hot_child = parent->folder.first_child;
		}
		else {
			parent->folder.hot_child = item->next_sibling;
		}
	}

	if (debuginator->hot_item == item) {
		if (parent->folder.hot_child == NULL) {
			debuginator->hot_item = parent;
		}
		else {
			debuginator->hot_item = parent->folder.hot_child;
		}
	}
}

TheDebuginator debuginator_create(DebuginatorItem* item_buffer, size_t item_buffer_capacity) {
	TheDebuginator debuginator;
	memset(&debuginator, 0, sizeof(debuginator));
	debuginator.item_buffer_capacity = item_buffer_capacity;
	debuginator.item_buffer = item_buffer;
	memset(item_buffer, 0, sizeof(DebuginatorItem) * item_buffer_capacity);
	DebuginatorItem* item = debuginator_new_folder_item(&debuginator, NULL, "Menu Root", 0);
	debuginator.root = item;
	return debuginator;
}

void debuginator_print(DebuginatorItem* item, int indentation) {
	DEBUGINATOR_debug_print("%*s%s\n", indentation, "", item->title);
	if (item->is_folder) {
		item = item->folder.first_child;
		while (item) {
			debuginator_print(item, indentation + 4);
			item = item->next_sibling;
		}
	}
	else {
		for (size_t i = 0; i < item->leaf.num_values; i++) {
			DEBUGINATOR_debug_print("%*s%s\n", indentation + 4, "", item->leaf.value_titles[i]);
		}
	}
}

void debuginator_initialize(TheDebuginator* debuginator) {
	debuginator->hot_item = debuginator->root->folder.first_child;
	debuginator->root->folder.hot_child = debuginator->root->folder.first_child;
	//if (debuginator->hot_item->!is_folder) {
	//	debuginator->hot_item->is
	//}
	debuginator_print(debuginator->root, 0);
	
}

//██╗███╗   ██╗██████╗ ██╗   ██╗████████╗
//██║████╗  ██║██╔══██╗██║   ██║╚══██╔══╝
//██║██╔██╗ ██║██████╔╝██║   ██║   ██║
//██║██║╚██╗██║██╔═══╝ ██║   ██║   ██║
//██║██║ ╚████║██║     ╚██████╔╝   ██║
//╚═╝╚═╝  ╚═══╝╚═╝      ╚═════╝    ╚═╝

static void debuginator_activate(DebuginatorItem* item) {
	item->leaf.active_index = item->leaf.hot_index;
	void* value = ((char*)item->leaf.values) + item->leaf.hot_index * item->leaf.array_element_size;
	item->leaf.on_item_changed_callback(item, value, item->leaf.value_titles[item->leaf.hot_index]);
}

void debuginator_move_sibling_previous(TheDebuginator* debuginator) {
	// This is a bit stupid, consider changing to doubly linked list
	DebuginatorItem* hot_item = debuginator->hot_item;

	if (!hot_item->is_folder && hot_item->leaf.is_active) {
		if (--hot_item->leaf.hot_index < 0) {
			hot_item->leaf.hot_index = hot_item->leaf.num_values - 1;
		}
	}
	else {
		DebuginatorItem* hot_item_new = debuginator->hot_item;
		DebuginatorItem* parent_child = hot_item_new->parent->folder.first_child;
		if (parent_child == hot_item_new) {
			while (parent_child) {
				hot_item_new = parent_child;
				parent_child = parent_child->next_sibling;
			}
		}
		else {
			while (parent_child) {
				if (parent_child->next_sibling == hot_item_new) {
					hot_item_new = parent_child;
					break;
				}

				parent_child = parent_child->next_sibling;
			}
		}

		if (hot_item != hot_item_new) {
			hot_item_new->parent->folder.hot_child = hot_item_new;
			debuginator->hot_item = hot_item_new;
		}
	}
}

void debuginator_move_sibling_next(TheDebuginator* debuginator) {
	DebuginatorItem* hot_item = debuginator->hot_item;

	if (!hot_item->is_folder && hot_item->leaf.is_active) {
		if (++hot_item->leaf.hot_index == hot_item->leaf.num_values) {
			hot_item->leaf.hot_index = 0;
		}
	}
	else {
		DebuginatorItem* hot_item_new = debuginator->hot_item;
		if (hot_item->next_sibling != NULL) {
			hot_item_new = hot_item->next_sibling;
		}
		else {
			hot_item_new = hot_item->parent->folder.first_child;
		}

		if (hot_item != hot_item_new) {
			hot_item_new->parent->folder.hot_child = hot_item_new;
			debuginator->hot_item = hot_item_new;
		}
	}
}

void debuginator_move_to_child(TheDebuginator* debuginator) {
	DebuginatorItem* hot_item = debuginator->hot_item;
	DebuginatorItem* hot_item_new = debuginator->hot_item;

	if (!hot_item->is_folder) {
		if (hot_item->leaf.is_active) {
			debuginator_activate(debuginator->hot_item);
		}
		else {
			hot_item->leaf.is_active = true;
		}
	}
	else {
		if (hot_item->folder.hot_child != NULL) {
			hot_item_new = hot_item->folder.hot_child;
		}
		else if (hot_item->folder.first_child != NULL) {
			hot_item_new = hot_item->folder.first_child;
			hot_item_new->parent->folder.hot_child = hot_item_new;
		}

		if (hot_item != hot_item_new) {
			hot_item_new->parent->folder.hot_child = hot_item_new;
			debuginator->hot_item = hot_item_new;
		}
	}
}

void debuginator_move_to_parent(TheDebuginator* debuginator) {
	DebuginatorItem* hot_item = debuginator->hot_item;
	DebuginatorItem* hot_item_new = debuginator->hot_item;
	if (!hot_item->is_folder && hot_item->leaf.is_active) {
		hot_item->leaf.is_active = false;
	}
	else if (hot_item->parent != debuginator->root) {
		hot_item_new = debuginator->hot_item->parent;
	}

	if (hot_item != hot_item_new) {
		hot_item_new->parent->folder.hot_child = hot_item_new;
		debuginator->hot_item = hot_item_new;
	}
}

typedef struct DebuginatorInput {
	//bool activate;
	bool move_sibling_previous;
	bool move_sibling_next;
	bool move_to_parent;
	bool move_to_child;
} DebuginatorInput;

void debug_menu_handle_input(TheDebuginator* debuginator, DebuginatorInput* input) {
	if (input->move_sibling_next) {
		debuginator_move_sibling_next(debuginator);
	}

	if (input->move_to_child) {
		debuginator_move_to_child(debuginator);
	}

	if (input->move_to_parent) {
		debuginator_move_to_parent(debuginator);		
	}

	// HACK
	//if (hot_item != hot_item_new) {
		//debuginator->hot_item = hot_item_new;
	//}

	//if (input->activate && hot_item->!is_folder) {
	//	debuginator_activate(debuginator->hot_item);		
	//}
}

// ██╗   ██╗████████╗██╗██╗     ██╗████████╗██╗   ██╗
// ██║   ██║╚══██╔══╝██║██║     ██║╚══██╔══╝╚██╗ ██╔╝
// ██║   ██║   ██║   ██║██║     ██║   ██║    ╚████╔╝
// ██║   ██║   ██║   ██║██║     ██║   ██║     ╚██╔╝
// ╚██████╔╝   ██║   ██║███████╗██║   ██║      ██║
//  ╚═════╝    ╚═╝   ╚═╝╚══════╝╚═╝   ╚═╝      ╚═╝

void debuginator_copy_1byte(DebuginatorItem* item, void* value, const char* value_title) {
	(void)value_title;
	memcpy(item->user_data, value, 1);
}

void debuginator_create_bool_item(TheDebuginator* debuginator, const char* path, const char* description, void* user_data) {
	static bool bool_values[2] = { true, false }; 
	static const char* bool_titles[2] = { "True", "False" };
	DEBUGINATOR_assert(sizeof(bool_values[0]) == 1);
	debuginator_create_array_item(debuginator, NULL, path,
		description, debuginator_copy_1byte, user_data,
		bool_titles, bool_values, 2, sizeof(bool_values[0]));
}

#endif // INCLUDE_THE_DEBUGINATOR_H
