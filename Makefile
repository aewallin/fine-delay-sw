
.PHONY: all clean modules install modules_install
.PHONY: gitmodules prereq prereq_install prereq_install_warn

DIRS = kernel tools lib

all clean modules install modules_install: gitmodules
	for d in $(DIRS); do $(MAKE) -C $$d $@ || exit 1; done
	@if echo $@ | grep -q install; then $(MAKE) prereq_install_warn; fi

all modules: prereq

# a hack, to prevent compiling wr-nic.ko, which won't work on older kernels
CONFIG_WR_NIC=n
export CONFIG_WR_NIC

#### The following targets are used to manage prerequisite repositories
gitmodules:
	@test -d fmc-bus/doc || echo "Checking out submodules"
	@test -d fmc-bus/doc || git submodule update --init
	@git submodule update

# The user can override, using environment variables, all these three:
FMC_BUS ?= fmc-bus
ZIO ?= zio
SUBMOD = $(FMC_BUS) $(ZIO)

prereq:
	for d in $(SUBMOD); do $(MAKE) -C $$d || exit 1; done

prereq_install_warn:
	@test -f .prereq_installed || \
		echo -e "\n\n\tWARNING: Consider \"make prereq_install\"\n"

prereq_install:
	for d in $(SUBMOD); do $(MAKE) -C $$d modules_install || exit 1; done
	touch .prereq_installed
