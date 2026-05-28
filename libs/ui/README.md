# UI Library

A modern, object-oriented widget system for Esposito apps. Provides reusable UI components with automatic event handling, consistent styling, and callback-based interaction.

## Overview

The UI library eliminates manual coordinate checking and event parsing from apps. Widgets handle their own drawing, touch detection, and event routing, making apps simpler and more consistent.

## Widget Types

### Button Widget (`ui_button.h`)

Clickable buttons with automatic touch detection and keyboard shortcuts.

**Features:**
- Automatic touch/keyboard event handling
- Configurable colors and styling
- Callback-based click handling
- Support for both touch and keyboard (Enter) activation

**Example:**
```c
ui_button_t *btn = ui_button_create(x, y, width, height, "Click Me");
ui_button_set_callback(btn, my_click_handler, user_data);
ui_button_draw(btn);

// In event loop:
ui_button_handle_touch(btn, touch_event);
```

### List Widget (`ui_list.h`)

Scrollable, selectable item lists with integrated titles and scrollbars.

**Features:**
- Automatic scroll management
- Touch and keyboard navigation (W/S keys, Enter to select)
- Integrated title support
- Visual selection highlighting
- Optional scrollbar indicator

**Example:**
```c
ui_list_widget_t *list = ui_list_create(x, y, width, height);
ui_list_set_title(list, "Select Item");
ui_list_set_items(list, items, item_count);
ui_list_set_callbacks(list, on_selection_changed, on_item_selected, user_data);
ui_list_draw(list);

// In event loop:
ui_list_handle_key(list, key);
ui_list_handle_touch(list, touch_event);
```

### Toolbar Widget (`ui_toolbar.h`)

Horizontal container that arranges buttons evenly across the screen width.

**Features:**
- Auto-calculates button widths to fill available columns
- Single call to draw and handle touch for all buttons
- Access individual buttons via `ui_toolbar_get_button()`

**Example:**
```c
const char *labels[] = {"\xE2\x96\xB2", "\xE2\x9C\x93", "\xE2\x96\xBC"};  // ▲ ✓ ▼
ui_toolbar_t *tb = ui_toolbar_create(rows - 1, 1, 3, labels);
ui_button_set_callback(ui_toolbar_get_button(tb, 0), on_up, app);
ui_button_set_callback(ui_toolbar_get_button(tb, 1), on_ok, app);
ui_button_set_callback(ui_toolbar_get_button(tb, 2), on_down, app);
ui_toolbar_draw(tb);

// In event loop:
ui_toolbar_handle_touch(tb, touch_event);
```

### Progress Bar Widget (`ui_progressbar.h`)

Horizontal progress bar with percentage display.

**Features:**
- Configurable foreground/background/border colors
- Optional percentage text overlay
- Simple set_value/redraw cycle

**Example:**
```c
ui_progressbar_t *bar = ui_progressbar_create(2, 10, 40);
ui_progressbar_draw(bar);

// Update:
ui_progressbar_set_value(bar, bytes_done, bytes_total);
ui_progressbar_draw(bar);
```

### Text Input Widget (`ui_text_input.h`)

Keyboard-based text entry fields with masking and validation support.

**Features:**
- Automatic text editing (backspace, character input)
- Password masking support
- Enter/ESC callback handling
- Configurable labels and hints
- Automatic redraw on input

**Example:**
```c
ui_text_input_widget_t *input = ui_text_input_create(x, y, width, height);
ui_text_input_set_title(input, "Enter Text");
ui_text_input_set_label(input, "Value:");
ui_text_input_set_buffer(input, buffer, buffer_size);
ui_text_input_set_callbacks(input, NULL, on_confirm, on_cancel, user_data);
ui_text_input_draw(input);

// In event loop:
ui_text_input_handle_key(input, key);
```

## Common Patterns

### Widget Lifecycle

1. **Create** - Widget constructor with position and size
2. **Configure** - Set titles, colors, callbacks, data
3. **Draw** - Render widget to screen
4. **Handle Events** - Route touch/keyboard events to widget
5. **Destroy** - Clean up widget resources when done

### Event Handling

Widgets use a consistent event handling pattern:

```c
// In your app's event handler:
if (event->type == EVENT_TOUCH && event->touch.pressed) {
    // Try widgets first
    if (ui_button_handle_touch(my_button, event)) return;
    if (ui_list_handle_touch(my_list, event)) return;
}

if (event->type == EVENT_KEYBOARD && event->keyboard.pressed) {
    char key = event->keyboard.key;
    
    // Route to active widget
    if (ui_text_input_handle_key(my_input, key)) {
        ui_text_input_draw(my_input);
        text_mode_flush();
        return;
    }
    
    // Handle other keys...
}
```

### Callbacks

Most widgets support callbacks for user interactions:

```c
void on_button_click(ui_button_t *button, void *user_data) {
    app_state_t *app = (app_state_t*)user_data;
    // Handle button click
}

void on_list_select(ui_list_widget_t *list, int index, void *user_data) {
    app_state_t *app = (app_state_t*)user_data;
    // Handle list item selection
}
```

## Memory Management

- Widgets are allocated with `malloc()` and must be freed with their respective destroy functions
- Widget data (strings, buffers) is typically owned by the app, not the widget
- Always destroy widgets in your app's `app_close()` function

```c
void app_close(app_context_t *ctx) {
    // Clean up widgets
    if (my_button) {
        free(my_button);
        my_button = NULL;
    }
    if (my_list) {
        ui_list_destroy(my_list);
        my_list = NULL;
    }
    if (my_input) {
        ui_text_input_destroy(my_input);
        my_input = NULL;
    }
}
```

## Styling

Widgets use consistent color schemes:

- **Text**: `TEXT_COLOR_WHITE`, `TEXT_COLOR_BRIGHT_CYAN`, etc.
- **Backgrounds**: `TEXT_COLOR_BLACK`, `TEXT_COLOR_BLUE`, `TEXT_COLOR_GREEN`
- **Borders**: `TEXT_COLOR_CYAN` for most widgets
- **Selection**: `TEXT_COLOR_GREEN` background with white text

See `text_mode.h` for available color constants.

## Coordinate System

- Screen: 320×240 pixels
- Text grid: 64×30 characters
- Character size: 5×8 pixels (5 wide, 8 tall)
- Touch events are automatically converted from pixels to character coordinates

## Migration from Old UI

The old UI system required manual coordinate checking:

```c
// OLD WAY (manual)
if (x_col >= state->btn_x && x_col < state->btn_x + state->btn_w) {
    if (y_col == state->btn_y) {
        handle_click();
    }
}
```

The new system is automatic:

```c
// NEW WAY (automatic)
ui_button_handle_touch(state->btn, &event);
```

## Examples

See the following apps for complete examples:
- **Reader** - Button widgets, list widgets, text input (search/goto)
- **File Manager** - Text input widget for renaming
- **Settings** - Multiple text input widgets for configuration

## Best Practices

1. **Create widgets once** - Create widgets during initialization, not on every redraw
2. **Use callbacks** - Let widgets handle their own events instead of manual checking
3. **Check widget visibility** - Only draw/handle events for visible widgets
4. **Clean up properly** - Always destroy widgets when closing your app
5. **Test on device** - Touch coordinates are device-specific, always test on real hardware

## API Reference

See individual header files for complete API documentation:
- `ui_button.h` - Button widget API
- `ui_list.h` - List widget API  
- `ui_text_input.h` - Text input widget API
- `ui_toolbar.h` - Toolbar container widget API
- `ui_progressbar.h` - Progress bar widget API
- `ui.h` - Legacy utility functions (windows, labels, etc.)