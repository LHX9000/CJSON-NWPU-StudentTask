#include <stdio.h>
#include <stdlib.h>
#include "cJSON.h"

int main() {
    // 创建一个测试 JSON 对象
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        printf("Failed to create root object\n");
        return 1;
    }

    // 添加一些测试数据
    cJSON_AddStringToObject(root, "name", "John Doe");
    cJSON_AddNumberToObject(root, "age", 30);
    cJSON_AddBoolToObject(root, "isStudent", 0);

    // 添加嵌套对象
    cJSON *address = cJSON_CreateObject();
    cJSON_AddStringToObject(address, "street", "123 Main St");
    cJSON_AddStringToObject(address, "city", "New York");
    cJSON_AddStringToObject(address, "zip", "10001");
    cJSON_AddItemToObject(root, "address", address);

    // 添加数组
    cJSON *hobbies = cJSON_CreateArray();
    cJSON_AddItemToArray(hobbies, cJSON_CreateString("reading"));
    cJSON_AddItemToArray(hobbies, cJSON_CreateString("gaming"));
    cJSON_AddItemToArray(hobbies, cJSON_CreateString("coding"));
    cJSON_AddItemToObject(root, "hobbies", hobbies);

    // 测试默认格式化输出
    printf("=== Default formatted output ===\n");
    char *default_formatted = cJSON_Print(root);
    if (default_formatted) {
        printf("%s\n", default_formatted);
        free(default_formatted);
    }

    // 测试自定义缩进：4个空格
    printf("\n=== Custom indent: 4 spaces ===\n");
    char *custom_spaces = cJSON_PrintPretty(root, ' ', 4);
    if (custom_spaces) {
        printf("%s\n", custom_spaces);
        free(custom_spaces);
    }

    // 测试自定义缩进：tab
    printf("\n=== Custom indent: tab ===\n");
    char *custom_tab = cJSON_PrintPretty(root, '\t', 1);
    if (custom_tab) {
        printf("%s\n", custom_tab);
        free(custom_tab);
    }

    // 测试自定义缩进：2个空格（默认）
    printf("\n=== Custom indent: 2 spaces ===\n");
    char *custom_2spaces = cJSON_PrintPretty(root, ' ', 2);
    if (custom_2spaces) {
        printf("%s\n", custom_2spaces);
        free(custom_2spaces);
    }

    // 释放内存
    cJSON_Delete(root);

    return 0;
}
