#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")"

echo "[RUN MPI HOST]"
echo "Host        : $(hostname)"
echo "User        : $(whoami)"
echo "PWD         : $(pwd)"
echo "PATH        : $PATH"
echo "LD_LIBRARY_PATH: ${LD_LIBRARY_PATH:-}"

export PATH=/usr/local/bin:/usr/bin:/bin:${PATH:-}
export LD_LIBRARY_PATH=/usr/local/lib:/usr/local/lib/x86_64-linux-gnu:${LD_LIBRARY_PATH:-}

# Evita crashes conocidos de hwloc con OpenCL / Level Zero.
export HWLOC_COMPONENTS=-opencl,-levelzero

# Opcional, útil para evitar problemas raros con OpenMPI/PRTE.
export OMPI_MCA_hwloc_base_binding_policy=none
export OMPI_MCA_rmaps_base_mapping_policy=slot

echo "[MPI VERSION]"
which mpirun
mpirun --version || true

echo "[FILES]"
ls -lh imagen_payload.bin hosts.txt mpi_proyectosce

echo "[EXEC]"
mpirun \
  -hostfile hosts.txt \
  ./mpi_proyectosce \
  imagen_payload.bin \
  --send