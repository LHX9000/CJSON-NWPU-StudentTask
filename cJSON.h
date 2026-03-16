/*
  Copyright (c) 2009-2017 Dave Gamble and cJSON contributors

  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"), to deal
  in the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included in
  all copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
  THE SOFTWARE.
*/

/*
 * cJSON 轻量级 JSON 解析/生成库核心头文件
 * 核心设计：基于双向链表+树形结构存储 JSON 节点，递归解析/生成
 * 内存管理：所有节点需手动释放，解析生成的字符串需手动 free
 */
#ifndef cJSON__h
#define cJSON__h

#ifdef __cplusplus
extern "C"
{
#endif

#if !defined(__WINDOWS__) && (defined(WIN32) || defined(WIN64) || defined(_MSC_VER) || defined(_WIN32))
#define __WINDOWS__
#endif
    /**
     * @brief Windows 平台调用约定定义
     * @details Windows 下指定显式调用约定，避免不同编译项目默认调用约定不兼容问题
     *          - CJSON_CDECL: __cdecl 调用约定（栈由调用者清理，可变参数函数默认）
     *          - CJSON_STDCALL: __stdcall 调用约定（栈由被调用者清理，Windows API 常用）
     */
#ifdef __WINDOWS__

/* When compiling for windows, we specify a specific calling convention to avoid issues where we are being called from a project with a different default calling convention.  For windows you have 3 define options:

CJSON_HIDE_SYMBOLS - Define this in the case where you don't want to ever dllexport symbols
CJSON_EXPORT_SYMBOLS - Define this on library build when you want to dllexport symbols (default)
CJSON_IMPORT_SYMBOLS - Define this if you want to dllimport symbol

For *nix builds that support visibility attribute, you can define similar behavior by

setting default visibility to hidden by adding
-fvisibility=hidden (for gcc)
or
-xldscope=hidden (for sun cc)
to CFLAGS

then using the CJSON_API_VISIBILITY flag to "export" the same symbols the way CJSON_EXPORT_SYMBOLS does

*/

#define CJSON_CDECL __cdecl
#define CJSON_STDCALL __stdcall
/**
 * @brief Windows 平台符号导出/导入控制
 * @details 三种编译模式：
 *          1. CJSON_HIDE_SYMBOLS: 不导出任何符号（静态库场景）
 *          2. CJSON_EXPORT_SYMBOLS: 导出符号（动态库编译时，默认）
 *          3. CJSON_IMPORT_SYMBOLS: 导入符号（动态库使用时）
 * @note 核心宏 CJSON_PUBLIC 封装符号导出逻辑，保证跨项目调用符号可见性
 */
/* export symbols by default, this is necessary for copy pasting the C and header file */


#if !defined(CJSON_HIDE_SYMBOLS) && !defined(CJSON_IMPORT_SYMBOLS) && !defined(CJSON_EXPORT_SYMBOLS)
#define CJSON_EXPORT_SYMBOLS
#endif

#if defined(CJSON_HIDE_SYMBOLS)
#define CJSON_PUBLIC(type)   type CJSON_STDCALL
#elif defined(CJSON_EXPORT_SYMBOLS)
#define CJSON_PUBLIC(type)   __declspec(dllexport) type CJSON_STDCALL
#elif defined(CJSON_IMPORT_SYMBOLS)
#define CJSON_PUBLIC(type)   __declspec(dllimport) type CJSON_STDCALL
#endif
#else /* !__WINDOWS__ */
#define CJSON_CDECL
#define CJSON_STDCALL

  /**
  * @brief 非 Windows 平台符号可见性控制
  * @details 兼容 GCC/Sun CC 编译器的 visibility 属性，实现符号导出/隐藏
  *          CJSON_API_VISIBILITY 定义时，导出符号（动态库场景），否则默认可见
  */
#if (defined(__GNUC__) || defined(__SUNPRO_CC) || defined (__SUNPRO_C)) && defined(CJSON_API_VISIBILITY)
#define CJSON_PUBLIC(type)   __attribute__((visibility("default"))) type
#else
#define CJSON_PUBLIC(type) type
#endif
#endif

/* project version */
/* 版本定义：便于版本兼容判断 */
#define CJSON_VERSION_MAJOR 1
#define CJSON_VERSION_MINOR 7
#define CJSON_VERSION_PATCH 19

#include <stddef.h>

/* cJSON Types: */
/* cJSON 节点类型：标记节点存储的数据类型 */
#define cJSON_Invalid (0)
#define cJSON_False  (1 << 0)//布尔假
#define cJSON_True   (1 << 1)//布尔真
#define cJSON_NULL   (1 << 2)//空值
#define cJSON_Number (1 << 3)//数字
#define cJSON_String (1 << 4)//字符串
#define cJSON_Array  (1 << 5)//数组
#define cJSON_Object (1 << 6)//对象
#define cJSON_Raw    (1 << 7) /* raw json */

/**
 * @brief 节点内存管理标记（非类型位，用于特殊内存策略）
 * @details
 * - cJSON_IsReference: 节点为引用类型，cJSON_Delete 不释放其内存
 * - cJSON_StringIsConst: 字符串值为常量，cJSON_Delete 不释放 valuestring
 */
#define cJSON_IsReference 256
#define cJSON_StringIsConst 512

/* The cJSON structure: */
/*
 * cJSON 核心结构体：单个 JSON 节点的内存布局
 * 设计：双向链表（next/prev）+ 树形结构（child），适配 JSON 层级+同级元素特性
 */
typedef struct cJSON
{
    /* next/prev allow you to walk array/object chains. Alternatively, use GetArraySize/GetArrayItem/GetObjectItem */
    struct cJSON *next;//同级下一个节点（兄弟节点），NULL 表示最后一个
    struct cJSON *prev;//同级上一个节点（兄弟节点），NULL 表示第一个
    /* An array or object item will have a child pointer pointing to a chain of the items in the array/object. */
    struct cJSON *child;//子节点（仅 Array/Object 类型有效），NULL 表示无子节点

    /* The type of the item, as above. */
    int type;//节点类型，对应 cJSON_Type 枚举

    /* The item's string, if type==cJSON_String  and type == cJSON_Raw */
    char *valuestring;//字符串值（仅 String 类型有效），malloc 分配需手动 free
    /* writing to valueint is DEPRECATED, use cJSON_SetNumberValue instead */
    int valueint;//整数值（Number 类型兼容字段，建议用 valuedouble）
    /* The item's number, if type==cJSON_Number */
    double valuedouble;//数值（Number 类型核心存储字段）

    /* The item's name string, if this item is the child of, or is in the list of subitems of an object. */
    char *string;//节点名（仅 Object 的键值对有效），malloc 分配需手动 free 
} cJSON;

/* 内存管理钩子：允许自定义 malloc/free，适配不同内存池 */
typedef struct cJSON_Hooks
{
     /* malloc/free are CDECL on Windows regardless of the default calling convention of the compiler, so ensure the hooks allow passing those functions directly. */
     /*
     * 作用：自定义内存分配函数
     * 参数：sz - 分配的内存大小（字节）
     * 返回值：成功 - 内存指针；失败 - NULL
     */
      void *(CJSON_CDECL *malloc_fn)(size_t sz);
      /*
      * 作用：自定义内存释放函数
      * 参数：ptr - 待释放的内存指针
      * 返回值：无
      */
      void (CJSON_CDECL *free_fn)(void *ptr);
} cJSON_Hooks;

typedef int cJSON_bool;

/* Limits how deeply nested arrays/objects can be before cJSON rejects to parse them.
 * This is to prevent stack overflows. */
#ifndef CJSON_NESTING_LIMIT
#define CJSON_NESTING_LIMIT 1000
#endif

/* Limits the length of circular references can be before cJSON rejects to parse them.
 * This is to prevent stack overflows. */
#ifndef CJSON_CIRCULAR_LIMIT
#define CJSON_CIRCULAR_LIMIT 10000
#endif

/* returns the version of cJSON as a string */
CJSON_PUBLIC(const char*) cJSON_Version(void);

/* Supply malloc, realloc and free functions to cJSON */
/**
 * @brief 初始化自定义内存管理钩子
 * @param hooks 内存钩子结构体指针（NULL 则恢复默认 malloc/free）
 * @return 无
 * @note 需在调用其他 cJSON 函数前调用，否则已分配的内存无法被自定义 free_fn 释放
 * @warning hooks 指向的结构体需保持生命周期，函数内部会拷贝成员而非指针
 */
CJSON_PUBLIC(void) cJSON_InitHooks(cJSON_Hooks* hooks);

/* Memory Management: the caller is always responsible to free the results from all variants of cJSON_Parse (with cJSON_Delete) and cJSON_Print (with stdlib free, cJSON_Hooks.free_fn, or cJSON_free as appropriate). The exception is cJSON_PrintPreallocated, where the caller has full responsibility of the buffer. */
/* Supply a block of JSON, and this returns a cJSON object you can interrogate. */
/**
 * @brief 基础 JSON 解析函数：将 JSON 字符串解析为 cJSON 树形结构
 * @param value JSON 字符串（需以 \0 结尾，否则可能越界）
 * @return 成功：根节点指针；失败：NULL（可通过 cJSON_GetErrorPtr 获取错误位置）
 * @note 内存管理：返回的节点树需手动调用 cJSON_Delete 释放
 * @warning 不检查 JSON 字符串长度，超长字符串可能导致性能问题，建议用 ParseWithLength
 */
CJSON_PUBLIC(cJSON *) cJSON_Parse(const char *value);
/**
 * @brief 带长度限制的 JSON 解析函数
 * @param value JSON 字符串（无需 \0 结尾，由 buffer_length 限制长度）
 * @param buffer_length JSON 字符串长度（字节）
 * @return 成功：根节点指针；失败：NULL
 * @note 内存管理：返回的节点树需手动调用 cJSON_Delete 释放
 * @优势 避免 \0 截断问题，支持解析非 \0 结尾的缓冲区（如网络流/文件块）
 */
CJSON_PUBLIC(cJSON *) cJSON_ParseWithLength(const char *value, size_t buffer_length);
/* ParseWithOpts allows you to require (and check) that the JSON is null terminated, and to retrieve the pointer to the final byte parsed. */
/* If you supply a ptr in return_parse_end and parsing fails, then return_parse_end will contain a pointer to the error so will match cJSON_GetErrorPtr(). */
/**
 * @brief 带扩展选项的 JSON 解析函数
 * @param value JSON 字符串
 * @param return_parse_end 输出参数：解析结束的指针（成功时指向 JSON 末尾，失败时指向错误位置）
 * @param require_null_terminated 是否要求字符串以 \0 结尾（1=要求，0=不要求）
 * @return 成功：根节点指针；失败：NULL
 * @note 内存管理：返回的节点树需手动调用 cJSON_Delete 释放
 * @细节 return_parse_end 可为 NULL，此时不返回结束指针；失败时与 cJSON_GetErrorPtr 结果一致
 */
CJSON_PUBLIC(cJSON *) cJSON_ParseWithOpts(const char *value, const char **return_parse_end, cJSON_bool require_null_terminated);
/**
 * @brief 带长度+扩展选项的 JSON 解析函数（最安全的解析接口）
 * @param value JSON 字符串（无需 \0 结尾）
 * @param buffer_length JSON 字符串长度（字节）
 * @param return_parse_end 输出参数：解析结束的指针
 * @param require_null_terminated 是否要求 \0 结尾
 * @return 成功：根节点指针；失败：NULL
 * @note 内存管理：返回的节点树需手动调用 cJSON_Delete 释放
 * @适用场景 解析网络/文件读取的二进制缓冲区（无 \0 结尾，长度已知）
 */
CJSON_PUBLIC(cJSON *) cJSON_ParseWithLengthOpts(const char *value, size_t buffer_length, const char **return_parse_end, cJSON_bool require_null_terminated);

/* Render a cJSON entity to text for transfer/storage. */
/**
 * @brief 将 cJSON 节点树渲染为格式化的 JSON 字符串
 * @param item 待渲染的节点（根节点/子节点均可）
 * @return 成功：JSON 字符串（malloc 分配）；失败：NULL
 * @note 内存管理：返回的字符串需手动调用 free/cJSON_free 释放
 * @细节 格式化输出：缩进、换行，便于阅读；递归渲染所有子节点
 */
CJSON_PUBLIC(char *) cJSON_Print(const cJSON *item);
/* Render a cJSON entity to text for transfer/storage without any formatting. */
/**
 * @brief 将 cJSON 节点树渲染为无格式的 JSON 字符串（紧凑模式）
 * @param item 待渲染的节点
 * @return 成功：JSON 字符串（malloc 分配）；失败：NULL
 * @note 内存管理：返回的字符串需手动调用 free/cJSON_free 释放
 * @优势 无额外空格/换行，体积更小，适合网络传输/存储
 */
CJSON_PUBLIC(char *) cJSON_PrintUnformatted(const cJSON *item);
/* Render a cJSON entity to text using a buffered strategy. prebuffer is a guess at the final size. guessing well reduces reallocation. fmt=0 gives unformatted, =1 gives formatted */
/*
 * @brief 带缓冲区预分配的 JSON 渲染函数（减少内存重分配）
 * @param item 待渲染的节点
 * @param prebuffer 预分配缓冲区大小（字节），建议按预期输出大小估算
 * @param fmt 是否格式化（1=格式化，0=紧凑模式）
 * @return 成功：JSON 字符串（malloc 分配）；失败：NULL
 * @note 内存管理：返回的字符串需手动调用 free/cJSON_free 释放
 * @优化 预分配缓冲区减少 realloc 调用次数，提升性能；预估值不足时自动扩容
 */
CJSON_PUBLIC(char *) cJSON_PrintBuffered(const cJSON *item, int prebuffer, cJSON_bool fmt);
/* Render a cJSON entity to text using a buffer already allocated in memory with given length. Returns 1 on success and 0 on failure. */
/* NOTE: cJSON is not always 100% accurate in estimating how much memory it will use, so to be safe allocate 5 bytes more than you actually need */
/*
 * @brief 使用预分配缓冲区渲染 JSON 字符串（零内存分配）
 * @param item 待渲染的节点
 * @param buffer 预分配的缓冲区指针（需可写）
 * @param length 缓冲区长度（字节）
 * @param format 是否格式化（1=格式化，0=紧凑模式）
 * @return 成功：1；失败：0
 * @note 内存管理：缓冲区由调用者管理，函数不分配/释放内存
 * @警告 cJSON 估算的内存需求可能有误差，建议缓冲区多分配 5 字节避免溢出
 */
CJSON_PUBLIC(cJSON_bool) cJSON_PrintPreallocated(cJSON *item, char *buffer, const int length, const cJSON_bool format);
/* Delete a cJSON entity and all subentities. */
/*
 * @brief 递归删除 cJSON 节点树（释放所有关联内存）
 * @param item 待删除的节点（根节点/子节点均可）
 * @return 无
 * @note 内存管理：
 *       1. 递归释放所有子节点（child 链表）
 *       2. 释放节点的 string（键名）、valuestring（值字符串）
 *       3. 释放节点自身内存
 * @warning
 *       1. 禁止重复删除同一节点（二次释放会导致崩溃）
 *       2. 禁止删除 NULL 指针（函数内部未做 NULL 检查）
 *       3. 引用类型节点（cJSON_IsReference）仅释放节点自身，不释放子节点
 */
CJSON_PUBLIC(void) cJSON_Delete(cJSON *item);

 /*
  * @brief 获取 JSON 数组/对象的子节点数量
  * @param array 目标节点（必须是 Array/Object 类型，否则返回 0）
  * @return 子节点数量（>=0）
  * @细节 遍历双向链表（child 开始），统计节点数；空数组/对象返回 0
  */
/* Returns the number of items in an array (or object). */
CJSON_PUBLIC(int) cJSON_GetArraySize(const cJSON *array);

/* Retrieve item number "index" from array "array". Returns NULL if unsuccessful. */
//获取 JSON 数组指定索引的子节点
CJSON_PUBLIC(cJSON *) cJSON_GetArrayItem(const cJSON *array, int index);
/* Get item "string" from object. Case insensitive. */
//获取 JSON 对象指定键的子节点（大小写不敏感）
CJSON_PUBLIC(cJSON *) cJSON_GetObjectItem(const cJSON * const object, const char * const string);
//获取 JSON 对象指定键的子节点（大小写敏感，符合 JSON 规范）
CJSON_PUBLIC(cJSON *) cJSON_GetObjectItemCaseSensitive(const cJSON * const object, const char * const string);
//检查 JSON 对象是否包含指定键（大小写不敏感）
CJSON_PUBLIC(cJSON_bool) cJSON_HasObjectItem(const cJSON *object, const char *string);
/* For analysing failed parses. This returns a pointer to the parse error. You'll probably need to look a few chars back to make sense of it. Defined when cJSON_Parse() returns 0. 0 when cJSON_Parse() succeeds. */
//获取解析错误位置指针
CJSON_PUBLIC(const char *) cJSON_GetErrorPtr(void);

/* Check item type and return its value */
//获取字符串类型节点的值
CJSON_PUBLIC(char *) cJSON_GetStringValue(const cJSON * const item);
//获取数字类型节点的值
CJSON_PUBLIC(double) cJSON_GetNumberValue(const cJSON * const item);

/* These functions check the type of an item */
//下列函数作用为检查节点是否为相应类型
CJSON_PUBLIC(cJSON_bool) cJSON_IsInvalid(const cJSON * const item);
CJSON_PUBLIC(cJSON_bool) cJSON_IsFalse(const cJSON * const item);
CJSON_PUBLIC(cJSON_bool) cJSON_IsTrue(const cJSON * const item);
CJSON_PUBLIC(cJSON_bool) cJSON_IsBool(const cJSON * const item);
CJSON_PUBLIC(cJSON_bool) cJSON_IsNull(const cJSON * const item);
CJSON_PUBLIC(cJSON_bool) cJSON_IsNumber(const cJSON * const item);
CJSON_PUBLIC(cJSON_bool) cJSON_IsString(const cJSON * const item);
CJSON_PUBLIC(cJSON_bool) cJSON_IsArray(const cJSON * const item);
CJSON_PUBLIC(cJSON_bool) cJSON_IsObject(const cJSON * const item);
CJSON_PUBLIC(cJSON_bool) cJSON_IsRaw(const cJSON * const item);

/* These calls create a cJSON item of the appropriate type. */
/*
 * @brief 创建一个空的 JSON 节点（基础类型）
 * @param type 节点类型（cJSON_False/cJSON_True/cJSON_NULL/cJSON_Number/cJSON_String）
 * @return 成功返回节点指针，失败返回 NULL
 * @note 内存管理：返回的节点需要手动调用 cJSON_Delete 释放
 * @warning 不支持创建 Array/Object 类型（需用 cJSON_CreateArray/cJSON_CreateObject）
 */
CJSON_PUBLIC(cJSON *) cJSON_CreateNull(void);
CJSON_PUBLIC(cJSON *) cJSON_CreateTrue(void);
CJSON_PUBLIC(cJSON *) cJSON_CreateFalse(void);
CJSON_PUBLIC(cJSON *) cJSON_CreateBool(cJSON_bool boolean);
CJSON_PUBLIC(cJSON *) cJSON_CreateNumber(double num);
CJSON_PUBLIC(cJSON *) cJSON_CreateString(const char *string);
/* raw json */
CJSON_PUBLIC(cJSON *) cJSON_CreateRaw(const char *raw);

CJSON_PUBLIC(cJSON *) cJSON_CreateArray(void);
/**
 * @brief 创建 JSON 数组节点
 * @return 成功返回数组节点指针，失败返回 NULL
 * @note 内存管理：返回的节点需要手动调用 cJSON_Delete 释放；数组子节点由数组节点托管，删除数组时自动删除子节点
 */
CJSON_PUBLIC(cJSON *) cJSON_CreateObject(void);
/**
 * @brief 创建 JSON 对象节点
 * @return 成功返回对象节点指针，失败返回 NULL
 * @note 内存管理：返回的节点需要手动调用 cJSON_Delete 释放；对象子节点由对象节点托管，删除对象时自动删除子节点
 */
/* Create a string where valuestring references a string so
 * it will not be freed by cJSON_Delete */
CJSON_PUBLIC(cJSON *) cJSON_CreateStringReference(const char *string);
/* Create an object/array that only references it's elements so
 * they will not be freed by cJSON_Delete */
CJSON_PUBLIC(cJSON *) cJSON_CreateObjectReference(const cJSON *child);
CJSON_PUBLIC(cJSON *) cJSON_CreateArrayReference(const cJSON *child);

/* These utilities create an Array of count items.
 * The parameter count cannot be greater than the number of elements in the number array, otherwise array access will be out of bounds.*/
CJSON_PUBLIC(cJSON *) cJSON_CreateIntArray(const int *numbers, int count);
CJSON_PUBLIC(cJSON *) cJSON_CreateFloatArray(const float *numbers, int count);
CJSON_PUBLIC(cJSON *) cJSON_CreateDoubleArray(const double *numbers, int count);
CJSON_PUBLIC(cJSON *) cJSON_CreateStringArray(const char *const *strings, int count);

/* Append item to the specified array/object. */
/*
 * @brief 向 JSON 数组添加子节点
 * @param array 目标数组节点（必须是 cJSON_Array 类型）
 * @param item 要添加的子节点（不能为 NULL）
 * @return 成功返回 1，失败返回 0
 * @note 内存管理：item 所有权转移给 array，删除 array 时会自动删除 item；禁止重复添加同一个 item
 */
CJSON_PUBLIC(cJSON_bool) cJSON_AddItemToArray(cJSON *array, cJSON *item);
/*
 * @brief 向 JSON 对象添加 key-value 节点
 * @param object 目标对象节点（必须是 cJSON_Object 类型）
 * @param string 节点的 key 名（字符串会被拷贝，原字符串可自行释放）
 * @param item 要添加的 value 节点（不能为 NULL）
 * @return 成功返回 1，失败返回 0
 * @note 内存管理：item 所有权转移给 object；key 字符串会被内部拷贝，需确保 string 非空
 */
CJSON_PUBLIC(cJSON_bool) cJSON_AddItemToObject(cJSON *object, const char *string, cJSON *item);
/* Use this when string is definitely const (i.e. a literal, or as good as), and will definitely survive the cJSON object.
 * WARNING: When this function was used, make sure to always check that (item->type & cJSON_StringIsConst) is zero before
 * writing to `item->string` */



//向对象节点添加键值对节点（键名为常量，不拷贝）
CJSON_PUBLIC(cJSON_bool) cJSON_AddItemToObjectCS(cJSON *object, const char *string, cJSON *item);

/* Append reference to item to the specified array/object. Use this when you want to add an existing cJSON to a new cJSON, but don't want to corrupt your existing cJSON. */
// 向数组添加引用类型子节点（不托管内存）
CJSON_PUBLIC(cJSON_bool) cJSON_AddItemReferenceToArray(cJSON *array, cJSON *item);
//向对象添加引用类型键值对节点（不托管内存）
CJSON_PUBLIC(cJSON_bool) cJSON_AddItemReferenceToObject(cJSON *object, const char *string, cJSON *item);

/* Remove/Detach items from Arrays/Objects. */
//从父节点中分离指定子节点（不释放内存）
CJSON_PUBLIC(cJSON *) cJSON_DetachItemViaPointer(cJSON *parent, cJSON * const item);
//从数组中分离指定索引的子节点（不释放内存）
CJSON_PUBLIC(cJSON *) cJSON_DetachItemFromArray(cJSON *array, int which);
//从数组中删除指定索引的子节点（释放内存）
CJSON_PUBLIC(void) cJSON_DeleteItemFromArray(cJSON *array, int which);
//从对象中分离指定键的子节点（不释放内存，大小写不敏感）
CJSON_PUBLIC(cJSON *) cJSON_DetachItemFromObject(cJSON *object, const char *string);
//从对象中分离指定键的子节点（释放内存，大小写不敏感）
CJSON_PUBLIC(cJSON *) cJSON_DetachItemFromObjectCaseSensitive(cJSON *object, const char *string);
//从对象中删除指定键的子节点（释放内存，大小写不敏感）
CJSON_PUBLIC(void) cJSON_DeleteItemFromObject(cJSON *object, const char *string);
//从对象中删除指定键的子节点（不释放内存，大小写敏感）
CJSON_PUBLIC(void) cJSON_DeleteItemFromObjectCaseSensitive(cJSON *object, const char *string);

/* Update array items. */
//在数组指定索引插入子节点（原节点右移）
CJSON_PUBLIC(cJSON_bool) cJSON_InsertItemInArray(cJSON *array, int which, cJSON *newitem); 
/* Shifts pre-existing items to the right. */
//替换父节点中的指定子节点（通过指针）
CJSON_PUBLIC(cJSON_bool) cJSON_ReplaceItemViaPointer(cJSON * const parent, cJSON * const item, cJSON * replacement);
//替换数组指定索引的子节点
CJSON_PUBLIC(cJSON_bool) cJSON_ReplaceItemInArray(cJSON *array, int which, cJSON *newitem);
//替换对象指定键的子节点（大小写不敏感）
CJSON_PUBLIC(cJSON_bool) cJSON_ReplaceItemInObject(cJSON *object,const char *string,cJSON *newitem);
//替换对象指定键的子节点（大小写敏感）
CJSON_PUBLIC(cJSON_bool) cJSON_ReplaceItemInObjectCaseSensitive(cJSON *object,const char *string,cJSON *newitem);

/* Duplicate a cJSON item */
CJSON_PUBLIC(cJSON *) cJSON_Duplicate(const cJSON *item, cJSON_bool recurse);
/* Duplicate will create a new, identical cJSON item to the one you pass, in new memory that will
 * need to be released. With recurse!=0, it will duplicate any children connected to the item.
 * The item->next and ->prev pointers are always zero on return from Duplicate. */
/* Recursively compare two cJSON items for equality. If either a or b is NULL or invalid, they will be considered unequal.
 * case_sensitive determines if object keys are treated case sensitive (1) or case insensitive (0) */
CJSON_PUBLIC(cJSON_bool) cJSON_Compare(const cJSON * const a, const cJSON * const b, const cJSON_bool case_sensitive);

/* Minify a strings, remove blank characters(such as ' ', '\t', '\r', '\n') from strings.
 * The input pointer json cannot point to a read-only address area, such as a string constant, 
 * but should point to a readable and writable address area. */
CJSON_PUBLIC(void) cJSON_Minify(char *json);

/**
 * @brief 将 cJSON 节点树渲染为美化的 JSON 字符串（支持自定义缩进）
 * @param item 待渲染的节点（根节点/子节点均可）
 * @param indent_char 缩进字符（如 ' ' 或 '\t'）
 * @param indent_size 每级缩进的字符数
 * @return 成功：JSON 字符串（malloc 分配）；失败：NULL
 * @note 内存管理：返回的字符串需手动调用 free/cJSON_free 释放
 * @details 美化输出：使用指定的缩进字符和大小，便于阅读；递归渲染所有子节点
 */
CJSON_PUBLIC(char *) cJSON_PrintPretty(const cJSON *item, char indent_char, int indent_size);

/* Helper functions for creating and adding items to an object at the same time.
 * They return the added item or NULL on failure. */
//下列函数的作用是向对象添加相应的类型节点（快捷接口）
CJSON_PUBLIC(cJSON*) cJSON_AddNullToObject(cJSON * const object, const char * const name);
CJSON_PUBLIC(cJSON*) cJSON_AddTrueToObject(cJSON * const object, const char * const name);
CJSON_PUBLIC(cJSON*) cJSON_AddFalseToObject(cJSON * const object, const char * const name);
CJSON_PUBLIC(cJSON*) cJSON_AddBoolToObject(cJSON * const object, const char * const name, const cJSON_bool boolean);
CJSON_PUBLIC(cJSON*) cJSON_AddNumberToObject(cJSON * const object, const char * const name, const double number);
CJSON_PUBLIC(cJSON*) cJSON_AddStringToObject(cJSON * const object, const char * const name, const char * const string);
CJSON_PUBLIC(cJSON*) cJSON_AddRawToObject(cJSON * const object, const char * const name, const char * const raw);
CJSON_PUBLIC(cJSON*) cJSON_AddObjectToObject(cJSON * const object, const char * const name);
CJSON_PUBLIC(cJSON*) cJSON_AddArrayToObject(cJSON * const object, const char * const name);

/* When assigning an integer value, it needs to be propagated to valuedouble too. */
#define cJSON_SetIntValue(object, number) ((object) ? (object)->valueint = (object)->valuedouble = (number) : (number))
/* helper for the cJSON_SetNumberValue macro */
CJSON_PUBLIC(double) cJSON_SetNumberHelper(cJSON *object, double number);
#define cJSON_SetNumberValue(object, number) ((object != NULL) ? cJSON_SetNumberHelper(object, (double)number) : (number))
/* Change the valuestring of a cJSON_String object, only takes effect when type of object is cJSON_String */
CJSON_PUBLIC(char*) cJSON_SetValuestring(cJSON *object, const char *valuestring);

/* If the object is not a boolean type this does nothing and returns cJSON_Invalid else it returns the new type*/
#define cJSON_SetBoolValue(object, boolValue) ( \
    (object != NULL && ((object)->type & (cJSON_False|cJSON_True))) ? \
    (object)->type=((object)->type &(~(cJSON_False|cJSON_True)))|((boolValue)?cJSON_True:cJSON_False) : \
    cJSON_Invalid\
)

/* Macro for iterating over an array or object */
#define cJSON_ArrayForEach(element, array) for(element = (array != NULL) ? (array)->child : NULL; element != NULL; element = element->next)

/* malloc/free objects using the malloc/free functions that have been set with cJSON_InitHooks */
CJSON_PUBLIC(void *) cJSON_malloc(size_t size);
CJSON_PUBLIC(void) cJSON_free(void *object);

#ifdef __cplusplus
}
#endif

#endif
