# Hardware

The development target is a Linux box that can run QEMU with KVM
acceleration *and* nest another QEMU inside a guest (Phase 5/P6 VFIO
passthrough). Local options were ruled out — Lima on macOS doesn't expose
nested KVM cleanly. Cloud is the path.

## Cloud provider

Google Cloud. ~$300 of credits on hand at project start (2026-05).

## Constraint that picks the machine

mxgpu-mini needs **KVM in the L1 VM**, which means the cloud VM must
support **nested virtualization**. On GCP that filters:

- **E2** — no nested virt. Out.
- **Memory-optimized** (M-series) — no nested virt. Out.
- **AMD-based** (N2D, C3D, C4D) — no nested virt. Out.
- **Arm-based** (T2A, C4A, N4A) — no nested virt. Out. Also wrong ISA
  for our QEMU `x86_64-softmmu` target.
- **H4D** — no nested virt. Out.
- **C3** — ambiguous as of 2026-05. GCP's overview doc doesn't list it
  as unsupported, but third-party docs claim it isn't. Avoid until
  confirmed; don't burn an afternoon finding out the hard way.
- **C4, N2, N4** — supported. C4 fastest in current benchmarks.

Also required at create time: `--enable-nested-virtualization` flag
(off by default) and `--min-cpu-platform=...` to guarantee VMX/VT-x in
the guest CPU.

## The pick

**`c4-highcpu-8` in `asia-northeast1-b` (Tokyo), 50 GB Hyperdisk
Balanced, Ubuntu 24.04 LTS.**

Zone gotcha: C4 is in `asia-northeast1-b` and `-c` but **not** `-a` as
of 2026-05. Check `gcloud compute machine-types list --filter="zone~<region> AND name=<type>"`
before assuming a zone.

Why each piece:

- **C4 over N2**: Martin Pitt's Feb 2026 KVM-CI benchmark put
  `c4-highcpu-8` at 2m52s vs `n2-standard-8` at 6m31s for a real
  nested-VM workload — ~2.3× faster at similar price. Per the
  fastest-machine-spin-down rule, optimize wall clock.
- **C4 over C3**: C3 nested-virt support is unclear in current docs.
  C4 is confirmed working in 2026. Don't gamble.
- **Intel over AMD**: VFIO/IOMMU tutorials, QEMU's `-device intel-iommu`
  path, and the AMD GIM reference driver's host-side docs are all
  Intel-canonical. AMD VMs also don't support nested virt on GCP.
- **`highcpu-8` over `standard-8`**: 16 GB RAM is enough for QEMU +
  guest + a build shell. The `standard` variant doubles RAM at notable
  cost; we don't need it.
- **Tokyo (`asia-northeast1-a`)**: closest region. Lower RTT for `ssh`
  and `scp`.
- **50 GB Hyperdisk Balanced**: QEMU build ~6 GB, kernel headers + tools
  ~2 GB, guest images ~5 GB total, headroom for a nested-guest disk.
  Original instinct of 100 GB was overkill. C4 defaults to Hyperdisk
  Balanced rather than pd-balanced.
- **Ubuntu 24.04 LTS**: most QEMU/kernel-driver docs are Debian-family;
  recent enough to have current kernel headers without backport fights.

## Spin-up

```sh
gcloud compute instances create mxgpu-dev \
  --zone=asia-northeast1-b \
  --machine-type=c4-highcpu-8 \
  --image-family=ubuntu-2404-lts-amd64 \
  --image-project=ubuntu-os-cloud \
  --boot-disk-size=50GB \
  --boot-disk-type=hyperdisk-balanced \
  --enable-nested-virtualization
```

Note: `--min-cpu-platform` is unnecessary for C4 — it always runs on
Intel Emerald Rapids, many generations past the Haswell minimum nested
virt requires. Setting it with a wrong string just causes a needless
failure.

The boot disk resize warning at create time is benign — Ubuntu cloud
images auto-grow the root partition on first boot.

Then `gcloud compute ssh mxgpu-dev --zone=asia-northeast1-b`.

## First-boot sanity checks

Run these in order. Any failure means the VM was provisioned wrong —
delete and recreate rather than fight the config.

```sh
# 1. KVM is exposed to L1.
ls /dev/kvm                          # exists
egrep -c '(vmx|svm)' /proc/cpuinfo   # > 0
sudo apt install -y cpu-checker && sudo kvm-ok   # "KVM acceleration can be used"

# 2. Your user can actually use KVM (Ubuntu 24.04 default is mode 660
#    owned root:kvm — bare ls doesn't catch this).
sudo usermod -aG kvm $USER
# Reconnect SSH for the group to take effect.
groups | grep -q kvm && echo "kvm group ok"

# 3. Kernel headers match the running kernel (needed for the PF module).
uname -r
ls /usr/src/linux-headers-$(uname -r) 2>/dev/null || \
  sudo apt install -y linux-headers-$(uname -r)
```

## Setup deps

```sh
sudo apt update && sudo apt install -y \
  build-essential ninja-build pkg-config \
  libglib2.0-dev libpixman-1-dev libslirp-dev \
  python3-venv git qemu-utils \
  linux-headers-$(uname -r)
```

## Lifecycle

- **Stop when walking away** — stopped instances bill only for disk
  (~$5/month at 50 GB Hyperdisk Balanced), not compute.

  ```sh
  gcloud compute instances stop mxgpu-dev --zone=asia-northeast1-b
  gcloud compute instances start mxgpu-dev --zone=asia-northeast1-b
  ```

- **Delete when done with the project** — `gcloud compute instances
  delete mxgpu-dev --zone=asia-northeast1-b`. Disk goes with it.

## Cost ballpark

- `c4-highcpu-8` on-demand: ~$0.34/hr in asia-northeast1 (≈$0.31 in
  us-central1; Tokyo is ~10% premium).
- Hyperdisk Balanced 50 GB: ~$6/month.
- Active dev at 4 hr/day × 5 days/week ≈ $30/month compute + $6 disk.
- $300 in credits → realistically 6+ months at that cadence.

## Verified at provisioning (2026-05)

- **KVM in L1**: `/dev/kvm` exposed; QEMU `-accel kvm` works after adding
  the user to the `kvm` group (Ubuntu's `/dev/kvm` is mode 660 owned by
  `root:kvm`). One-time step: `sudo usermod -aG kvm $USER`, then a fresh
  SSH session.
- **QEMU emulated `intel-iommu` under KVM**: instantiates cleanly with
  `-machine q35,kernel-irqchip=split -device intel-iommu,intremap=on
  -accel kvm`. Sufficient for Phase 4/5 — VFIO inside L2 will use this
  emulated IOMMU, not L1's real one.
- **L1 IOMMU groups**: `/sys/kernel/iommu_groups/` is empty on GCP C4.
  GCP doesn't expose virtual IOMMU groups to nested VMs. Doesn't matter
  for mxgpu-mini — we never bind anything to vfio-pci on L1; passthrough
  happens L2→L3 via QEMU's emulated IOMMU.
- **Disk baseline (Hyperdisk Balanced 50 GB)**:
  - Sequential write: 235 MB/s (1 GiB direct I/O)
  - Sequential read: 234 MB/s
  - Random 4K direct write: 13 MB/s (~3300 IOPS)
  - Verdict: comfortable for QEMU image churn and incremental ninja
    builds. No quirks observed.
