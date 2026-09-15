<!-- 本文件由 .claude/skills/jl-dot-ui/tools/gen_widgets_ref.py 生成，勿手改 -->
<!-- 控件库一变就重跑：python gen_widgets_ref.py > ../reference/widgets_props.md -->

# 控件属性表（自动导出）

共 12 个控件。「类型码」是 `-type` 在 typecodes.ini 里的值，也是设备端 `control.h` 里 `CTRL_TYPE_*` 的值。

## 电池电量

| | |
|---|---|
| `-class` | `NewFrame` |
| `-type` | `Battery` |
| `caption` | `电池电量` |
| 类型码 | 9（由 caption 定）|
| 来源 | widgets.json |

- `id` — 唯一ID号（唯一 ID。`ename` 就是生成到 ename.h 里的宏名）
- `element_css` — CSS元素（复合项，展开见下）
  - `align` — 对齐方式（`ALIGN_LEFT`=0 / `ALIGN_CENTER`=1 / `ALIGN_RIGHT`=2（默认 `ALIGN_CENTER`））
  - `invisible` — 默认隐藏（`true`=1 / `false`=0（默认 `false`））
  - `flags` — 标志（`ELM_FLAG_NORMAL`=0 / `ELM_FLAG_HEAD`=1（默认 `ELM_FLAG_NORMAL`））
  - `rect` — 坐标（坐标 `{x, y, width, height}`，**相对父节点**，单位像素）
  - `background_color` — 背景颜色（背景色）
  - `background_image` — 背景图片（背景图路径）
  - `border` — 边框（边框 `{top, bottom, left, right}`）
- `image` — 电量图片列表（列表（上限 不限），默认 `config\images\battery0.png`）
- `charge_image` — 充电图片列表（列表（上限 不限），默认 `config\images\charging.png`）
- `action` — 事件属性（事件动作表（见 layout.md「action」一节））

## 图片

| | |
|---|---|
| `-class` | `NewFrame` |
| `-type` | `ImageList` |
| `caption` | `图片` |
| 类型码 | 8（由 caption 定）|
| 来源 | widgets.json |

- `id` — ID号（唯一 ID。`ename` 就是生成到 ename.h 里的宏名）
- `element_css` — CSS元素（复合项，展开见下）
  - `align` — 对齐方式（`ALIGN_LEFT`=0 / `ALIGN_CENTER`=1 / `ALIGN_RIGHT`=2（默认 `ALIGN_CENTER`））
  - `invisible` — 默认隐藏（`true`=1 / `false`=0（默认 `false`））
  - `flags` — 标志（`ELM_FLAG_NORMAL`=0 / `ELM_FLAG_HEAD`=1（默认 `ELM_FLAG_NORMAL`））
  - `rect` — 坐标（坐标 `{x, y, width, height}`，**相对父节点**，单位像素）
  - `background_color` — 背景颜色（背景色）
  - `background_image` — 背景图片（背景图路径）
  - `border` — 边框（边框 `{top, bottom, left, right}`）
- `highlight` — 默认高亮（整数 0..128，默认 `0`）
- `cent_x` — 旋转中心点: X（整数 0..32768，默认 `0`）
- `cent_y` — 旋转中心点: Y（整数 0..32768，默认 `0`）
- `normal_image` — 图片列表（列表（上限 30），默认 ``）
- `highlight_image` — 高亮图片列表（列表（上限 不限），默认 ``）
- `action` — 事件属性（事件动作表（见 layout.md「action」一节））

## 图层

| | |
|---|---|
| `-class` | `NewLayer` |
| `-type` | `NewLayer` |
| `caption` | `图层` |
| 类型码 | 4（由 caption 定）|
| 来源 | widgets.json |

- `id` — ID号（唯一 ID。`ename` 就是生成到 ename.h 里的宏名）
- `element_css` — CSS元素（复合项，展开见下）
  - `align` — 对齐方式（`ALIGN_LEFT`=0 / `ALIGN_CENTER`=1 / `ALIGN_RIGHT`=2（默认 `ALIGN_CENTER`））
  - `invisible` — 默认隐藏（`true`=1 / `false`=0（默认 `false`））
  - `z_order` — z轴坐标（整数 0..128，默认 `0`）
  - `rect` — 坐标（坐标 `{x, y, width, height}`，**相对父节点**，单位像素）
  - `background_color` — 背景颜色（背景色）
  - `background_image` — 背景图片（背景图路径）
  - `border` — 边框（边框 `{top, bottom, left, right}`）
- `color_format` — 颜色类型（`OSD16`=2 / `OSD1`=4（默认 `OSD16`））
- `action` — 事件属性（事件动作表（见 layout.md「action」一节））

## 布局

| | |
|---|---|
| `-class` | `NewLayout` |
| `-type` | `NewLayout` |
| `caption` | `布局` |
| 类型码 | 3（由 caption 定）|
| 来源 | widgets.json |

- `id` — ID号（唯一 ID。`ename` 就是生成到 ename.h 里的宏名）
- `element_css` — CSS元素（复合项，展开见下）
  - `align` — 对齐方式（`ALIGN_LEFT`=0 / `ALIGN_CENTER`=1 / `ALIGN_RIGHT`=2（默认 `ALIGN_CENTER`））
  - `invisible` — 默认隐藏（`true`=1 / `false`=0（默认 `false`））
  - `flags` — 标志（`ELM_FLAG_NORMAL`=0 / `ELM_FLAG_HEAD`=1（默认 `ELM_FLAG_NORMAL`））
  - `rect` — 坐标（坐标 `{x, y, width, height}`，**相对父节点**，单位像素）
  - `background_color` — 背景颜色（背景色）
  - `background_image` — 背景图片（背景图路径）
  - `border` — 边框（边框 `{top, bottom, left, right}`）
- `action` — 事件属性（事件动作表（见 layout.md「action」一节））

## 文字

| | |
|---|---|
| `-class` | `NewFrame` |
| `-type` | `Text` |
| `caption` | `文字` |
| 类型码 | 12（由 caption 定）|
| 来源 | widgets.json |

- `id` — 唯一ID号（唯一 ID。`ename` 就是生成到 ename.h 里的宏名）
- `element_css` — CSS元素（复合项，展开见下）
  - `align` — 对齐方式（`ALIGN_LEFT`=0 / `ALIGN_CENTER`=1 / `ALIGN_RIGHT`=2（默认 `ALIGN_CENTER`））
  - `invisible` — 默认隐藏（`true`=1 / `false`=0（默认 `false`））
  - `flags` — 标志（`ELM_FLAG_NORMAL`=0 / `ELM_FLAG_HEAD`=1（默认 `ELM_FLAG_NORMAL`））
  - `rect` — 坐标（坐标 `{x, y, width, height}`，**相对父节点**，单位像素）
  - `background_color` — 背景颜色（背景色）
  - `background_image` — 背景图片（背景图路径）
  - `border` — 边框（边框 `{top, bottom, left, right}`）
- `source` — 数据源（字符串（上限 8），默认 `none`）
- `code` — 编码格式（字符串（上限 8），默认 `strpic`）
- `color` — 文字颜色（颜色）
- `color` — 高亮颜色（颜色）
- `str` — 文字列表（列表（上限 100），默认 ``）
- `action` — 事件属性（事件动作表（见 layout.md「action」一节））

## 时间

| | |
|---|---|
| `-class` | `NewFrame` |
| `-type` | `Time` |
| `caption` | `时间` |
| 类型码 | 10（由 caption 定）|
| 来源 | widgets.json |

- `id` — 唯一ID号（唯一 ID。`ename` 就是生成到 ename.h 里的宏名）
- `element_css` — CSS元素（复合项，展开见下）
  - `align` — 对齐方式（`ALIGN_LEFT`=0 / `ALIGN_CENTER`=1 / `ALIGN_RIGHT`=2（默认 `ALIGN_CENTER`））
  - `invisible` — 默认隐藏（`true`=1 / `false`=0（默认 `false`））
  - `flags` — 标志（`ELM_FLAG_NORMAL`=0 / `ELM_FLAG_HEAD`=1（默认 `ELM_FLAG_NORMAL`））
  - `rect` — 坐标（坐标 `{x, y, width, height}`，**相对父节点**，单位像素）
  - `background_color` — 背景颜色（背景色）
  - `background_image` — 背景图片（背景图路径）
  - `border` — 边框（边框 `{top, bottom, left, right}`）
- `source` — 数据源（字符串（上限 8），默认 `none`）
- `auto_cnt` — 自动记时（`YES`=1 / `NO`=0（默认 `YES`））
- `format` — 格式（字符串（上限 16），默认 `Y/M/D h:m:s`）
- `color` — 文字颜色（背景色）
- `color` — 高亮颜色（颜色）
- `number` — 数字图片列表（列表（上限 10），默认 ``）
- `delimiter` — 分隔符图片列表（列表（上限 10），默认 ``）
- `action` — 事件属性（事件动作表（见 layout.md「action」一节））

## 数字

| | |
|---|---|
| `-class` | `NewFrame` |
| `-type` | `Number` |
| `caption` | `数字` |
| 类型码 | 15（由 caption 定）|
| 来源 | widgets.json |

- `id` — 唯一ID号（唯一 ID。`ename` 就是生成到 ename.h 里的宏名）
- `element_css` — CSS元素（复合项，展开见下）
  - `align` — 对齐方式（`ALIGN_LEFT`=0 / `ALIGN_CENTER`=1 / `ALIGN_RIGHT`=2（默认 `ALIGN_CENTER`））
  - `invisible` — 默认隐藏（`true`=1 / `false`=0（默认 `false`））
  - `flags` — 标志（`ELM_FLAG_NORMAL`=0 / `ELM_FLAG_HEAD`=1（默认 `ELM_FLAG_NORMAL`））
  - `rect` — 坐标（坐标 `{x, y, width, height}`，**相对父节点**，单位像素）
  - `background_color` — 背景颜色（背景色）
  - `background_image` — 背景图片（背景图路径）
  - `border` — 边框（边框 `{top, bottom, left, right}`）
- `source` — 数据源（字符串（上限 8），默认 `none`）
- `format` — 格式（字符串（上限 16），默认 `%04d`）
- `color` — 文字颜色（背景色）
- `color` — 高亮颜色（颜色）
- `number` — 数字图片列表（列表（上限 10），默认 ``）
- `delimiter` — 分隔符图片列表（列表（上限 10），默认 ``）
- `space` — 空格图片列表（列表（上限 2），默认 ``）
- `action` — 事件属性（事件动作表（见 layout.md「action」一节））

## 表格控件

| | |
|---|---|
| `-class` | `NewGrid` |
| `-type` | `NewGrid` |
| `caption` | `表格控件` |
| 类型码 | 5（由 caption 定）|
| 来源 | widgets.json |

- `id` — ID号（唯一 ID。`ename` 就是生成到 ename.h 里的宏名）
- `element_css` — CSS元素（复合项，展开见下）
  - `align` — 对齐方式（`ALIGN_LEFT`=0 / `ALIGN_CENTER`=1 / `ALIGN_RIGHT`=2（默认 `ALIGN_CENTER`））
  - `invisible` — 默认隐藏（`true`=1 / `false`=0（默认 `false`））
  - `flags` — 标志（`ELM_FLAG_NORMAL`=0 / `ELM_FLAG_HEAD`=1（默认 `ELM_FLAG_NORMAL`））
  - `rect` — 坐标（坐标 `{x, y, width, height}`，**相对父节点**，单位像素）
  - `background_color` — 背景颜色（背景色）
  - `background_image` — 背景图片（背景图路径）
  - `border` — 边框（边框 `{top, bottom, left, right}`）
- `scroll` — 滚动方式（`SCROLL`=0 / `PAGE`=1（默认 `SCROLL`））
- `highlight_index` — 高亮索引（整数 0..128，默认 `0`）
- `action` — 事件属性（事件动作表（见 layout.md「action」一节））

## 垂直列表

| | |
|---|---|
| `-class` | `NewList` |
| `-type` | `VerticalList` |
| `caption` | `垂直列表` |
| 类型码 | 5（由 caption 定）|
| 来源 | widgets.json |

- `id` — ID号（唯一 ID。`ename` 就是生成到 ename.h 里的宏名）
- `element_css` — CSS元素（复合项，展开见下）
  - `align` — 对齐方式（`ALIGN_LEFT`=0 / `ALIGN_CENTER`=1 / `ALIGN_RIGHT`=2（默认 `ALIGN_CENTER`））
  - `invisible` — 默认隐藏（`true`=1 / `false`=0（默认 `false`））
  - `flags` — 标志（`ELM_FLAG_NORMAL`=0 / `ELM_FLAG_HEAD`=1（默认 `ELM_FLAG_NORMAL`））
  - `rect` — 坐标（坐标 `{x, y, width, height}`，**相对父节点**，单位像素）
  - `background_color` — 背景颜色（背景色）
  - `background_image` — 背景图片（背景图路径）
  - `border` — 边框（边框 `{top, bottom, left, right}`）
- `scroll_mode` — 滚动方式（`SCROLL`=0 / `PAGE`=1（默认 `SCROLL`））
- `highlight_index` — 默认高亮行号（整数 0..128，默认 `0`）
- `action` — 事件属性（事件动作表（见 layout.md「action」一节））

## 水平列表

| | |
|---|---|
| `-class` | `NewList` |
| `-type` | `HorizontalList` |
| `caption` | `水平列表` |
| 类型码 | 5（由 caption 定）|
| 来源 | widgets.json |

- `id` — ID号（唯一 ID。`ename` 就是生成到 ename.h 里的宏名）
- `element_css` — CSS元素（复合项，展开见下）
  - `align` — 对齐方式（`ALIGN_LEFT`=0 / `ALIGN_CENTER`=1 / `ALIGN_RIGHT`=2（默认 `ALIGN_CENTER`））
  - `invisible` — 默认隐藏（`true`=1 / `false`=0（默认 `false`））
  - `flags` — 标志（`ELM_FLAG_NORMAL`=0 / `ELM_FLAG_HEAD`=1（默认 `ELM_FLAG_NORMAL`））
  - `rect` — 坐标（坐标 `{x, y, width, height}`，**相对父节点**，单位像素）
  - `background_color` — 背景颜色（背景色）
  - `background_image` — 背景图片（背景图路径）
  - `border` — 边框（边框 `{top, bottom, left, right}`）
- `scroll` — 滚动方式（`SCROLL`=0 / `PAGE`=1（默认 `SCROLL`））
- `highlight_index` — 高亮列号（整数 0..128，默认 `0`）
- `action` — 事件属性（事件动作表（见 layout.md「action」一节））

## slider

| | |
|---|---|
| `-class` | `NewLayout` |
| `-type` | `NewLayout` |
| `caption` | `slider` |
| 类型码 | 28（由 caption 定）|
| 来源 | widgets.d/slider.json |

**组合控件**：库里就是一棵子树，插入时整棵一起进工程。零件的类型码同样由 `caption` 决定，**改了 caption 就不再是这个零件**。

- `layout[]` → `right_pic`（`-type`=ImageList，类型码 29 由 caption 定）
- `layout[]` → `left_pic`（`-type`=ImageList，类型码 30 由 caption 定）
- `layout[]` → `slider_pic`（`-type`=ImageList，类型码 31 由 caption 定）

- `id` — ID号（唯一 ID。`ename` 就是生成到 ename.h 里的宏名）
- `element_css` — CSS元素（复合项，展开见下）
  - `align` — 对齐方式（`ALIGN_LEFT`=0 / `ALIGN_CENTER`=1 / `ALIGN_RIGHT`=2（默认 `ALIGN_CENTER`））
  - `invisible` — 默认隐藏（`true`=1 / `false`=0（默认 `false`））
  - `flags` — 标志（`ELM_FLAG_NORMAL`=0 / `ELM_FLAG_HEAD`=1（默认 `ELM_FLAG_NORMAL`））
  - `rect` — 坐标（坐标 `{x, y, width, height}`，**相对父节点**，单位像素）
  - `background_color` — 背景颜色（背景色）
  - `background_image` — 背景图片（背景图路径）
  - `border` — 边框（边框 `{top, bottom, left, right}`）
- `step` — 步进（整数 1..100，默认 `1`）

## vslider

| | |
|---|---|
| `-class` | `NewLayout` |
| `-type` | `NewLayout` |
| `caption` | `vslider` |
| 类型码 | 33（由 caption 定）|
| 来源 | widgets.d/vslider.json |

**组合控件**：库里就是一棵子树，插入时整棵一起进工程。零件的类型码同样由 `caption` 决定，**改了 caption 就不再是这个零件**。

- `layout[]` → `vslider_right_pic`（`-type`=ImageList，类型码 34 由 caption 定）
- `layout[]` → `vslider_left_pic`（`-type`=ImageList，类型码 35 由 caption 定）
- `layout[]` → `vslider_pic`（`-type`=ImageList，类型码 36 由 caption 定）

- `id` — ID号（唯一 ID。`ename` 就是生成到 ename.h 里的宏名）
- `element_css` — CSS元素（复合项，展开见下）
  - `align` — 对齐方式（`ALIGN_LEFT`=0 / `ALIGN_CENTER`=1 / `ALIGN_RIGHT`=2（默认 `ALIGN_CENTER`））
  - `invisible` — 默认隐藏（`true`=1 / `false`=0（默认 `false`））
  - `flags` — 标志（`ELM_FLAG_NORMAL`=0 / `ELM_FLAG_HEAD`=1（默认 `ELM_FLAG_NORMAL`））
  - `rect` — 坐标（坐标 `{x, y, width, height}`，**相对父节点**，单位像素）
  - `background_color` — 背景颜色（背景色）
  - `background_image` — 背景图片（背景图路径）
  - `border` — 边框（边框 `{top, bottom, left, right}`）
- `step` — 步进（整数 1..100，默认 `1`）

