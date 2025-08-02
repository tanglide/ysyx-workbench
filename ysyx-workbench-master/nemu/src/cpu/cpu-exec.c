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

#include <cpu/cpu.h>
#include <cpu/decode.h>
#include <cpu/difftest.h>
#include <locale.h>

/* The assembly code of instructions executed is only output to the screen
 * when the number of instructions executed is less than this value.
 * This is useful when you use the `si' command.
 * You can modify this value as you want.
 */
#define MAX_INST_TO_PRINT 10  //定义 “最大打印指令数” 为 10

CPU_state cpu = {};     //CPU_state是一个结构体，用于存储当前 CPU 的运行状态
uint64_t g_nr_guest_inst = 0;//记录被模拟程序（客户机）已执行的总指令数
static uint64_t g_timer = 0; // unit: us  
static bool g_print_step = false; //控制是否开启 “单步打印”（每条指令执行后打印细节）

void device_update();      //该函数用于更新设备状态（如处理定时事件、中断等），在execute函数中每次执行指令后调用

static void trace_and_difftest(Decode *_this, vaddr_t dnpc) {//整合 “指令跟踪日志输出”“单步打印” 和 “差异测试” 三大功能，在每条指令执行后被调用（见execute函数），是连接指令执行与调试 / 验证的关键环节。
#ifdef CONFIG_ITRACE_COND
  if (ITRACE_COND) { log_write("%s\n", _this->logbuf); }   //满足条件，则打印对应日志
#endif
  if (g_print_step) { IFDEF(CONFIG_ITRACE, puts(_this->logbuf)); }
  IFDEF(CONFIG_DIFFTEST, difftest_step(_this->pc, dnpc));
}

static void exec_once(Decode *s, vaddr_t pc) { //单条指令的 “执行单元”
  s->pc = pc;
  s->snpc = pc;  //这里初始化其 pc（指令地址）和 snpc（下一条指令的临时地址）。
  isa_exec_once(s); //实际完成指令的解码和执行（如运算、内存访问、寄存器修改等）
  cpu.pc = s->dnpc; //执行后，s->dnpc 会被设置为下一条指令的地址
#ifdef CONFIG_ITRACE//若定义 CONFIG_ITRACE（启用指令跟踪）
  char *p = s->logbuf;//则格式化日志缓冲区 s->logbuf：
  p += snprintf(p, sizeof(s->logbuf), FMT_WORD ":", s->pc); //执行后，p 指针向前移动，指向缓冲区中已写入内容的下一个空闲位置（为后续写入预留空间）。
  int ilen = s->snpc - s->pc; //s->snpc 是 “当前指令执行完毕后的下一个地址”（可以理解为 “指令结束地址”）。
                              //ilen 表示当前指令的长度（字节数）（地址差 = 指令长度）
  int i; 
  uint8_t *inst = (uint8_t *)&s->isa.inst; //s->isa.inst 存储当前指令的原始机器码（二进制数据，如 0x5589e5 这样的字节序列）。
                                           //转换为 uint8_t* 指针，方便按字节访问机器码的每个字节（因为机器码通常由多个字节组成）
#ifdef CONFIG_ISA_x86  //这部分是将机器码的每个字节以两位十六进制形式（如 55、89）写入日志，方便人类阅读
  for (i = 0; i < ilen; i ++) {
#else
  for (i = ilen - 1; i >= 0; i --) {
#endif
    p += snprintf(p, 4, " %02x", inst[i]);
  }
  int ilen_max = MUXDEF(CONFIG_ISA_x86, 8, 4); /// 不同架构的最大指令长度
  int space_len = ilen_max - ilen;           //// 当前指令与最大长度的差值
  if (space_len < 0) space_len = 0;         //// 确保差值非负
  space_len = space_len * 3 + 1;            //// 计算需要填充的空格数（每个字节占3字符：空格+2位十六进制）
  memset(p, ' ', space_len);                /// 用空格填充
  p += space_len;                           /// 移动指针到空格后的位置

  void disassemble(char *str, int size, uint64_t pc, uint8_t *code, int nbyte);//disassemble 是反汇编函数（需在其他地方实现），功能是将二进制机器码转换为人类可读的汇编指令（如 push ebp、add t0, t1, t2）。
  disassemble(p, s->logbuf + sizeof(s->logbuf) - p,  
      MUXDEF(CONFIG_ISA_x86, s->snpc, s->pc), (uint8_t *)&s->isa.inst, ilen);
#endif
}

static void execute(uint64_t n) {
  Decode s; // 声明一个用于指令解码的结构体（推测用于存储解码后的指令信息）
  for (;n > 0; n --) {  // 循环 n 次，每次执行一条指令
    exec_once(&s, cpu.pc);// cpu.pc 是当前程序计数器（指向要执行的指令地址）
    g_nr_guest_inst ++; // 全局变量，累加已执行的 guest 指令数（guest 指被模拟的程序）
    trace_and_difftest(&s, cpu.pc);// - 跟踪指令执行过程（如打印指令信息）
    if (nemu_state.state != NEMU_RUNNING) break;// 若状态变为停止、结束等，则不再执行剩余指令
    IFDEF(CONFIG_DEVICE, device_update());// 若定义了 CONFIG_DEVICE 宏（启用设备模拟），则调用 device_update() 更新设备状态（如处理设备中断、定时事件等）
  }
}

static void statistic() {     //负责输出模拟器的核心性能指标（耗时、指令数、模拟频率）
  IFNDEF(CONFIG_TARGET_AM, setlocale(LC_NUMERIC, ""));
#define NUMBERIC_FMT MUXDEF(CONFIG_TARGET_AM, "%", "%'") PRIu64
  Log("host time spent = " NUMBERIC_FMT " us", g_timer);
  Log("total guest instructions = " NUMBERIC_FMT, g_nr_guest_inst);
  if (g_timer > 0) Log("simulation frequency = " NUMBERIC_FMT " inst/s", g_nr_guest_inst * 1000000 / g_timer);
  else Log("Finish running in less than 1 us and can not calculate the simulation frequency");
}

void assert_fail_msg() {
  isa_reg_display();    //作用是打印当前所有寄存器的状态
  statistic();
}

/* Simulate how the CPU works. */
void cpu_exec(uint64_t n) {
  g_print_step = (n < MAX_INST_TO_PRINT);    //当n小于最大打印指令数时为开启；反之为关闭
  switch (nemu_state.state) {           
    case NEMU_END: case NEMU_ABORT: case NEMU_QUIT: //正常结束；异常中断；退出nemu
      printf("Program execution has ended. To restart the program, exit NEMU and run again.\n"); //程序执行已经结束了。为了重启这个程序,退出nemu并且再次运行
      return;
    default: nemu_state.state = NEMU_RUNNING;  //其他状态时均为running状态
  }

  uint64_t timer_start = get_time(); //起始时间

  execute(n);   //执行n次

  uint64_t timer_end = get_time();   //终止时间
  g_timer += timer_end - timer_start;//总用时

  switch (nemu_state.state) {    
    case NEMU_RUNNING: nemu_state.state = NEMU_STOP; break;  //若仍在运行中，转为“停止”状态（可能因执行完n条指令暂停）

    case NEMU_END: case NEMU_ABORT:   // 打印结束信息：包含结束类型（正常终止/异常终止）和结束时的PC值
      Log("nemu: %s at pc = " FMT_WORD,
          (nemu_state.state == NEMU_ABORT ? ANSI_FMT("ABORT", ANSI_FG_RED) :
           (nemu_state.halt_ret == 0 ? ANSI_FMT("HIT GOOD TRAP", ANSI_FG_GREEN) :
            ANSI_FMT("HIT BAD TRAP", ANSI_FG_RED))),
          nemu_state.halt_pc);
      // fall through  （穿透执行下一个case）
    case NEMU_QUIT: statistic();  // 执行统计函数（可能输出总指令数、总耗时等信息）
  }
}
