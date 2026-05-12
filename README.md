# mxgpu-mini

A toy SR-IOV GPU virtualization stack: a synthetic PCIe device for QEMU and the
Linux PF/VF drivers that drive it. Walks the same surface as AMD's MxGPU/GIM
stack — PCIe, SR-IOV, BAR partitioning, world-switch scheduling, DMA, IOMMU,
VFIO passthrough — at a size one person can read in an evening.

## Scope

| Phase | Component | Status |
|------:|-----------|--------|
| 1 | QEMU device exposing BAR0 (MMIO) + BAR2 (scratch RAM) | in progress |
| 2 | SR-IOV capability; VF spawn on `NumVFs` write | planned |
| 3 | PF driver: probe, BAR remap, MSI-X, world-switch scheduler | planned |
| 4 | DMA descriptor engine; IOMMU mapping via streaming DMA API | planned |
| 5 | VFIO passthrough into nested guest; guest VF driver | planned |
| 6 | Concurrency-bug post-mortem (ftrace / lockdep / KASAN / drgn) | planned |

## Layout

- `qemu-device/` — synthetic mxgpu PCIe device, built into a QEMU tree
- `pf-driver/` — Linux Physical Function driver, out-of-tree kernel module

## Building

PF driver (against the running kernel's headers):

```
cd pf-driver && make
sudo insmod mxgpu_pf.ko
```

QEMU device: drop `qemu-device/mxgpu_mini.c` into a QEMU source tree under
`hw/misc/`, wire it into `hw/misc/Kconfig` and `hw/misc/meson.build`, build
QEMU, then run with `-device mxgpu-mini`. Full notes land with phase 2.

## License

GPL-2.0. The kernel module and QEMU device are GPL-bound by their headers;
the rest of the repo follows for consistency. See [LICENSE](LICENSE).
