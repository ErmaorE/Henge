/* The GICv3 IROUTER window: a guest MMIO access one register past the last SPI
   must not read or write past route[]. Before the fix in devices/gicv3.c the
   range check ended at 0x6100 + 8 * HV_GIC_NUM_IRQS, which admitted IROUTER<288>
   .. IROUTER<319>: reads leaked spi_on[]/cpu[0] bytes and writes corrupted them. */
#include <stdio.h>
#include <string.h>

#include "gicv3.h"
#include "machine.h"

#define DIST 0x20000000ull

int main(void) {
    static HvMachine m;
    static HvCpu cpu;
    if (hv_machine_init(&m, 1 << 20) != 0) {
        printf("machine init failed\n");
        return 1;
    }
    m.ncpus = 1; /* hv_gicv3_update walks the CPUs */
    m.cpus[0] = &cpu;
    HvGic *g = hv_gicv3_create(&m, DIST, 0x20100000ull);
    if (!g) {
        printf("gic create failed\n");
        return 1;
    }
    int failed = 0;

    /* IROUTER<288> is the first offset past the last SPI's router word. */
    memset(g->spi_on, 0, sizeof g->spi_on);
    g->spi_on[0] = 0x123456789abcdef0ull;
    uint64_t v = 0;
    int rc = hv_phys_read(&m, DIST + 0x6900, &v, 8);
    if (rc != 0 || v != 0) {
        printf("read IROUTER<288>: rc=%d value %016llx, expected 0\n", rc, (unsigned long long)v);
        failed++;
    }

    g->spi_on[0] = 0xffffffffffffffffull;
    uint64_t zero = 0;
    hv_phys_write(&m, DIST + 0x6900, &zero, 8);
    if (g->spi_on[0] != 0xffffffffffffffffull) {
        printf("write IROUTER<288> changed spi_on[0] to %016llx\n", (unsigned long long)g->spi_on[0]);
        failed++;
    }

    /* The last legal IROUTER, <287>, still reaches route[287]. */
    g->route[HV_GIC_NUM_IRQS - 1] = 0;
    uint64_t good = 0x0123456789abcdefull;
    hv_phys_write(&m, DIST + 0x6000 + 8 * (HV_GIC_NUM_IRQS - 1), &good, 8);
    if (g->route[HV_GIC_NUM_IRQS - 1] != good) {
        printf("write IROUTER<%d> did not reach route[%d]\n", HV_GIC_NUM_IRQS - 1, HV_GIC_NUM_IRQS - 1);
        failed++;
    }

    printf("gic window: %s\n", failed ? "FAILED" : "ok");
    return failed != 0;
}
