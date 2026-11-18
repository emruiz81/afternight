# The kinds of an extension's panel

<!-- Generated from src/AfterNightTests/Test_ControlsExtensionSeam/data/PanelKinds.txt by build/scripts/tools/panel_kinds_reference.py: edit the record (and the kinds it records), then write this page again with --write. -->

An extension describes its controls as data, and AfterNight draws them. A process window's controls are the list its
`get_params()` answers; a panel sent through the protocol, and the settings section an extension's
`get_settings_params()` list is drawn as, are a tree of widgets. Both are read into one description: a tree of nodes,
each a kind with the fields it states and, for a group, what it holds. This page lists every kind, each field, what the
field holds, what its absence means and how long it may be. A description is refused whole at the first thing it
breaks, and the reason is said: a kind or a field this page does not list, a value of the wrong type, a text or a list
past its bound, a range whose least is above its most, a choice with no entries. Nothing of a refused one is drawn.

## Bounds

| Bound | Value | What it bounds |
|---|---|---|
| nodes | 512 | How many nodes a description may hold, groups and what they hold counted. |
| depth | 32 | How deep groups may nest. |
| short text | 256 | The characters of an id, a label, a title or a placeholder. |
| long text | 8192 | The characters of a prose text: a banner's message, a tooltip, a description. |
| console text | 262144 | The characters of a console's text. |
| entries of a choice | 1024 | The entries of a choice, a combo box or a button group. |
| entries of a list | 4096 | The entries of any other list: points, ranges, rows. |

## A process window's list of parameters

The entries of the list, in order. A `meta` entry's `panel_variant` (`native`, or `custom` for the custom look) is the
panel's; the rest of `meta` is the window's own and is not listed here. `section` opens a section in the page or at the
top, and `tabs` a set of tabs whose `tab` entries are its pages, until `end_tabs`. A parameter, an action, a readout or
a hosted control with no section open before it opens one, titled Parameters (Creative Controls in the custom look). An
entry with no id, or of a type not listed here, is passed over, as the window has always passed over it.

### `float` (also `double`)

A real number: a spin box beside a slider. Role: parameter.

| Field | Holds | When absent | Bound | What it does |
|---|---|---|---|---|
| `id` | text | - | 256 characters | The name the extension knows the control by: a parameter's key, an action's, the target of a patch. |
| `label` | text | the id | 256 characters | The text beside the control. |
| `tooltip` | text | - | 8192 characters | Shown when the pointer rests on the control. |
| `default` | number | 0 | - | The value the control starts at and resets to. |
| `min` | number | -1000000 | - | The least value. |
| `max` | number | 1000000 | - | The greatest value. |
| `step` | number | 0.01 | - | The step of the spin box; it also sets how many decimals are shown, from two to six. |
| `tracking` | boolean | true | - | Whether the slider changes the value while it is dragged. |
| `slider_tracking` | boolean | true | - | The older name of `tracking`. |
| `preview_invalidates` | boolean | false | - | A change marks a preview refreshed by hand as out of date instead of asking for a frame. |
| `visible_when` | condition | always | 1024 characters | Shows the control only while the expression over the parameters holds. |
| `enabled_when` | condition | always | 1024 characters | Enables the control only while the expression holds, or while a parameter equals (`equals`) or differs from (`not_equals`) a value. |

### `int`

A whole number: a spin box beside a slider. Its numbers are read as whole numbers. Role: parameter.

| Field | Holds | When absent | Bound | What it does |
|---|---|---|---|---|
| `id` | text | - | 256 characters | The name the extension knows the control by: a parameter's key, an action's, the target of a patch. |
| `label` | text | the id | 256 characters | The text beside the control. |
| `tooltip` | text | - | 8192 characters | Shown when the pointer rests on the control. |
| `default` | number | 0 | - | The value the control starts at and resets to. |
| `min` | number | -1000 | - | The least value. |
| `max` | number | 1000 | - | The greatest value. |
| `step` | number | 1 | - | The step of the spin box; at least 1. |
| `tracking` | boolean | true | - | Whether the slider changes the value while it is dragged. |
| `slider_tracking` | boolean | true | - | The older name of `tracking`. |
| `preview_invalidates` | boolean | false | - | A change marks a preview refreshed by hand as out of date instead of asking for a frame. |
| `visible_when` | condition | always | 1024 characters | Shows the control only while the expression over the parameters holds. |
| `enabled_when` | condition | always | 1024 characters | Enables the control only while the expression holds, or while a parameter equals (`equals`) or differs from (`not_equals`) a value. |

### `bool`

A check box. Role: parameter.

| Field | Holds | When absent | Bound | What it does |
|---|---|---|---|---|
| `id` | text | - | 256 characters | The name the extension knows the control by: a parameter's key, an action's, the target of a patch. |
| `label` | text | the id | 256 characters | The text beside the control. |
| `tooltip` | text | - | 8192 characters | Shown when the pointer rests on the control. |
| `default` | boolean | false | - | Whether it starts checked. |
| `preview_invalidates` | boolean | false | - | A change marks a preview refreshed by hand as out of date instead of asking for a frame. |
| `visible_when` | condition | always | 1024 characters | Shows the control only while the expression over the parameters holds. |
| `enabled_when` | condition | always | 1024 characters | Enables the control only while the expression holds, or while a parameter equals (`equals`) or differs from (`not_equals`) a value. |

### `choice`

One value of a list: a combo box. Its value is the chosen entry's value, as text. Role: parameter.

| Field | Holds | When absent | Bound | What it does |
|---|---|---|---|---|
| `id` | text | - | 256 characters | The name the extension knows the control by: a parameter's key, an action's, the target of a patch. |
| `label` | text | the id | 256 characters | The text beside the control. |
| `tooltip` | text | - | 8192 characters | Shown when the pointer rests on the control. |
| `default` | value | the first entry | 256 characters | The value of the entry chosen at the start. |
| `options` | entries | - | 1024 entries | The entries, as `[label, value]` pairs; at least one. |
| `inline_actions` | actions | - | 16 entries | Buttons beside the combo box, each an action of its own. |
| `preview_invalidates` | boolean | false | - | A change marks a preview refreshed by hand as out of date instead of asking for a frame. |
| `visible_when` | condition | always | 1024 characters | Shows the control only while the expression over the parameters holds. |
| `enabled_when` | condition | always | 1024 characters | Enables the control only while the expression holds, or while a parameter equals (`equals`) or differs from (`not_equals`) a value. |

### `string`

A line of text. Role: parameter.

| Field | Holds | When absent | Bound | What it does |
|---|---|---|---|---|
| `id` | text | - | 256 characters | The name the extension knows the control by: a parameter's key, an action's, the target of a patch. |
| `label` | text | the id | 256 characters | The text beside the control. |
| `tooltip` | text | - | 8192 characters | Shown when the pointer rests on the control. |
| `default` | text | - | 8192 characters | The text it starts with. |
| `placeholder` | text | the label | 256 characters | Shown while the line is empty. |
| `preview_invalidates` | boolean | false | - | A change marks a preview refreshed by hand as out of date instead of asking for a frame. |
| `visible_when` | condition | always | 1024 characters | Shows the control only while the expression over the parameters holds. |
| `enabled_when` | condition | always | 1024 characters | Enables the control only while the expression holds, or while a parameter equals (`equals`) or differs from (`not_equals`) a value. |

### `view_selector`

One of the open images: a combo box of their names, listed by the process that shows the panel. Its value is the chosen image's name. Role: parameter.

| Field | Holds | When absent | Bound | What it does |
|---|---|---|---|---|
| `id` | text | - | 256 characters | The name the extension knows the control by: a parameter's key, an action's, the target of a patch. |
| `label` | text | the id | 256 characters | The text beside the control. |
| `tooltip` | text | - | 8192 characters | Shown when the pointer rests on the control. |
| `default` | text | - | 256 characters | The value of the empty entry. |
| `placeholder` | text | Select image | 256 characters | The text of the empty entry. |
| `channel_filter` | whole numbers | every count | 8 entries | The channel counts an image may have to be listed. |
| `match_target_geometry` | boolean | false | - | Lists only images of the target's width and height. |
| `match_target_channels` | boolean | false | - | Lists only images with as many channels as the target. |
| `exclude_target` | boolean | false | - | Leaves the target out. |
| `preview_invalidates` | boolean | false | - | A change marks a preview refreshed by hand as out of date instead of asking for a frame. |
| `visible_when` | condition | always | 1024 characters | Shows the control only while the expression over the parameters holds. |
| `enabled_when` | condition | always | 1024 characters | Enables the control only while the expression holds, or while a parameter equals (`equals`) or differs from (`not_equals`) a value. |

### `curve_editor`

A tone curve the person drags, over the image's histogram. Its value is its points. Role: parameter.

| Field | Holds | When absent | Bound | What it does |
|---|---|---|---|---|
| `id` | text | - | 256 characters | The name the extension knows the control by: a parameter's key, an action's, the target of a patch. |
| `label` | text | the id | 256 characters | The text beside the control. |
| `tooltip` | text | - | 8192 characters | Shown when the pointer rests on the control. |
| `default` | points | the straight line | 4096 entries | The points it starts with, in [0, 1]. |
| `interpolation` | text | akima | 256 characters | How the curve passes through its points: `akima`, `linear`, `cubic` or `steffen`. |
| `clipping_info` | boolean | true | - | Shows the clipped share at each end. |
| `grid` | boolean | true | - | Draws the grid. |
| `guide` | boolean | true | - | Draws the guide line. |
| `histogram` | boolean | true | - | Draws the histogram behind the curve. |
| `histogram_areas` | boolean | false | - | Fills the histogram's areas. |
| `preview_invalidates` | boolean | false | - | A change marks a preview refreshed by hand as out of date instead of asking for a frame. |
| `visible_when` | condition | always | 1024 characters | Shows the control only while the expression over the parameters holds. |
| `enabled_when` | condition | always | 1024 characters | Enables the control only while the expression holds, or while a parameter equals (`equals`) or differs from (`not_equals`) a value. |

### `action` (also `button`)

A button; the extension hears of a press and may answer with parameters to change. Role: action.

| Field | Holds | When absent | Bound | What it does |
|---|---|---|---|---|
| `id` | text | - | 256 characters | The name the extension hears the press by. |
| `label` | text | the id | 256 characters | The text beside the control. |
| `tooltip` | text | - | 8192 characters | Shown when the pointer rests on the control. |
| `button_role` | text | secondary | 256 characters | How the button is drawn: `primary`, `secondary` or `danger`. |
| `role` | text | - | 256 characters | An older name of `button_role`. |
| `variant` | text | - | 256 characters | An older name of `button_role`. |
| `preview_refresh_role` | boolean | false | - | The button refreshes a preview the person refreshes by hand. |
| `preview_refresh_on_open` | boolean | false | - | Such a refresh also runs once as the panel opens. |
| `visible_when` | condition | always | 1024 characters | Shows the control only while the expression over the parameters holds. |
| `enabled_when` | condition | always | 1024 characters | Enables the control only while the expression holds, or while a parameter equals (`equals`) or differs from (`not_equals`) a value. |

### `section`

A titled group of the controls after it: a card, or a section that folds. Role: group; it holds other nodes.

| Field | Holds | When absent | Bound | What it does |
|---|---|---|---|---|
| `id` | text | - | 256 characters | The section's name, for a patch. |
| `label` | text | the id | 256 characters | The text beside the control. |
| `text` | text | the label | 256 characters | The title. |
| `presentation` | text | card | 256 characters | `card`, or `collapsible` for a section that folds; every section folds in the custom look. |
| `visible_when` | condition | always | 1024 characters | Shows the control only while the expression over the parameters holds. |
| `enabled_when` | condition | always | 1024 characters | Enables the control only while the expression holds, or while a parameter equals (`equals`) or differs from (`not_equals`) a value. |

### `tabs`

A set of tabs; each `tab` after it is a page, until `end_tabs`. Role: group; it holds other nodes.

| Field | Holds | When absent | Bound | What it does |
|---|---|---|---|---|
| `id` | text | - | 256 characters | The set's name, for a patch. |
| `visible_when` | condition | always | 1024 characters | Shows the control only while the expression over the parameters holds. |
| `enabled_when` | condition | always | 1024 characters | Enables the control only while the expression holds, or while a parameter equals (`equals`) or differs from (`not_equals`) a value. |

### `tab`

A page of a set of tabs, holding the controls after it. Role: group; it holds other nodes.

| Field | Holds | When absent | Bound | What it does |
|---|---|---|---|---|
| `id` | text | - | 256 characters | The page's name, for a patch. |
| `label` | text | the id | 256 characters | The text beside the control. |
| `text` | text | the label | 256 characters | The tab's title. |

### `info`

A banner of text. One marked as the extension's credits is shown with the process's description instead. Role: display.

| Field | Holds | When absent | Bound | What it does |
|---|---|---|---|---|
| `id` | text | - | 256 characters | The banner's name; `attribution`, `credits` or `credit` marks the credits. |
| `label` | text | the id | 256 characters | The text, when `text` is absent. |
| `text` | text | the label | 8192 characters | The banner's text. |
| `tone` | text | info | 256 characters | `info`, `success`, `warning` or `error`; `warning` when absent in the custom look. |
| `role` | text | - | 256 characters | `attribution` or `credits` marks the credits. |
| `purpose` | text | - | 256 characters | `attribution` or `credits` marks the credits. |
| `preview_status` | boolean | false | - | The banner says whether a preview refreshed by hand is current. |
| `stale_text` | text | Preview update needed. | 256 characters | Its text while the preview is out of date. |
| `ready_text` | text | Preview is up to date. | 256 characters | Its text while the preview is current. |
| `updating_text` | text | Updating preview... | 256 characters | Its text while the preview is refreshed. |

### `percentage_mix_label`

A readout of a blend: `75% Ha, 25% OIII` for a parameter at 0.25. Role: display.

| Field | Holds | When absent | Bound | What it does |
|---|---|---|---|---|
| `id` | text | - | 256 characters | The readout's name. |
| `label` | text | the id | 256 characters | The text beside the control. |
| `text` | text | the label | 256 characters | Its text until the parameter is read. |
| `tooltip` | text | - | 8192 characters | Shown when the pointer rests on the control. |
| `source` | text | - | 256 characters | The parameter read, from 0 (all left) to 1 (all right). |
| `left_label` | text | Ha | 256 characters | The name of the left share. |
| `right_label` | text | OIII | 256 characters | The name of the right share. |
| `visible_when` | condition | always | 1024 characters | Shows the control only while the expression over the parameters holds. |
| `enabled_when` | condition | always | 1024 characters | Enables the control only while the expression holds, or while a parameter equals (`equals`) or differs from (`not_equals`) a value. |

### `value_description_label`

A readout of a parameter's value, with the words of the range it falls in. Role: display.

| Field | Holds | When absent | Bound | What it does |
|---|---|---|---|---|
| `id` | text | - | 256 characters | The readout's name. |
| `label` | text | the id | 256 characters | The text beside the control. |
| `text` | text | the label | 256 characters | Its text until the parameter is read. |
| `tooltip` | text | - | 8192 characters | Shown when the pointer rests on the control. |
| `source` | text | - | 256 characters | The parameter read. |
| `decimals` | number | 2 | - | The decimals shown, from 0 to 6. |
| `prefix` | text | - | 256 characters | Written before the value. |
| `suffix` | text | - | 256 characters | Written after the value. |
| `separator` | text |  -  | 256 characters | Written between the value and the range's words. |
| `ranges` | ranges | - | 256 entries | `[least, most, words]` for each range. |
| `visible_when` | condition | always | 1024 characters | Shows the control only while the expression over the parameters holds. |
| `enabled_when` | condition | always | 1024 characters | Enables the control only while the expression holds, or while a parameter equals (`equals`) or differs from (`not_equals`) a value. |

### `rt_histogram`

A histogram with its toolbar. The first is the preview's, made with each frame; any other is counted from what is shown. Role: display.

| Field | Holds | When absent | Bound | What it does |
|---|---|---|---|---|
| `id` | text | - | 256 characters | The histogram's name. |
| `tooltip` | text | - | 8192 characters | Shown when the pointer rests on the control. |
| `height` | number | 220 | - | Its least height, at least 120. |
| `clipping_info` | boolean | true | - | Shows the clipped share at each end. |
| `grid` | boolean | true | - | Draws the grid. |
| `local_channel_normalization` | boolean | false | - | Scales each channel to its own peak. |
| `histogram_areas` | boolean | false | - | Fills the histogram's areas. |
| `show_clipping_info_control` | boolean | true | - | Puts the clipping button in the toolbar. |
| `visible_when` | condition | always | 1024 characters | Shows the control only while the expression over the parameters holds. |
| `enabled_when` | condition | always | 1024 characters | Enables the control only while the expression holds, or while a parameter equals (`equals`) or differs from (`not_equals`) a value. |

### `vector_slope`

A graph of hue vectors drawn from pairs of parameters. It sits in the page, beside the sections. Role: display.

| Field | Holds | When absent | Bound | What it does |
|---|---|---|---|---|
| `id` | text | - | 256 characters | The graph's name. |
| `label` | text | the id | 256 characters | The title above the graph; none when empty. |
| `tooltip` | text | - | 8192 characters | Shown when the pointer rests on the control. |
| `height` | number | 220 | - | The graph's height. |
| `vectors` | vectors | the hue wheel's six | 64 entries | The vectors: each its key, its hue and saturation parameters, its base hue and colour. |
| `visible_when` | condition | always | 1024 characters | Shows the control only while the expression over the parameters holds. |
| `enabled_when` | condition | always | 1024 characters | Enables the control only while the expression holds, or while a parameter equals (`equals`) or differs from (`not_equals`) a value. |

### `target_selector`

Where the process that shows the panel lets the person choose the image the process runs on. Role: hosted.

| Field | Holds | When absent | Bound | What it does |
|---|---|---|---|---|
| `id` | text | - | 256 characters | The selector's name. |
| `label` | text | the id | 256 characters | The text beside it. |
| `tooltip` | text | - | 8192 characters | Shown when the pointer rests on the control. |
| `visible_when` | condition | always | 1024 characters | Shows the control only while the expression over the parameters holds. |
| `enabled_when` | condition | always | 1024 characters | Enables the control only while the expression holds, or while a parameter equals (`equals`) or differs from (`not_equals`) a value. |

### `manual_mask_editor`

A mask the person paints over the image, drawn by the process that shows the panel. Its use is a parameter of its own. Role: hosted.

| Field | Holds | When absent | Bound | What it does |
|---|---|---|---|---|
| `id` | text | manual_mask | 256 characters | The editor's name. |
| `label` | text | Manual Mask | 256 characters | The editor's title. |
| `tooltip` | text | - | 8192 characters | Shown when the pointer rests on the control. |
| `use_param` | text | use_manual_mask | 256 characters | The parameter that says whether the mask is used. |
| `use_label` | text | Use manual mask | 256 characters | The text beside its check box. |
| `use_default` | boolean | false | - | Whether the mask is used at the start. |
| `display_param` | text | - | 256 characters | A choice whose entry shows the mask while it is chosen. |
| `display_value` | value | - | 256 characters | That entry's value. |
| `brush_size` | number | 50 | - | The brush's size, from 1 to 512. |
| `min_brush_size` | number | 10 | - | The least brush size. |
| `max_brush_size` | number | 200 | - | The greatest brush size. |
| `preview_invalidates` | boolean | false | - | A change marks a preview refreshed by hand as out of date instead of asking for a frame. |
| `visible_when` | condition | always | 1024 characters | Shows the control only while the expression over the parameters holds. |
| `enabled_when` | condition | always | 1024 characters | Enables the control only while the expression holds, or while a parameter equals (`equals`) or differs from (`not_equals`) a value. |

## A tree of widgets

A tree's root is a node with its kind under `widget` and what it holds under `children`. A group holds nodes of the tree
alone.

### `column`

Its children, one under another. Role: group; it holds other nodes.

| Field | Holds | When absent | Bound | What it does |
|---|---|---|---|---|
| `id` | text | - | 256 characters | The name the extension knows the control by: a parameter's key, an action's, the target of a patch. |
| `tooltip` | text | - | 8192 characters | Shown when the pointer rests on the control. |
| `visible` | boolean | true | - | Whether the widget is shown. |
| `enabled` | boolean | true | - | Whether the widget takes input. |

### `card`

A titled card holding its children. Role: group; it holds other nodes.

| Field | Holds | When absent | Bound | What it does |
|---|---|---|---|---|
| `id` | text | - | 256 characters | The name the extension knows the control by: a parameter's key, an action's, the target of a patch. |
| `tooltip` | text | - | 8192 characters | Shown when the pointer rests on the control. |
| `visible` | boolean | true | - | Whether the widget is shown. |
| `enabled` | boolean | true | - | Whether the widget takes input. |
| `label` | text | - | 256 characters | The title. |
| `compact` | boolean | false | - | Draws the card's tighter header. |
| `body_padded` | boolean | false | - | Pads the card's body. |

### `collapsible_section`

A titled section holding its children, which folds. Role: group; it holds other nodes.

| Field | Holds | When absent | Bound | What it does |
|---|---|---|---|---|
| `id` | text | - | 256 characters | The name the extension knows the control by: a parameter's key, an action's, the target of a patch. |
| `tooltip` | text | - | 8192 characters | Shown when the pointer rests on the control. |
| `visible` | boolean | true | - | Whether the widget is shown. |
| `enabled` | boolean | true | - | Whether the widget takes input. |
| `label` | text | - | 256 characters | The title. |
| `expanded` | boolean | true | - | Whether it starts unfolded. |

### `slider`

A real number: a slider beside a spin box, under its label. Role: parameter.

| Field | Holds | When absent | Bound | What it does |
|---|---|---|---|---|
| `id` | text | - | 256 characters | The name the extension knows the control by: a parameter's key, an action's, the target of a patch. |
| `tooltip` | text | - | 8192 characters | Shown when the pointer rests on the control. |
| `visible` | boolean | true | - | Whether the widget is shown. |
| `enabled` | boolean | true | - | Whether the widget takes input. |
| `label` | text | - | 256 characters | The text above the control. |
| `min` | number | 0 | - | The least value. |
| `max` | number | 1 | - | The greatest value. |
| `step` | number | 0.01 | - | The step. |
| `value` | number | the least | - | The value. |

### `spin_int`

A whole number: a spin box beside its label. Its numbers are read as whole numbers. Role: parameter.

| Field | Holds | When absent | Bound | What it does |
|---|---|---|---|---|
| `id` | text | - | 256 characters | The name the extension knows the control by: a parameter's key, an action's, the target of a patch. |
| `tooltip` | text | - | 8192 characters | Shown when the pointer rests on the control. |
| `visible` | boolean | true | - | Whether the widget is shown. |
| `enabled` | boolean | true | - | Whether the widget takes input. |
| `label` | text | - | 256 characters | The text beside the control. |
| `min` | number | 0 | - | The least value. |
| `max` | number | 100 | - | The greatest value. |
| `step` | number | 1 | - | The step; at least 1. |
| `value` | number | the least | - | The value. |

### `spin_double`

A real number: a spin box beside its label. Role: parameter.

| Field | Holds | When absent | Bound | What it does |
|---|---|---|---|---|
| `id` | text | - | 256 characters | The name the extension knows the control by: a parameter's key, an action's, the target of a patch. |
| `tooltip` | text | - | 8192 characters | Shown when the pointer rests on the control. |
| `visible` | boolean | true | - | Whether the widget is shown. |
| `enabled` | boolean | true | - | Whether the widget takes input. |
| `label` | text | - | 256 characters | The text beside the control. |
| `min` | number | 0 | - | The least value. |
| `max` | number | 1 | - | The greatest value. |
| `step` | number | 0.01 | - | The step; it also sets the decimals shown. |
| `value` | number | the least | - | The value. |

### `checkbox`

A check box beside its label. Role: parameter.

| Field | Holds | When absent | Bound | What it does |
|---|---|---|---|---|
| `id` | text | - | 256 characters | The name the extension knows the control by: a parameter's key, an action's, the target of a patch. |
| `tooltip` | text | - | 8192 characters | Shown when the pointer rests on the control. |
| `visible` | boolean | true | - | Whether the widget is shown. |
| `enabled` | boolean | true | - | Whether the widget takes input. |
| `label` | text | - | 256 characters | The text beside the control. |
| `value` | boolean | false | - | Whether it is checked. |

### `combo`

One value of a list: a combo box beside its label. Role: parameter.

| Field | Holds | When absent | Bound | What it does |
|---|---|---|---|---|
| `id` | text | - | 256 characters | The name the extension knows the control by: a parameter's key, an action's, the target of a patch. |
| `tooltip` | text | - | 8192 characters | Shown when the pointer rests on the control. |
| `visible` | boolean | true | - | Whether the widget is shown. |
| `enabled` | boolean | true | - | Whether the widget takes input. |
| `label` | text | - | 256 characters | The text beside the control. |
| `options` | entries | - | 1024 entries | The entries: texts, or `[label, value]` pairs; at least one. |
| `value` | value | none chosen | 256 characters | The chosen entry's value. |

### `line_edit`

A line of text under its label. Role: parameter.

| Field | Holds | When absent | Bound | What it does |
|---|---|---|---|---|
| `id` | text | - | 256 characters | The name the extension knows the control by: a parameter's key, an action's, the target of a patch. |
| `tooltip` | text | - | 8192 characters | Shown when the pointer rests on the control. |
| `visible` | boolean | true | - | Whether the widget is shown. |
| `enabled` | boolean | true | - | Whether the widget takes input. |
| `label` | text | - | 256 characters | The text above the control. |
| `placeholder` | text | - | 256 characters | Shown while the line is empty. |
| `value` | value | - | 8192 characters | The text. |
| `echo_mode` | text | normal | 256 characters | `password` hides what is typed. |

### `file_picker`

A file's path: a line of text and a button that opens a file dialog. Role: parameter.

| Field | Holds | When absent | Bound | What it does |
|---|---|---|---|---|
| `id` | text | - | 256 characters | The name the extension knows the control by: a parameter's key, an action's, the target of a patch. |
| `tooltip` | text | - | 8192 characters | Shown when the pointer rests on the control. |
| `visible` | boolean | true | - | Whether the widget is shown. |
| `enabled` | boolean | true | - | Whether the widget takes input. |
| `label` | text | - | 256 characters | The text beside the control. |
| `placeholder` | text | - | 256 characters | Shown while the line is empty. |
| `value` | value | - | 8192 characters | The path. |
| `read_only` | boolean | false | - | Only the dialog sets the path. |
| `button_label` | text | Select File | 256 characters | The button's text. |

### `dir_picker`

A folder's path: a line of text and a button that opens a folder dialog. Role: parameter.

| Field | Holds | When absent | Bound | What it does |
|---|---|---|---|---|
| `id` | text | - | 256 characters | The name the extension knows the control by: a parameter's key, an action's, the target of a patch. |
| `tooltip` | text | - | 8192 characters | Shown when the pointer rests on the control. |
| `visible` | boolean | true | - | Whether the widget is shown. |
| `enabled` | boolean | true | - | Whether the widget takes input. |
| `label` | text | - | 256 characters | The text beside the control. |
| `placeholder` | text | - | 256 characters | Shown while the line is empty. |
| `value` | value | - | 8192 characters | The path. |
| `read_only` | boolean | false | - | Only the dialog sets the path. |
| `button_label` | text | Select Folder | 256 characters | The button's text. |

### `status`

A line of status text with a tone's mark, beside its label. Role: display.

| Field | Holds | When absent | Bound | What it does |
|---|---|---|---|---|
| `id` | text | - | 256 characters | The name the extension knows the control by: a parameter's key, an action's, the target of a patch. |
| `tooltip` | text | - | 8192 characters | Shown when the pointer rests on the control. |
| `visible` | boolean | true | - | Whether the widget is shown. |
| `enabled` | boolean | true | - | Whether the widget takes input. |
| `label` | text | - | 256 characters | The text beside the status. |
| `value` | value | - | 8192 characters | The status text. |
| `tone` | text | neutral | 256 characters | `success`, `warning`, `error` or `info`. |

### `console_output`

Read-only console text, under its label. Role: display.

| Field | Holds | When absent | Bound | What it does |
|---|---|---|---|---|
| `id` | text | - | 256 characters | The name the extension knows the control by: a parameter's key, an action's, the target of a patch. |
| `tooltip` | text | - | 8192 characters | Shown when the pointer rests on the control. |
| `visible` | boolean | true | - | Whether the widget is shown. |
| `enabled` | boolean | true | - | Whether the widget takes input. |
| `label` | text | - | 256 characters | The text above the console. |
| `placeholder` | text | - | 256 characters | Shown while it is empty. |
| `value` | lines | - | 262144 characters | The text, or its lines, each a text or a map of `text` and `stream`. |
| `min_lines` | number | 12 | - | Its least height in lines, at least 4. |
| `collapsed_when_empty` | boolean | false | - | Hidden while it is empty. |

### `section_header`

A heading with a description and, when it has a text, an action button. Role: action.

| Field | Holds | When absent | Bound | What it does |
|---|---|---|---|---|
| `id` | text | - | 256 characters | The name the extension knows the control by: a parameter's key, an action's, the target of a patch. |
| `tooltip` | text | - | 8192 characters | Shown when the pointer rests on the control. |
| `visible` | boolean | true | - | Whether the widget is shown. |
| `enabled` | boolean | true | - | Whether the widget takes input. |
| `label` | text | - | 256 characters | The heading. |
| `description` | text | - | 8192 characters | The line under the heading. |
| `action_text` | text | - | 256 characters | The button's text; no button when empty. |

### `info_banner`

A banner of text. Role: display.

| Field | Holds | When absent | Bound | What it does |
|---|---|---|---|---|
| `id` | text | - | 256 characters | The name the extension knows the control by: a parameter's key, an action's, the target of a patch. |
| `tooltip` | text | - | 8192 characters | Shown when the pointer rests on the control. |
| `visible` | boolean | true | - | Whether the widget is shown. |
| `enabled` | boolean | true | - | Whether the widget takes input. |
| `message` | text | the label | 8192 characters | The banner's text. |
| `label` | text | - | 256 characters | The text, when `message` is absent. |
| `tone` | text | info | 256 characters | `info`, `warning` or `error`. |

### `button_group`

One value of a few: a row of buttons, the chosen one drawn as primary. Role: parameter.

| Field | Holds | When absent | Bound | What it does |
|---|---|---|---|---|
| `id` | text | - | 256 characters | The name the extension knows the control by: a parameter's key, an action's, the target of a patch. |
| `tooltip` | text | - | 8192 characters | Shown when the pointer rests on the control. |
| `visible` | boolean | true | - | Whether the widget is shown. |
| `enabled` | boolean | true | - | Whether the widget takes input. |
| `options` | entries | - | 1024 entries | The entries: texts, or `[label, value]` pairs; at least one. |
| `value` | value | none chosen | 256 characters | The chosen entry's value. |

### `table`

A table of rows. Role: display.

| Field | Holds | When absent | Bound | What it does |
|---|---|---|---|---|
| `id` | text | - | 256 characters | The name the extension knows the control by: a parameter's key, an action's, the target of a patch. |
| `tooltip` | text | - | 8192 characters | Shown when the pointer rests on the control. |
| `visible` | boolean | true | - | Whether the widget is shown. |
| `enabled` | boolean | true | - | Whether the widget takes input. |
| `columns` | columns | - | 64 entries | The columns: texts, or maps of `id` and `label`. |
| `rows` | rows | - | 4096 entries | The rows: lists of values in the columns' order, or maps of a column's id to its value. |
