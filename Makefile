
.PHONY: all clean modules install modules_install
.PHONY: gitmodules prerequisites prerequisites_install

DIRS = kernel tools lib

all clean modules install modules_install: gitmodules
	for d in $(DIRS); do $(MAKE) -C $$d $@ || exit 1; done

all modules: prerequisites

install modules_install: prerequisites_install

# The following targets are used to manage prerequisite repositories
gitmodules:
	@test -d fmc-bus/doc || echo "Checking out submodules"
	@test -d fmc-bus/doc || git submodule init && git submodule update

SUBMOD = fmc-bus spec-sw zio

prerequisites:
	for d in $(SUBMOD); do $(MAKE) -C $$d || exit 1; done

prerequisites_install:
	for d in $(SUBMOD); do $(MAKE) -C $$d install || exit 1; done

