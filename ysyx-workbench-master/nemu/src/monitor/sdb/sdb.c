/***************************************************************************************
* Copyright (c) 2014-2024 Zihao Yu, Nanjing University
*
* NEMU is licensed under Mulan PSL v2.
* You can use this software according to the terms and conditions of the Mulan PSL v2.
* You may obtain a copy of Mulan PSL v2 at:
*          http://license.coscl.org.cn/MulanPSL2
*
* THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
* EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
* MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
*
* See the Mulan PSL v2 for more details.
***************************************************************************************/

#include <isa.h>
#include <cpu/cpu.h>
#include <readline/readline.h>
#include <readline/history.h>
#include "sdb.h"

static int is_batch_mode = false;

void init_regex();
void init_wp_pool();

/* We use the `readline' library to provide more flexibility to read from stdin. */
static char* rl_gets() {
  static char *line_read = NULL;

  if (line_read) {
    free(line_read);
    line_read = NULL;
  }

  line_read = readline("(nemu) ");

  if (line_read && *line_read) {
    add_history(line_read);
  }

  return line_read;
}

static int cmd_c(char *args) {
  cpu_exec(-1);   // uint64_t 是无符号 64 位整数类型（只能表示非负整数）。
//当给无符号整数类型传入 -1 时，会发生无符号整数的溢出转换：
//在计算机中，-1 的二进制补码表示为 “全 1”（64 位下即 0xFFFFFFFFFFFFFFFF）
  return 0;
}


static int cmd_q(char *args) {
  return -1;  //如果收到 -1，就会终止交互循环
}

static int cmd_help(char *args);

static struct {
  const char *name;       // 命令名称（用户输入的字符串）
  const char *description;// 命令的描述信息（用于帮助文档）
  int (*handler) (char *);// 命令处理函数指针（指向实现命令逻辑的函数），处理函数的参数是char *
} cmd_table [] = {
  { "help", "Display information about all supported commands", cmd_help },
  { "c", "Continue the execution of the program", cmd_c },
  { "q", "Exit NEMU", cmd_q },

  /* TODO: Add more commands */
  { "si", "Let the programexcute N instuctions and then suspend the excution,while the N is not given,the default value is 1", cmd_si},
  { "info","Print the register status or watchpoint information" ,cmd_info}
  { "x","Evaluate the value of the expression EXPR, take the result as the starting memory address, and output N consecutive 4-byte values in hexadecimal format",cmd_x},

  { "p","Evaluate the value of the expression EXPR. For the operations supported by EXPR, please refer to the section on expression evaluation in debugging",cmd_p},

  { "w","When the value of the expression EXPR changes, pause the program execution",cmd_w},

  {"d" ,"Delete the watchpoint with serial number N",cmd_d}

};

#define NR_CMD ARRLEN(cmd_table)  //ARRLEN(cmd_table)：这是一个用于计算数组元素个数的通用宏

static int cmd_help(char *args) {
  /* extract the first argument */
  char *arg = strtok(NULL, " "); //strtok 是字符串分割函数，这里用于从命令参数中提取第一个子串
  int i;

  if (arg == NULL) {
    /* no argument given */
    for (i = 0; i < NR_CMD; i ++) { //当用户输入 help 且不带参数时，遍历整个 cmd_table 数组（从 i=0 到 i=NR_CMD-1），逐个打印每个命令的名称（name）和描述（description）。
      printf("%s - %s\n", cmd_table[i].name, cmd_table[i].description);
    }
  }
  else {  //当用户输入 help <命令名>（如 help si）时，遍历 cmd_table 查找名称与参数 arg 匹配的命令
    for (i = 0; i < NR_CMD; i ++) {
      if (strcmp(arg, cmd_table[i].name) == 0) {
        printf("%s - %s\n", cmd_table[i].name, cmd_table[i].description);
        return 0;
      } //若找到，则打印该命令的名称和描述
    }
    printf("Unknown command '%s'\n", arg);//若未找到，则打印 “未知命令” 提示
  }
  return 0; //，返回 0 表示 "help" 命令执行成功，程序继续保持交互状态
}

static int cmd_si(char *args) {


  int i;
  if (args==NULL) i=1;
  else  sscanf(args,"%d",&i);

  cpu_exec(i);

}

static int cmd_info(char *args) {
  if (args == NULL) {
    printf("no args\n");
  } else {
    char n;
    // 从args中读取第一个字符（忽略空格）
    int ret = sscanf(args, "%c", &n);
    // 确保读取成功且参数是单个字符（避免多字符输入）
    if (ret == 1) {
      if (n == 'r') {  // 比较单个字符，用单引号
        isa_reg_display();
      } else if (n == 'w') {
        info_watchpoint();
      }
      // 可添加else处理无效参数，如printf("invalid arg: %c\n", n);
    }
  }
  return 0;  // 补充返回值
}

void sdb_set_batch_mode() {
  is_batch_mode = true; //将全局变量 is_batch_mode（批处理模式标志）设为 true，用于切换调试器的运行模式
}

void sdb_mainloop() {
  if (is_batch_mode) { //若处于批处理模式（is_batch_mode = true）：                      
    cmd_c(NULL);  //直接调用 cmd_c(NULL)（即执行 "c" 命令，让程序继续运行直到结束）
    return;
  }
//将用户输入的字符串（如 "si 5"）拆分为命令名（cmd = "si"）和参数（args = "5"）
  for (char *str; (str = rl_gets()) != NULL; ) {//若不处于批处理模式，则进入交互循环，持续等待并处理用户输入的命令
    char *str_end = str + strlen(str); //// 计算输入字符串的结束位置（便于参数边界判断）

    /* extract the first token as the command */
    char *cmd = strtok(str, " ");  // 用空格分割字符串，第一个部分为命令名（如"si"、"help"）
    if (cmd == NULL) { continue; } // 若输入为空（仅回车），跳过本次循环

    /* treat the remaining string as the arguments,
     * which may need further parsing
     */
    char *args = cmd + strlen(cmd) + 1;// 参数起始位置：命令名后第一个字符（跳过命令名和空格）
    if (args >= str_end) { // 若参数起始位置超过字符串末尾（即无参数）
      args = NULL;         // 设为NULL表示无参数
    }

#ifdef CONFIG_DEVICE  //若启用设备模拟（CONFIG_DEVICE 宏定义，如模拟器包含图形 / 输入设备）
    extern void sdl_clear_event_queue();//则调用 sdl_clear_event_queue() 清除 SDL 事件队列（如键盘、鼠标事件）
    sdl_clear_event_queue(); //避免设备事件干扰调试命令的处理（确保输入仅来自用户的调试命令）。
#endif

    int i;
    for (i = 0; i < NR_CMD; i ++) { // // 遍历命令表cmd_table
      if (strcmp(cmd, cmd_table[i].name) == 0) { //利用比较字符大小函数strcmp来找到名称匹配的命令
        if (cmd_table[i].handler(args) < 0) { return; }//调用处理函数，若返回<0则退出主循环
            //执行当前命令的处理函数，得到函数的返回值。
        break;
      }
    }

    if (i == NR_CMD) { printf("Unknown command '%s'\n", cmd); } // 未找到匹配的命令
  }
}

void init_sdb() {
  /* Compile the regular expressions. */
  init_regex(); //初始化正则表达式（init_regex()）

  /* Initialize the watchpoint pool. */
  init_wp_pool(); //初始化监视点池
}
