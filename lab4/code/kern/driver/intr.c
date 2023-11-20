#include <intr.h>
#include <riscv.h>

/* intr_enable - 启用中断 */
void intr_enable(void) { set_csr(sstatus, SSTATUS_SIE); }

/* intr_disable - 禁用中断 */
void intr_disable(void) { clear_csr(sstatus, SSTATUS_SIE); }
