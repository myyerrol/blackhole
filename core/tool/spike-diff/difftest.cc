#include "mmu.h"
#include "sim.h"
#include "../../include/common.h"
#include <debug/difftest.h>

#define NR_GPR MUXDEF(CONFIG_RVE, 16, 32)

static std::vector<std::pair<reg_t, abstract_device_t*>> difftest_plugin_devices;
static std::vector<std::string> difftest_htif_args;
static std::vector<std::pair<reg_t, mem_t*>> difftest_mem(
    1, std::make_pair(reg_t(DRAM_BASE), new mem_t(CONFIG_MSIZE)));
static debug_module_config_t difftest_dm_config = {
  .progbufsize = 2,
  .max_sba_data_width = 0,
  .require_authentication = false,
  .abstract_rti = 0,
  .support_hasel = true,
  .support_abstract_csr_access = true,
  .support_abstract_fpr_access = true,
  .support_haltgroups = true,
  .support_impebreak = true
};

#define CSR_ARR_LEN 4096
#define CSR_MSTATUS 0x300
#define CSR_MTVEC   0x305
#define CSR_MEPC    0x341
#define CSR_MCAUSE  0x342

struct diff_context_t {
  word_t gpr[MUXDEF(CONFIG_RVE, 16, 32)];
  word_t csr[CSR_ARR_LEN];
  word_t pc;
};

static sim_t* s = NULL;
static processor_t *p = NULL;
static state_t *state = NULL;

void sim_t::diff_init(int port) {
  p = get_core("0");
  state = p->get_state();
  p->set_debug(true);
  p->put_csr(CSR_MSTATUS, MUXDEF(CONFIG_ISA_riscv32, 0x1800, 0xa00001800));
}

void sim_t::diff_step(uint64_t n) {
  step(n);
}

void sim_t::diff_get_regs(void* diff_context) {
  struct diff_context_t* ctx = (struct diff_context_t*)diff_context;
  ctx->pc = state->pc;
  for (int i = 0; i < NR_GPR; i++) {
    ctx->gpr[i] = state->XPR[i];
  }
  ctx->csr[CSR_MSTATUS] = p->get_csr(CSR_MSTATUS);
  ctx->csr[CSR_MTVEC]   = p->get_csr(CSR_MTVEC);
  ctx->csr[CSR_MEPC]    = p->get_csr(CSR_MEPC);
  ctx->csr[CSR_MCAUSE]  = p->get_csr(CSR_MCAUSE);
}

void sim_t::diff_set_regs(void* diff_context) {
  struct diff_context_t* ctx = (struct diff_context_t*)diff_context;
  state->pc = ctx->pc;
  for (int i = 0; i < NR_GPR; i++) {
    state->XPR.write(i, (sword_t)ctx->gpr[i]);
  }
}

void sim_t::diff_memcpy(reg_t dest, void* src, size_t n) {
  mmu_t* mmu = p->get_mmu();
  for (size_t i = 0; i < n; i++) {
    mmu->store<uint8_t>(dest+i, *((uint8_t*)src+i));
  }
}

extern "C" {

__EXPORT void difftest_memcpy(paddr_t addr, void *buf, size_t n, bool direction) {
  if (direction == DIFFTEST_TO_REF) {
    s->diff_memcpy(addr, buf, n);
  } else {
    assert(0);
  }
}

__EXPORT void difftest_regcpy(void* dut, bool direction) {
  if (direction == DIFFTEST_TO_REF) {
    s->diff_set_regs(dut);
  } else {
    s->diff_get_regs(dut);
  }
}

__EXPORT void difftest_exec(uint64_t n) {
  s->diff_step(n);
}

__EXPORT void difftest_init(int port) {
  difftest_htif_args.push_back("");
  const char *isa = "RV" MUXDEF(CONFIG_ISA_riscv32, "32", "64") MUXDEF(CONFIG_RVE, "E", "I") "MAFDC";
  cfg_t cfg(/*default_initrd_bounds=*/std::make_pair((reg_t)0, (reg_t)0),
            /*default_bootargs=*/nullptr,
            /*default_isa=*/isa,
            /*default_priv=*/DEFAULT_PRIV,
            /*default_varch=*/DEFAULT_VARCH,
            /*default_misaligned=*/false,
            /*default_endianness*/endianness_little,
            /*default_pmpregions=*/16,
            /*default_mem_layout=*/std::vector<mem_cfg_t>(),
            /*default_hartids=*/std::vector<size_t>(1),
            /*default_real_time_clint=*/false,
            /*default_trigger_count=*/4);
  s = new sim_t(&cfg, false,
      difftest_mem, difftest_plugin_devices, difftest_htif_args,
      difftest_dm_config, nullptr, false, NULL,
      false,
      NULL,
      true);
  s->diff_init(port);
}

__EXPORT void difftest_raise_intr(uint64_t NO) {
  trap_t t(NO);
  p->take_trap_public(t, state->pc);
}

}
