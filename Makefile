
DIRS = kernel tools lib doc

all clean modules install modules_install: gitmodules
	for d in $(DIRS); do $(MAKE) -C $$d $@ || exit 1; done

# This target is used to checkout the submodules after a clone
gitmodules:
	@test -d fmc-bus/doc || echo "Checking out submodules"
	@test -d fmc-bus/doc || git submodule init && git submodule update


.PHONY: all clean modules install modules_install gitmodules
